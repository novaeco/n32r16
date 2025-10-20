#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_codec.h"

esp_err_t sensor_ws_server_start(void);
void sensor_ws_server_send_snapshot(void);
void sensor_ws_server_send_heartbeat(const proto_buffer_t *packet);
esp_err_t sensor_ws_server_send_ack(uint32_t ref_seq_id, bool success, const char *reason);

