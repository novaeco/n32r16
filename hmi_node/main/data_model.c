#include "data_model.h"

#include <string.h>

#include "proto_codec.h"
#include "proto_crc32.h"

static hmi_data_snapshot_t s_snapshot;

void hmi_data_model_init(void) {
    memset(&s_snapshot, 0, sizeof(s_snapshot));
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
        cJSON *sht20 = cJSON_GetObjectItemCaseSensitive(env.payload, "sht20");
        cJSON *ds18 = cJSON_GetObjectItemCaseSensitive(env.payload, "ds18b20");
        cJSON *gpio = cJSON_GetObjectItemCaseSensitive(env.payload, "gpio");
        cJSON *pwm = cJSON_GetObjectItemCaseSensitive(env.payload, "pwm");
        parse_sht20(sht20);
        parse_ds18b20(ds18);
        parse_gpio(gpio);
        parse_pwm(pwm);
        s_snapshot.last_ts_ms = env.timestamp_ms;
        s_snapshot.last_crc = env.crc32;
        updated = true;
    }
    proto_envelope_free(&env);
    return updated;
}

const hmi_data_snapshot_t *hmi_data_model_peek(void) {
    return &s_snapshot;
}

