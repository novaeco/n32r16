#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t sensor_ws_server_start(void);
void sensor_ws_server_send_snapshot(void);

