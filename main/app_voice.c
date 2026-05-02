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

static lv_obj_t *root = NULL;
static lv_obj_t *conv_area = NULL;
static lv_obj_t *status_lbl = NULL;
static lv_obj_t *btn_speak = NULL;

static uint8_t *audio_buf = NULL;
static int audio_len = 0;
static int conv_y = 5;

/* ---- Base64 decode ---- */
static int b64_decode(const char *in, uint8_t *out, int max)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len = 0, bits = 0, val = 0;
    for (const char *p = in; *p; p++) {
        const char *c = strchr(tbl, *p);
        if (!c) continue;
        val = (val << 6) | (int)(c - tbl);
        bits += 6;
        if (bits >= 8) { bits -= 8; if (len < max) out[len++] = (val >> bits) & 0xFF; }
    }
    return len;
}

/* ---- HTTP helpers ---- */
typedef struct { uint8_t *buf; int len, max, status; } http_ctx_t;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    http_ctx_t *c = (http_ctx_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && c->len + e->data_len < c->max) {
        memcpy(c->buf + c->len, e->data, e->data_len);
        c->len += e->data_len; c->buf[c->len] = 0;
    }
    if (e->event_id == HTTP_EVENT_ON_FINISH)
        c->status = esp_http_client_get_status_code(e->client);
    return ESP_OK;
}

static esp_err_t http_post(const char *url, const char *body,
                            const char *auth_hdr, const char *auth_val,
                            uint8_t *buf, int max, int *out_len, int *out_status)
{
    http_ctx_t ctx = { .buf = buf, .len = 0, .max = max, .status = 0 };
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 20000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_evt, .user_data = &ctx,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    if (auth_hdr) esp_http_client_set_header(cli, auth_hdr, auth_val);
    esp_http_client_set_post_field(cli, body, strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);
    *out_len = ctx.len;
    *out_status = ctx.status;
    return err;
}

/* ---- DeepSeek LLM ---- */
static char *llm_chat(const char *question)
{
    char body[2048];
    /* Escape quotes in question for JSON */
    char safe_q[512];
    const char *s = question;
    char *d = safe_q;
    while (*s && d < safe_q + sizeof(safe_q) - 2) {
        if (*s == '"' || *s == '\\') *d++ = '\\';
        *d++ = *s++;
    }
    *d = 0;

    snprintf(body, sizeof(body),
             "{\"model\":\"deepseek-chat\",\"messages\":["
             "{\"role\":\"system\",\"content\":\"You are a friendly Chinese voice assistant. Keep answers short, under 50 words. Reply in Chinese.\"},"
             "{\"role\":\"user\",\"content\":\"%s\"}]}",
             safe_q);

    uint8_t *buf = malloc(4096);
    if (!buf) return NULL;
    int len, status;
    esp_err_t err = http_post("https://api.deepseek.com/v1/chat/completions",
                               body, "Authorization", "Bearer " DEEPSEEK_API_KEY,
                               buf, 4096, &len, &status);
    printf("LLM: err=%d status=%d\n", err, status);

    char *reply = NULL;
    if (err == ESP_OK && status == 200) {
        cJSON *j = cJSON_Parse((char *)buf);
        if (j) {
            cJSON *choices = cJSON_GetObjectItem(j, "choices");
            if (choices) {
                cJSON *c0 = cJSON_GetArrayItem(choices, 0);
                if (c0) {
                    cJSON *msg = cJSON_GetObjectItem(c0, "message");
                    if (msg) {
                        cJSON *content = cJSON_GetObjectItem(msg, "content");
                        if (content && content->valuestring) {
                            reply = strdup(content->valuestring);
                            printf("LLM: %s\n", reply);
                        }
                    }
                }
            }
            cJSON_Delete(j);
        }
    }
    free(buf);
    return reply;
}

/* ---- MiMo TTS ---- */
static bool tts_speak(const char *text)
{
    /* Escape text for JSON */
    char safe[512];
    const char *s = text;
    char *d = safe;
    while (*s && d < safe + sizeof(safe) - 2) {
        if (*s == '"' || *s == '\\') *d++ = '\\';
        *d++ = *s++;
    }
    *d = 0;

    char body[1024];
    snprintf(body, sizeof(body),
             "{\"model\":\"mimo-v2.5-tts\","
             "\"messages\":[{\"role\":\"assistant\",\"content\":\"%s\"}],"
             "\"audio\":{\"format\":\"wav\",\"voice\":\"Chloe\"}}",
             safe);

    uint8_t *buf = malloc(32768);
    if (!buf) return false;
    int len, status;
    esp_err_t err = http_post("https://api.xiaomimimo.com/v1/chat/completions",
                               body, "api-key", MIMO_API_KEY,
                               buf, 32768, &len, &status);
    printf("TTS: err=%d status=%d len=%d\n", err, status, len);

    bool ok = false;
    if (err == ESP_OK && status == 200) {
        cJSON *j = cJSON_Parse((char *)buf);
        if (j) {
            cJSON *choices = cJSON_GetObjectItem(j, "choices");
            if (choices) {
                cJSON *c0 = cJSON_GetArrayItem(choices, 0);
                if (c0) {
                    cJSON *msg = cJSON_GetObjectItem(c0, "message");
                    if (msg) {
                        cJSON *audio = cJSON_GetObjectItem(msg, "audio");
                        if (audio) {
                            cJSON *data = cJSON_GetObjectItem(audio, "data");
                            if (data && data->valuestring) {
                                int b64len = strlen(data->valuestring);
                                audio_buf = malloc(b64len);
                                if (audio_buf) {
                                    audio_len = b64_decode(data->valuestring, audio_buf, b64len);
                                    ok = audio_len > 44;
                                }
                            }
                        }
                    }
                }
            }
            cJSON_Delete(j);
        }
    }
    free(buf);
    return ok;
}

/* ---- UI helpers ---- */
static void add_msg(const char *prefix, const char *text, uint32_t color)
{
    lv_obj_t *l = lv_label_create(conv_area);
    lv_label_set_text_fmt(l, "%s%s", prefix, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_width(l, 310);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, 10, conv_y);
    lv_obj_set_style_pad_all(l, 4, 0);
    conv_y += lv_obj_get_height(l) + 10;
    lv_obj_scroll_to_y(conv_area, conv_y - 200, LV_ANIM_ON);
}

/* ---- Ask + LLM + TTS ---- */
static void ask(const char *question)
{
    add_msg("You: ", question, 0x4488cc);

    lv_label_set_text(status_lbl, "Thinking...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);

    char *reply = llm_chat(question);
    if (reply) {
        add_msg("AI: ", reply, 0x44cc88);

        lv_label_set_text(status_lbl, "Speaking...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xcc8844), 0);
        lv_timer_handler(); /* flush UI */

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

/* ---- Preset question buttons ---- */
static void on_q1(lv_event_t *e) { ask("今天北京天气怎么样？"); }
static void on_q2(lv_event_t *e) { ask("讲一个10秒的笑话"); }
static void on_q3(lv_event_t *e) { ask("用一句话介绍你自己"); }

/* ---- Create UI ---- */
static void create(lv_obj_t *parent)
{
    conv_y = 5;
    audio_len = 0;

    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    /* Title */
    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "Voice AI");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8888cc), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 5);

    /* Status */
    status_lbl = lv_label_create(root);
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(status_lbl, LV_ALIGN_TOP_RIGHT, -10, 8);

    /* Conversation area */
    conv_area = lv_obj_create(root);
    lv_obj_set_size(conv_area, 330, 200);
    lv_obj_align(conv_area, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_style_bg_color(conv_area, lv_color_hex(0x0a0a18), 0);
    lv_obj_set_style_border_width(conv_area, 0, 0);
    lv_obj_set_style_pad_all(conv_area, 5, 0);
    lv_obj_set_scroll_dir(conv_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(conv_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(conv_area, LV_OBJ_FLAG_SCROLLABLE);

    /* Preset buttons */
    static const char *qs[] = {"Weather?", "Tell a joke", "About you"};
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

static void destroy(void) { if (root) { lv_obj_del(root); root = NULL; } }
static void resume(void) {}

const app_entry_t app_voice = {
    .name = "Voice AI",
    .create = create, .destroy = destroy, .resume = resume,
};
