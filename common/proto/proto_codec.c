#include "proto_codec.h"

#include <stdlib.h>
#include <string.h>

#include "cjson_helper.h"
#include "proto_crc32.h"

#define PROTO_VERSION 1U

static bool proto_encode(const cJSON *payload, const char *type_str, uint64_t timestamp_ms,
                         char **out_str, uint32_t *crc_out) {
    if (payload == NULL || type_str == NULL || out_str == NULL || crc_out == NULL) {
        return false;
    }

    json_doc_t doc;
    if (!json_doc_init(&doc)) {
        return false;
    }

    if (!json_doc_set_uint(&doc, "v", PROTO_VERSION) ||
        !json_doc_set_string(&doc, "type", type_str) ||
        !json_doc_set_uint(&doc, "ts", timestamp_ms)) {
        json_doc_free(&doc);
        return false;
    }

    cJSON *payload_copy = cJSON_Duplicate(payload, true);
    if (payload_copy == NULL || !json_doc_set_object(&doc, "payload", payload_copy)) {
        cJSON_Delete(payload_copy);
        json_doc_free(&doc);
        return false;
    }

    char *json_str = json_doc_print_unformatted(&doc);
    json_doc_free(&doc);
    if (json_str == NULL) {
        return false;
    }
    *crc_out = proto_crc32_compute((const uint8_t *)json_str, strlen(json_str));

    *out_str = json_str;
    return true;
}

bool proto_encode_sensor_update(const cJSON *payload, uint64_t timestamp_ms, char **out_str,
                                uint32_t *crc_out) {
    return proto_encode(payload, "sensor_update", timestamp_ms, out_str, crc_out);
}

bool proto_encode_command(const cJSON *payload, uint64_t timestamp_ms, char **out_str,
                          uint32_t *crc_out) {
    return proto_encode(payload, "cmd", timestamp_ms, out_str, crc_out);
}

static proto_msg_type_t parse_type(const char *type_str) {
    if (strcmp(type_str, "sensor_update") == 0) {
        return PROTO_MSG_SENSOR_UPDATE;
    }
    if (strcmp(type_str, "cmd") == 0) {
        return PROTO_MSG_COMMAND;
    }
    if (strcmp(type_str, "heartbeat") == 0) {
        return PROTO_MSG_HEARTBEAT;
    }
    return PROTO_MSG_HEARTBEAT;
}

bool proto_decode(const uint8_t *buffer, size_t len, proto_envelope_t *out_msg) {
    if (buffer == NULL || out_msg == NULL) {
        return false;
    }
    memset(out_msg, 0, sizeof(*out_msg));

    cJSON *root = cJSON_ParseWithLength((const char *)buffer, len);
    if (root == NULL) {
        return false;
    }

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *auth = cJSON_GetObjectItemCaseSensitive(root, "auth");

    if (!cJSON_IsNumber(version) || !cJSON_IsString(type) || !cJSON_IsNumber(ts) ||
        payload == NULL) {
        cJSON_Delete(root);
        return false;
    }

    out_msg->version = (uint32_t)version->valuedouble;
    out_msg->type = parse_type(type->valuestring);
    out_msg->timestamp_ms = (uint64_t)ts->valuedouble;
    out_msg->payload = cJSON_Duplicate(payload, true);
    out_msg->crc32 = proto_crc32_compute(buffer, len);
    if (auth != NULL) {
        out_msg->auth = cJSON_Duplicate(auth, true);
    }

    cJSON_Delete(root);
    if (out_msg->payload == NULL) {
        proto_envelope_free(out_msg);
        return false;
    }
    if (auth != NULL && out_msg->auth == NULL) {
        proto_envelope_free(out_msg);
        return false;
    }
    return true;
}

void proto_envelope_free(proto_envelope_t *msg) {
    if (msg == NULL) {
        return;
    }
    if (msg->payload != NULL) {
        cJSON_Delete(msg->payload);
        msg->payload = NULL;
    }
    if (msg->auth != NULL) {
        cJSON_Delete(msg->auth);
        msg->auth = NULL;
    }
}

