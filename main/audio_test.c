#include "audio_test.h"
#include "sys_i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "es8311.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "audio_t";

/* I2S pins (matching Waveshare demo) */
#define PIN_MCK     GPIO_NUM_16
#define PIN_BCK     GPIO_NUM_9
#define PIN_WS      GPIO_NUM_45
#define PIN_DOUT    GPIO_NUM_8
#define PIN_PA      GPIO_NUM_46

#define SAMPLE_RATE 16000
#define MCLK_MULT   384

static i2s_chan_handle_t tx = NULL;

void audio_test_play(void)
{
    printf("=== Audio Test Start ===\n");

    /* 1. PA enable */
    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_PA, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_PA, 1);
    printf("AT: PA on\n");

    /* 2. ES8311 codec */
    printf("AT: ES8311 create...\n");
    es8311_handle_t es = es8311_create(sys_i2c_get_port(), ES8311_ADDRRES_0);
    if (!es) { printf("AT: ES8311 not found!\n"); return; }
    printf("AT: ES8311 found\n");

    es8311_clock_config_t clk = {
        .mclk_inverted = false, .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_RATE * MCLK_MULT,
        .sample_frequency = SAMPLE_RATE,
    };
    printf("AT: ES8311 init...\n");
    es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    printf("AT: ES8311 volume...\n");
    es8311_voice_volume_set(es, 80, NULL);
    es8311_microphone_config(es, false);
    printf("AT: ES8311 ready\n");

    /* 3. I2S TX */
    printf("AT: I2S init...\n");
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    i2s_new_channel(&cc, &tx, NULL);

    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = PIN_MCK, .bclk = PIN_BCK, .ws = PIN_WS,
                       .dout = PIN_DOUT,
                       .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }},
    };
    sc.clk_cfg.mclk_multiple = MCLK_MULT;
    i2s_channel_init_std_mode(tx, &sc);
    i2s_channel_enable(tx);
    printf("AT: I2S ready\n");

    /* 4. Generate 440Hz sine wave (1 second, 16-bit mono→stereo) */
    int n = SAMPLE_RATE; /* 1 second */
    int16_t *buf = malloc(n * 4); /* stereo: 2x16bit per sample */
    if (!buf) { printf("AT: malloc fail\n"); return; }
    for (int i = 0; i < n; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / SAMPLE_RATE) * 16000.0f);
        buf[i*2] = s; buf[i*2+1] = s; /* mono→stereo */
    }

    printf("AT: playing 440Hz...\n");
    size_t written;
    i2s_channel_write(tx, buf, n * 4, &written, portMAX_DELAY);
    printf("AT: wrote %d bytes\n", (int)written);
    free(buf);

    /* Drain */
    vTaskDelay(pdMS_TO_TICKS(200));
    i2s_channel_disable(tx);
    printf("=== Audio Test Done ===\n");
}
