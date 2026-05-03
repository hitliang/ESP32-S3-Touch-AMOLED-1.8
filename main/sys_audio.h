#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void sys_audio_init(void);
bool sys_audio_is_playing(void);
void sys_audio_play_wav(const uint8_t *data, int len);
void sys_audio_play_pcm(const int16_t *stereo_data, int sample_count);
void sys_audio_stop(void);

/* Recording */
bool sys_audio_record_start(int max_duration_ms);
uint8_t *sys_audio_record_stop(int *out_wav_len);  /* returns malloc'd WAV buffer */
bool sys_audio_is_recording(void);

#ifdef __cplusplus
}
#endif
