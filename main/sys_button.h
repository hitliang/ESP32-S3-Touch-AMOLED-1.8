#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void sys_button_init(void);
bool sys_button_poll(void);   /* call every ~50ms, returns true on press-release */

#ifdef __cplusplus
}
#endif
