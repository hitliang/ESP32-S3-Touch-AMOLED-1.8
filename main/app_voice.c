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

static const char *TAG = "voice";

static lv_obj_t *root = NULL, *status_lbl = NULL;
static int state = 0; /* 0=idle, 1=loading, 2=speaking, 3=done, -1=error */
static uint8_t *audio_buf = NULL;
static int audio_len = 0;

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

/* ---- HTTP fetch with event handler ---- */
typedef struct { uint8_t *buf; int len, max; int status; } vctx_t;

static esp_err_t evt(esp_http_client_event_t *e)
{
    vctx_t *c = (vctx_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && c->len + e->data_len < c->max) {
        memcpy(c->buf + c->len, e->data, e->data_len);
        c->len += e->data_len; c->buf[c->len] = 0;
    }
    if (e->event_id == HTTP_EVENT_ON_FINISH)
        c->status = esp_http_client_get_status_code(e->client);
    return ESP_OK;
}

/* ---- TTS API call ---- */
static bool tts_speak(const char *text)
{
    if (!sys_wifi_is_connected()) return false;

    /* Build JSON body */
    char body[1024];
    snprintf(body, sizeof(body),
             "{\"model\":\"mimo-v2.5-tts\","
             "\"messages\":[{\"role\":\"assistant\",\"content\":\"%s\"}],"
             "\"audio\":{\"format\":\"wav\",\"voice\":\"%s\"}}",
             text, "Chloe");

    char url[256];
    snprintf(url, sizeof(url), "https://api.xiaomimimo.com/v1/chat/completions");

    vctx_t ctx = { .buf = malloc(16384), .len = 0, .max = 16384, .status = 0 };
    if (!ctx.buf) return false;

    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 20000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = evt, .user_data = &ctx,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    esp_http_client_set_method(cli, HTTP_METHOD_POST);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_header(cli, "api-key", MIMO_API_KEY);
    esp_http_client_set_post_field(cli, body, strlen(body));

    esp_err_t err = esp_http_client_perform(cli);
    esp_http_client_cleanup(cli);

    printf("TTS: err=%d status=%d\n", err, ctx.status);

    if (err == ESP_OK && ctx.status == 200) {
        /* Parse JSON: choices[0].message.audio.data */
        cJSON *j = cJSON_Parse((char *)ctx.buf);
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
                                const char *b64 = data->valuestring;
                                audio_buf = malloc(strlen(b64));
                                if (audio_buf) {
                                    audio_len = b64_decode(b64, audio_buf, strlen(b64));
                                    printf("TTS: audio=%d bytes\n", audio_len);
                                }
                            }
                        }
                    }
                }
            }
            cJSON_Delete(j);
        }
    }
    free(ctx.buf);
    return audio_len > 0;
}

/* ---- LVGL UI ---- */
static void on_speak(lv_event_t *e)
{
    state = 1;
    lv_label_set_text(status_lbl, "Speaking...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);

    bool ok = tts_speak("你好，我是你的语音助手。今天天气怎么样？");
    if (ok && audio_buf && audio_len > 44) {
        state = 2;
        lv_label_set_text(status_lbl, "Playing...");
        sys_audio_play_wav(audio_buf, audio_len);
        free(audio_buf); audio_buf = NULL;
        state = 3;
        lv_label_set_text(status_lbl, "Done!");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44cc44), 0);
    } else {
        state = -1;
        lv_label_set_text(status_lbl, "Error!");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xcc4444), 0);
    }
}

static void create(lv_obj_t *parent)
{
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "Voice Assistant");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8888cc), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 30);

    status_lbl = lv_label_create(root);
    lv_label_set_text(status_lbl, "Tap to speak");
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn = lv_btn_create(root);
    lv_obj_set_size(btn, 160, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -60);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_AUDIO "  Speak");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, on_speak, LV_EVENT_CLICKED, NULL);
}

static void destroy(void) { if (root) { lv_obj_del(root); root = NULL; } }
static void resume(void) {}

const app_entry_t app_voice = {
    .name = "Voice AI",
    .create = create, .destroy = destroy, .resume = resume,
};
