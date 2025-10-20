#include "data_model.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "cjson_helper.h"
#include "proto_codec.h"
#include "proto_crc32.h"
#include "time_sync.h"

static data_model_snapshot_t s_snapshot;

void data_model_init(void) {
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.pwm.frequency_hz = 500;
}

void data_model_set_sht20(size_t index, const sht20_sample_t *sample) {
    if (index >= 2 || sample == NULL) {
        return;
    }
    s_snapshot.sht20[index] = *sample;
}

void data_model_set_ds18b20(const ds18b20_sample_t *samples, size_t count) {
    if (samples == NULL) {
        return;
    }
    if (count > DS18B20_MAX_SENSORS) {
        count = DS18B20_MAX_SENSORS;
    }
    memcpy(s_snapshot.ds18b20, samples, count * sizeof(ds18b20_sample_t));
    s_snapshot.ds18b20_count = count;
}

void data_model_set_gpio(uint8_t device_index, const mcp23017_state_t *state) {
    if (device_index >= 2 || state == NULL) {
        return;
    }
    s_snapshot.gpio[device_index] = *state;
}

void data_model_set_pwm(const pca9685_state_t *state) {
    if (state == NULL) {
        return;
    }
    s_snapshot.pwm = *state;
}

static void append_sht20(cJSON *root) {
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < 2; ++i) {
        cJSON *obj = cJSON_CreateObject();
        char id[16];
        snprintf(id, sizeof(id), "SHT20_%zu", i + 1);
        cJSON_AddStringToObject(obj, "id", id);
        cJSON_AddNumberToObject(obj, "t", s_snapshot.sht20[i].temperature_c);
        cJSON_AddNumberToObject(obj, "rh", s_snapshot.sht20[i].humidity_rh);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "sht20", arr);
}

static void append_ds18b20(cJSON *root) {
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < s_snapshot.ds18b20_count; ++i) {
        cJSON *obj = cJSON_CreateObject();
        char rom_str[17];
        snprintf(rom_str, sizeof(rom_str), "%016llX", (unsigned long long)s_snapshot.ds18b20[i].rom_code);
        cJSON_AddStringToObject(obj, "rom", rom_str);
        cJSON_AddNumberToObject(obj, "t", s_snapshot.ds18b20[i].temperature_c);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "ds18b20", arr);
}

static void append_gpio(cJSON *root) {
    cJSON *gpio = cJSON_CreateObject();
    cJSON *mcp0 = cJSON_CreateObject();
    cJSON_AddNumberToObject(mcp0, "A", s_snapshot.gpio[0].port_a);
    cJSON_AddNumberToObject(mcp0, "B", s_snapshot.gpio[0].port_b);
    cJSON_AddItemToObject(gpio, "mcp0", mcp0);

    cJSON *mcp1 = cJSON_CreateObject();
    cJSON_AddNumberToObject(mcp1, "A", s_snapshot.gpio[1].port_a);
    cJSON_AddNumberToObject(mcp1, "B", s_snapshot.gpio[1].port_b);
    cJSON_AddItemToObject(gpio, "mcp1", mcp1);

    cJSON_AddItemToObject(root, "gpio", gpio);
}

static void append_pwm(cJSON *root) {
    cJSON *pwm = cJSON_CreateObject();
    cJSON *pca = cJSON_CreateObject();
    cJSON_AddNumberToObject(pca, "freq", s_snapshot.pwm.frequency_hz);
    cJSON *duties = cJSON_CreateArray();
    for (size_t i = 0; i < 16; ++i) {
        cJSON_AddItemToArray(duties, cJSON_CreateNumber(s_snapshot.pwm.duty_cycle[i]));
    }
    cJSON_AddItemToObject(pca, "duty", duties);
    cJSON_AddItemToObject(pwm, "pca9685", pca);
    cJSON_AddItemToObject(root, "pwm", pwm);
}

char *data_model_create_sensor_update(uint32_t *crc_out) {
    cJSON *payload = cJSON_CreateObject();
    append_sht20(payload);
    append_ds18b20(payload);
    append_gpio(payload);
    append_pwm(payload);

    uint64_t ts = time_sync_get_monotonic_ms();
    char *json = NULL;
    uint32_t crc = 0;
    if (!proto_encode_sensor_update(payload, ts, &json, &crc)) {
        cJSON_Delete(payload);
        return NULL;
    }
    cJSON_Delete(payload);
    if (crc_out != NULL) {
        *crc_out = crc;
    }
    return json;
}

bool data_model_parse_command(const uint8_t *buffer, size_t len, io_command_t *cmd) {
    if (buffer == NULL || cmd == NULL) {
        return false;
    }
    proto_envelope_t env;
    if (!proto_decode(buffer, len, &env)) {
        return false;
    }
    bool handled = false;
    if (env.type == PROTO_MSG_COMMAND && env.payload != NULL) {
        cJSON *set_pwm = cJSON_GetObjectItemCaseSensitive(env.payload, "set_pwm");
        cJSON *write_gpio = cJSON_GetObjectItemCaseSensitive(env.payload, "write_gpio");
        if (cJSON_IsObject(set_pwm)) {
            cJSON *ch = cJSON_GetObjectItemCaseSensitive(set_pwm, "ch");
            cJSON *duty = cJSON_GetObjectItemCaseSensitive(set_pwm, "duty");
            if (cJSON_IsNumber(ch) && cJSON_IsNumber(duty)) {
                cmd->type = IO_CMD_SET_PWM;
                cmd->data.pwm.channel = (uint8_t)ch->valuedouble;
                cmd->data.pwm.duty = (uint16_t)duty->valuedouble;
                handled = true;
            }
        } else if (cJSON_IsObject(write_gpio)) {
            cJSON *dev = cJSON_GetObjectItemCaseSensitive(write_gpio, "dev");
            cJSON *port = cJSON_GetObjectItemCaseSensitive(write_gpio, "port");
            cJSON *mask = cJSON_GetObjectItemCaseSensitive(write_gpio, "mask");
            cJSON *value = cJSON_GetObjectItemCaseSensitive(write_gpio, "value");
            if (cJSON_IsString(dev) && cJSON_IsString(port) && cJSON_IsNumber(mask) &&
                cJSON_IsNumber(value)) {
                cmd->type = IO_CMD_WRITE_GPIO;
                cmd->data.gpio.device = (strcmp(dev->valuestring, "mcp1") == 0) ? 1 : 0;
                cmd->data.gpio.port = (port->valuestring[0] == 'B') ? 1 : 0;
                cmd->data.gpio.mask = (uint8_t)mask->valuedouble;
                cmd->data.gpio.value = (uint8_t)value->valuedouble;
                handled = true;
            }
        }
    }
    proto_envelope_free(&env);
    return handled;
}

const data_model_snapshot_t *data_model_peek(void) {
    return &s_snapshot;
}

