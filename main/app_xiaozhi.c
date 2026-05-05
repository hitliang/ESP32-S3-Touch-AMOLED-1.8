#include "app_framework.h"
#include "sys_wifi.h"
#include "sys_audio.h"

/* Point to our custom server instead of official OTA */
#define XZ_WS_URL "ws://59.110.161.101:7070"

#include "xiaozhi_client.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "xz_app";

/* ---- Resampling: 24kHz ↔ 16kHz ---- */
static int resample_24_to_16(const int16_t *in, int in_samples, int16_t *out)
{
    int out_samples = 0;
    for (int i = 0; i < in_samples; i++) {
        if (i % 3 != 2) out[out_samples++] = in[i]; /* take 2 of every 3 */
    }
    return out_samples;
}

/* ---- UI ---- */
static lv_obj_t *root, *conv_area, *status_lbl;
static int conv_y = 5;
static lv_obj_t *mic_btn, *mic_lbl;

enum { MIC_IDLE, MIC_RECORDING, MIC_PROCESSING };
static volatile int mic_state = MIC_IDLE;
static bool xz_running = false;

/* Mic stream task */
#define MIC_STREAM_STACK_WORDS 2048
static StackType_t stream_stack[MIC_STREAM_STACK_WORDS];
static StaticTask_t stream_tcb;
static TaskHandle_t stream_handle = NULL;

extern const lv_font_t lv_font_simsun_16_cjk;

static void mic_stream_task(void *arg);

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
    lv_obj_scroll_to_y(conv_area, conv_y - 180, LV_ANIM_ON);
}

static void update_mic_btn(void)
{
    switch (mic_state) {
    case MIC_IDLE:
        lv_label_set_text(mic_lbl, "Tap to Talk");
        lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0x3366cc), 0);
        break;
    case MIC_RECORDING:
        lv_label_set_text(mic_lbl, "Listening...");
        lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0x33aa33), 0);
        break;
    case MIC_PROCESSING:
        lv_label_set_text(mic_lbl, "Processing...");
        lv_obj_set_style_bg_color(mic_btn, lv_color_hex(0x888833), 0);
        break;
    }
}

/* ---- XZ client callbacks (called from client task) ---- */
static void xz_state_handler(xz_state_t st, const char *text)
{
    ESP_LOGI(TAG, "xz state=%d txt=%s", st, text ? text : "");
    switch (st) {
    case XZ_STATE_CONNECTED:
        mic_state = MIC_RECORDING;
        update_mic_btn();
        lv_label_set_text(status_lbl, "Connected");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x44cc44), 0);
        /* Start listening immediately */
        xz_client_start_listening();
        /* Start mic streaming task now that we're connected */
        stream_handle = xTaskCreateStatic(mic_stream_task, "mic_strm",
            MIC_STREAM_STACK_WORDS, NULL, 4, stream_stack, &stream_tcb);
        break;
    case XZ_STATE_LISTENING:
        add_msg("You: ", text, 0x4488cc);
        break;
    case XZ_STATE_SPEAKING:
        add_msg("AI: ", text, 0x44cc88);
        break;
    case XZ_STATE_IDLE:
    case XZ_STATE_DISCONNECTED:
        mic_state = MIC_IDLE;
        update_mic_btn();
        lv_label_set_text(status_lbl, "Ready");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
        break;
    default:
        break;
    }
}

static void xz_audio_handler(const int16_t *pcm, int samples, int sample_rate)
{
    sys_audio_play_mono(pcm, samples, sample_rate);
}

/* ---- Mic streaming task: continuously read mic and feed to XZ ---- */
static void mic_stream_task(void *arg)
{
    ESP_LOGI(TAG, "mic stream start");
    while (mic_state == MIC_RECORDING) {
        int16_t buf[480]; /* stereo, 480 samples/read */
        int got = sys_audio_read_mic(buf, 480);
        if (got > 0) {
            /* Extract left channel only, resample 24→16, feed */
            int16_t mono[480];
            int mono_samples = 0;
            for (int i = 0; i < got; i += 2)
                mono[mono_samples++] = buf[i];
            int16_t rs[320]; /* 480 * 2/3 */
            int rs_samples = resample_24_to_16(mono, mono_samples, rs);
            xz_client_feed_audio(rs, rs_samples);
        }
    }
    ESP_LOGI(TAG, "mic stream end");
    stream_handle = NULL;
    vTaskDelete(NULL);
}

/* ---- Mic button handler ---- */
static void on_mic_btn(lv_event_t *e)
{
    ESP_LOGI(TAG, "Mic btn state=%d wifi=%d", mic_state, sys_wifi_is_connected());

    if (mic_state == MIC_IDLE) {
        if (!sys_wifi_is_connected()) {
            add_msg("", "WiFi not connected", 0xcc4444);
            return;
        }
        sys_audio_init();
        xz_config_t cfg = {
            .hardware_sample_rate = 24000,
        };
        strncpy(cfg.ws_url, XZ_WS_URL, sizeof(cfg.ws_url) - 1);

        xz_client_init(&cfg);
        xz_client_set_callbacks(xz_state_handler, xz_audio_handler);

        if (!xz_client_start()) {
            ESP_LOGE(TAG, "xz start fail");
            add_msg("", "Connection failed", 0xcc4444);
            return;
        }
        xz_running = true;
        mic_state = MIC_PROCESSING;
        update_mic_btn();
        lv_label_set_text(status_lbl, "Connecting...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);

    } else if (mic_state == MIC_RECORDING || mic_state == MIC_PROCESSING) {
        mic_state = MIC_IDLE;
        update_mic_btn();
        xz_client_stop_listening();
        xz_client_stop();
        xz_running = false;
        lv_label_set_text(status_lbl, "Ready");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x888888), 0);
        /* Mic stream task will exit on mic_state change */
    }
}

/* ---- App entry points ---- */
static void create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Xiaozhi app created");
    conv_y = 5;
    root = lv_obj_create(parent);
    lv_obj_set_size(root, 340, 380);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, "XiaoZhi AI | Hi!");
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

    /* Large mic button — tap to stop/resume */
    mic_btn = lv_btn_create(root);
    lv_obj_set_size(mic_btn, 280, 100);
    lv_obj_align(mic_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_radius(mic_btn, 50, 0);
    lv_obj_add_event_cb(mic_btn, on_mic_btn, LV_EVENT_CLICKED, NULL);

    mic_lbl = lv_label_create(mic_btn);
    lv_obj_set_style_text_font(mic_lbl, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(mic_lbl, lv_color_white(), 0);
    lv_obj_center(mic_lbl);

    /* Auto-connect on app start — no need to tap the button */
    if (sys_wifi_is_connected()) {
        sys_audio_init();
        xz_config_t cfg = { .hardware_sample_rate = 24000 };
        strncpy(cfg.ws_url, XZ_WS_URL, sizeof(cfg.ws_url) - 1);

        xz_client_init(&cfg);
        xz_client_set_callbacks(xz_state_handler, xz_audio_handler);
        xz_client_start();
        xz_running = true;
        mic_state = MIC_PROCESSING;
        update_mic_btn();
        lv_label_set_text(status_lbl, "Connecting...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xccaa44), 0);
    } else {
        mic_state = MIC_IDLE;
        update_mic_btn();
        lv_label_set_text(status_lbl, "No WiFi");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xcc4444), 0);
    }
}

static void destroy(void)
{
    ESP_LOGI(TAG, "destroy");
    if (xz_running) {
        xz_client_stop();
        xz_running = false;
    }
    mic_state = MIC_IDLE;
    if (stream_handle) {
        int wait = 0;
        while (stream_handle && wait < 200) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait++;
        }
    }
    if (root) { lv_obj_del(root); root = NULL; }
}

static void resume(void) {}

const app_entry_t app_xiaozhi = {
    .name = "XiaoZhi AI",
    .create = create,
    .destroy = destroy,
    .resume = resume,
};
