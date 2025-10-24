#pragma once

#include "data_model.h"

void hmi_ws_client_start(hmi_data_model_t *model);
void hmi_ws_client_stop(void);
bool hmi_ws_client_is_connected(void);
