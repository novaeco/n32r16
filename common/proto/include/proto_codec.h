#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROTO_MSG_SENSOR_UPDATE,
    PROTO_MSG_COMMAND,
    PROTO_MSG_HEARTBEAT,
    PROTO_MSG_COMMAND_ACK,
} proto_msg_type_t;

typedef struct {
    uint8_t *data;
    size_t len;
    bool is_text;
} proto_buffer_t;

typedef struct {
    uint32_t version;
    proto_msg_type_t type;
    uint64_t timestamp_ms;
    uint32_t seq_id;
    uint32_t crc32;
    bool crc_ok;
    bool is_cbor;
    cJSON *payload;
} proto_envelope_t;

bool proto_encode_sensor_update(const cJSON *payload, uint64_t timestamp_ms, uint32_t seq_id,
                                proto_buffer_t *out_buf, uint32_t *crc_out);
bool proto_encode_command(const cJSON *payload, uint64_t timestamp_ms, uint32_t seq_id,
                          proto_buffer_t *out_buf, uint32_t *crc_out);
bool proto_encode_heartbeat(const cJSON *payload, uint64_t timestamp_ms, uint32_t seq_id,
                            proto_buffer_t *out_buf, uint32_t *crc_out);
bool proto_encode_command_ack(uint32_t ref_seq_id, bool success, const char *reason,
                              uint64_t timestamp_ms, uint32_t seq_id, proto_buffer_t *out_buf,
                              uint32_t *crc_out);

bool proto_decode(const uint8_t *buffer, size_t len, proto_envelope_t *out_msg);
void proto_envelope_free(proto_envelope_t *msg);
void proto_buffer_free(proto_buffer_t *buf);

#ifdef __cplusplus
}
#endif

