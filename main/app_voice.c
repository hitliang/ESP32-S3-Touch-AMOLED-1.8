#include "app_voice.h"
#include "sys_wifi.h"
#include "sys_audio.h"
#include "sys_sdcard.h"
#include "secrets.h"
#include "lvgl.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "tts_test_wav.h"
#include "tts_test_short_wav.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HISTORY_FILE    "voice_history.txt"
#define MAX_HISTORY     200
#define BODY_MAX        32768

static const char *TAG = "voice";

/* ---- System Prompt ---- */
static const char *SYSTEM_PROMPT =
    "你是一个给6岁小男孩用的语音助手。"
    "男孩中文名叫瑞迪，英文名Reddy，上小学一年级。"
    "规则：用简单中文，句子短，适合6岁小孩。"
    "语气温暖耐心，不要讲可怕内容。回答简短，不超过三句话。";

/* ---- JSON escape ---- */
static char *json_esc(const char *s, char *buf, int max)
{
    char *d = buf;
    while (*s && d < buf + max - 6) {
        switch (*s) {
        case '"':  *d++ = '\\'; *d++ = '"';  break;
        case '\\': *d++ = '\\'; *d++ = '\\'; break;
        case '\n': *d++ = '\\'; *d++ = 'n';  break;
        case '\r': *d++ = '\\'; *d++ = 'r';  break;
        case '\t': *d++ = '\\'; *d++ = 't';  break;
        default:   *d++ = *s; break;
        }
        s++;
    }
    *d = 0;
    return buf;
}

/* ---- HTTP ---- */
typedef struct { uint8_t *buf; int len, max, status; bool truncated; } hctx_t;
static esp_err_t hevt(esp_http_client_event_t *e)
{
    hctx_t *c = (hctx_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (c->len + e->data_len < c->max) {
            memcpy(c->buf + c->len, e->data, e->data_len);
            c->len += e->data_len; c->buf[c->len] = 0;
        } else {
            c->truncated = true;
        }
    }
    if (e->event_id == HTTP_EVENT_ON_FINISH)
        c->status = esp_http_client_get_status_code(e->client);
    return ESP_OK;
}

static esp_err_t http_post(const char *url, const char *body,
                            const char *h1, const char *v1,
                            const char *h2, const char *v2,
                            uint8_t *buf, int max, int *olen, int *osta,
                            int timeout_ms)
{
    hctx_t ctx = { .buf = buf, .len = 0, .max = max, .status = 0, .truncated = false };
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = timeout_ms,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = hevt, .user_data = &ctx,
        .buffer_size = 16384, .buffer_size_tx = 4096,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "User-Agent", "ESP32-S3");
    if (h1) esp_http_client_set_header(cli, h1, v1);
    if (h2) esp_http_client_set_header(cli, h2, v2);
    esp_http_client_set_post_field(cli, body, strlen(body));
    ESP_LOGI(TAG, "HTTP POST %s body_len=%d", url, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);
    *olen = ctx.len; *osta = ctx.status;
    if (ctx.truncated) {
        ESP_LOGW(TAG, "HTTP response truncated! buf=%d max=%d", ctx.len, ctx.max);
    }
    return err;
}

/* ---- History ---- */
static char *history_user[MAX_HISTORY];
static char *history_asst[MAX_HISTORY];
static int   history_count = 0;

static void history_save_to_sd(void)
{
    if (!sys_sdcard_mounted()) return;
    char p[128]; snprintf(p, sizeof(p), "%s/" HISTORY_FILE, sys_sdcard_mount_point());
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int i = 0; i < history_count; i++) {
        if (history_user[i]) fprintf(f, "U:%s\n", history_user[i]);
        if (history_asst[i]) fprintf(f, "A:%s\n", history_asst[i]);
    }
    fclose(f);
}

static void history_load_from_sd(void)
{
    if (!sys_sdcard_mounted()) return;
    char p[128]; snprintf(p, sizeof(p), "%s/" HISTORY_FILE, sys_sdcard_mount_point());
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f) && history_count < MAX_HISTORY) {
        int len = strlen(line); if (len>0 && line[len-1]=='\n') line[len-1]=0;
        if (line[0]=='U' && line[1]==':') history_user[history_count] = strdup(line+2);
        else if (line[0]=='A' && line[1]==':') {
            history_asst[history_count] = strdup(line+2); history_count++;
        }
    }
    fclose(f);
}

static void history_add(const char *u, const char *a)
{
    if (history_count >= MAX_HISTORY - 5) {
        char s[2048]="[Earlier: "; int n=10, keep=history_count-n;
        for (int i=0; i<n && strlen(s)<sizeof(s)-100; i++)
            snprintf(s+strlen(s), sizeof(s)-strlen(s), "Q:%s A:%s ", history_user[i]?:"?", history_asst[i]?:"?");
        strncat(s,"]",sizeof(s)-strlen(s)-1);
        for (int i=0;i<n;i++){free(history_user[i]);free(history_asst[i]);}
        for (int i=0;i<keep;i++){history_user[i]=history_user[i+n];history_asst[i]=history_asst[i+n];}
        for (int i=keep;i<MAX_HISTORY;i++){history_user[i]=NULL;history_asst[i]=NULL;}
        history_count=keep;
        history_user[0]=strdup(s); free(history_asst[0]); history_asst[0]=strdup("OK");
    }
    history_user[history_count]=u?strdup(u):strdup("");
    history_asst[history_count]=a?strdup(a):strdup("");
    history_count++;
    history_save_to_sd();
}

/* ---- LLM ---- */
static char *llm_chat(const char *question)
{
    if (!question) { ESP_LOGI(TAG, "LLM: null q"); return NULL; }
    char *body = heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) body = malloc(BODY_MAX);
    if (!body) { ESP_LOGI(TAG, "LLM: no mem"); return NULL; }

    char *e1 = malloc(2048), *e2 = malloc(2048);
    if (!e1 || !e2) { free(e1); free(e2); free(body); return NULL; }
    int off = snprintf(body, BODY_MAX,
        "{\"model\":\"deepseek-v4-flash\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s\"}",
        json_esc(SYSTEM_PROMPT, e1, 2048));

    for (int i=0; i<history_count && off<BODY_MAX-4096; i++) {
        if (history_user[i])
            off += snprintf(body+off, BODY_MAX-off,
                ",{\"role\":\"user\",\"content\":\"%s\"}", json_esc(history_user[i], e1, 2048));
        if (history_asst[i] && strcmp(history_asst[i],"OK"))
            off += snprintf(body+off, BODY_MAX-off,
                ",{\"role\":\"assistant\",\"content\":\"%s\"}", json_esc(history_asst[i], e2, 2048));
    }
    off += snprintf(body+off, BODY_MAX-off,
        ",{\"role\":\"user\",\"content\":\"%s\"}]}", json_esc(question, e1, 2048));

    uint8_t *buf = malloc(16384);
    if (!buf) { free(body); free(e1); free(e2); ESP_LOGI(TAG, "LLM: no buf"); return NULL; }
    int len, status;
    http_post("https://api.deepseek.com/v1/chat/completions",
              body, "Authorization", "Bearer " DEEPSEEK_API_KEY, NULL, NULL,
              buf, 16384, &len, &status, 25000);
    free(body);
    ESP_LOGI(TAG, "LLM: s=%d l=%d", status, len);

    char *reply = NULL;
    if (status == 200 && len > 0) {
        cJSON *root = cJSON_Parse((char*)buf);
        if (root) {
            cJSON *j = cJSON_GetObjectItem(root, "choices");
            if (j) j = cJSON_GetArrayItem(j, 0);
            if (j) j = cJSON_GetObjectItem(j, "message");
            if (j) j = cJSON_GetObjectItem(j, "content");
            if (j && j->valuestring) reply = strdup(j->valuestring);
            cJSON_Delete(root);
        }
    }
    free(buf);
    free(e1); free(e2);
    return reply;
}

/* ---- TTS ---- */
static char *extract_b64(const char *json, int json_len)
{
    const char *needle = "\"data\":\"";
    const char *p = json;
    const char *end = json + json_len;
    while (p < end - 20) {
        const char *f = memchr(p, '"', end - p);
        if (!f) break;
        if (strncmp(f, needle, 8) == 0) {
            const char *start = f + 8;
            const char *q = start;
            while (q < end && *q != '"') q++;
            int bl = q - start;
            if (bl > 0) {
                char *out = malloc(bl + 1);
                if (out) { memcpy(out, start, bl); out[bl] = 0; }
                return out;
            }
        }
        p = f + 1;
    }
    return NULL;
}

static int b64_decode(const char *in, uint8_t *out, int max)
{
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len=0,bits=0,val=0;
    for(const char *p=in;*p;p++){const char *c=strchr(t,*p);if(!c)continue;
        val=(val<<6)|(int)(c-t);bits+=6;
        if(bits>=8){bits-=8;if(len<max)out[len++]=(val>>bits)&0xFF;}}
    return len;
}

static bool tts_play(const char *text)
{
    ESP_LOGI(TAG, "T0 TTS start heap=%lu psram=%lu",
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    char *e = malloc(2048), *body = malloc(4096);
    if (!e || !body) { ESP_LOGI(TAG, "T1 TTS no mem"); free(e); free(body); return false; }
    json_esc(text, e, 2048);
    snprintf(body, 4096,
        "{\"model\":\"mimo-v2-tts\","
        "\"messages\":["
        "{\"role\":\"user\",\"content\":\"please speak\"},"
        "{\"role\":\"assistant\",\"content\":\"%s\"}"
        "],\"audio\":{\"format\":\"wav\",\"voice\":\"mimo_default\"}}", e);
    free(e);

    /* Allocate from PSRAM. 1MB holds ~11s of TTS audio (mono 16-bit 24kHz base64). */
    uint8_t *buf = heap_caps_malloc(1048576, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(1048576);
    if (!buf) { ESP_LOGE(TAG, "T1 TTS no resp buf"); free(body); return false; }

    bool ok = false;
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "TTS retry attempt %d", attempt);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        ESP_LOGI(TAG, "T1.5 heap=%lu", (unsigned long)esp_get_free_heap_size());
        int len, status;
        esp_err_t err = http_post("https://api.xiaomimimo.com/v1/chat/completions",
                  body, "Authorization", "Bearer " MIMO_API_KEY, NULL, NULL,
                  buf, 1048576, &len, &status, 120000);
        ESP_LOGI(TAG, "T2 TTS HTTP err=%d s=%d l=%d", err, status, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "T2 HTTP request failed: %s", esp_err_to_name(err));
            continue;
        }
        if (status != 200 || len <= 0) {
            ESP_LOGE(TAG, "T2 HTTP status=%d", status);
            continue;
        }

        char *b64 = extract_b64((char*)buf, len);
        int bl = b64 ? strlen(b64) : 0;
        ESP_LOGI(TAG, "T2.5 b64 len=%d", bl);
        if (b64 && bl > 0) {
            uint8_t *wav = malloc(bl);
            if (wav) {
                int wlen = b64_decode(b64, wav, bl);
                ESP_LOGI(TAG, "T3 wav len=%d", wlen);
                if (wlen > 44) { sys_audio_play_wav(wav, wlen); ok = true; }
                else ESP_LOGW(TAG, "T3 wav too short: %d", wlen);
                free(wav);
            } else ESP_LOGE(TAG, "T3 no mem for wav");
            free(b64);
        } else ESP_LOGW(TAG, "T4 no audio data found");
    }
    free(buf);
    free(body);
    return ok;
}

/* ---- UI ---- */
static lv_obj_t *root, *conv_area, *status_lbl;
static int conv_y = 5;
static volatile int ask_state = 0;
static char *ask_result_text = NULL;
static TaskHandle_t voice_task_handle = NULL;

/* Static task allocation — avoids heap fragmentation issues after vTaskDelete */
#define VOICE_STACK_WORDS 4096  /* 4096 words = 16KB */
static StackType_t voice_task_stack[VOICE_STACK_WORDS];
static StaticTask_t voice_task_tcb;

extern const lv_font_t lv_font_simsun_16_cjk;

static void add_msg(const char *pfx, const char *txt, uint32_t color)
{
    lv_obj_t *l = lv_label_create(conv_area);
    lv_label_set_text_fmt(l, "%s%s", pfx, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_width(l, 310);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(l, 10, conv_y);
    conv_y += lv_obj_get_height(l) + 10;
    lv_obj_scroll_to_y(conv_area, conv_y-180, LV_ANIM_ON);
}

static void ask_bg_task(void *arg)
{
    char *q = (char*)arg;
    ESP_LOGI(TAG, "V1 task start, wifi=%d heap=%lu", sys_wifi_is_connected(), esp_get_free_heap_size());
    char *reply = llm_chat(q);
    if (reply) {
        ESP_LOGI(TAG, "V2 LLM ok len=%d", (int)strlen(reply));
        history_add(q, reply);
        ask_result_text = reply;
        ask_state = 2;
        ESP_LOGI(TAG, "V3 TTS start");
        char *tts_text = strdup(reply);
        if (tts_text) {
            sys_audio_init();
            ESP_LOGI(TAG, "V4 audio init done");
            tts_play(tts_text);
            ESP_LOGI(TAG, "V5 TTS done");
            free(tts_text);
        }
    } else {
        ESP_LOGI(TAG, "VF LLM fail");
        ask_state = -1;
    }
    free(q);
    ESP_LOGI(TAG, "VE task end");
    voice_task_handle = NULL;
    vTaskDelete(NULL);
}

static void ask_result_cb(lv_timer_t *t)
{
    ESP_LOGI(TAG, "timer state=%d", ask_state);
    if (ask_state==0 || ask_state==1) return;
    lv_timer_del(t);
    if (ask_state==2 && ask_result_text) {
        add_msg("AI: ", ask_result_text, 0x44cc88);
        free(ask_result_text); ask_result_text=NULL;
    } else add_msg("AI: ", "(error)", 0xcc4444);
    ask_state=0;
    lv_label_set_text(status_lbl, "Ready");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
}

static void ask(const char *q)
{
    ESP_LOGI(TAG, "ask() called, state=%d", ask_state);
    if (ask_state) { ESP_LOGI(TAG, "busy, skip"); return; }

    /* Don't kill old task — if TTS/HTTP in progress, socket will leak.
       Instead, just skip if previous task still running. */
    if (voice_task_handle) {
        ESP_LOGI(TAG, "prev task still running, skip");
        return;
    }

    add_msg("You: ", q, 0x4488cc);
    lv_label_set_text(status_lbl, "Thinking...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);
    ask_state=1;
    char *s = strdup(q); if (!s) { ask_state=-1; ESP_LOGI(TAG, "strdup fail"); return; }
    /* Use static allocation — always succeeds, no heap fragmentation issues */
    voice_task_handle = xTaskCreateStatic(ask_bg_task, "voice_bg",
        VOICE_STACK_WORDS, s, 3, voice_task_stack, &voice_task_tcb);
    if (!voice_task_handle) {
        free(s);
        ask_state = -1;
        ESP_LOGI(TAG, "task create fail");
    }
    lv_timer_create(ask_result_cb, 300, NULL);
}

static void on_q1(lv_event_t *e) { ESP_LOGI(TAG, "BTN1 clicked"); ask("Hi, who are you?"); }
static void on_q2(lv_event_t *e) { ESP_LOGI(TAG, "BTN2 clicked"); ask("Tell me a short story"); }
static void tts_raw_tls_task(void *arg)
{
    const char *body = "{\"model\":\"mimo-v2-tts\",\"messages\":["
        "{\"role\":\"user\",\"content\":\"hi\"},"
        "{\"role\":\"assistant\",\"content\":\"hello\"}"
        "],\"audio\":{\"format\":\"wav\",\"voice\":\"mimo_default\"}}";
    int body_len = strlen(body);

    for (int attempt = 0; attempt < 3; attempt++) {
        ESP_LOGI(TAG, "RAW attempt %d heap=%lu", attempt, (unsigned long)esp_get_free_heap_size());
        esp_tls_t *tls = esp_tls_init();
        if (!tls) { ESP_LOGE(TAG, "RAW: esp_tls_init fail"); break; }

        esp_tls_cfg_t cfg = {
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 30000,
        };
        int ret = esp_tls_conn_new_sync("api.xiaomimimo.com", strlen("api.xiaomimimo.com"), 443, &cfg, tls);
        if (ret < 0) { ESP_LOGE(TAG, "RAW: conn fail ret=%d", ret); esp_tls_conn_destroy(tls); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }

        char *hdr = malloc(512);
        if (!hdr) { esp_tls_conn_destroy(tls); break; }
        int hdr_len = snprintf(hdr, 512,
            "POST /v1/chat/completions HTTP/1.1\r\n"
            "Host: api.xiaomimimo.com\r\n"
            "Content-Type: application/json\r\n"
            "Authorization: Bearer " MIMO_API_KEY "\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n", body_len);
        esp_tls_conn_write(tls, hdr, hdr_len);
        esp_tls_conn_write(tls, body, body_len);
        free(hdr);

        int rbuf_sz = 131072;
        char *rbuf = malloc(rbuf_sz);
        if (!rbuf) { esp_tls_conn_destroy(tls); break; }
        int total = 0;
        while (total < rbuf_sz - 1) {
            int r = esp_tls_conn_read(tls, rbuf + total, rbuf_sz - 1 - total);
            if (r <= 0) break;
            total += r;
            rbuf[total] = 0;
        }
        esp_tls_conn_destroy(tls);

        ESP_LOGI(TAG, "RAW: attempt %d done, got %d bytes", attempt, total);
        if (total > 100 && strstr(rbuf, "200 OK")) {
            ESP_LOGI(TAG, "RAW: SUCCESS on attempt %d", attempt);
            /* Find JSON body start */
            char *json_start = strstr(rbuf, "\r\n\r\n");
            if (json_start) {
                json_start += 4;
                ESP_LOGI(TAG, "RAW JSON: %.500s", json_start);
            }
            free(rbuf);
            break;
        } else {
            ESP_LOGW(TAG, "RAW: attempt %d failed, got %d bytes", attempt, total);
            free(rbuf);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    ESP_LOGI(TAG, "RAW TLS test done heap=%lu", (unsigned long)esp_get_free_heap_size());
    vTaskDelete(NULL);
}

static void on_q3(lv_event_t *e) {
    ESP_LOGI(TAG, "BTN3 - embedded WAV test");
    sys_audio_init();
    sys_audio_play_wav(tts_test_short_wav, tts_test_short_wav_len);
    ESP_LOGI(TAG, "BTN3 - play done");
}

static void create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Voice AI app created");
    conv_y=5; history_load_from_sd();
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "Voice AI | Hi Reddy!");
    lv_obj_set_style_text_font(t, &lv_font_simsun_16_cjk, 0);
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

    static const char *qs[]={"Who r u?","A story","1+1=?"};
    static lv_event_cb_t cbs[]={on_q1,on_q2,on_q3};
    for(int i=0;i<3;i++){
        lv_obj_t *btn=lv_btn_create(root); lv_obj_set_size(btn,105,36);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 8+i*112, -8);
        lv_obj_t *bl=lv_label_create(btn); lv_label_set_text(bl,qs[i]); lv_obj_center(bl);
        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, NULL);
    }
}

static void destroy(void)
{
    /* Let background task finish naturally; don't vTaskDelete
       or HTTP sockets will leak and exhaust the pool. */
    ask_state = 0;
    ask_result_text = NULL;
    if (root) { lv_obj_del(root); root=NULL; }
    for (int i=0;i<history_count;i++){free(history_user[i]);free(history_asst[i]);}
    history_count=0;
}
static void resume(void) {}

const app_entry_t app_voice = {
    .name = "Voice AI",
    .create = create, .destroy = destroy, .resume = resume,
};
