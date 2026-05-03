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
#include "esp_random.h"
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

static esp_err_t http_post(const char *url, const char *body, int body_len,
                            const char *ct,
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
    esp_http_client_set_header(cli, "Content-Type", ct);
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "User-Agent", "ESP32-S3");
    if (h1) esp_http_client_set_header(cli, h1, v1);
    if (h2) esp_http_client_set_header(cli, h2, v2);
    esp_http_client_set_post_field(cli, body, body_len);
    ESP_LOGI(TAG, "HTTP POST %s body_len=%d", url, body_len);
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

    char *reply = NULL;
    for (int attempt = 0; attempt < 3 && !reply; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "LLM retry %d", attempt);
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            /* Jitter to avoid server rate-limit window on reconnects */
            int jitter_ms = 500 + (esp_random() % 500);
            vTaskDelay(pdMS_TO_TICKS(jitter_ms));
        }
        int len, status;
        http_post("https://api.deepseek.com/v1/chat/completions",
                  body, (int)strlen(body), "application/json",
                  "Authorization", "Bearer " DEEPSEEK_API_KEY, NULL, NULL,
                  buf, 16384, &len, &status, 8000);
        ESP_LOGI(TAG, "LLM: s=%d l=%d", status, len);
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
    }
    free(body);
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

/* ---- STT: speech-to-text via multipart/form-data POST ---- */
static char *stt_transcribe(const uint8_t *wav, int wav_len)
{
    ESP_LOGI(TAG, "STT start wav=%d", wav_len);
    const char *boundary = "ESP32BoundaryXYZ";
    const char *fd1 = "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    const char *fd2 = "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1";

    int body_len = 0;
    body_len += 2 + strlen(boundary) + 2;                     /* --boundary\r\n */
    body_len += strlen(fd1);                                   /* file header */
    body_len += wav_len;                                       /* WAV data */
    body_len += 2 + 2 + strlen(boundary) + 2;                 /* \r\n--boundary\r\n */
    body_len += strlen(fd2);                                   /* model field */
    body_len += 2 + 2 + strlen(boundary) + 2 + 2;             /* \r\n--boundary--\r\n */

    char *body = malloc(body_len + 1);
    if (!body) { ESP_LOGE(TAG, "STT no mem for body"); return NULL; }
    char *p = body;
    p += sprintf(p, "--%s\r\n", boundary);
    p += sprintf(p, "%s", fd1);
    memcpy(p, wav, wav_len); p += wav_len;
    p += sprintf(p, "\r\n--%s\r\n", boundary);
    p += sprintf(p, "%s", fd2);
    p += sprintf(p, "\r\n--%s--\r\n", boundary);

    uint8_t *buf = malloc(32768);
    if (!buf) { free(body); return NULL; }
    int len, status;
    esp_err_t err = http_post(STT_API_URL, body, body_len,
        "multipart/form-data; boundary=ESP32BoundaryXYZ",
        "Authorization", "Bearer " STT_API_KEY, NULL, NULL,
        buf, 32768, &len, &status, 30000);
    free(body);

    char *text = NULL;
    if (err == ESP_OK && status == 200 && len > 0) {
        cJSON *root = cJSON_Parse((char*)buf);
        if (root) {
            cJSON *j = cJSON_GetObjectItem(root, "text");
            if (j && j->valuestring && strlen(j->valuestring) > 0)
                text = strdup(j->valuestring);
            cJSON_Delete(root);
        }
    }
    free(buf);
    ESP_LOGI(TAG, "STT: s=%d text=%s", status, text ? text : "(null)");
    return text;
}

/* ---- TTS ---- */
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

    /* Allocate from PSRAM. 2MB holds ~22s of TTS audio (mono 16-bit 24kHz base64). */
    uint8_t *buf = heap_caps_malloc(2097152, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(2097152);
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
                  body, (int)strlen(body), "application/json",
                  "Authorization", "Bearer " MIMO_API_KEY, NULL, NULL,
                  buf, 2097152, &len, &status, 120000);
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

/* ---- Mic button state (forward decl — used by ask_result_cb) ---- */
enum { MIC_IDLE, MIC_RECORDING, MIC_PROCESSING };
static volatile int mic_state = MIC_IDLE;
static lv_obj_t *mic_btn, *mic_lbl;
static void update_mic_btn(void)
{
    switch (mic_state) {
    case MIC_IDLE:
        lv_label_set_text(mic_lbl, "Tap to Talk");
        lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0x3366cc), 0);
        break;
    case MIC_RECORDING:
        lv_label_set_text(mic_lbl, "Recording... Tap to Stop");
        lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0xcc3333), 0);
        break;
    case MIC_PROCESSING:
        lv_label_set_text(mic_lbl, "Processing...");
        lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0x888833), 0);
        break;
    }
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
    if (mic_state != MIC_IDLE) {
        mic_state = MIC_IDLE;
        update_mic_btn();
    }
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

/* Voice background task: STT → LLM → TTS */
static void voice_ask_bg_task(void *arg)
{
    uint8_t *wav = ((uint8_t**)arg)[0];
    int wav_len = (int)((uint8_t**)arg)[1];
    free(arg);

    ESP_LOGI(TAG, "VV1 voice task, wifi=%d wav=%d", sys_wifi_is_connected(), wav_len);

    char *q = stt_transcribe(wav, wav_len);
    free(wav);

    if (!q) {
        ESP_LOGI(TAG, "VVF STT fail");
        ask_state = -1;
        voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "VV2 STT ok: %s", q);
    add_msg("You: ", q, 0x4488cc);
    ask_result_text = NULL;
    ask_state = 0;

    char *reply = llm_chat(q);
    if (reply) {
        ESP_LOGI(TAG, "VV3 LLM ok len=%d", (int)strlen(reply));
        history_add(q, reply);
        ask_result_text = reply;
        ask_state = 2;
        char *tts_text = strdup(reply);
        if (tts_text) {
            sys_audio_init();
            tts_play(tts_text);
            free(tts_text);
        }
    } else {
        ESP_LOGI(TAG, "VVF LLM fail");
        ask_state = -1;
    }
    free(q);
    ESP_LOGI(TAG, "VVE voice task end");
    mic_state = MIC_IDLE;
    voice_task_handle = NULL;
    vTaskDelete(NULL);
}

static void on_mic_btn(lv_event_t *e)
{
    ESP_LOGI(TAG, "Mic btn state=%d", mic_state);

    if (mic_state == MIC_IDLE) {
        if (ask_state || voice_task_handle) {
            ESP_LOGI(TAG, "busy, skip");
            return;
        }
        sys_audio_init();
        if (!sys_audio_record_start(30000)) {
            ESP_LOGE(TAG, "record start fail");
            return;
        }
        mic_state = MIC_RECORDING;
        update_mic_btn();
        lv_label_set_text(status_lbl, "Recording...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xcc4444), 0);

    } else if (mic_state == MIC_RECORDING) {
        int wav_len = 0;
        uint8_t *wav = sys_audio_record_stop(&wav_len);
        mic_state = MIC_PROCESSING;
        update_mic_btn();
        lv_label_set_text(status_lbl, "Processing...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);

        if (!wav || wav_len < 100) {
            ESP_LOGW(TAG, "recording too short: %d", wav_len);
            if (wav) free(wav);
            mic_state = MIC_IDLE;
            update_mic_btn();
            lv_label_set_text(status_lbl, "Ready");
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
            return;
        }

        /* Pack wav pointer + length into arg for the background task */
        uint8_t **arg = malloc(2 * sizeof(void*));
        arg[0] = wav;
        arg[1] = (uint8_t*)(intptr_t)wav_len;
        ask_state = 1;
        voice_task_handle = xTaskCreateStatic(voice_ask_bg_task, "voice_bg",
            VOICE_STACK_WORDS, arg, 3, voice_task_stack, &voice_task_tcb);
        if (!voice_task_handle) {
            free(wav);
            free(arg);
            mic_state = MIC_IDLE;
            update_mic_btn();
            ask_state = -1;
        }
        lv_timer_create(ask_result_cb, 300, NULL);
    }
}

static void create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Voice AI app created");
    conv_y = 5; history_load_from_sd();
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
    lv_obj_set_size(conv_area, 330, 180);
    lv_obj_align(conv_area, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_style_bg_color(conv_area, lv_color_hex(0x0a0a18), 0);
    lv_obj_set_style_border_width(conv_area, 0, 0);
    lv_obj_set_style_pad_all(conv_area, 5, 0);
    lv_obj_set_scroll_dir(conv_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(conv_area, LV_SCROLLBAR_MODE_OFF);

    /* Large mic button */
    mic_btn = lv_btn_create(root);
    lv_obj_set_size(mic_btn, 280, 100);
    lv_obj_align(mic_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_radius(mic_btn, 50, 0);
    lv_obj_add_event_cb(mic_btn, on_mic_btn, LV_EVENT_CLICKED, NULL);

    mic_lbl = lv_label_create(mic_btn);
    lv_obj_set_style_text_font(mic_lbl, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(mic_lbl, lv_color_white(), 0);
    lv_obj_center(mic_lbl);

    mic_state = MIC_IDLE;
    update_mic_btn();
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
