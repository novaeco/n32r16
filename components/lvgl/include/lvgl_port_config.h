#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lvgl_port_configure_logging(void);
uint32_t lvgl_port_get_default_dpi(void);

#ifdef __cplusplus
}
#endif

