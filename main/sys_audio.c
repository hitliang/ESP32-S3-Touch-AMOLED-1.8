#include "sys_audio.h"
#include "sys_i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "es8311.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sys_audio";

/* I2S pins */
#define I2S_PORT       I2S_NUM_0
#define PIN_MCK        GPIO_NUM_16
#define PIN_BCK        GPIO_NUM_9
#define PIN_WS         GPIO_NUM_45
#define PIN_DOUT       GPIO_NUM_8
#define PIN_DIN        GPIO_NUM_10
#define PIN_PA         GPIO_NUM_46

#define SAMPLE_RATE    16000
#define MCLK_MULT      384

static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;

void sys_audio_init(void)
{
    /* Power amplifier enable */
    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_PA,
                         .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(PIN_PA, 1);
    ESP_LOGI(TAG, "PA enabled");

    /* ES8311 codec init over existing I2C bus */
    es8311_handle_t es = es8311_create(sys_i2c_get_port(), ES8311_ADDRRES_0);
    if (!es) {
        ESP_LOGW(TAG, "ES8311 not found");
        return;
    }
    es8311_clock_config_t clk = { .mclk_inverted = false,
                                   .sclk_inverted = false,
                                   .mclk_from_mclk_pin = true,
                                   .mclk_frequency = SAMPLE_RATE * MCLK_MULT,
                                   .sample_frequency = SAMPLE_RATE };
    es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    es8311_voice_volume_set(es, 80, NULL);
    es8311_microphone_config(es, false);
    ESP_LOGI(TAG, "ES8311 ready");

    /* I2S channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = PIN_MCK, .bclk = PIN_BCK, .ws = PIN_WS,
                       .dout = PIN_DOUT, .din = PIN_DIN,
                       .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }},
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULT;
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);
    ESP_LOGI(TAG, "I2S ready");
}

void sys_audio_play_wav(const uint8_t *data, int len)
{
    if (!tx_chan || len < 44) {
        ESP_LOGW(TAG, "Audio not available (tx=%p len=%d)", tx_chan, len);
        return;
    }

    /* Parse WAV header */
    int sample_rate = *(int *)(data + 24);
    int channels    = *(short *)(data + 22);
    int bits        = *(short *)(data + 34);
    int data_ofs    = 0;

    /* Find "data" chunk */
    for (int i = 12; i < len - 8; i++) {
        if (data[i] == 'd' && data[i+1] == 'a' && data[i+2] == 't' && data[i+3] == 'a') {
            data_ofs = i + 8;
            break;
        }
    }
    if (data_ofs == 0) return;

    int pcm_len = len - data_ofs;
    const uint8_t *pcm = data + data_ofs;

    ESP_LOGI(TAG, "WAV: %dHz %dch %dbits, pcm=%d bytes",
             sample_rate, channels, bits, pcm_len);

    /* Reconfigure I2S if sample rate differs */
    if (sample_rate != SAMPLE_RATE) {
        i2s_channel_disable(tx_chan);
        /* Just use the WAV's rate — approximate */
        i2s_std_clk_config_t clk = { .sample_rate_hz = (uint32_t)sample_rate,
                                      .mclk_multiple = MCLK_MULT };
        i2s_channel_reconfig_std_clock(tx_chan, &clk);
        i2s_channel_enable(tx_chan);
    }

    /* If mono, duplicate samples for stereo I2S */
    if (channels == 1 && bits == 16) {
        int16_t *buf = malloc(pcm_len * 2);
        if (!buf) return;
        const int16_t *src = (const int16_t *)pcm;
        int n = pcm_len / 2;
        for (int i = 0; i < n; i++) {
            buf[i * 2] = src[i];
            buf[i * 2 + 1] = src[i];
        }
        size_t written;
        i2s_channel_write(tx_chan, buf, pcm_len * 2, &written, portMAX_DELAY);
        free(buf);
    } else {
        size_t written;
        i2s_channel_write(tx_chan, pcm, pcm_len, &written, portMAX_DELAY);
    }

    /* Drain */
    i2s_channel_disable(tx_chan);
    i2s_channel_enable(tx_chan);
}

bool sys_audio_is_playing(void) { return false; }
void sys_audio_stop(void) {}
