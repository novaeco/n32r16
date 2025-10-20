#include "data_model.h"

#include <string.h>

#include "proto_codec.h"
#include "time_sync.h"

static hmi_data_snapshot_t s_snapshot;
static uint32_t s_cmd_seq;
static uint32_t s_last_command_seq;

void hmi_data_model_init(void) {
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.wifi_rssi = -127;
    s_snapshot.link_up = false;
    s_cmd_seq = 0;
    s_last_command_seq = 0;
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
        cJSON *valid = cJSON_GetObjectItemCaseSensitive(item, "valid");
        if (cJSON_IsString(id)) {
            strlcpy(s_snapshot.sht20[index].id, id->valuestring,
                    sizeof(s_snapshot.sht20[index].id));
        }
        if (cJSON_IsNumber(t)) {
            s_snapshot.sht20[index].temperature_c = t->valuedouble;
        }
        if (cJSON_IsNumber(rh)) {
            s_snapshot.sht20[index].humidity_rh = rh->valuedouble;
        }
        s_snapshot.sht20[index].valid = cJSON_IsBool(valid) ? cJSON_IsTrue(valid) : true;
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
        cJSON *valid = cJSON_GetObjectItemCaseSensitive(item, "valid");
        if (cJSON_IsString(rom)) {
            strlcpy(s_snapshot.ds18b20[index].rom, rom->valuestring,
                    sizeof(s_snapshot.ds18b20[index].rom));
        }
        if (cJSON_IsNumber(t)) {
            s_snapshot.ds18b20[index].temperature_c = t->valuedouble;
        }
        s_snapshot.ds18b20[index].valid = cJSON_IsBool(valid) ? cJSON_IsTrue(valid) : true;
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

static void handle_sensor_update(const proto_envelope_t *env) {
    cJSON *payload = env->payload;
    cJSON *sht20 = cJSON_GetObjectItemCaseSensitive(payload, "sht20");
    cJSON *ds18 = cJSON_GetObjectItemCaseSensitive(payload, "ds18b20");
    cJSON *gpio = cJSON_GetObjectItemCaseSensitive(payload, "gpio");
    cJSON *pwm = cJSON_GetObjectItemCaseSensitive(payload, "pwm");
    parse_sht20(sht20);
    parse_ds18b20(ds18);
    parse_gpio(gpio);
    parse_pwm(pwm);
    s_snapshot.last_crc = env->crc32;
    s_snapshot.crc_ok = env->crc_ok;
    s_snapshot.last_seq = env->seq_id;
    s_snapshot.last_sensor_update_ms = env->timestamp_ms;
    s_snapshot.link_up = true;
}

static void handle_heartbeat(const proto_envelope_t *env) {
    cJSON *payload = env->payload;
    cJSON *cpu = cJSON_GetObjectItemCaseSensitive(payload, "cpu");
    cJSON *rssi = cJSON_GetObjectItemCaseSensitive(payload, "rssi");
    cJSON *uptime = cJSON_GetObjectItemCaseSensitive(payload, "uptime");
    cJSON *i2c_errors = cJSON_GetObjectItemCaseSensitive(payload, "i2c_errors");
    cJSON *ow_errors = cJSON_GetObjectItemCaseSensitive(payload, "onewire_errors");
    if (cJSON_IsNumber(cpu)) {
        s_snapshot.cpu_percent = cpu->valuedouble;
    }
    if (cJSON_IsNumber(rssi)) {
        s_snapshot.wifi_rssi = (int8_t)rssi->valuedouble;
    }
    if (cJSON_IsNumber(uptime)) {
        s_snapshot.uptime_ms = (uint32_t)uptime->valuedouble;
    }
    if (cJSON_IsNumber(i2c_errors)) {
        s_snapshot.i2c_errors = (uint32_t)i2c_errors->valuedouble;
    }
    if (cJSON_IsNumber(ow_errors)) {
        s_snapshot.onewire_errors = (uint32_t)ow_errors->valuedouble;
    }
    s_snapshot.last_heartbeat_ms = env->timestamp_ms;
    s_snapshot.link_up = true;
}

static void handle_command_ack(const proto_envelope_t *env) {
    cJSON *payload = env->payload;
    if (!cJSON_IsObject(payload)) {
        return;
    }
    cJSON *ref_seq = cJSON_GetObjectItemCaseSensitive(payload, "ref_seq");
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(payload, "ok");
    if (!cJSON_IsNumber(ref_seq) || !cJSON_IsBool(ok)) {
        return;
    }
    uint32_t seq = (uint32_t)ref_seq->valuedouble;
    s_snapshot.last_ack_seq = seq;
    s_snapshot.last_ack_ok = cJSON_IsTrue(ok);
    if (seq == s_last_command_seq && !cJSON_IsTrue(ok)) {
        s_snapshot.link_up = false;
    }
}

bool hmi_data_model_ingest(const uint8_t *payload, size_t len) {
    if (payload == NULL || len == 0) {
        return false;
    }
    proto_envelope_t env;
    if (!proto_decode(payload, len, &env)) {
        return false;
    }
    bool updated = false;
    switch (env.type) {
        case PROTO_MSG_SENSOR_UPDATE:
            handle_sensor_update(&env);
            updated = true;
            break;
        case PROTO_MSG_HEARTBEAT:
            handle_heartbeat(&env);
            updated = true;
            break;
        case PROTO_MSG_COMMAND_ACK:
            handle_command_ack(&env);
            updated = true;
            break;
        default:
            break;
    }
    proto_envelope_free(&env);
    return updated;
}

const hmi_data_snapshot_t *hmi_data_model_peek(void) {
    return &s_snapshot;
}

uint32_t hmi_data_model_next_command_seq(void) {
    ++s_cmd_seq;
    if (s_cmd_seq == 0) {
        s_cmd_seq = 1;
    }
    return s_cmd_seq;
}

void hmi_data_model_register_command(uint32_t seq_id) {
    s_last_command_seq = seq_id;
}

