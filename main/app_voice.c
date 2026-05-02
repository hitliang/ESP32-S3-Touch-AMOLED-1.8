#include "app_voice.h"
#include "sys_wifi.h"
#include "sys_audio.h"
#include "secrets.h"
#include "lvgl.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "voice";

/* ---- System Prompt ---- */
static const char *SYSTEM_PROMPT =
    "你是一个给6岁小男孩用的语音助手。\n"
    "男孩中文名叫瑞迪，英文名Reddy，今年上小学一年级。\n"
    "聊天规则：\n"
    "- 用简单中文交流，句子要短，词汇要适合6岁小孩\n"
    "- 语气要温暖、耐心、鼓励，像一个大哥哥或大姐姐\n"
    "- 不要说可怕的内容，不要讲鬼故事、恐怖的事情\n"
    "- 回答保持简短，一般不超过三句话\n"
    "- 可以适当用一些可爱的语气，但不要太幼稚\n"
    "- 瑞迪问你问题的时候，认真回答，保护他的好奇心\n"
    "- 如果瑞迪分享学校的事情，要夸奖他、鼓励他\n"
    "- 可以偶尔加一个简单的emoji表情符号";

/* ---- Conversation History ---- */
#define MAX_HISTORY  20
#define MAX_MSG_LEN  512

static char  *history_user[MAX_HISTORY];
static char  *history_asst[MAX_HISTORY];
static int    history_count = 0;

static void history_add(const char *user, const char *assistant)
{
    if (history_count >= MAX_HISTORY) {
        /* Compress: merge oldest 4 exchanges into a summary */
        char summary[1024];
        int keep = MAX_HISTORY - 4;
        snprintf(summary, sizeof(summary),
                 "[Earlier conversation summary: ");
        for (int i = 0; i < 4; i++) {
            int off = strlen(summary);
            snprintf(summary + off, sizeof(summary) - off,
                     "Q%d:%s A%d:%s ", i+1, history_user[i] ? history_user[i] : "",
                     i+1, history_asst[i] ? history_asst[i] : "");
        }
        strncat(summary, "]", sizeof(summary) - strlen(summary) - 1);

        /* Free old messages */
        for (int i = 0; i < 4; i++) {
            free(history_user[i]); history_user[i] = NULL;
            free(history_asst[i]); history_asst[i] = NULL;
        }
        /* Shift remaining */
        for (int i = 0; i < keep; i++) {
            history_user[i] = history_user[i + 4];
            history_asst[i] = history_asst[i + 4];
        }
        for (int i = keep; i < MAX_HISTORY; i++) {
            history_user[i] = NULL;
            history_asst[i] = NULL;
        }
        history_count = keep;

        /* Store summary as a system message */
        history_user[0] = strdup(summary);
        free(history_asst[0]);
        history_asst[0] = strdup("OK");
    }

    history_user[history_count] = user ? strdup(user) : strdup("");
    history_asst[history_count] = assistant ? strdup(assistant) : strdup("");
    history_count++;
}

/* ---- UI ---- */
static lv_obj_t *root = NULL, *conv_area = NULL, *status_lbl = NULL;
static int conv_y = 5;
static uint8_t *audio_buf = NULL;
static int audio_len = 0;

static int b64_decode(const char *in, uint8_t *out, int max)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len = 0, bits = 0, val = 0;
    for (const char *p = in; *p; p++) {
        const char *c = strchr(t, *p);
        if (!c) continue;
        val = (val << 6) | (int)(c - t);
        bits += 6;
        if (bits >= 8) { bits -= 8; if (len < max) out[len++] = (val >> bits) & 0xFF; }
    }
    return len;
}

typedef struct { uint8_t *buf; int len, max, status; } hctx_t;
static esp_err_t hevt(esp_http_client_event_t *e)
{
    hctx_t *c = (hctx_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && c->len + e->data_len < c->max) {
        memcpy(c->buf + c->len, e->data, e->data_len);
        c->len += e->data_len; c->buf[c->len] = 0;
    }
    if (e->event_id == HTTP_EVENT_ON_FINISH)
        c->status = esp_http_client_get_status_code(e->client);
    return ESP_OK;
}

static esp_err_t http_post(const char *url, const char *body,
                            const char *hdr1, const char *val1,
                            const char *hdr2, const char *val2,
                            uint8_t *buf, int max, int *olen, int *osta)
{
    hctx_t ctx = { .buf = buf, .len = 0, .max = max, .status = 0 };
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 25000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = hevt, .user_data = &ctx,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    if (hdr1) esp_http_client_set_header(cli, hdr1, val1);
    if (hdr2) esp_http_client_set_header(cli, hdr2, val2);
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);
    *olen = ctx.len; *osta = ctx.status;
    return err;
}

/* ---- JSON string escape helper ---- */
static char *json_esc(const char *s, char *buf, int max)
{
    char *d = buf;
    while (*s && d < buf + max - 2) {
        if (*s == '"' || *s == '\\') *d++ = '\\';
        *d++ = *s++;
    }
    *d = 0;
    return buf;
}

/* ---- DeepSeek LLM with history ---- */
static char *llm_chat(const char *question)
{

    /* Build messages array: system + history + current question */
    char *body = malloc(16384);
    if (!body) return NULL;

    char e1[1024], e2[1024];
    int off = snprintf(body, 16384,
        "{\"model\":\"deepseek-chat\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"}",
        json_esc(SYSTEM_PROMPT, e1, sizeof(e1)));

    /* Add history (skip system-asst pseudo-messages from compression) */
    for (int i = 0; i < history_count; i++) {
        if (history_user[i]) {
            off += snprintf(body + off, 16384 - off,
                ",{\"role\":\"user\",\"content\":\"%s\"}",
                json_esc(history_user[i], e1, sizeof(e1)));
        }
        if (history_asst[i] && strcmp(history_asst[i], "OK") != 0) {
            off += snprintf(body + off, 16384 - off,
                ",{\"role\":\"assistant\",\"content\":\"%s\"}",
                json_esc(history_asst[i], e2, sizeof(e2)));
        }
    }

    /* Current question */
    off += snprintf(body + off, 16384 - off,
        ",{\"role\":\"user\",\"content\":\"%s\"}]}",
        json_esc(question, e1, sizeof(e1)));

    printf("LLM: req=%d bytes, history=%d\n", off, history_count);

    uint8_t *buf = malloc(8192);
    if (!buf) { free(body); return NULL; }
    int len, status;
    http_post("https://api.deepseek.com/v1/chat/completions",
              body, "Authorization", "Bearer " DEEPSEEK_API_KEY, NULL, NULL,
              buf, 8192, &len, &status);
    free(body);

    char *reply = NULL;
    if (status == 200 && len > 0) {
        cJSON *j = cJSON_Parse((char *)buf);
        if (j) {
            cJSON *c = cJSON_GetObjectItem(j, "choices");
            if (c) { c = cJSON_GetArrayItem(c, 0); }
            if (c) { c = cJSON_GetObjectItem(c, "message"); }
            if (c) { c = cJSON_GetObjectItem(c, "content"); }
            if (c && c->valuestring) {
                reply = strdup(c->valuestring);
                printf("LLM: %s\n", reply);
            }
            cJSON_Delete(j);
        }
    }
    free(buf);
    return reply;
}

/* ---- MiMo TTS (same as before) ---- */
static bool tts_speak(const char *text)
{
    char e[1024], *d = e;
    for (const char *s = text; *s && d < e + sizeof(e) - 2; s++) {
        if (*s == '"' || *s == '\\') *d++ = '\\';
        *d++ = *s;
    }
    *d = 0;

    char body[2048];
    snprintf(body, sizeof(body),
        "{\"model\":\"mimo-v2.5-tts\","
        "\"messages\":[{\"role\":\"assistant\",\"content\":\"%s\"}],"
        "\"audio\":{\"format\":\"wav\",\"voice\":\"Chloe\"}}", e);

    uint8_t *buf = malloc(32768);
    if (!buf) return false;
    int len, status;
    http_post("https://api.xiaomimimo.com/v1/chat/completions",
              body, "api-key", MIMO_API_KEY, NULL, NULL,
              buf, 32768, &len, &status);

    bool ok = false;
    if (status == 200) {
        cJSON *j = cJSON_Parse((char *)buf);
        if (j) {
            cJSON *c = cJSON_GetObjectItem(j, "choices");
            if (c) c = cJSON_GetArrayItem(c, 0);
            if (c) c = cJSON_GetObjectItem(c, "message");
            if (c) c = cJSON_GetObjectItem(c, "audio");
            if (c) c = cJSON_GetObjectItem(c, "data");
            if (c && c->valuestring) {
                int blen = strlen(c->valuestring);
                audio_buf = malloc(blen);
                if (audio_buf) {
                    audio_len = b64_decode(c->valuestring, audio_buf, blen);
                    ok = audio_len > 44;
                }
            }
            cJSON_Delete(j);
        }
    }
    free(buf);
    return ok;
}

/* ---- UI ---- */
static void add_msg(const char *prefix, const char *text, uint32_t color)
{
    lv_obj_t *l = lv_label_create(conv_area);
    lv_label_set_text_fmt(l, "%s%s", prefix, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_width(l, 310);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, 10, conv_y);
    conv_y += lv_obj_get_height(l) + 10;
    lv_obj_scroll_to_y(conv_area, conv_y - 180, LV_ANIM_ON);
}

static void ask(const char *question)
{
    add_msg("You: ", question, 0x4488cc);
    lv_label_set_text(status_lbl, "Thinking...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);
    lv_timer_handler();

    char *reply = llm_chat(question);
    if (reply) {
        history_add(question, reply);
        add_msg("AI: ", reply, 0x44cc88);

        lv_label_set_text(status_lbl, "Speaking...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xcc8844), 0);
        lv_timer_handler();

        if (tts_speak(reply)) {
            sys_audio_play_wav(audio_buf, audio_len);
            free(audio_buf); audio_buf = NULL;
        }
        lv_label_set_text(status_lbl, "Ready");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
        free(reply);
    } else {
        add_msg("AI: ", "(error)", 0xcc4444);
        lv_label_set_text(status_lbl, "Error");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xcc4444), 0);
    }
}

static void on_q1(lv_event_t *e) { ask("你好，我是瑞迪，你是谁呀？"); }
static void on_q2(lv_event_t *e) { ask("今天在学校学了加法，有点难"); }
static void on_q3(lv_event_t *e) { ask("给我讲一个小故事吧"); }

static void create(lv_obj_t *parent)
{
    conv_y = 5;
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "Voice AI  |  Hi Reddy!");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8888cc), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 5);

    status_lbl = lv_label_create(root);
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(status_lbl, LV_ALIGN_TOP_RIGHT, -10, 8);

    conv_area = lv_obj_create(root);
    lv_obj_set_size(conv_area, 330, 200);
    lv_obj_align(conv_area, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_style_bg_color(conv_area, lv_color_hex(0x0a0a18), 0);
    lv_obj_set_style_border_width(conv_area, 0, 0);
    lv_obj_set_style_pad_all(conv_area, 5, 0);
    lv_obj_set_scroll_dir(conv_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(conv_area, LV_SCROLLBAR_MODE_OFF);

    static const char *qs[] = {"Who are you?", "School talk", "Tell a story"};
    static lv_event_cb_t cbs[] = {on_q1, on_q2, on_q3};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(root);
        lv_obj_set_size(btn, 105, 36);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 8 + i * 112, -8);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, qs[i]);
        lv_obj_center(bl);
        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, NULL);
    }
}

static void destroy(void)
{
    if (root) { lv_obj_del(root); root = NULL; }
    for (int i = 0; i < history_count; i++) {
        free(history_user[i]);
        free(history_asst[i]);
    }
    history_count = 0;
}

static void resume(void) {}

const app_entry_t app_voice = {
    .name = "Voice AI",
    .create = create, .destroy = destroy, .resume = resume,
};
