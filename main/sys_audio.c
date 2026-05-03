#include "sys_audio.h"
#include "sys_i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "es8311.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "sys_audio";

#define PIN_MCK    GPIO_NUM_16
#define PIN_BCK    GPIO_NUM_9
#define PIN_WS     GPIO_NUM_45
#define PIN_DOUT   GPIO_NUM_8
#define PIN_PA     GPIO_NUM_46
#define SAMPLE_RATE 24000
#define MCLK_MULT   256

static i2s_chan_handle_t tx = NULL;
static bool inited = false;

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

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    cc.dma_desc_num = 8;
    cc.dma_frame_num = 512;
    if (i2s_new_channel(&cc, &tx, NULL) != ESP_OK) {
        printf("AUDIO: I2S busy, using existing\n");
    }

    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk=PIN_MCK, .bclk=PIN_BCK, .ws=PIN_WS, .dout=PIN_DOUT,
                       .invert_flags={.mclk_inv=false,.bclk_inv=false,.ws_inv=false} },
    };
    sc.clk_cfg.mclk_multiple = MCLK_MULT;
    if (tx) i2s_channel_init_std_mode(tx, &sc);
    if (tx) i2s_channel_enable(tx);

    inited = true;
    printf("AUDIO: ready\n");
}

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

    int total_written = 0;
    if (channels == 1 && bits == 16) {
        /* Mono → stereo */
        int n = pcm_len / 2;
        int16_t *buf = malloc(n * 4);
        if (!buf) { printf("AUDIO: stereo buf OOM\n"); return; }
        const int16_t *src = (const int16_t*)pcm;
        for (int i = 0; i < n; i++) { buf[i*2] = src[i]; buf[i*2+1] = src[i]; }
        int stereo_bytes = n * 4;
        /* Write in chunks — DMA buffer is small */
        while (total_written < stereo_bytes) {
            size_t w = 0;
            esp_err_t r = i2s_channel_write(tx, (uint8_t*)buf + total_written,
                            stereo_bytes - total_written, &w, pdMS_TO_TICKS(10000));
            total_written += w;
            if (r != ESP_OK || w == 0) break;
        }
        free(buf);
    } else {
        while (total_written < pcm_len) {
            size_t w = 0;
            esp_err_t r = i2s_channel_write(tx, pcm + total_written,
                            pcm_len - total_written, &w, pdMS_TO_TICKS(10000));
            total_written += w;
            if (r != ESP_OK || w == 0) break;
        }
    }
    printf("AUDIO: total_written=%d\n", total_written);

    /* Flush: write silence to push remaining data through DMA pipeline */
    int16_t zero[512] = {0};
    for (int i = 0; i < 4; i++) {
        size_t w = 0;
        i2s_channel_write(tx, zero, sizeof(zero), &w, pdMS_TO_TICKS(1000));
    }

    /* Wait for DMA to drain + codec output */
    int play_ms = (wav_sr > 0) ? (pcm_len / (channels * bits / 8) * 1000 / wav_sr) + 1500 : 3000;
    printf("AUDIO: waiting %dms\n", play_ms);
    vTaskDelay(pdMS_TO_TICKS(play_ms));
}

void sys_audio_play_pcm(const int16_t *stereo_data, int sample_count)
{
    if (!tx) { printf("AUDIO: pcm not ready\n"); return; }
    size_t w;
    i2s_channel_write(tx, stereo_data, sample_count * 4, &w, pdMS_TO_TICKS(10000));
    printf("AUDIO: pcm wrote %d\n", (int)w);
    vTaskDelay(pdMS_TO_TICKS(200));
}

bool sys_audio_is_playing(void) { return false; }
void sys_audio_stop(void) {}
