#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

typedef enum {
    PROTO_MSG_SENSOR_UPDATE,
    PROTO_MSG_COMMAND,
    PROTO_MSG_HEARTBEAT,
} proto_msg_type_t;

typedef struct {
    uint32_t version;
    proto_msg_type_t type;
    uint64_t timestamp_ms;
    uint32_t crc32;
    cJSON *payload;
    cJSON *auth;
} proto_envelope_t;

bool proto_encode_sensor_update(const cJSON *payload, uint64_t timestamp_ms, char **out_str,
                                uint32_t *crc_out);
bool proto_encode_command(const cJSON *payload, uint64_t timestamp_ms, char **out_str,
                          uint32_t *crc_out);
bool proto_decode(const uint8_t *buffer, size_t len, proto_envelope_t *out_msg);
void proto_envelope_free(proto_envelope_t *msg);

