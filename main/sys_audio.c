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

static const char *TAG = "sys_audio";

#define PIN_MCK    GPIO_NUM_16
#define PIN_BCK    GPIO_NUM_9
#define PIN_WS     GPIO_NUM_45
#define PIN_DOUT   GPIO_NUM_8
#define PIN_DIN    GPIO_NUM_10
#define PIN_PA     GPIO_NUM_46
#define SAMPLE_RATE 24000
#define MCLK_MULT   256

/* Queue-based streaming: same architecture as xiaozhi */
#define AUDIO_QUEUE_ITEMS    32
#define AUDIO_CHUNK_SAMPLES  480   /* 480 samples = 10ms @24kHz stereo */

typedef struct {
    int16_t *data;   /* heap-allocated, freed by output task */
    int      bytes;  /* data size in bytes */
    bool     last;   /* marks end of a playback */
} audio_chunk_t;

static i2s_chan_handle_t tx = NULL;
static i2s_chan_handle_t rx = NULL;
static bool inited = false;

static QueueHandle_t audio_queue = NULL;
static TaskHandle_t  audio_task_handle = NULL;
static EventGroupHandle_t audio_events = NULL;
#define AUDIO_EVT_DONE  BIT0

/* Recording state */
static bool rec_running = false;
static int16_t *rec_buf = NULL;
static volatile int rec_samples = 0;
static int rec_max_samples = 0;
static TaskHandle_t rec_task_handle = NULL;
#define REC_CHUNK_SAMPLES 480  /* read 480 mono samples = 10ms @24kHz */

/* ---- Output task: reads chunks from queue, writes to I2S ---- */
static void audio_output_task(void *arg)
{
    audio_chunk_t chunk;
    int total_bytes = 0;  /* track bytes for drain timing */
    while (1) {
        if (xQueueReceive(audio_queue, &chunk, portMAX_DELAY) == pdTRUE) {
            if (chunk.data && chunk.bytes > 0) {
                int sent = 0;
                while (sent < chunk.bytes) {
                    size_t w = 0;
                    esp_err_t r = i2s_channel_write(tx,
                        (uint8_t*)chunk.data + sent,
                        chunk.bytes - sent, &w, pdMS_TO_TICKS(5000));
                    sent += w;
                    if (r != ESP_OK || w == 0) break;
                }
                total_bytes += sent;
                free(chunk.data);
            }
            if (chunk.last) {
                /* DMA buffer holds at most dma_desc_num * dma_frame_num * 4 bytes.
                   6 * 240 * 4 = 5760 bytes. At 96000 bytes/sec, drains in 60ms.
                   Add 240ms safety margin → 300ms total. */
                vTaskDelay(pdMS_TO_TICKS(300));
                total_bytes = 0;
                xEventGroupSetBits(audio_events, AUDIO_EVT_DONE);
            }
        }
    }
}

/* ---- Init ---- */
void sys_audio_init(void)
{
    if (inited) return;
    printf("AUDIO: init...\n");

    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_PA, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_PA, 1);

    es8311_handle_t es = es8311_create(sys_i2c_get_port(), ES8311_ADDRRES_0);
    if (!es) { printf("AUDIO: no ES8311\n"); return; }

    es8311_clock_config_t clk = {
        .mclk_inverted=false, .sclk_inverted=false, .mclk_from_mclk_pin=true,
        .mclk_frequency=SAMPLE_RATE*MCLK_MULT, .sample_frequency=SAMPLE_RATE,
    };
    es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    es8311_voice_volume_set(es, 80, NULL);
    es8311_microphone_config(es, false);

    /* I2S channel — config aligned with xiaozhi */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    cc.auto_clear_after_cb = true;
    cc.dma_desc_num = 6;
    cc.dma_frame_num = 240;
    if (i2s_new_channel(&cc, &tx, NULL) != ESP_OK) {
        printf("AUDIO: I2S busy, using existing\n");
    }

    i2s_std_config_t sc = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = PIN_MCK, .bclk = PIN_BCK, .ws = PIN_WS, .dout = PIN_DOUT,
            .invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false },
        },
    };
    if (tx) i2s_channel_init_std_mode(tx, &sc);
    if (tx) i2s_channel_enable(tx);

    /* I2S RX channel for microphone */
    i2s_chan_config_t rcc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    rcc.auto_clear = true;
    rcc.auto_clear_after_cb = true;
    rcc.dma_desc_num = 6;
    rcc.dma_frame_num = 240;
    if (i2s_new_channel(&rcc, NULL, &rx) != ESP_OK) {
        printf("AUDIO: I2S RX busy\n");
    }
    i2s_std_config_t rsc = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO, /* codec sends stereo, we pick left channel */
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED,
            .ws = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_DIN,
            .invert_flags = { .mclk_inv=false, .bclk_inv=false, .ws_inv=false },
        },
    };
    if (rx) i2s_channel_init_std_mode(rx, &rsc);
    if (rx) i2s_channel_enable(rx);

    /* Queue + output task */
    audio_queue = xQueueCreate(AUDIO_QUEUE_ITEMS, sizeof(audio_chunk_t));
    audio_events = xEventGroupCreate();
    xTaskCreate(audio_output_task, "audio_out", 4096, NULL, 5, &audio_task_handle);

    inited = true;
    printf("AUDIO: ready\n");
}

/* ---- Helper: enqueue a chunk (takes ownership of data pointer) ---- */
static bool enqueue_chunk(int16_t *data, int bytes, bool last)
{
    audio_chunk_t ch = { .data = data, .bytes = bytes, .last = last };
    if (xQueueSend(audio_queue, &ch, pdMS_TO_TICKS(5000)) != pdTRUE) {
        printf("AUDIO: queue full!\n");
        free(data);
        return false;
    }
    return true;
}

/* ---- Play WAV ---- */
void sys_audio_play_wav(const uint8_t *data, int len)
{
    if (!tx || len < 44) return;
    printf("AUDIO: play %d bytes\n", len);

    /* Find data chunk */
    int off = 0;
    for (int i = 12; i < len - 8; i++) {
        if (data[i]=='d' && data[i+1]=='a' && data[i+2]=='t' && data[i+3]=='a') {
            off = i + 8; break;
        }
    }
    if (!off) { printf("AUDIO: no data chunk\n"); return; }

    int channels = *(short*)(data + 22);
    int bits     = *(short*)(data + 34);
    int wav_sr   = *(int*)(data + 24);
    int pcm_len  = len - off;
    const uint8_t *pcm = data + off;
    printf("AUDIO: %dHz %dbit %dch pcm=%d\n", wav_sr, bits, channels, pcm_len);

    /* Convert to stereo 16-bit and enqueue in chunks */
    int samples;       /* total mono samples */
    const int16_t *src;

    if (channels == 1 && bits == 16) {
        samples = pcm_len / 2;
        src = (const int16_t*)pcm;
    } else if (channels == 2 && bits == 16) {
        /* Already stereo — enqueue directly in chunks */
        int stereo_bytes = pcm_len;
        int chunk_bytes = AUDIO_CHUNK_SAMPLES * 2 * 2; /* samples * ch * bytes */
        int pos = 0;
        while (pos < stereo_bytes) {
            int this_bytes = stereo_bytes - pos;
            if (this_bytes > chunk_bytes) this_bytes = chunk_bytes;
            bool last = (pos + this_bytes >= stereo_bytes);
            int16_t *buf = malloc(this_bytes);
            if (!buf) break;
            memcpy(buf, pcm + pos, this_bytes);
            if (!enqueue_chunk(buf, this_bytes, last)) break;
            pos += this_bytes;
        }
        printf("AUDIO: enqueued stereo %d bytes\n", pos);
        /* Wait for playback to finish */
        xEventGroupWaitBits(audio_events, AUDIO_EVT_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
        printf("AUDIO: done\n");
        return;
    } else {
        printf("AUDIO: unsupported format\n");
        return;
    }

    /* Mono → stereo, enqueue in chunks of AUDIO_CHUNK_SAMPLES mono samples */
    int pos = 0;
    while (pos < samples) {
        int chunk_samples = samples - pos;
        if (chunk_samples > AUDIO_CHUNK_SAMPLES) chunk_samples = AUDIO_CHUNK_SAMPLES;
        bool last = (pos + chunk_samples >= samples);
        int16_t *buf = malloc(chunk_samples * 4); /* stereo: *2 channels *2 bytes */
        if (!buf) { printf("AUDIO: OOM at %d\n", pos); break; }
        for (int i = 0; i < chunk_samples; i++) {
            buf[i*2]   = src[pos + i];
            buf[i*2+1] = src[pos + i];
        }
        if (!enqueue_chunk(buf, chunk_samples * 4, last)) break;
        pos += chunk_samples;
    }
    printf("AUDIO: enqueued %d/%d samples\n", pos, samples);

    /* Block until playback complete */
    xEventGroupWaitBits(audio_events, AUDIO_EVT_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
    printf("AUDIO: done\n");
}

/* ---- Play raw stereo PCM ---- */
void sys_audio_play_pcm(const int16_t *stereo_data, int sample_count)
{
    if (!tx) { printf("AUDIO: pcm not ready\n"); return; }

    int total_bytes = sample_count * 4;
    int chunk_bytes = AUDIO_CHUNK_SAMPLES * 4;
    int pos = 0;
    while (pos < total_bytes) {
        int this_bytes = total_bytes - pos;
        if (this_bytes > chunk_bytes) this_bytes = chunk_bytes;
        bool last = (pos + this_bytes >= total_bytes);
        int16_t *buf = malloc(this_bytes);
        if (!buf) break;
        memcpy(buf, (uint8_t*)stereo_data + pos, this_bytes);
        if (!enqueue_chunk(buf, this_bytes, last)) break;
        pos += this_bytes;
    }

    xEventGroupWaitBits(audio_events, AUDIO_EVT_DONE, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
    printf("AUDIO: pcm done\n");
}

bool sys_audio_is_playing(void)
{
    return uxQueueMessagesWaiting(audio_queue) > 0;
}

void sys_audio_stop(void)
{
    /* Flush queue */
    audio_chunk_t ch;
    while (xQueueReceive(audio_queue, &ch, 0) == pdTRUE) {
        if (ch.data) free(ch.data);
    }
    xEventGroupSetBits(audio_events, AUDIO_EVT_DONE);
}

/* ---- Recording task ---- */
static void audio_record_task(void *arg)
{
    int16_t chunk[REC_CHUNK_SAMPLES * 2]; /* stereo input buffer */
    printf("REC: task start max=%d samples\n", rec_max_samples);
    while (rec_running && rec_samples < rec_max_samples) {
        size_t got = 0;
        esp_err_t r = i2s_channel_read(rx, chunk,
            sizeof(chunk), &got, pdMS_TO_TICKS(1000));
        if (r == ESP_OK && got > 0) {
            int stereo_samples = got / 4; /* 2ch * 2bytes */
            int to_copy = stereo_samples;
            if (rec_samples + to_copy > rec_max_samples)
                to_copy = rec_max_samples - rec_samples;
            /* Extract left channel only (mono) */
            for (int i = 0; i < to_copy; i++)
                rec_buf[rec_samples + i] = chunk[i * 2];
            rec_samples += to_copy;
        }
    }
    printf("REC: task done samples=%d\n", rec_samples);
    vTaskDelete(NULL);
}

bool sys_audio_record_start(int max_duration_ms)
{
    if (!rx || rec_running) return false;

    rec_max_samples = max_duration_ms * SAMPLE_RATE / 1000;
    int buf_bytes = rec_max_samples * 2; /* mono 16-bit */
    rec_buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rec_buf) rec_buf = malloc(buf_bytes);
    if (!rec_buf) { printf("REC: no mem\n"); return false; }

    rec_samples = 0;
    rec_running = true;
    xTaskCreate(audio_record_task, "audio_rec", 4096, NULL, 4, &rec_task_handle);
    printf("REC: started max=%dms buf=%d\n", max_duration_ms, buf_bytes);
    return true;
}

uint8_t *sys_audio_record_stop(int *out_wav_len)
{
    if (!rec_running) return NULL;
    rec_running = false;

    /* Wait for task to finish (max 500ms) */
    int wait = 0;
    while (rec_task_handle && wait < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait += 10;
    }
    rec_task_handle = NULL;

    int pcm_bytes = rec_samples * 2;
    int wav_len = pcm_bytes + 44;
    uint8_t *wav = malloc(wav_len);
    if (!wav) { free(rec_buf); rec_buf = NULL; return NULL; }

    /* Write WAV header */
    int offset = 0;
    memcpy(wav + offset, "RIFF", 4); offset += 4;
    int file_size = wav_len - 8;
    memcpy(wav + offset, &file_size, 4); offset += 4;
    memcpy(wav + offset, "WAVE", 4); offset += 4;
    memcpy(wav + offset, "fmt ", 4); offset += 4;
    int fmt_size = 16;
    memcpy(wav + offset, &fmt_size, 4); offset += 4;
    short audio_fmt = 1; /* PCM */
    memcpy(wav + offset, &audio_fmt, 2); offset += 2;
    short channels = 1;
    memcpy(wav + offset, &channels, 2); offset += 2;
    int sr = SAMPLE_RATE;
    memcpy(wav + offset, &sr, 4); offset += 4;
    int byte_rate = SAMPLE_RATE * 2; /* mono 16-bit */
    memcpy(wav + offset, &byte_rate, 4); offset += 4;
    short block_align = 2;
    memcpy(wav + offset, &block_align, 2); offset += 2;
    short bits = 16;
    memcpy(wav + offset, &bits, 2); offset += 2;
    memcpy(wav + offset, "data", 4); offset += 4;
    memcpy(wav + offset, &pcm_bytes, 4); offset += 4;
    /* Copy PCM data */
    memcpy(wav + offset, rec_buf, pcm_bytes);
    offset += pcm_bytes;

    free(rec_buf);
    rec_buf = NULL;
    *out_wav_len = wav_len;
    printf("REC: stopped wav=%d pcm=%d samples=%d\n", wav_len, pcm_bytes, rec_samples);
    return wav;
}

bool sys_audio_is_recording(void)
{
    return rec_running;
}

int sys_audio_read_mic(int16_t *buf, int max_samples)
{
    if (!rx) return 0;
    size_t got = 0;
    esp_err_t r = i2s_channel_read(rx, buf, max_samples * 4, &got, pdMS_TO_TICKS(100));
    if (r != ESP_OK || got == 0) return 0;
    return (int)(got / 4); /* stereo → sample count per channel */
}

bool sys_audio_play_mono(const int16_t *mono, int samples, int src_sample_rate)
{
    if (!tx) return false;

    /* Nearest-neighbor resampling to hardware sample rate */
    int out_max = samples * SAMPLE_RATE / src_sample_rate + 1;
    int16_t *resampled = malloc(out_max * sizeof(int16_t));
    if (!resampled) return false;

    int out_samples = 0;
    for (int i = 0; i < out_max - 1; i++) {
        int src_idx = (int)((int64_t)i * src_sample_rate / SAMPLE_RATE);
        if (src_idx < samples)
            resampled[out_samples++] = mono[src_idx];
    }

    /* Enqueue in stereo chunks */
    int pos = 0;
    while (pos < out_samples) {
        int chunk = out_samples - pos;
        if (chunk > AUDIO_CHUNK_SAMPLES) chunk = AUDIO_CHUNK_SAMPLES;
        int16_t *stereo = malloc(chunk * 4);
        if (!stereo) break;
        for (int i = 0; i < chunk; i++) {
            stereo[i * 2]     = resampled[pos + i];
            stereo[i * 2 + 1] = resampled[pos + i];
        }
        if (!enqueue_chunk(stereo, chunk * 4, false)) {
            free(stereo);
            break;
        }
        pos += chunk;
    }
    free(resampled);
    return pos > 0;
}
