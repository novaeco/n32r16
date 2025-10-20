#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*ws_update_cb_t)(void);

esp_err_t hmi_ws_client_start(void);
void hmi_ws_client_set_update_callback(ws_update_cb_t cb);
esp_err_t hmi_ws_client_send(const char *payload);

