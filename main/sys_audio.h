#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void sys_audio_init(void);
bool sys_audio_is_playing(void);
void sys_audio_play_wav(const uint8_t *data, int len);
void sys_audio_stop(void);

#ifdef __cplusplus
}
#endif
