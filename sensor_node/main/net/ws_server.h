#pragma once

#include "data_model.h"

void sensor_ws_server_start(sensor_data_model_t *model);
void sensor_ws_server_send_update(sensor_data_model_t *model, bool use_cbor);
