#include "audio_test.h"
#include "sys_audio.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define SAMPLE_RATE 16000

void audio_test_play(void)
{
    printf("=== Audio Test Start ===\n");

    /* Use shared audio init (same I2S channel as Voice AI) */
    sys_audio_init();

    /* Generate 440Hz sine wave (1 second, 16-bit stereo) */
    int n = SAMPLE_RATE;
    int16_t *buf = malloc(n * 4);
    if (!buf) { printf("AT: malloc fail\n"); return; }
    for (int i = 0; i < n; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / SAMPLE_RATE) * 16000.0f);
        buf[i*2] = s; buf[i*2+1] = s;
    }

    printf("AT: playing 440Hz...\n");
    sys_audio_play_pcm(buf, n);
    free(buf);
    printf("=== Audio Test Done ===\n");
}
