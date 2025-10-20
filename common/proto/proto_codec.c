#include "proto_codec.h"

#include <stdlib.h>
#include <string.h>

#include "cjson_helper.h"
#include "proto_crc32.h"
#include "sdkconfig.h"

#if CONFIG_USE_CBOR
#include "tinycbor/cbor.h"
#endif

#define PROTO_VERSION 1U

static uint32_t compute_payload_crc(const cJSON *payload) {
    uint32_t crc = 0;
    char *serialized = cJSON_PrintUnformatted((cJSON *)payload);
    if (serialized != NULL) {
        crc = proto_crc32_compute((const uint8_t *)serialized, strlen(serialized));
        free(serialized);
    }
    return crc;
}

static bool json_add_envelope(json_doc_t *doc, const cJSON *payload, const char *type_str,
                              uint64_t timestamp_ms, uint32_t seq_id, uint32_t *crc_out) {
    if (!json_doc_set_uint(doc, "v", PROTO_VERSION) || !json_doc_set_string(doc, "type", type_str) ||
        !json_doc_set_uint(doc, "ts", timestamp_ms) || !json_doc_set_uint(doc, "seq", seq_id)) {
        return false;
    }

    cJSON *payload_copy = cJSON_Duplicate(payload, true);
    if (payload_copy == NULL) {
        return false;
    }

    if (!json_doc_set_object(doc, "payload", payload_copy)) {
        cJSON_Delete(payload_copy);
        return false;
    }

    uint32_t crc = compute_payload_crc(payload_copy);
    if (!json_doc_set_uint(doc, "crc", crc)) {
        return false;
    }
    if (crc_out != NULL) {
        *crc_out = crc;
    }
    return true;
}

static bool encode_json(const cJSON *payload, const char *type_str, uint64_t timestamp_ms, uint32_t seq_id,
                        proto_buffer_t *out_buf, uint32_t *crc_out) {
    json_doc_t doc;
    if (!json_doc_init(&doc)) {
        return false;
    }

    if (!json_add_envelope(&doc, payload, type_str, timestamp_ms, seq_id, crc_out)) {
        json_doc_free(&doc);
        return false;
    }

    char *json_str = json_doc_print_unformatted(&doc);
    json_doc_free(&doc);
    if (json_str == NULL) {
        return false;
    }

    out_buf->data = (uint8_t *)json_str;
    out_buf->len = strlen(json_str);
    out_buf->is_text = true;
    return true;
}

#if CONFIG_USE_CBOR

static bool cjson_to_cbor(CborEncoder *encoder, const cJSON *item);

static bool cjson_array_to_cbor(CborEncoder *encoder, const cJSON *array) {
    size_t length = 0;
    for (const cJSON *child = array->child; child != NULL; child = child->next) {
        ++length;
    }
    CborEncoder arr_encoder;
    CborError err = cbor_encoder_create_array(encoder, &arr_encoder, length);
    if (err != CborNoError) {
        return false;
    }
    for (const cJSON *child = array->child; child != NULL; child = child->next) {
        if (!cjson_to_cbor(&arr_encoder, child)) {
            return false;
        }
    }
    return cbor_encoder_close_container(encoder, &arr_encoder) == CborNoError;
}

static bool cjson_object_to_cbor(CborEncoder *encoder, const cJSON *object) {
    size_t length = 0;
    for (const cJSON *child = object->child; child != NULL; child = child->next) {
        ++length;
    }
    CborEncoder map_encoder;
    CborError err = cbor_encoder_create_map(encoder, &map_encoder, length);
    if (err != CborNoError) {
        return false;
    }
    for (const cJSON *child = object->child; child != NULL; child = child->next) {
        if (cbor_encode_text_stringz(&map_encoder, child->string) != CborNoError) {
            return false;
        }
        if (!cjson_to_cbor(&map_encoder, child)) {
            return false;
        }
    }
    return cbor_encoder_close_container(encoder, &map_encoder) == CborNoError;
}

static bool cjson_to_cbor(CborEncoder *encoder, const cJSON *item) {
    if (cJSON_IsNumber(item)) {
        if (item->valuedouble == (double)(int64_t)item->valueint) {
            return cbor_encode_int(encoder, (int64_t)item->valuedouble) == CborNoError;
        }
        return cbor_encode_double(encoder, item->valuedouble) == CborNoError;
    }
    if (cJSON_IsString(item)) {
        return cbor_encode_text_stringz(encoder, item->valuestring) == CborNoError;
    }
    if (cJSON_IsBool(item)) {
        return cbor_encode_boolean(encoder, cJSON_IsTrue(item)) == CborNoError;
    }
    if (cJSON_IsNull(item)) {
        return cbor_encode_null(encoder) == CborNoError;
    }
    if (cJSON_IsArray(item)) {
        return cjson_array_to_cbor(encoder, item);
    }
    if (cJSON_IsObject(item)) {
        return cjson_object_to_cbor(encoder, item);
    }
    return false;
}

static bool encode_cbor(const cJSON *payload, const char *type_str, uint64_t timestamp_ms, uint32_t seq_id,
                        proto_buffer_t *out_buf, uint32_t *crc_out) {
    size_t capacity = 512;
    uint8_t *buffer = NULL;
    CborError err = CborNoError;
    uint32_t crc = compute_payload_crc(payload);

    do {
        free(buffer);
        buffer = (uint8_t *)malloc(capacity);
        if (buffer == NULL) {
            return false;
        }
        CborEncoder encoder;
        cbor_encoder_init(&encoder, buffer, capacity, 0);

        CborEncoder map;
        err = cbor_encoder_create_map(&encoder, &map, 6);
        if (err != CborNoError) {
            break;
        }

        err = cbor_encode_text_stringz(&map, "v");
        if (err != CborNoError) {
            break;
        }
        err = cbor_encode_uint(&map, PROTO_VERSION);
        if (err != CborNoError) {
            break;
        }

        err = cbor_encode_text_stringz(&map, "type");
        if (err != CborNoError) {
            break;
        }
        err = cbor_encode_text_stringz(&map, type_str);
        if (err != CborNoError) {
            break;
        }

        err = cbor_encode_text_stringz(&map, "ts");
        if (err != CborNoError) {
            break;
        }
        err = cbor_encode_uint(&map, timestamp_ms);
        if (err != CborNoError) {
            break;
        }

        err = cbor_encode_text_stringz(&map, "seq");
        if (err != CborNoError) {
            break;
        }
        err = cbor_encode_uint(&map, seq_id);
        if (err != CborNoError) {
            break;
        }

        err = cbor_encode_text_stringz(&map, "payload");
        if (err != CborNoError) {
            break;
        }
        if (!cjson_to_cbor(&map, payload)) {
            err = CborErrorUnknownType;
            break;
        }

        err = cbor_encode_text_stringz(&map, "crc");
        if (err != CborNoError) {
            break;
        }
        err = cbor_encode_uint(&map, crc);
        if (err != CborNoError) {
            break;
        }

        err = cbor_encoder_close_container(&encoder, &map);
        if (err == CborErrorOutOfMemory) {
            capacity *= 2;
            err = CborNoError;
            continue;
        }
        if (err != CborNoError) {
            break;
        }

        size_t used = cbor_encoder_get_buffer_size(&encoder, buffer);
        out_buf->data = buffer;
        out_buf->len = used;
        out_buf->is_text = false;
        if (crc_out != NULL) {
            *crc_out = crc;
        }
        return true;
    } while (true);

    free(buffer);
    return false;
}
#endif

static bool encode_message(const cJSON *payload, const char *type_str, uint64_t timestamp_ms, uint32_t seq_id,
                           proto_buffer_t *out_buf, uint32_t *crc_out) {
#if CONFIG_USE_CBOR
    if (!CONFIG_USE_CBOR) {
        return encode_json(payload, type_str, timestamp_ms, seq_id, out_buf, crc_out);
    }
    if (encode_cbor(payload, type_str, timestamp_ms, seq_id, out_buf, crc_out)) {
        return true;
    }
    return encode_json(payload, type_str, timestamp_ms, seq_id, out_buf, crc_out);
#else
    return encode_json(payload, type_str, timestamp_ms, seq_id, out_buf, crc_out);
#endif
}

bool proto_encode_sensor_update(const cJSON *payload, uint64_t timestamp_ms, uint32_t seq_id,
                                proto_buffer_t *out_buf, uint32_t *crc_out) {
    return encode_message(payload, "sensor_update", timestamp_ms, seq_id, out_buf, crc_out);
}

bool proto_encode_command(const cJSON *payload, uint64_t timestamp_ms, uint32_t seq_id,
                          proto_buffer_t *out_buf, uint32_t *crc_out) {
    return encode_message(payload, "cmd", timestamp_ms, seq_id, out_buf, crc_out);
}

bool proto_encode_heartbeat(const cJSON *payload, uint64_t timestamp_ms, uint32_t seq_id,
                            proto_buffer_t *out_buf, uint32_t *crc_out) {
    return encode_message(payload, "heartbeat", timestamp_ms, seq_id, out_buf, crc_out);
}

bool proto_encode_command_ack(uint32_t ref_seq_id, bool success, const char *reason, uint64_t timestamp_ms,
                              uint32_t seq_id, proto_buffer_t *out_buf, uint32_t *crc_out) {
    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        return false;
    }
    cJSON_AddNumberToObject(payload, "ref_seq", ref_seq_id);
    cJSON_AddBoolToObject(payload, "ok", success);
    if (!success && reason != NULL) {
        cJSON_AddStringToObject(payload, "reason", reason);
    }
    bool encoded = encode_message(payload, "cmd_ack", timestamp_ms, seq_id, out_buf, crc_out);
    cJSON_Delete(payload);
    return encoded;
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
    if (strcmp(type_str, "cmd_ack") == 0) {
        return PROTO_MSG_COMMAND_ACK;
    }
    return PROTO_MSG_HEARTBEAT;
}

static bool decode_from_json(const uint8_t *buffer, size_t len, proto_envelope_t *out_msg) {
    cJSON *root = cJSON_ParseWithLength((const char *)buffer, len);
    if (root == NULL) {
        return false;
    }

    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *crc = cJSON_GetObjectItemCaseSensitive(root, "crc");

    if (!cJSON_IsNumber(version) || !cJSON_IsString(type) || !cJSON_IsNumber(ts) || !cJSON_IsNumber(seq) ||
        payload == NULL || !cJSON_IsNumber(crc)) {
        cJSON_Delete(root);
        return false;
    }

    out_msg->version = (uint32_t)version->valuedouble;
    out_msg->type = parse_type(type->valuestring);
    out_msg->timestamp_ms = (uint64_t)ts->valuedouble;
    out_msg->seq_id = (uint32_t)seq->valuedouble;
    out_msg->payload = cJSON_Duplicate(payload, true);
    out_msg->is_cbor = false;
    out_msg->crc32 = compute_payload_crc(payload);
    out_msg->crc_ok = (out_msg->crc32 == (uint32_t)crc->valuedouble);

    cJSON_Delete(root);
    return out_msg->payload != NULL;
}

#if CONFIG_USE_CBOR

static bool cbor_to_cjson(CborValue *value, cJSON **out_item);

static bool cbor_array_to_cjson(CborValue *value, cJSON **out_item) {
    cJSON *array = cJSON_CreateArray();
    if (array == NULL) {
        return false;
    }
    CborValue iter;
    CborError err = cbor_value_enter_container(value, &iter);
    if (err != CborNoError) {
        cJSON_Delete(array);
        return false;
    }
    while (!cbor_value_at_end(&iter)) {
        cJSON *child = NULL;
        if (!cbor_to_cjson(&iter, &child)) {
            cJSON_Delete(array);
            cbor_value_leave_container(value, &iter);
            return false;
        }
        cJSON_AddItemToArray(array, child);
    }
    cbor_value_leave_container(value, &iter);
    *out_item = array;
    return true;
}

static bool cbor_map_to_cjson(CborValue *value, cJSON **out_item) {
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return false;
    }
    CborValue iter;
    if (cbor_value_enter_container(value, &iter) != CborNoError) {
        cJSON_Delete(object);
        return false;
    }
    while (!cbor_value_at_end(&iter)) {
        size_t key_len = 0;
        if (!cbor_value_is_text_string(&iter)) {
            cJSON_Delete(object);
            cbor_value_leave_container(value, &iter);
            return false;
        }
        cbor_value_calculate_string_length(&iter, &key_len);
        char *key = (char *)malloc(key_len + 1);
        if (key == NULL) {
            cJSON_Delete(object);
            cbor_value_leave_container(value, &iter);
            return false;
        }
        CborError err = cbor_value_copy_text_string(&iter, key, &key_len, &iter);
        if (err != CborNoError) {
            free(key);
            cJSON_Delete(object);
            cbor_value_leave_container(value, &iter);
            return false;
        }
        cJSON *child = NULL;
        if (!cbor_to_cjson(&iter, &child)) {
            free(key);
            cJSON_Delete(object);
            cbor_value_leave_container(value, &iter);
            return false;
        }
        cJSON_AddItemToObject(object, key, child);
        free(key);
    }
    cbor_value_leave_container(value, &iter);
    *out_item = object;
    return true;
}

static bool cbor_to_cjson(CborValue *value, cJSON **out_item) {
    if (cbor_value_is_integer(value)) {
        int64_t val = 0;
        if (cbor_value_get_int64(value, &val) != CborNoError) {
            return false;
        }
        *out_item = cJSON_CreateNumber((double)val);
        cbor_value_advance(value);
        return *out_item != NULL;
    }
    if (cbor_value_is_unsigned_integer(value)) {
        uint64_t val = 0;
        if (cbor_value_get_uint64(value, &val) != CborNoError) {
            return false;
        }
        *out_item = cJSON_CreateNumber((double)val);
        cbor_value_advance(value);
        return *out_item != NULL;
    }
    if (cbor_value_is_double(value) || cbor_value_is_half_float(value) ||
        cbor_value_is_float(value)) {
        double d = 0.0;
        if (cbor_value_get_double(value, &d) != CborNoError) {
            return false;
        }
        *out_item = cJSON_CreateNumber(d);
        cbor_value_advance(value);
        return *out_item != NULL;
    }
    if (cbor_value_is_boolean(value)) {
        bool b = false;
        if (cbor_value_get_boolean(value, &b) != CborNoError) {
            return false;
        }
        *out_item = cJSON_CreateBool(b);
        cbor_value_advance(value);
        return *out_item != NULL;
    }
    if (cbor_value_is_null(value)) {
        *out_item = cJSON_CreateNull();
        cbor_value_advance(value);
        return *out_item != NULL;
    }
    if (cbor_value_is_text_string(value)) {
        size_t len = 0;
        cbor_value_calculate_string_length(value, &len);
        char *str = (char *)malloc(len + 1);
        if (str == NULL) {
            return false;
        }
        CborError err = cbor_value_copy_text_string(value, str, &len, value);
        if (err != CborNoError) {
            free(str);
            return false;
        }
        str[len] = '\0';
        *out_item = cJSON_CreateString(str);
        free(str);
        return *out_item != NULL;
    }
    if (cbor_value_is_array(value)) {
        return cbor_array_to_cjson(value, out_item);
    }
    if (cbor_value_is_map(value)) {
        return cbor_map_to_cjson(value, out_item);
    }
    return false;
}

static bool decode_from_cbor(const uint8_t *buffer, size_t len, proto_envelope_t *out_msg) {
    CborParser parser;
    CborValue value;
    if (cbor_parser_init(buffer, len, 0, &parser, &value) != CborNoError) {
        return false;
    }
    if (!cbor_value_is_map(&value)) {
        return false;
    }

    CborValue map_it;
    if (cbor_value_enter_container(&value, &map_it) != CborNoError) {
        return false;
    }

    bool have_version = false;
    bool have_type = false;
    bool have_ts = false;
    bool have_seq = false;
    bool have_crc = false;
    uint32_t crc_value = 0;

    while (!cbor_value_at_end(&map_it)) {
        if (!cbor_value_is_text_string(&map_it)) {
            cbor_value_leave_container(&value, &map_it);
            return false;
        }
        size_t key_len = 0;
        cbor_value_calculate_string_length(&map_it, &key_len);
        char *key = (char *)malloc(key_len + 1);
        if (key == NULL) {
            cbor_value_leave_container(&value, &map_it);
            return false;
        }
        if (cbor_value_copy_text_string(&map_it, key, &key_len, &map_it) != CborNoError) {
            free(key);
            cbor_value_leave_container(&value, &map_it);
            return false;
        }

        if (strcmp(key, "v") == 0) {
            uint64_t version = 0;
            if (cbor_value_get_uint64(&map_it, &version) != CborNoError) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            out_msg->version = (uint32_t)version;
            cbor_value_advance(&map_it);
            have_version = true;
        } else if (strcmp(key, "type") == 0) {
            size_t type_len = 0;
            cbor_value_calculate_string_length(&map_it, &type_len);
            char *type = (char *)malloc(type_len + 1);
            if (type == NULL) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            if (cbor_value_copy_text_string(&map_it, type, &type_len, &map_it) != CborNoError) {
                free(type);
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            out_msg->type = parse_type(type);
            free(type);
            have_type = true;
        } else if (strcmp(key, "ts") == 0) {
            uint64_t ts = 0;
            if (cbor_value_get_uint64(&map_it, &ts) != CborNoError) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            out_msg->timestamp_ms = ts;
            cbor_value_advance(&map_it);
            have_ts = true;
        } else if (strcmp(key, "seq") == 0) {
            uint64_t seq_val = 0;
            if (cbor_value_get_uint64(&map_it, &seq_val) != CborNoError) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            out_msg->seq_id = (uint32_t)seq_val;
            cbor_value_advance(&map_it);
            have_seq = true;
        } else if (strcmp(key, "payload") == 0) {
            if (!cbor_to_cjson(&map_it, &out_msg->payload)) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
        } else if (strcmp(key, "crc") == 0) {
            uint64_t crc = 0;
            if (cbor_value_get_uint64(&map_it, &crc) != CborNoError) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            crc_value = (uint32_t)crc;
            cbor_value_advance(&map_it);
            have_crc = true;
        } else {
            cJSON *ignored = NULL;
            if (!cbor_to_cjson(&map_it, &ignored)) {
                free(key);
                cbor_value_leave_container(&value, &map_it);
                return false;
            }
            cJSON_Delete(ignored);
        }
        free(key);
    }
    cbor_value_leave_container(&value, &map_it);

    if (!have_version || !have_type || !have_ts || !have_seq || !have_crc || out_msg->payload == NULL) {
        cJSON_Delete(out_msg->payload);
        out_msg->payload = NULL;
        return false;
    }

    out_msg->is_cbor = true;
    out_msg->crc32 = compute_payload_crc(out_msg->payload);
    out_msg->crc_ok = (out_msg->crc32 == crc_value);

    return true;
}
#endif

bool proto_decode(const uint8_t *buffer, size_t len, proto_envelope_t *out_msg) {
    if (buffer == NULL || len == 0 || out_msg == NULL) {
        return false;
    }
    memset(out_msg, 0, sizeof(*out_msg));

    if (buffer[0] == '{' || buffer[0] == '[') {
        return decode_from_json(buffer, len, out_msg);
    }
#if CONFIG_USE_CBOR
    if (decode_from_cbor(buffer, len, out_msg)) {
        return true;
    }
#endif
    return decode_from_json(buffer, len, out_msg);
}

void proto_envelope_free(proto_envelope_t *msg) {
    if (msg == NULL) {
        return;
    }
    if (msg->payload != NULL) {
        cJSON_Delete(msg->payload);
        msg->payload = NULL;
    }
}

void proto_buffer_free(proto_buffer_t *buf) {
    if (buf == NULL) {
        return;
    }
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->is_text = true;
}

