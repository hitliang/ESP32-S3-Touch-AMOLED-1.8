#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- XIAOZHI Client States ---- */
typedef enum {
    XZ_STATE_IDLE,
    XZ_STATE_CONNECTING,
    XZ_STATE_CONNECTED,
    XZ_STATE_LISTENING,
    XZ_STATE_SPEAKING,
    XZ_STATE_DISCONNECTED,
} xz_state_t;

/* ---- Configuration ---- */
typedef struct {
    char ws_url[256];
    char device_id[64];
    int  hardware_sample_rate;
} xz_config_t;

/* ---- Callbacks ---- */
typedef void (*xz_state_cb_t)(xz_state_t state, const char *text);
typedef void (*xz_audio_cb_t)(const int16_t *pcm, int samples, int sample_rate);

/* ---- API ---- */
void xz_client_init(const xz_config_t *config);
void xz_client_set_callbacks(xz_state_cb_t state_cb, xz_audio_cb_t audio_cb);
bool xz_client_start(void);
void xz_client_stop(void);
void xz_client_feed_audio(const int16_t *pcm, int samples);
void xz_client_start_listening(void);
void xz_client_stop_listening(void);
xz_state_t xz_client_get_state(void);
bool xz_client_is_connected(void);

#ifdef __cplusplus
}
#endif
