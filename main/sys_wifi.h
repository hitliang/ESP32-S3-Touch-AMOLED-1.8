#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
} wifi_status_t;

void          sys_wifi_init(void);
wifi_status_t sys_wifi_get_status(void);
int           sys_wifi_get_rssi(void);
bool          sys_wifi_is_connected(void);

/* NTP time */
bool          sys_time_is_synced(void);
struct tm     sys_time_get_tm(void);
time_t        sys_time_get_unix(void);

#ifdef __cplusplus
}
#endif
