#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void        sys_sdcard_init(void);
bool        sys_sdcard_mounted(void);
const char *sys_sdcard_mount_point(void);
bool        sys_sdcard_file_exists(const char *path);
int         sys_sdcard_file_size(const char *path);

#ifdef __cplusplus
}
#endif
