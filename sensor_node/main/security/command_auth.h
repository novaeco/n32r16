#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sensor_command_auth_init(void);
void sensor_command_auth_shutdown(void);

#ifdef __cplusplus
}
#endif

