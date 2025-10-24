#include "data_model.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "proto_codec.h"

#include "proto_crc32.h"
#include "time_sync.h"
#include "command_auth.h"
#include "esp_log.h"

static const char *TAG = "data_model";

static data_model_snapshot_t s_snapshot;
static SemaphoreHandle_t s_snapshot_mutex;

void data_model_init(void) {
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.pwm.frequency_hz = 500;
    s_snapshot.network.wifi = NETWORK_STATE_DOWN;
    s_snapshot.network.websocket = NETWORK_STATE_DOWN;
    s_snapshot.network.websocket_clients = 0;
    s_snapshot.network.time_synchronized = false;
    s_snapshot_mutex = xSemaphoreCreateMutex();
}

static bool data_model_lock(TickType_t timeout) {
    if (s_snapshot_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_snapshot_mutex, timeout) == pdTRUE;
}

static void data_model_unlock(void) {
    if (s_snapshot_mutex != NULL) {
        xSemaphoreGive(s_snapshot_mutex);
    }
}

void data_model_set_sht20(size_t index, const sht20_sample_t *sample) {
    if (index >= 2 || sample == NULL) {
        return;
    }
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.sht20[index] = *sample;
    data_model_unlock();
}

void data_model_set_ds18b20(const ds18b20_sample_t *samples, size_t count) {
    if (samples == NULL) {
        return;
    }
    if (count > DS18B20_MAX_SENSORS) {
        count = DS18B20_MAX_SENSORS;
    }
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    memcpy(s_snapshot.ds18b20, samples, count * sizeof(ds18b20_sample_t));
    if (count < DS18B20_MAX_SENSORS) {
        memset(&s_snapshot.ds18b20[count], 0,
               (DS18B20_MAX_SENSORS - count) * sizeof(ds18b20_sample_t));
    }
    s_snapshot.ds18b20_count = count;
    data_model_unlock();
}

void data_model_set_gpio(uint8_t device_index, const mcp23017_state_t *state) {
    if (device_index >= 2 || state == NULL) {
        return;
    }
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.gpio[device_index] = *state;
    data_model_unlock();
}

void data_model_set_pwm(const pca9685_state_t *state) {
    if (state == NULL) {
        return;
    }
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.pwm = *state;
    data_model_unlock();
}

void data_model_set_wifi_state(network_state_t state) {
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.network.wifi = state;
    data_model_unlock();
}

void data_model_set_ws_state(network_state_t state, uint8_t client_count) {
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.network.websocket = state;
    s_snapshot.network.websocket_clients = client_count;
    data_model_unlock();
}

void data_model_set_time_synchronized(bool synchronized) {
    if (!data_model_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.network.time_synchronized = synchronized;
    data_model_unlock();
}

bool data_model_snapshot_copy(data_model_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return false;
    }
    if (!data_model_lock(pdMS_TO_TICKS(50))) {
        return false;
    }
    s_snapshot.heartbeat_counter++;
    *out_snapshot = s_snapshot;
    data_model_unlock();
    return true;
}

static bool json_append(char *buffer, size_t capacity, size_t *used, const char *fmt, ...) {
    if (buffer == NULL || capacity == 0 || used == NULL) {
        return false;
    }
    if (*used >= capacity) {
        return false;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *used, capacity - *used, fmt, args);
    va_end(args);
    if (written < 0) {
        return false;
    }
    size_t available = capacity - *used;
    if ((size_t)written >= available) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

bool data_model_format_sensor_update(char *buffer, size_t capacity, size_t *out_len, uint32_t *crc_out) {
    if (buffer == NULL || capacity == 0) {
        return false;
    }

    data_model_snapshot_t snapshot;
    if (!data_model_snapshot_copy(&snapshot)) {
        return false;
    }

    size_t used = 0;
    uint64_t ts_monotonic = time_sync_get_monotonic_ms();
    uint64_t ts_real = ts_monotonic;
    if (snapshot.network.time_synchronized) {
        ts_real = time_sync_get_unix_ms();
    }

    if (!json_append(buffer, capacity, &used,
                     "{\"v\":1,\"type\":\"sensor_update\",\"ts\":%llu,\"payload\":{",
                     (unsigned long long)ts_real)) {
        return false;
    }

    if (!json_append(buffer, capacity, &used, "\"heartbeat\":%u,", snapshot.heartbeat_counter)) {
        return false;
    }

    if (!json_append(buffer, capacity, &used, "\"sht20\":[")) {
        return false;
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!json_append(buffer, capacity, &used,
                         "{\"id\":\"SHT20_%zu\",\"t\":%.3f,\"rh\":%.3f,\"valid\":%s,\"ts\":%u}%s",
                         i + 1, snapshot.sht20[i].temperature_c, snapshot.sht20[i].humidity_rh,
                         snapshot.sht20[i].valid ? "true" : "false", snapshot.sht20[i].timestamp_ms,
                         (i + 1 < 2) ? "," : "")) {
            return false;
        }
    }
    if (!json_append(buffer, capacity, &used, "],")) {
        return false;
    }

    if (!json_append(buffer, capacity, &used, "\"ds18b20\":[")) {
        return false;
    }
    for (size_t i = 0; i < snapshot.ds18b20_count; ++i) {
        if (!json_append(buffer, capacity, &used,
                         "{\"rom\":\"%016llX\",\"t\":%.3f,\"valid\":%s}%s",
                         (unsigned long long)snapshot.ds18b20[i].rom_code,
                         snapshot.ds18b20[i].temperature_c,
                         snapshot.ds18b20[i].valid ? "true" : "false",
                         (i + 1 < snapshot.ds18b20_count) ? "," : "")) {
            return false;
        }
    }
    if (!json_append(buffer, capacity, &used, "],")) {
        return false;
    }

    if (!json_append(buffer, capacity, &used,
                     "\"gpio\":{\"mcp0\":{\"A\":%u,\"B\":%u},\"mcp1\":{\"A\":%u,\"B\":%u}},",
                     snapshot.gpio[0].port_a, snapshot.gpio[0].port_b, snapshot.gpio[1].port_a,
                     snapshot.gpio[1].port_b)) {
        return false;
    }

    if (!json_append(buffer, capacity, &used, "\"pwm\":{\"pca9685\":{\"freq\":%u,\"duty\":[",
                     snapshot.pwm.frequency_hz)) {
        return false;
    }
    for (size_t i = 0; i < 16; ++i) {
        if (!json_append(buffer, capacity, &used, "%u%s", snapshot.pwm.duty_cycle[i],
                         (i + 1 < 16) ? "," : "")) {
            return false;
        }
    }
    if (!json_append(buffer, capacity, &used, "]}}")) {
        return false;
    }

    if (!json_append(buffer, capacity, &used,
                     ",\"network\":{\"wifi\":%d,\"ws\":%d,\"clients\":%u,\"time_sync\":%s}}",
                     (int)snapshot.network.wifi, (int)snapshot.network.websocket,
                     snapshot.network.websocket_clients,
                     snapshot.network.time_synchronized ? "true" : "false")) {
        return false;
    }

    if (!json_append(buffer, capacity, &used, "}")) {
        return false;
    }

    if (used >= capacity) {
        return false;
    }

    if (out_len != NULL) {
        *out_len = used;
    }
    if (crc_out != NULL) {
        *crc_out = proto_crc32_compute((const uint8_t *)buffer, used);
    }
    buffer[used] = '\0';
    return true;
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
    if (!command_auth_validate(&env)) {
        ESP_LOGW(TAG, "Command authentication failed");
    } else if (env.type == PROTO_MSG_COMMAND && env.payload != NULL) {
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

