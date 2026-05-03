#include "xiaozhi_client.h"
#include "secrets.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "encoder/impl/esp_opus_enc.h"
#include "decoder/impl/esp_opus_dec.h"
#include "decoder/esp_audio_dec.h"
#include "esp_audio_enc.h"
#include "esp_audio_types.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "xz_client";

#ifndef XZ_WS_URL
#define XZ_WS_URL "ws://192.168.1.100:8000"
#endif

/* ---- Opus config ---- */
#define OPUS_SAMPLE_RATE  16000
#define OPUS_FRAME_MS     60
#define OPUS_SAMPLES      960   /* 16000 * 60 / 1000 */
#define OPUS_MAX_BYTES     256

#define OPUS_ENC_CFG() (esp_opus_enc_config_t) {           \
    .sample_rate      = OPUS_SAMPLE_RATE,                    \
    .channel          = 1,                                   \
    .bits_per_sample  = 16,                                  \
    .bitrate          = ESP_OPUS_BITRATE_AUTO,               \
    .frame_duration   = ESP_OPUS_ENC_FRAME_DURATION_60_MS,   \
    .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,       \
    .complexity       = 0,                                   \
    .enable_fec       = false,                               \
    .enable_dtx       = false,                               \
    .enable_vbr       = false,                               \
}

#define OPUS_DEC_CFG() (esp_opus_dec_cfg_t) {               \
    .sample_rate    = OPUS_SAMPLE_RATE,                       \
    .channel        = 1,                                      \
    .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,      \
    .self_delimited = false,                                  \
}

/* ---- Binary protocol v3 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  reserved;
    uint16_t payload_size; /* big-endian */
    uint8_t  payload[];
} bin_frame_t;

enum { BIN_TYPE_OPUS = 0, BIN_TYPE_JSON = 1 };

/* ---- Internal state ---- */
static esp_websocket_client_handle_t ws = NULL;
static xz_config_t cfg;
static xz_state_t state = XZ_STATE_IDLE;
static xz_state_cb_t state_cb = NULL;
static xz_audio_cb_t audio_cb = NULL;

static TaskHandle_t client_task = NULL;
static EventGroupHandle_t events = NULL;
#define EVT_STOP      BIT0
#define EVT_AUDIO_IN  BIT1

static QueueHandle_t pcm_in_queue = NULL;

static void *opus_enc = NULL;
static void *opus_dec = NULL;

/* ---- Forward decls ---- */
static void set_state(xz_state_t s, const char *text);
static void client_thread(void *arg);

/* ---- Public API ---- */
void xz_client_init(const xz_config_t *config)
{
    memcpy(&cfg, config, sizeof(cfg));
    if (!cfg.ws_url[0])
        strncpy(cfg.ws_url, XZ_WS_URL, sizeof(cfg.ws_url) - 1);
    if (!cfg.hardware_sample_rate)
        cfg.hardware_sample_rate = 24000;

    events = xEventGroupCreate();
    pcm_in_queue = xQueueCreate(8, OPUS_SAMPLES * sizeof(int16_t));
    ESP_LOGI(TAG, "init url=%s", cfg.ws_url);
}

void xz_client_set_callbacks(xz_state_cb_t scb, xz_audio_cb_t acb)
{
    state_cb = scb;
    audio_cb = acb;
}

xz_state_t xz_client_get_state(void) { return state; }
bool xz_client_is_connected(void) {
    return (state >= XZ_STATE_CONNECTED && state < XZ_STATE_DISCONNECTED);
}

/* ---- State ---- */
static void set_state(xz_state_t s, const char *text)
{
    state = s;
    ESP_LOGI(TAG, "state %d %s", s, text ? text : "");
    if (state_cb) state_cb(s, text);
}

/* ---- Opus ---- */
static bool opus_init(void)
{
    esp_opus_enc_config_t enc_cfg = OPUS_ENC_CFG();
    if (esp_opus_enc_open(&enc_cfg, sizeof(enc_cfg), &opus_enc) != ESP_OK || !opus_enc) {
        ESP_LOGE(TAG, "opus enc fail");
        return false;
    }

    esp_opus_dec_cfg_t dec_cfg = OPUS_DEC_CFG();
    if (esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg), &opus_dec) != ESP_OK || !opus_dec) {
        ESP_LOGE(TAG, "opus dec fail");
        esp_opus_enc_close(opus_enc);
        opus_enc = NULL;
        return false;
    }

    ESP_LOGI(TAG, "opus ready sr=%d frame=%dms", OPUS_SAMPLE_RATE, OPUS_FRAME_MS);
    return true;
}

/* ---- Send binary frame (protocol v3) ---- */
static bool ws_send_binary(uint8_t type, const uint8_t *data, uint16_t len)
{
    if (!ws) return false;
    int total = sizeof(bin_frame_t) + len;
    uint8_t *buf = malloc(total);
    if (!buf) return false;
    bin_frame_t *hdr = (bin_frame_t*)buf;
    hdr->type = type;
    hdr->reserved = 0;
    hdr->payload_size = (uint16_t)((len >> 8) | (len << 8));
    if (data && len > 0) memcpy(hdr->payload, data, len);
    int ret = esp_websocket_client_send_bin(ws, (char*)buf, total, pdMS_TO_TICKS(1000));
    free(buf);
    return ret >= 0;
}

/* ---- Send JSON ---- */
static bool ws_send_json(const char *json)
{
    if (!ws) return false;
    ESP_LOGI(TAG, "send: %s", json);
    return esp_websocket_client_send_text(ws, json, strlen(json), pdMS_TO_TICKS(3000)) >= 0;
}

/* ---- Send hello ---- */
static bool send_hello(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON *features = cJSON_AddObjectToObject(root, "features");
    cJSON_AddBoolToObject(features, "aec", false);
    cJSON_AddBoolToObject(features, "mcp", false);
    cJSON *ap = cJSON_AddObjectToObject(root, "audio_params");
    cJSON_AddStringToObject(ap, "format", "opus");
    cJSON_AddNumberToObject(ap, "sample_rate", OPUS_SAMPLE_RATE);
    cJSON_AddNumberToObject(ap, "channels", 1);
    cJSON_AddNumberToObject(ap, "frame_duration", OPUS_FRAME_MS);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return false;
    bool ok = ws_send_json(json);
    free(json);
    return ok;
}

/* ---- Handle server messages ---- */
static void handle_server_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !type->valuestring) { cJSON_Delete(root); return; }

    if (strcmp(type->valuestring, "hello") == 0) {
        ESP_LOGI(TAG, "server hello");
        set_state(XZ_STATE_CONNECTED, "Connected");
    } else if (strcmp(type->valuestring, "tts") == 0) {
        cJSON *s = cJSON_GetObjectItem(root, "state");
        cJSON *txt = cJSON_GetObjectItem(root, "text");
        if (s && s->valuestring) {
            if (strcmp(s->valuestring, "start") == 0 || strcmp(s->valuestring, "sentence_start") == 0)
                set_state(XZ_STATE_SPEAKING, txt ? txt->valuestring : "");
            else if (strcmp(s->valuestring, "stop") == 0)
                set_state(XZ_STATE_LISTENING, "");
        }
    } else if (strcmp(type->valuestring, "stt") == 0) {
        cJSON *txt = cJSON_GetObjectItem(root, "text");
        if (txt && txt->valuestring && state_cb)
            state_cb(XZ_STATE_LISTENING, txt->valuestring);
    }
    cJSON_Delete(root);
}

/* ---- Encode PCM → Opus ---- */
static int encode_opus(const int16_t *pcm, uint8_t *out, int max_out)
{
    esp_audio_enc_in_frame_t in = {
        .buffer = (uint8_t*)pcm,
        .len = OPUS_SAMPLES * sizeof(int16_t),
    };
    esp_audio_enc_out_frame_t out_frame = {
        .buffer = out,
        .len = max_out,
    };
    if (esp_opus_enc_process(opus_enc, &in, &out_frame) != ESP_OK) return 0;
    return (int)out_frame.encoded_bytes;
}

/* ---- Decode Opus → PCM ---- */
static int decode_opus(const uint8_t *data, int len, int16_t *pcm, int max_samples)
{
    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t*)data,
        .len = len,
    };
    esp_audio_dec_out_frame_t out_frame = {
        .buffer = (uint8_t*)pcm,
        .len = max_samples * sizeof(int16_t),
    };
    esp_audio_dec_info_t info;
    if (esp_opus_dec_decode(opus_dec, &raw, &out_frame, &info) != ESP_OK) return 0;
    return (int)(out_frame.decoded_size / sizeof(int16_t));
}

/* ---- Handle binary data from server ---- */
static void handle_binary(const uint8_t *data, int len)
{
    if (len < (int)sizeof(bin_frame_t)) return;
    bin_frame_t *hdr = (bin_frame_t*)data;
    uint16_t pl = (uint16_t)((hdr->payload_size << 8) | (hdr->payload_size >> 8));
    if ((int)pl > len - (int)sizeof(bin_frame_t)) return;

    if (hdr->type == BIN_TYPE_OPUS) {
        int16_t pcm[OPUS_SAMPLES * 2]; /* max decoded samples (60ms @ 48kHz) */
        int samples = decode_opus(hdr->payload, pl, pcm, sizeof(pcm) / sizeof(int16_t));
        if (samples > 0 && audio_cb) {
            audio_cb(pcm, samples, OPUS_SAMPLE_RATE);
        }
    }
}

/* ---- WebSocket event handler ---- */
static void ws_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t*)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WS disconnected");
        set_state(XZ_STATE_DISCONNECTED, "Disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x02) {
            handle_binary((const uint8_t*)data->data_ptr, data->data_len);
        } else if (data->op_code == 0x01) {
            char *json = malloc(data->data_len + 1);
            if (json) {
                memcpy(json, data->data_ptr, data->data_len);
                json[data->data_len] = 0;
                handle_server_json(json);
                free(json);
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        break;
    }
}

/* ---- Client thread ---- */
static void client_thread(void *arg)
{
    ESP_LOGI(TAG, "client thread start");

    if (!opus_init()) {
        set_state(XZ_STATE_IDLE, "Opus init failed");
        client_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = cfg.ws_url,
        .task_prio = 5,
        .task_stack = 8192,
        .buffer_size = 4096,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .disable_auto_reconnect = false,
    };
    ws = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(ws);

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(events,
            EVT_STOP | EVT_AUDIO_IN,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(100));

        if (bits & EVT_STOP) break;

        if ((bits & EVT_AUDIO_IN) && xz_client_is_connected()) {
            int16_t pcm[OPUS_SAMPLES];
            if (xQueueReceive(pcm_in_queue, pcm, 0) == pdTRUE) {
                uint8_t opus_buf[OPUS_MAX_BYTES];
                int enc_len = encode_opus(pcm, opus_buf, sizeof(opus_buf));
                if (enc_len > 0) {
                    ws_send_binary(BIN_TYPE_OPUS, opus_buf, (uint16_t)enc_len);
                }
            }
        }
    }

    ESP_LOGI(TAG, "client thread exit");
    if (ws) {
        esp_websocket_client_stop(ws);
        esp_websocket_client_destroy(ws);
        ws = NULL;
    }
    if (opus_enc) { esp_opus_enc_close(opus_enc); opus_enc = NULL; }
    if (opus_dec) { esp_opus_dec_close(opus_dec); opus_dec = NULL; }
    set_state(XZ_STATE_IDLE, "Idle");
    client_task = NULL;
    vTaskDelete(NULL);
}

/* ---- Public API: start / stop ---- */
bool xz_client_start(void)
{
    if (client_task) return false;
    xEventGroupClearBits(events, EVT_STOP | EVT_AUDIO_IN);
    return xTaskCreate(client_thread, "xz_client", 8192,
                       NULL, 4, &client_task) == pdPASS;
}

void xz_client_stop(void)
{
    if (client_task) {
        xEventGroupSetBits(events, EVT_STOP);
        int wait = 0;
        while (client_task && wait < 300) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait++;
        }
    }
}

/* ---- Feed audio from mic ---- */
void xz_client_feed_audio(const int16_t *pcm, int samples)
{
    if (!xz_client_is_connected()) return;

    static int16_t buf[OPUS_SAMPLES];
    static int buf_pos = 0;

    for (int i = 0; i < samples; i++) {
        buf[buf_pos++] = pcm[i];
        if (buf_pos >= OPUS_SAMPLES) {
            int16_t frame[OPUS_SAMPLES];
            memcpy(frame, buf, sizeof(frame));
            if (xQueueSend(pcm_in_queue, frame, 0) != pdTRUE) {
                int16_t discard[OPUS_SAMPLES];
                xQueueReceive(pcm_in_queue, discard, 0);
                xQueueSend(pcm_in_queue, frame, 0);
            }
            xEventGroupSetBits(events, EVT_AUDIO_IN);
            buf_pos = 0;
        }
    }
}

/* ---- Send listen control ---- */
void xz_client_start_listening(void)
{
    if (!xz_client_is_connected()) return;
    set_state(XZ_STATE_LISTENING, "");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "start");
    cJSON_AddStringToObject(root, "mode", "manual");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) { ws_send_json(json); free(json); }
}

void xz_client_stop_listening(void)
{
    if (!xz_client_is_connected()) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "stop");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json) { ws_send_json(json); free(json); }
}
