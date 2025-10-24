#pragma once

#include "common/proto/messages.h"
#include <stdbool.h>

void ui_init(void);
void ui_update_sensor_data(const proto_sensor_update_t *update);
void ui_update_connection_status(bool connected);
void ui_process(void);
