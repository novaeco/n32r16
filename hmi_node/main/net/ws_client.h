#pragma once

#include "common/proto/messages.h"
#include "data_model.h"
#include "esp_err.h"

esp_err_t hmi_ws_client_start(hmi_data_model_t *model);
void hmi_ws_client_stop(void);
bool hmi_ws_client_is_connected(void);
esp_err_t hmi_ws_client_send_command(const proto_command_t *cmd);
