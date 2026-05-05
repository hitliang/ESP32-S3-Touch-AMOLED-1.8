#include "sys_audio.h"
#include "sys_i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

#define TAG "sys_audio"

#define PIN_MCK    GPIO_NUM_16
#define PIN_BCK    GPIO_NUM_9
#define PIN_WS     GPIO_NUM_45
#define PIN_DOUT   GPIO_NUM_8
#define PIN_DIN    GPIO_NUM_10
#define PIN_PA     GPIO_NUM_46
#define SAMPLE_RATE 24000
#define MCLK_MULT   256

#define AUDIO_QUEUE_ITEMS    32
#define AUDIO_CHUNK_SAMPLES  480

typedef struct {
    int16_t *data;
    int      bytes;
    bool     last;
} audio_chunk_t;

/* I2S handles — both created in full-duplex, but only ONE active at a time */
static i2s_chan_handle_t tx = NULL;
static i2s_chan_handle_t rx = NULL;
static bool tx_active = false;
static bool rx_active = false;
static bool inited = false;

static QueueHandle_t audio_queue = NULL;
static TaskHandle_t  audio_task_handle = NULL;
static EventGroupHandle_t audio_events = NULL;
#define AUDIO_EVT_DONE  BIT0

/* Recording */
static volatile bool rec_running = false;
static int16_t *rec_buf = NULL;
static volatile int rec_samples = 0;
static int rec_max_samples = 0;
static TaskHandle_t rec_task_handle = NULL;
#define REC_CHUNK_SAMPLES 480

/* ---- Activate/deactivate channels ---- */
static void ensure_tx_on(void)
{
    if (tx_active) return;
    if (rx_active) { i2s_channel_disable(rx); rx_active = false; }
    if (tx) { i2s_channel_enable(tx); tx_active = true; }
}

static void ensure_rx_on(void)
{
    if (rx_active) return;
    if (tx_active) { i2s_channel_disable(tx); tx_active = false; }
    if (rx) { i2s_channel_enable(rx); rx_active = true; }
}

/* ---- Output task: reads chunks, writes to I2S TX ---- */
static void audio_output_task(void *arg)
{
    audio_chunk_t chunk;
    while (1) {
        if (xQueueReceive(audio_queue, &chunk, portMAX_DELAY) == pdTRUE) {
            ensure_tx_on();
            if (chunk.data && chunk.bytes > 0) {
                int sent = 0;
                while (sent < chunk.bytes) {
                    size_t w = 0;
                    if (i2s_channel_write(tx, (uint8_t*)chunk.data + sent,
                            chunk.bytes - sent, &w, pdMS_TO_TICKS(5000)) != ESP_OK || w == 0)
                        break;
                    sent += w;
                }
                free(chunk.data);
            }
            if (chunk.last) {
                vTaskDelay(pdMS_TO_TICKS(300));
                xEventGroupSetBits(audio_events, AUDIO_EVT_DONE);
            }
        }
    }
}

/* ---- Init (full-duplex channel, but only one direction active) ---- */
void sys_audio_init(void)
{
    if (inited) return;
    ESP_LOGI(TAG, "init (time-sharing TX/RX)...");

    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_PA, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_PA, 1);

    es8311_handle_t es = es8311_create(sys_i2c_get_port(), ES8311_ADDRRES_0);
    if (!es) { ESP_LOGE(TAG, "no ES8311"); return; }

    es8311_clock_config_t clk = {
        .mclk_inverted=false, .sclk_inverted=false, .mclk_from_mclk_pin=true,
        .mclk_frequency=SAMPLE_RATE*MCLK_MULT, .sample_frequency=SAMPLE_RATE,
    };
    es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    es8311_voice_volume_set(es, 80, NULL);
    es8311_microphone_config(es, false);

    /* Full-duplex channel — both handles created, but time-shared */
    i2s_chan_config_t cc = {
        .id = I2S_NUM_0, .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6, .dma_frame_num = 240,
        .auto_clear_after_cb = true, .auto_clear_before_cb = false, .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, &rx));
    ESP_LOGI(TAG, "I2S full-duplex channel created");

    i2s_std_config_t sc = {
        .clk_cfg = { .sample_rate_hz = SAMPLE_RATE, .clk_src = I2S_CLK_SRC_DEFAULT,
                     .mclk_multiple = I2S_MCLK_MULTIPLE_256, .ext_clk_freq_hz = 0 },
        .slot_cfg = { .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT, .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                      .slot_mode = I2S_SLOT_MODE_STEREO, .slot_mask = I2S_STD_SLOT_BOTH,
                      .ws_width = I2S_DATA_BIT_WIDTH_16BIT, .ws_pol = false, .bit_shift = true,
                      .left_align = true, .big_endian = false, .bit_order_lsb = false },
        .gpio_cfg = { .mclk = PIN_MCK, .bclk = PIN_BCK, .ws = PIN_WS,
                      .dout = PIN_DOUT, .din = PIN_DIN,
                      .invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false } },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &sc));

    /* Start with TX enabled */
    ensure_tx_on();

    audio_queue = xQueueCreate(AUDIO_QUEUE_ITEMS, sizeof(audio_chunk_t));
    audio_events = xEventGroupCreate();
    xTaskCreate(audio_output_task, "audio_out", 2048, NULL, 5, &audio_task_handle);

    inited = true;
    ESP_LOGI(TAG, "ready");
}

/* ---- Enqueue ---- */
static bool enqueue_chunk(int16_t *data, int bytes, bool last)
{
    audio_chunk_t ch = { .data = data, .bytes = bytes, .last = last };
    if (xQueueSend(audio_queue, &ch, pdMS_TO_TICKS(5000)) != pdTRUE) {
        free(data);
        return false;
    }
    return true;
}

/* ---- Play WAV ---- */
void sys_audio_play_wav(const uint8_t *data, int len)
{
    if (!tx || len < 44) return;
    int off = 0;
    for (int i = 12; i < len - 8; i++) {
        if (data[i]=='d' && data[i+1]=='a' && data[i+2]=='t' && data[i+3]=='a') {
            off = i + 8; break;
        }
    }
    if (!off) return;

    int channels = *(short*)(data + 22);
    int bits     = *(short*)(data + 34);
    int pcm_len  = len - off;
    const uint8_t *pcm = data + off;

    if (channels == 1 && bits == 16) {
        int samples = pcm_len / 2;
        const int16_t *src = (const int16_t*)pcm;
        int pos = 0;
        while (pos < samples) {
            int cs = samples - pos;
            if (cs > AUDIO_CHUNK_SAMPLES) cs = AUDIO_CHUNK_SAMPLES;
            bool last = (pos + cs >= samples);
            int16_t *buf = malloc(cs * 4);
            if (!buf) break;
            for (int i = 0; i < cs; i++) {
                buf[i*2]   = src[pos + i];
                buf[i*2+1] = src[pos + i];
            }
            if (!enqueue_chunk(buf, cs * 4, last)) break;
            pos += cs;
        }
    } else if (channels == 2 && bits == 16) {
        int pos = 0;
        int cb = AUDIO_CHUNK_SAMPLES * 4;
        while (pos < pcm_len) {
            int tb = pcm_len - pos;
            if (tb > cb) tb = cb;
            bool last = (pos + tb >= pcm_len);
            int16_t *buf = malloc(tb);
            if (!buf) break;
            memcpy(buf, pcm + pos, tb);
            if (!enqueue_chunk(buf, tb, last)) break;
            pos += tb;
        }
    }
    xEventGroupWaitBits(audio_events, AUDIO_EVT_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
}

void sys_audio_play_pcm(const int16_t *stereo_data, int sample_count)
{
    if (!tx) return;
    ensure_tx_on();
    i2s_channel_write(tx, stereo_data, sample_count * 4, NULL, pdMS_TO_TICKS(5000));
}

bool sys_audio_is_playing(void)
{
    return uxQueueMessagesWaiting(audio_queue) > 0;
}

void sys_audio_stop(void)
{
    audio_chunk_t ch;
    while (xQueueReceive(audio_queue, &ch, 0) == pdTRUE) {
        if (ch.data) free(ch.data);
    }
    xEventGroupSetBits(audio_events, AUDIO_EVT_DONE);
}

/* ---- Mic streaming (time-shares I2S0 with TX) ---- */
int sys_audio_read_mic(int16_t *buf, int max_samples)
{
    if (!rx) return 0;
    ensure_rx_on();
    size_t got = 0;
    esp_err_t r = i2s_channel_read(rx, buf, max_samples * 4, &got, pdMS_TO_TICKS(200));
    return (r == ESP_OK && got > 0) ? (int)(got / 4) : 0;
}

bool sys_audio_play_mono(const int16_t *mono, int samples, int src_sample_rate)
{
    if (!tx) return false;
    ensure_tx_on();

    int out_max = samples * SAMPLE_RATE / src_sample_rate + 1;
    int16_t *rs = malloc(out_max * sizeof(int16_t));
    if (!rs) return false;
    int out_samples = 0;
    for (int i = 0; i < out_max - 1; i++) {
        int si = (int)((int64_t)i * src_sample_rate / SAMPLE_RATE);
        if (si < samples) rs[out_samples++] = mono[si];
    }

    int pos = 0;
    while (pos < out_samples) {
        int cs = out_samples - pos;
        if (cs > AUDIO_CHUNK_SAMPLES) cs = AUDIO_CHUNK_SAMPLES;
        int16_t *st = malloc(cs * 4);
        if (!st) break;
        for (int i = 0; i < cs; i++) { st[i*2] = rs[pos+i]; st[i*2+1] = rs[pos+i]; }
        if (!enqueue_chunk(st, cs * 4, false)) { free(st); break; }
        pos += cs;
    }
    free(rs);
    return pos > 0;
}

/* ---- Recording ---- */
static void audio_record_task(void *arg)
{
    int16_t chunk[REC_CHUNK_SAMPLES];
    ensure_rx_on();
    while (rec_running && rec_samples < rec_max_samples) {
        size_t got = 0;
        if (i2s_channel_read(rx, chunk, sizeof(chunk), &got, pdMS_TO_TICKS(200)) == ESP_OK && got > 0) {
            int samples = got / 4;
            int tc = samples;
            if (rec_samples + tc > rec_max_samples) tc = rec_max_samples - rec_samples;
            memcpy(rec_buf + rec_samples, chunk, tc * sizeof(int16_t));
            rec_samples += tc;
        }
    }
    rec_task_handle = NULL;
    vTaskDelete(NULL);
}

bool sys_audio_record_start(int max_duration_ms)
{
    if (!rx || rec_running) return false;
    rec_max_samples = max_duration_ms * SAMPLE_RATE / 1000;
    int buf_bytes = rec_max_samples * sizeof(int16_t);
    rec_buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rec_buf) rec_buf = malloc(buf_bytes);
    if (!rec_buf) return false;
    rec_samples = 0;
    rec_running = true;
    xTaskCreate(audio_record_task, "audio_rec", 4096, NULL, 4, &rec_task_handle);
    return true;
}

uint8_t *sys_audio_record_stop(int *out_wav_len)
{
    if (!rec_running) return NULL;
    rec_running = false;
    int wait = 0;
    while (rec_task_handle && wait < 500) { vTaskDelay(pdMS_TO_TICKS(10)); wait += 10; }

    int pcm_bytes = rec_samples * sizeof(int16_t);
    int wav_len = pcm_bytes + 44;
    uint8_t *wav = malloc(wav_len);
    if (!wav) { free(rec_buf); rec_buf = NULL; return NULL; }

    int offset = 0;
    memcpy(wav + offset, "RIFF", 4); offset += 4;
    int fs = wav_len - 8; memcpy(wav + offset, &fs, 4); offset += 4;
    memcpy(wav + offset, "WAVE", 4); offset += 4;
    memcpy(wav + offset, "fmt ", 4); offset += 4;
    int fms = 16; memcpy(wav + offset, &fms, 4); offset += 4;
    short af = 1; memcpy(wav + offset, &af, 2); offset += 2;
    short ch = 1; memcpy(wav + offset, &ch, 2); offset += 2;
    int sr = SAMPLE_RATE; memcpy(wav + offset, &sr, 4); offset += 4;
    int br = SAMPLE_RATE * 2; memcpy(wav + offset, &br, 4); offset += 4;
    short ba = 2; memcpy(wav + offset, &ba, 2); offset += 2;
    short bi = 16; memcpy(wav + offset, &bi, 2); offset += 2;
    memcpy(wav + offset, "data", 4); offset += 4;
    memcpy(wav + offset, &pcm_bytes, 4); offset += 4;
    memcpy(wav + offset, rec_buf, pcm_bytes);

    free(rec_buf); rec_buf = NULL;
    *out_wav_len = wav_len;
    return wav;
}

bool sys_audio_is_recording(void) { return rec_running; }
