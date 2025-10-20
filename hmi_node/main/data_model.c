#include "data_model.h"

#include <string.h>

#include "proto_codec.h"
#include "proto_crc32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static hmi_data_snapshot_t s_snapshot;
static SemaphoreHandle_t s_mutex;

void hmi_data_model_init(void) {
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_mutex = xSemaphoreCreateMutex();
}

static bool lock_snapshot(TickType_t timeout) {
    if (s_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_mutex, timeout) == pdTRUE;
}

static void unlock_snapshot(void) {
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void parse_sht20(cJSON *array) {
    if (!cJSON_IsArray(array)) {
        return;
    }
    size_t index = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (index >= HMI_MAX_SHT20) {
            break;
        }
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON *t = cJSON_GetObjectItemCaseSensitive(item, "t");
        cJSON *rh = cJSON_GetObjectItemCaseSensitive(item, "rh");
        if (cJSON_IsString(id) && cJSON_IsNumber(t) && cJSON_IsNumber(rh)) {
            strlcpy(s_snapshot.sht20[index].id, id->valuestring,
                    sizeof(s_snapshot.sht20[index].id));
            s_snapshot.sht20[index].temperature_c = t->valuedouble;
            s_snapshot.sht20[index].humidity_rh = rh->valuedouble;
            s_snapshot.sht20[index].valid = true;
        }
        index++;
    }
}

static void parse_ds18b20(cJSON *array) {
    if (!cJSON_IsArray(array)) {
        return;
    }
    size_t index = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (index >= HMI_MAX_DS18B20) {
            break;
        }
        cJSON *rom = cJSON_GetObjectItemCaseSensitive(item, "rom");
        cJSON *t = cJSON_GetObjectItemCaseSensitive(item, "t");
        if (cJSON_IsString(rom) && cJSON_IsNumber(t)) {
            strlcpy(s_snapshot.ds18b20[index].rom, rom->valuestring,
                    sizeof(s_snapshot.ds18b20[index].rom));
            s_snapshot.ds18b20[index].temperature_c = t->valuedouble;
            s_snapshot.ds18b20[index].valid = true;
        }
        index++;
    }
}

static void parse_gpio(cJSON *gpio_obj) {
    if (!cJSON_IsObject(gpio_obj)) {
        return;
    }
    cJSON *mcp0 = cJSON_GetObjectItemCaseSensitive(gpio_obj, "mcp0");
    cJSON *mcp1 = cJSON_GetObjectItemCaseSensitive(gpio_obj, "mcp1");
    if (cJSON_IsObject(mcp0)) {
        cJSON *a = cJSON_GetObjectItemCaseSensitive(mcp0, "A");
        cJSON *b = cJSON_GetObjectItemCaseSensitive(mcp0, "B");
        if (cJSON_IsNumber(a) && cJSON_IsNumber(b)) {
            s_snapshot.gpio_state[0][0] = (uint8_t)a->valuedouble;
            s_snapshot.gpio_state[0][1] = (uint8_t)b->valuedouble;
        }
    }
    if (cJSON_IsObject(mcp1)) {
        cJSON *a = cJSON_GetObjectItemCaseSensitive(mcp1, "A");
        cJSON *b = cJSON_GetObjectItemCaseSensitive(mcp1, "B");
        if (cJSON_IsNumber(a) && cJSON_IsNumber(b)) {
            s_snapshot.gpio_state[1][0] = (uint8_t)a->valuedouble;
            s_snapshot.gpio_state[1][1] = (uint8_t)b->valuedouble;
        }
    }
}

static void parse_pwm(cJSON *pwm_obj) {
    if (!cJSON_IsObject(pwm_obj)) {
        return;
    }
    cJSON *pca = cJSON_GetObjectItemCaseSensitive(pwm_obj, "pca9685");
    if (!cJSON_IsObject(pca)) {
        return;
    }
    cJSON *freq = cJSON_GetObjectItemCaseSensitive(pca, "freq");
    cJSON *duty = cJSON_GetObjectItemCaseSensitive(pca, "duty");
    if (cJSON_IsNumber(freq)) {
        s_snapshot.pwm_freq = (uint16_t)freq->valuedouble;
    }
    if (cJSON_IsArray(duty)) {
        size_t index = 0;
        cJSON *entry;
        cJSON_ArrayForEach(entry, duty) {
            if (index >= 16) {
                break;
            }
            if (cJSON_IsNumber(entry)) {
                s_snapshot.pwm_duty[index] = (uint16_t)entry->valuedouble;
            }
            index++;
        }
    }
}

static void parse_network(cJSON *network_obj) {
    if (!cJSON_IsObject(network_obj)) {
        return;
    }
    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(network_obj, "wifi");
    cJSON *ws = cJSON_GetObjectItemCaseSensitive(network_obj, "ws");
    cJSON *clients = cJSON_GetObjectItemCaseSensitive(network_obj, "clients");
    if (cJSON_IsNumber(wifi)) {
        s_snapshot.network.wifi_connected = (wifi->valueint >= 2);
    }
    if (cJSON_IsNumber(ws)) {
        s_snapshot.network.ws_connected = (ws->valueint >= 2);
    }
    if (cJSON_IsNumber(clients)) {
        s_snapshot.network.remote_clients = (uint8_t)clients->valueint;
    }
}

bool hmi_data_model_apply_update(const uint8_t *payload, size_t len) {
    if (payload == NULL || len == 0) {
        return false;
    }
    proto_envelope_t env;
    if (!proto_decode(payload, len, &env)) {
        return false;
    }
    bool updated = false;
    if (env.type == PROTO_MSG_SENSOR_UPDATE && env.payload != NULL) {
        if (!lock_snapshot(pdMS_TO_TICKS(50))) {
            proto_envelope_free(&env);
            return false;
        }
        cJSON *sht20 = cJSON_GetObjectItemCaseSensitive(env.payload, "sht20");
        cJSON *ds18 = cJSON_GetObjectItemCaseSensitive(env.payload, "ds18b20");
        cJSON *gpio = cJSON_GetObjectItemCaseSensitive(env.payload, "gpio");
        cJSON *pwm = cJSON_GetObjectItemCaseSensitive(env.payload, "pwm");
        parse_sht20(sht20);
        parse_ds18b20(ds18);
        parse_gpio(gpio);
        parse_pwm(pwm);
        cJSON *network = cJSON_GetObjectItemCaseSensitive(env.payload, "network");
        parse_network(network);
        s_snapshot.last_ts_ms = env.timestamp_ms;
        s_snapshot.last_crc = env.crc32;
        updated = true;
        unlock_snapshot();
    }
    proto_envelope_free(&env);
    return updated;
}

bool hmi_data_model_get_snapshot(hmi_data_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return false;
    }
    if (!lock_snapshot(pdMS_TO_TICKS(20))) {
        return false;
    }
    *out_snapshot = s_snapshot;
    unlock_snapshot();
    return true;
}

void hmi_data_model_set_wifi_connected(bool connected) {
    if (!lock_snapshot(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.network.wifi_connected = connected;
    unlock_snapshot();
}

void hmi_data_model_set_ws_connected(bool connected) {
    if (!lock_snapshot(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.network.ws_connected = connected;
    unlock_snapshot();
}

void hmi_data_model_set_ws_retries(uint32_t attempts) {
    if (!lock_snapshot(pdMS_TO_TICKS(20))) {
        return;
    }
    s_snapshot.network.reconnect_attempts = attempts;
    unlock_snapshot();
}

