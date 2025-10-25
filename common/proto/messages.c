#include "messages.h"

#include "proto_crc32.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#if CONFIG_USE_CBOR
#include "tinycbor/cbor.h"
#endif

static const char *TAG = "proto";

static bool json_append(char **cursor, size_t *remaining, const char *fmt, ...)
{
    if (!cursor || !remaining || !*remaining) {
        return false;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(*cursor, *remaining, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= *remaining) {
        return false;
    }
    *cursor += (size_t)written;
    *remaining -= (size_t)written;
    return true;
}

static bool encode_sensor_update_json_into(const proto_sensor_update_t *msg, uint8_t *buffer,
                                      size_t *buffer_len, uint32_t *crc32)
{
    if (!msg || !buffer || !buffer_len || *buffer_len == 0) {
        return false;
    }
    char *cursor = (char *)buffer;
    size_t remaining = *buffer_len;
    if (!json_append(&cursor, &remaining,
                     "{\"v\":1,\"type\":\"sensor_update\",\"ts\":%" PRIu32 ",\"seq\":%" PRIu32 ",\"sht20\":[",
                     msg->timestamp_ms, msg->sequence_id)) {
        return false;
    }
    for (size_t i = 0; i < msg->sht20_count; ++i) {
        const proto_sht20_reading_t *entry = &msg->sht20[i];
        if (!json_append(&cursor, &remaining,
                         "%s{\"id\":\"%s\",\"t\":%.2f,\"rh\":%.2f,\"ok\":%s}",
                         i > 0 ? "," : "", entry->id, entry->temperature_c, entry->humidity_percent,
                         entry->valid ? "true" : "false")) {
            return false;
        }
    }
    if (!json_append(&cursor, &remaining, "],\"ds18b20\":[")) {
        return false;
    }
    for (size_t i = 0; i < msg->ds18b20_count; ++i) {
        const proto_ds18b20_reading_t *entry = &msg->ds18b20[i];
        char rom[17] = {0};
        for (size_t b = 0; b < sizeof(entry->rom_code); ++b) {
            snprintf(&rom[b * 2], sizeof(rom) - (b * 2), "%02X", entry->rom_code[b]);
        }
        if (!json_append(&cursor, &remaining,
                         "%s{\"rom\":\"%s\",\"t\":%.2f}",
                         i > 0 ? "," : "", rom, entry->temperature_c)) {
            return false;
        }
    }
    if (!json_append(&cursor, &remaining,
                     "],\"gpio\":{\"mcp0\":{\"A\":%u,\"B\":%u},\"mcp1\":{\"A\":%u,\"B\":%u}},\"pwm\":{\"pca9685\":{\"freq\":%u,\"duty\":[",
                     msg->mcp[0].port_a, msg->mcp[0].port_b, msg->mcp[1].port_a, msg->mcp[1].port_b,
                     msg->pwm.frequency_hz)) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        if (!json_append(&cursor, &remaining, "%s%u", i > 0 ? "," : "", msg->pwm.duty_cycle[i])) {
            return false;
        }
    }
    if (!json_append(&cursor, &remaining, "]}}}")) {
        return false;
    }
    size_t used = *buffer_len - remaining;
    if (crc32) {
        *crc32 = proto_crc32(buffer, used);
    }
    *buffer_len = used;
    return true;
}

static bool encode_command_json_into(const proto_command_t *msg, uint8_t *buffer, size_t *buffer_len,
                                     uint32_t *crc32)
{
    if (!msg || !buffer || !buffer_len || *buffer_len == 0) {
        return false;
    }
    char *cursor = (char *)buffer;
    size_t remaining = *buffer_len;
    if (!json_append(&cursor, &remaining,
                     "{\"v\":1,\"type\":\"cmd\",\"ts\":%" PRIu32 ",\"seq\":%" PRIu32,
                     msg->timestamp_ms, msg->sequence_id)) {
        return false;
    }
    if (msg->has_pwm_update) {
        if (!json_append(&cursor, &remaining,
                         ",\"set_pwm\":{\"ch\":%u,\"duty\":%u}",
                         msg->pwm_update.channel, msg->pwm_update.duty_cycle)) {
            return false;
        }
    }
    if (msg->has_pwm_frequency) {
        if (!json_append(&cursor, &remaining,
                         ",\"pwm_freq\":{\"freq\":%u}", msg->pwm_frequency)) {
            return false;
        }
    }
    if (msg->has_gpio_write) {
        const char *dev = msg->gpio_write.device_index == 0 ? "mcp0" : "mcp1";
        char port = msg->gpio_write.port == 0 ? 'A' : 'B';
        if (!json_append(&cursor, &remaining,
                         ",\"write_gpio\":{\"dev\":\"%s\",\"port\":\"%c\",\"mask\":%u,\"value\":%u}",
                         dev, port, msg->gpio_write.mask, msg->gpio_write.value)) {
            return false;
        }
    }
    if (!json_append(&cursor, &remaining, "}")) {
        return false;
    }
    size_t used = *buffer_len - remaining;
    if (crc32) {
        *crc32 = proto_crc32(buffer, used);
    }
    *buffer_len = used;
    return true;
}

#if CONFIG_USE_CBOR
#define CBOR_CHECK(x)                                                                                                   do {                                                                                                                    CborError __err = (x);                                                                                              if (__err != CborNoError) {                                                                                            return false;                                                                                                   }                                                                                                               } while (0)

static bool encode_sensor_update_cbor_into(const proto_sensor_update_t *msg, uint8_t *buffer, size_t *buffer_len,
                                           uint32_t *crc32)
{
    if (!msg || !buffer || !buffer_len || *buffer_len == 0) {
        return false;
    }
    CborEncoder encoder;
    cbor_encoder_init(&encoder, buffer, *buffer_len, 0);
    CborEncoder map;
    CBOR_CHECK(cbor_encoder_create_map(&encoder, &map, CborIndefiniteLength));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "v"));
    CBOR_CHECK(cbor_encode_uint(&map, 1));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "type"));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "sensor_update"));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "ts"));
    CBOR_CHECK(cbor_encode_uint(&map, msg->timestamp_ms));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "seq"));
    CBOR_CHECK(cbor_encode_uint(&map, msg->sequence_id));

    CBOR_CHECK(cbor_encode_text_stringz(&map, "sht20"));
    CborEncoder sht_arr;
    CBOR_CHECK(cbor_encoder_create_array(&map, &sht_arr, msg->sht20_count));
    for (size_t i = 0; i < msg->sht20_count; ++i) {
        CborEncoder item;
        CBOR_CHECK(cbor_encoder_create_map(&sht_arr, &item, 4));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "id"));
        CBOR_CHECK(cbor_encode_text_stringz(&item, msg->sht20[i].id));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "t"));
        CBOR_CHECK(cbor_encode_float(&item, msg->sht20[i].temperature_c));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "rh"));
        CBOR_CHECK(cbor_encode_float(&item, msg->sht20[i].humidity_percent));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "ok"));
        CBOR_CHECK(cbor_encode_boolean(&item, msg->sht20[i].valid));
        CBOR_CHECK(cbor_encoder_close_container(&sht_arr, &item));
    }
    CBOR_CHECK(cbor_encoder_close_container(&map, &sht_arr));

    CBOR_CHECK(cbor_encode_text_stringz(&map, "ds18b20"));
    CborEncoder ds_arr;
    CBOR_CHECK(cbor_encoder_create_array(&map, &ds_arr, msg->ds18b20_count));
    for (size_t i = 0; i < msg->ds18b20_count; ++i) {
        CborEncoder item;
        CBOR_CHECK(cbor_encoder_create_map(&ds_arr, &item, 2));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "rom"));
        char rom_str[17] = {0};
        for (size_t b = 0; b < sizeof(msg->ds18b20[i].rom_code); ++b) {
            snprintf(&rom_str[b * 2], sizeof(rom_str) - (b * 2), "%02X", msg->ds18b20[i].rom_code[b]);
        }
        CBOR_CHECK(cbor_encode_text_stringz(&item, rom_str));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "t"));
        CBOR_CHECK(cbor_encode_float(&item, msg->ds18b20[i].temperature_c));
        CBOR_CHECK(cbor_encoder_close_container(&ds_arr, &item));
    }
    CBOR_CHECK(cbor_encoder_close_container(&map, &ds_arr));

    CBOR_CHECK(cbor_encode_text_stringz(&map, "gpio"));
    CborEncoder gpio_map;
    CBOR_CHECK(cbor_encoder_create_map(&map, &gpio_map, 2));
    for (size_t i = 0; i < 2; ++i) {
        char key[6];
        snprintf(key, sizeof(key), "mcp%zu", i);
        CBOR_CHECK(cbor_encode_text_stringz(&gpio_map, key));
        CborEncoder item;
        CBOR_CHECK(cbor_encoder_create_map(&gpio_map, &item, 2));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "A"));
        CBOR_CHECK(cbor_encode_uint(&item, msg->mcp[i].port_a));
        CBOR_CHECK(cbor_encode_text_stringz(&item, "B"));
        CBOR_CHECK(cbor_encode_uint(&item, msg->mcp[i].port_b));
        CBOR_CHECK(cbor_encoder_close_container(&gpio_map, &item));
    }
    CBOR_CHECK(cbor_encoder_close_container(&map, &gpio_map));

    CBOR_CHECK(cbor_encode_text_stringz(&map, "pwm"));
    CborEncoder pwm_map;
    CBOR_CHECK(cbor_encoder_create_map(&map, &pwm_map, 1));
    CBOR_CHECK(cbor_encode_text_stringz(&pwm_map, "pca9685"));
    CborEncoder pca_map;
    CBOR_CHECK(cbor_encoder_create_map(&pwm_map, &pca_map, 2));
    CBOR_CHECK(cbor_encode_text_stringz(&pca_map, "freq"));
    CBOR_CHECK(cbor_encode_uint(&pca_map, msg->pwm.frequency_hz));
    CBOR_CHECK(cbor_encode_text_stringz(&pca_map, "duty"));
    CborEncoder duty_arr;
    CBOR_CHECK(cbor_encoder_create_array(&pca_map, &duty_arr, 16));
    for (size_t i = 0; i < 16; ++i) {
        CBOR_CHECK(cbor_encode_uint(&duty_arr, msg->pwm.duty_cycle[i]));
    }
    CBOR_CHECK(cbor_encoder_close_container(&pca_map, &duty_arr));
    CBOR_CHECK(cbor_encoder_close_container(&pwm_map, &pca_map));
    CBOR_CHECK(cbor_encoder_close_container(&map, &pwm_map));

    CBOR_CHECK(cbor_encoder_close_container(&encoder, &map));
    if (cbor_encoder_get_extra_bytes_needed(&encoder) > 0) {
        return false;
    }
    size_t used = cbor_encoder_get_buffer_size(&encoder, buffer);
    if (crc32) {
        *crc32 = proto_crc32(buffer, used);
    }
    *buffer_len = used;
    return true;
}

static bool encode_command_cbor_into(const proto_command_t *msg, uint8_t *buffer, size_t *buffer_len,
                                     uint32_t *crc32)
{
    if (!msg || !buffer || !buffer_len || *buffer_len == 0) {
        return false;
    }
    CborEncoder encoder;
    cbor_encoder_init(&encoder, buffer, *buffer_len, 0);
    CborEncoder map;
    CBOR_CHECK(cbor_encoder_create_map(&encoder, &map, CborIndefiniteLength));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "v"));
    CBOR_CHECK(cbor_encode_uint(&map, 1));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "type"));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "cmd"));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "ts"));
    CBOR_CHECK(cbor_encode_uint(&map, msg->timestamp_ms));
    CBOR_CHECK(cbor_encode_text_stringz(&map, "seq"));
    CBOR_CHECK(cbor_encode_uint(&map, msg->sequence_id));

    if (msg->has_pwm_update) {
        CBOR_CHECK(cbor_encode_text_stringz(&map, "set_pwm"));
        CborEncoder pwm;
        CBOR_CHECK(cbor_encoder_create_map(&map, &pwm, 2));
        CBOR_CHECK(cbor_encode_text_stringz(&pwm, "ch"));
        CBOR_CHECK(cbor_encode_uint(&pwm, msg->pwm_update.channel));
        CBOR_CHECK(cbor_encode_text_stringz(&pwm, "duty"));
        CBOR_CHECK(cbor_encode_uint(&pwm, msg->pwm_update.duty_cycle));
        CBOR_CHECK(cbor_encoder_close_container(&map, &pwm));
    }
    if (msg->has_pwm_frequency) {
        CBOR_CHECK(cbor_encode_text_stringz(&map, "pwm_freq"));
        CborEncoder freq;
        CBOR_CHECK(cbor_encoder_create_map(&map, &freq, 1));
        CBOR_CHECK(cbor_encode_text_stringz(&freq, "freq"));
        CBOR_CHECK(cbor_encode_uint(&freq, msg->pwm_frequency));
        CBOR_CHECK(cbor_encoder_close_container(&map, &freq));
    }
    if (msg->has_gpio_write) {
        CBOR_CHECK(cbor_encode_text_stringz(&map, "write_gpio"));
        CborEncoder gpio;
        CBOR_CHECK(cbor_encoder_create_map(&map, &gpio, 4));
        CBOR_CHECK(cbor_encode_text_stringz(&gpio, "dev"));
        CBOR_CHECK(cbor_encode_text_stringz(&gpio, msg->gpio_write.device_index == 0 ? "mcp0" : "mcp1"));
        CBOR_CHECK(cbor_encode_text_stringz(&gpio, "port"));
        CBOR_CHECK(cbor_encode_text_stringz(&gpio, msg->gpio_write.port == 0 ? "A" : "B"));
        CBOR_CHECK(cbor_encode_text_stringz(&gpio, "mask"));
        CBOR_CHECK(cbor_encode_uint(&gpio, msg->gpio_write.mask));
        CBOR_CHECK(cbor_encode_text_stringz(&gpio, "value"));
        CBOR_CHECK(cbor_encode_uint(&gpio, msg->gpio_write.value));
        CBOR_CHECK(cbor_encoder_close_container(&map, &gpio));
    }

    CBOR_CHECK(cbor_encoder_close_container(&encoder, &map));
    if (cbor_encoder_get_extra_bytes_needed(&encoder) > 0) {
        return false;
    }
    size_t used = cbor_encoder_get_buffer_size(&encoder, buffer);
    if (crc32) {
        *crc32 = proto_crc32(buffer, used);
    }
    *buffer_len = used;
    return true;
}
#endif

bool proto_encode_sensor_update_into(const proto_sensor_update_t *msg, bool use_cbor, uint8_t *buffer,
                                     size_t *buffer_len, uint32_t *crc32)
{
#if CONFIG_USE_CBOR
    if (use_cbor) {
        return encode_sensor_update_cbor_into(msg, buffer, buffer_len, crc32);
    }
#else
    (void)use_cbor;
#endif
    return encode_sensor_update_json_into(msg, buffer, buffer_len, crc32);
}

bool proto_encode_command_into(const proto_command_t *msg, bool use_cbor, uint8_t *buffer, size_t *buffer_len,
                               uint32_t *crc32)
{
#if CONFIG_USE_CBOR
    if (use_cbor) {
        return encode_command_cbor_into(msg, buffer, buffer_len, crc32);
    }
#else
    (void)use_cbor;
#endif
    return encode_command_json_into(msg, buffer, buffer_len, crc32);
}

static bool parse_rom(const char *rom_str, uint8_t out[8])
{
    size_t len = strlen(rom_str);
    if (len < 16) {
        return false;
    }
    for (size_t i = 0; i < 8; ++i) {
        unsigned int value;
        if (sscanf(&rom_str[i * 2], "%02X", &value) != 1) {
            return false;
        }
        out[i] = (uint8_t)value;
    }
    return true;
}

bool proto_decode_command(const uint8_t *payload, size_t payload_len, bool is_cbor,
                          proto_command_t *out_msg, uint32_t expected_crc32)
{
    if (!payload || !out_msg) {
        return false;
    }

    if (expected_crc32 != 0) {
        uint32_t crc = proto_crc32(payload, payload_len);
        if (crc != expected_crc32) {
            ESP_LOGW(TAG, "CRC mismatch: expected %" PRIu32 " got %" PRIu32, expected_crc32, crc);
            return false;
        }
    }

    memset(out_msg, 0, sizeof(*out_msg));

#if CONFIG_USE_CBOR
    if (is_cbor) {
        CborParser parser;
        CborValue root;
        if (cbor_parser_init(payload, payload_len, 0, &parser, &root) != CborNoError ||
            !cbor_value_is_map(&root)) {
            return false;
        }
        CborValue map;
        if (cbor_value_enter_container(&root, &map) != CborNoError) {
            return false;
        }
        while (!cbor_value_at_end(&map)) {
            char key[16] = {0};
            size_t key_len = sizeof(key) - 1;
            if (cbor_value_copy_text_string(&map, key, &key_len, &map) != CborNoError) {
                return false;
            }
            if (!strcmp(key, "ts")) {
                uint64_t ts = 0;
                if (cbor_value_get_uint64(&map, &ts) != CborNoError) {
                    return false;
                }
                out_msg->timestamp_ms = (uint32_t)ts;
                cbor_value_advance(&map);
            } else if (!strcmp(key, "seq")) {
                uint64_t seq = 0;
                if (cbor_value_get_uint64(&map, &seq) != CborNoError) {
                    return false;
                }
                out_msg->sequence_id = (uint32_t)seq;
                cbor_value_advance(&map);
            } else if (!strcmp(key, "set_pwm")) {
                CborValue pwm_map;
                if (cbor_value_enter_container(&map, &pwm_map) != CborNoError) {
                    return false;
                }
                while (!cbor_value_at_end(&pwm_map)) {
                    char sub[8] = {0};
                    size_t sub_len = sizeof(sub) - 1;
                    if (cbor_value_copy_text_string(&pwm_map, sub, &sub_len, &pwm_map) != CborNoError) {
                        return false;
                    }
                    if (!strcmp(sub, "ch")) {
                        uint64_t ch = 0;
                        cbor_value_get_uint64(&pwm_map, &ch);
                        out_msg->pwm_update.channel = (uint8_t)ch;
                    } else if (!strcmp(sub, "duty")) {
                        uint64_t duty = 0;
                        cbor_value_get_uint64(&pwm_map, &duty);
                        out_msg->pwm_update.duty_cycle = (uint16_t)duty;
                    }
                    cbor_value_advance(&pwm_map);
                }
                cbor_value_leave_container(&map, &pwm_map);
                out_msg->has_pwm_update = true;
                cbor_value_advance(&map);
            } else if (!strcmp(key, "pwm_freq")) {
                CborValue freq_map;
                if (cbor_value_enter_container(&map, &freq_map) != CborNoError) {
                    return false;
                }
                while (!cbor_value_at_end(&freq_map)) {
                    char sub[8] = {0};
                    size_t sub_len = sizeof(sub) - 1;
                    cbor_value_copy_text_string(&freq_map, sub, &sub_len, &freq_map);
                    if (!strcmp(sub, "freq")) {
                        uint64_t freq = 0;
                        cbor_value_get_uint64(&freq_map, &freq);
                        out_msg->pwm_frequency = (uint16_t)freq;
                        out_msg->has_pwm_frequency = true;
                    }
                    cbor_value_advance(&freq_map);
                }
                cbor_value_leave_container(&map, &freq_map);
                cbor_value_advance(&map);
            } else if (!strcmp(key, "write_gpio")) {
                CborValue gpio_map;
                if (cbor_value_enter_container(&map, &gpio_map) != CborNoError) {
                    return false;
                }
                while (!cbor_value_at_end(&gpio_map)) {
                    char sub[12] = {0};
                    size_t sub_len = sizeof(sub) - 1;
                    cbor_value_copy_text_string(&gpio_map, sub, &sub_len, &gpio_map);
                    if (!strcmp(sub, "dev")) {
                        char dev[8] = {0};
                        size_t dev_len = sizeof(dev) - 1;
                        cbor_value_copy_text_string(&gpio_map, dev, &dev_len, &gpio_map);
                        out_msg->gpio_write.device_index = strcmp(dev, "mcp1") == 0 ? 1 : 0;
                    } else if (!strcmp(sub, "port")) {
                        char port[2] = {0};
                        size_t port_len = sizeof(port) - 1;
                        cbor_value_copy_text_string(&gpio_map, port, &port_len, &gpio_map);
                        out_msg->gpio_write.port = (port[0] == 'B');
                    } else if (!strcmp(sub, "mask")) {
                        uint64_t mask = 0;
                        cbor_value_get_uint64(&gpio_map, &mask);
                        out_msg->gpio_write.mask = (uint16_t)mask;
                    } else if (!strcmp(sub, "value")) {
                        uint64_t val = 0;
                        cbor_value_get_uint64(&gpio_map, &val);
                        out_msg->gpio_write.value = (uint16_t)val;
                    }
                    cbor_value_advance(&gpio_map);
                }
                cbor_value_leave_container(&map, &gpio_map);
                out_msg->has_gpio_write = true;
                cbor_value_advance(&map);
            } else {
                cbor_value_advance(&map);
            }
        }
        return true;
    }
#else
    (void)is_cbor;
#endif

    cJSON *root = cJSON_ParseWithLengthOpts((const char *)payload, payload_len, NULL, false);
    if (!root) {
        return false;
    }

    out_msg->timestamp_ms = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "ts"));
    out_msg->sequence_id = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "seq"));

    cJSON *set_pwm = cJSON_GetObjectItem(root, "set_pwm");
    if (cJSON_IsObject(set_pwm)) {
        out_msg->pwm_update.channel = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(set_pwm, "ch"));
        out_msg->pwm_update.duty_cycle = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(set_pwm, "duty"));
        out_msg->has_pwm_update = true;
    }
    cJSON *pwm_freq = cJSON_GetObjectItem(root, "pwm_freq");
    if (cJSON_IsObject(pwm_freq)) {
        out_msg->pwm_frequency = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(pwm_freq, "freq"));
        out_msg->has_pwm_frequency = true;
    }
    cJSON *write_gpio = cJSON_GetObjectItem(root, "write_gpio");
    if (cJSON_IsObject(write_gpio)) {
        const cJSON *dev = cJSON_GetObjectItem(write_gpio, "dev");
        const cJSON *port = cJSON_GetObjectItem(write_gpio, "port");
        out_msg->gpio_write.device_index = (dev && dev->valuestring && strcmp(dev->valuestring, "mcp1") == 0) ? 1 : 0;
        out_msg->gpio_write.port = (port && port->valuestring && port->valuestring[0] == 'B') ? 1 : 0;
        out_msg->gpio_write.mask = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(write_gpio, "mask"));
        out_msg->gpio_write.value = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(write_gpio, "value"));
        out_msg->has_gpio_write = true;
    }

    cJSON_Delete(root);
    return true;
}

bool proto_decode_sensor_update(const uint8_t *payload, size_t payload_len, bool is_cbor,
                                proto_sensor_update_t *out_msg, uint32_t expected_crc32)
{
    if (!payload || !out_msg) {
        return false;
    }
    if (expected_crc32 != 0) {
        uint32_t crc = proto_crc32(payload, payload_len);
        if (crc != expected_crc32) {
            ESP_LOGW(TAG, "Sensor update CRC mismatch");
            return false;
        }
    }
    memset(out_msg, 0, sizeof(*out_msg));

#if CONFIG_USE_CBOR
    if (is_cbor) {
        CborParser parser;
        CborValue root;
        if (cbor_parser_init(payload, payload_len, 0, &parser, &root) != CborNoError ||
            !cbor_value_is_map(&root)) {
            return false;
        }
        CborValue map;
        if (cbor_value_enter_container(&root, &map) != CborNoError) {
            return false;
        }
        while (!cbor_value_at_end(&map)) {
            char key[16] = {0};
            size_t key_len = sizeof(key) - 1;
            if (cbor_value_copy_text_string(&map, key, &key_len, &map) != CborNoError) {
                return false;
            }
            if (!strcmp(key, "ts")) {
                uint64_t ts = 0;
                if (cbor_value_get_uint64(&map, &ts) != CborNoError) {
                    return false;
                }
                out_msg->timestamp_ms = (uint32_t)ts;
                cbor_value_advance(&map);
            } else if (!strcmp(key, "seq")) {
                uint64_t seq = 0;
                if (cbor_value_get_uint64(&map, &seq) != CborNoError) {
                    return false;
                }
                out_msg->sequence_id = (uint32_t)seq;
                cbor_value_advance(&map);
            } else if (!strcmp(key, "sht20")) {
                CborValue arr;
                if (cbor_value_enter_container(&map, &arr) != CborNoError) {
                    return false;
                }
                size_t idx = 0;
                while (!cbor_value_at_end(&arr) && idx < 2) {
                    out_msg->sht20[idx].valid = false;
                    CborValue item;
                    if (cbor_value_enter_container(&arr, &item) != CborNoError) {
                        return false;
                    }
                    while (!cbor_value_at_end(&item)) {
                        char sub[8] = {0};
                        size_t sl = sizeof(sub) - 1;
                        if (cbor_value_copy_text_string(&item, sub, &sl, &item) != CborNoError) {
                            return false;
                        }
                        if (!strcmp(sub, "id")) {
                            size_t id_len = sizeof(out_msg->sht20[idx].id) - 1;
                            cbor_value_copy_text_string(&item, out_msg->sht20[idx].id, &id_len, &item);
                        } else if (!strcmp(sub, "t")) {
                            double val;
                            cbor_value_get_double(&item, &val);
                            out_msg->sht20[idx].temperature_c = (float)val;
                        } else if (!strcmp(sub, "rh")) {
                            double val;
                            cbor_value_get_double(&item, &val);
                            out_msg->sht20[idx].humidity_percent = (float)val;
                        } else if (!strcmp(sub, "ok")) {
                            bool ok = false;
                            if (cbor_value_get_boolean(&item, &ok) != CborNoError) {
                                return false;
                            }
                            out_msg->sht20[idx].valid = ok;
                        }
                        cbor_value_advance(&item);
                    }
                    cbor_value_leave_container(&arr, &item);
                    idx++;
                }
                out_msg->sht20_count = idx;
                cbor_value_leave_container(&map, &arr);
                cbor_value_advance(&map);
            } else if (!strcmp(key, "ds18b20")) {
                CborValue arr;
                if (cbor_value_enter_container(&map, &arr) != CborNoError) {
                    return false;
                }
                size_t idx = 0;
                while (!cbor_value_at_end(&arr) && idx < 4) {
                    CborValue item;
                    if (cbor_value_enter_container(&arr, &item) != CborNoError) {
                        return false;
                    }
                    while (!cbor_value_at_end(&item)) {
                        char sub[8] = {0};
                        size_t sl = sizeof(sub) - 1;
                        if (cbor_value_copy_text_string(&item, sub, &sl, &item) != CborNoError) {
                            return false;
                        }
                        if (!strcmp(sub, "rom")) {
                            char rom[17] = {0};
                            size_t rlen = sizeof(rom) - 1;
                            cbor_value_copy_text_string(&item, rom, &rlen, &item);
                            parse_rom(rom, out_msg->ds18b20[idx].rom_code);
                        } else if (!strcmp(sub, "t")) {
                            double val;
                            cbor_value_get_double(&item, &val);
                            out_msg->ds18b20[idx].temperature_c = (float)val;
                        }
                        cbor_value_advance(&item);
                    }
                    cbor_value_leave_container(&arr, &item);
                    idx++;
                }
                out_msg->ds18b20_count = idx;
                cbor_value_leave_container(&map, &arr);
                cbor_value_advance(&map);
            } else if (!strcmp(key, "gpio")) {
                CborValue gpio_map;
                if (cbor_value_enter_container(&map, &gpio_map) != CborNoError) {
                    return false;
                }
                while (!cbor_value_at_end(&gpio_map)) {
                    char dev_key[6] = {0};
                    size_t dev_len = sizeof(dev_key) - 1;
                    cbor_value_copy_text_string(&gpio_map, dev_key, &dev_len, &gpio_map);
                    size_t idx = 0;
                    if (sscanf(dev_key, "mcp%zu", &idx) == 1 && idx < 2) {
                        CborValue ports;
                        if (cbor_value_enter_container(&gpio_map, &ports) != CborNoError) {
                            return false;
                        }
                        while (!cbor_value_at_end(&ports)) {
                            char port_key[2] = {0};
                            size_t port_len = sizeof(port_key) - 1;
                            cbor_value_copy_text_string(&ports, port_key, &port_len, &ports);
                            uint64_t val = 0;
                            cbor_value_get_uint64(&ports, &val);
                            if (port_key[0] == 'A') {
                                out_msg->mcp[idx].port_a = (uint16_t)val;
                            } else if (port_key[0] == 'B') {
                                out_msg->mcp[idx].port_b = (uint16_t)val;
                            }
                            cbor_value_advance(&ports);
                        }
                        cbor_value_leave_container(&gpio_map, &ports);
                    } else {
                        cbor_value_advance(&gpio_map);
                    }
                }
                cbor_value_leave_container(&map, &gpio_map);
                cbor_value_advance(&map);
            } else if (!strcmp(key, "pwm")) {
                CborValue pwm_map;
                if (cbor_value_enter_container(&map, &pwm_map) != CborNoError) {
                    return false;
                }
                while (!cbor_value_at_end(&pwm_map)) {
                    char pwm_key[12] = {0};
                    size_t pwm_len = sizeof(pwm_key) - 1;
                    cbor_value_copy_text_string(&pwm_map, pwm_key, &pwm_len, &pwm_map);
                    if (!strcmp(pwm_key, "pca9685")) {
                        CborValue pca_map;
                        if (cbor_value_enter_container(&pwm_map, &pca_map) != CborNoError) {
                            return false;
                        }
                        while (!cbor_value_at_end(&pca_map)) {
                            char sub[8] = {0};
                            size_t sub_len = sizeof(sub) - 1;
                            cbor_value_copy_text_string(&pca_map, sub, &sub_len, &pca_map);
                            if (!strcmp(sub, "freq")) {
                                uint64_t freq = 0;
                                cbor_value_get_uint64(&pca_map, &freq);
                                out_msg->pwm.frequency_hz = (uint16_t)freq;
                                cbor_value_advance(&pca_map);
                            } else if (!strcmp(sub, "duty")) {
                                CborValue duty_arr;
                                if (cbor_value_enter_container(&pca_map, &duty_arr) != CborNoError) {
                                    return false;
                                }
                                size_t i = 0;
                                while (!cbor_value_at_end(&duty_arr) && i < 16) {
                                    uint64_t duty = 0;
                                    cbor_value_get_uint64(&duty_arr, &duty);
                                    out_msg->pwm.duty_cycle[i++] = (uint16_t)duty;
                                    cbor_value_advance(&duty_arr);
                                }
                                cbor_value_leave_container(&pca_map, &duty_arr);
                            } else {
                                cbor_value_advance(&pca_map);
                            }
                        }
                        cbor_value_leave_container(&pwm_map, &pca_map);
                    } else {
                        cbor_value_advance(&pwm_map);
                    }
                }
                cbor_value_leave_container(&map, &pwm_map);
                cbor_value_advance(&map);
            }
            else {
                cbor_value_advance(&map);
            }
        }
        return true;
    }
#else
    (void)is_cbor;
#endif

    cJSON *root = cJSON_ParseWithLengthOpts((const char *)payload, payload_len, NULL, false);
    if (!root) {
        return false;
    }

    out_msg->timestamp_ms = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "ts"));
    out_msg->sequence_id = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(root, "seq"));

    const cJSON *sht = cJSON_GetObjectItem(root, "sht20");
    size_t idx = 0;
    if (cJSON_IsArray(sht)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, sht)
        {
            if (idx >= 2) {
                break;
            }
            const cJSON *id = cJSON_GetObjectItem(item, "id");
            const cJSON *t = cJSON_GetObjectItem(item, "t");
            const cJSON *rh = cJSON_GetObjectItem(item, "rh");
            const cJSON *ok = cJSON_GetObjectItem(item, "ok");
            if (id && id->valuestring) {
                strncpy(out_msg->sht20[idx].id, id->valuestring, sizeof(out_msg->sht20[idx].id) - 1);
            }
            out_msg->sht20[idx].temperature_c = (float)cJSON_GetNumberValue(t);
            out_msg->sht20[idx].humidity_percent = (float)cJSON_GetNumberValue(rh);
            out_msg->sht20[idx].valid = cJSON_IsTrue(ok);
            idx++;
        }
    }
    out_msg->sht20_count = idx;

    const cJSON *ds = cJSON_GetObjectItem(root, "ds18b20");
    idx = 0;
    if (cJSON_IsArray(ds)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, ds)
        {
            if (idx >= 4) {
                break;
            }
            const cJSON *rom = cJSON_GetObjectItem(item, "rom");
            const cJSON *temp = cJSON_GetObjectItem(item, "t");
            if (rom && rom->valuestring) {
                parse_rom(rom->valuestring, out_msg->ds18b20[idx].rom_code);
            }
            out_msg->ds18b20[idx].temperature_c = (float)cJSON_GetNumberValue(temp);
            idx++;
        }
    }
    out_msg->ds18b20_count = idx;

    cJSON *gpio = cJSON_GetObjectItem(root, "gpio");
    if (cJSON_IsObject(gpio)) {
        for (size_t i = 0; i < 2; ++i) {
            char key[6];
            snprintf(key, sizeof(key), "mcp%zu", i);
            cJSON *obj = cJSON_GetObjectItem(gpio, key);
            if (cJSON_IsObject(obj)) {
                out_msg->mcp[i].port_a = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "A"));
                out_msg->mcp[i].port_b = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(obj, "B"));
            }
        }
    }

    cJSON *pwm = cJSON_GetObjectItem(root, "pwm");
    if (cJSON_IsObject(pwm)) {
        cJSON *pca = cJSON_GetObjectItem(pwm, "pca9685");
        if (cJSON_IsObject(pca)) {
            out_msg->pwm.frequency_hz = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(pca, "freq"));
            cJSON *duty = cJSON_GetObjectItem(pca, "duty");
            if (cJSON_IsArray(duty)) {
                size_t i = 0;
                cJSON *val = NULL;
                cJSON_ArrayForEach(val, duty)
                {
                    if (i >= 16) {
                        break;
                    }
                    out_msg->pwm.duty_cycle[i++] = (uint16_t)cJSON_GetNumberValue(val);
                }
            }
        }
    }
    cJSON_Delete(root);
    return true;
}
