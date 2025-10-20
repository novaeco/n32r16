#include "data_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "proto_codec.h"
#include "proto_crc32.h"
#include "time_sync.h"

#define EMA_ALPHA 0.2f
#define TEMP_DELTA_THRESHOLD 0.05f
#define RH_DELTA_THRESHOLD 0.5f
#define DS_DELTA_THRESHOLD 0.1f
#define FORCE_PUBLISH_INTERVAL_MS 5000U
#define MAX_PWM_DELTA 1U

static data_model_snapshot_t s_snapshot;
static data_model_snapshot_t s_last_sent;
static bool s_has_last_sent;
static bool s_dirty_sensors;
static bool s_dirty_gpio;
static bool s_dirty_pwm;
static uint32_t s_seq_counter;
static uint64_t s_last_publish_ms;
static float s_sht20_temp_ema[2];
static float s_sht20_rh_ema[2];
static bool s_sht20_ema_valid[2];

static uint32_t next_seq(void) {
    ++s_seq_counter;
    if (s_seq_counter == 0) {
        s_seq_counter = 1;
    }
    return s_seq_counter;
}

void data_model_init(void) {
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_last_sent, 0, sizeof(s_last_sent));
    memset(s_sht20_temp_ema, 0, sizeof(s_sht20_temp_ema));
    memset(s_sht20_rh_ema, 0, sizeof(s_sht20_rh_ema));
    memset(s_sht20_ema_valid, 0, sizeof(s_sht20_ema_valid));
    s_has_last_sent = false;
    s_dirty_sensors = true;
    s_dirty_gpio = true;
    s_dirty_pwm = true;
    s_seq_counter = 0;
    s_last_publish_ms = 0;
}

static bool float_diff_exceeds(float a, float b, float threshold) {
    return fabsf(a - b) > threshold;
}

void data_model_set_sht20(size_t index, const sht20_sample_t *sample) {
    if (index >= 2 || sample == NULL) {
        return;
    }
    s_snapshot.sht20[index] = *sample;
    if (sample->valid) {
        if (!s_sht20_ema_valid[index]) {
            s_sht20_temp_ema[index] = sample->temperature_c;
            s_sht20_rh_ema[index] = sample->humidity_rh;
            s_sht20_ema_valid[index] = true;
        } else {
            s_sht20_temp_ema[index] += EMA_ALPHA * (sample->temperature_c - s_sht20_temp_ema[index]);
            s_sht20_rh_ema[index] += EMA_ALPHA * (sample->humidity_rh - s_sht20_rh_ema[index]);
        }
        s_snapshot.sht20[index].temperature_c = s_sht20_temp_ema[index];
        s_snapshot.sht20[index].humidity_rh = s_sht20_rh_ema[index];
    }

    if (!s_has_last_sent) {
        s_dirty_sensors = true;
        return;
    }

    const sht20_sample_t *prev = &s_last_sent.sht20[index];
    if (prev->valid != sample->valid) {
        s_dirty_sensors = true;
        return;
    }
    if (sample->valid &&
        (float_diff_exceeds(prev->temperature_c, s_snapshot.sht20[index].temperature_c, TEMP_DELTA_THRESHOLD) ||
         float_diff_exceeds(prev->humidity_rh, s_snapshot.sht20[index].humidity_rh, RH_DELTA_THRESHOLD))) {
        s_dirty_sensors = true;
    }
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

    if (!s_has_last_sent) {
        s_dirty_sensors = true;
        return;
    }

    if (s_last_sent.ds18b20_count != count) {
        s_dirty_sensors = true;
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const ds18b20_sample_t *prev = &s_last_sent.ds18b20[i];
        const ds18b20_sample_t *curr = &s_snapshot.ds18b20[i];
        if (prev->rom_code != curr->rom_code || prev->valid != curr->valid) {
            s_dirty_sensors = true;
            return;
        }
        if (curr->valid && float_diff_exceeds(prev->temperature_c, curr->temperature_c, DS_DELTA_THRESHOLD)) {
            s_dirty_sensors = true;
            return;
        }
    }
}

void data_model_set_gpio(uint8_t device_index, const mcp23017_state_t *state) {
    if (device_index >= 2 || state == NULL) {
        return;
    }
    if (memcmp(&s_snapshot.gpio[device_index], state, sizeof(*state)) != 0) {
        s_snapshot.gpio[device_index] = *state;
        s_dirty_gpio = true;
    }
}

void data_model_set_pwm(const pca9685_state_t *state) {
    if (state == NULL) {
        return;
    }
    if (s_snapshot.pwm.frequency_hz != state->frequency_hz) {
        s_dirty_pwm = true;
    }
    for (size_t i = 0; i < 16; ++i) {
        if (abs((int)s_snapshot.pwm.duty_cycle[i] - (int)state->duty_cycle[i]) > (int)MAX_PWM_DELTA) {
            s_dirty_pwm = true;
            break;
        }
    }
    s_snapshot.pwm = *state;
}

void data_model_set_metrics(const system_metrics_t *metrics) {
    if (metrics == NULL) {
        return;
    }
    s_snapshot.metrics = *metrics;
}

static void append_sht20(cJSON *root) {
    cJSON *arr = cJSON_AddArrayToObject(root, "sht20");
    for (size_t i = 0; i < 2; ++i) {
        cJSON *obj = cJSON_CreateObject();
        char id[16];
        snprintf(id, sizeof(id), "SHT20_%zu", i + 1);
        cJSON_AddStringToObject(obj, "id", id);
        cJSON_AddBoolToObject(obj, "valid", s_snapshot.sht20[i].valid);
        cJSON_AddNumberToObject(obj, "t", s_snapshot.sht20[i].temperature_c);
        cJSON_AddNumberToObject(obj, "rh", s_snapshot.sht20[i].humidity_rh);
        cJSON_AddItemToArray(arr, obj);
    }
}

static void append_ds18b20(cJSON *root) {
    cJSON *arr = cJSON_AddArrayToObject(root, "ds18b20");
    for (size_t i = 0; i < s_snapshot.ds18b20_count; ++i) {
        cJSON *obj = cJSON_CreateObject();
        char rom_str[17];
        snprintf(rom_str, sizeof(rom_str), "%016llX", (unsigned long long)s_snapshot.ds18b20[i].rom_code);
        cJSON_AddStringToObject(obj, "rom", rom_str);
        cJSON_AddBoolToObject(obj, "valid", s_snapshot.ds18b20[i].valid);
        cJSON_AddNumberToObject(obj, "t", s_snapshot.ds18b20[i].temperature_c);
        cJSON_AddItemToArray(arr, obj);
    }
}

static void append_gpio(cJSON *root) {
    cJSON *gpio = cJSON_AddObjectToObject(root, "gpio");
    cJSON *mcp0 = cJSON_AddObjectToObject(gpio, "mcp0");
    cJSON_AddNumberToObject(mcp0, "A", s_snapshot.gpio[0].port_a);
    cJSON_AddNumberToObject(mcp0, "B", s_snapshot.gpio[0].port_b);

    cJSON *mcp1 = cJSON_AddObjectToObject(gpio, "mcp1");
    cJSON_AddNumberToObject(mcp1, "A", s_snapshot.gpio[1].port_a);
    cJSON_AddNumberToObject(mcp1, "B", s_snapshot.gpio[1].port_b);
}

static void append_pwm(cJSON *root) {
    cJSON *pwm = cJSON_AddObjectToObject(root, "pwm");
    cJSON *pca = cJSON_AddObjectToObject(pwm, "pca9685");
    cJSON_AddNumberToObject(pca, "freq", s_snapshot.pwm.frequency_hz);
    cJSON *duties = cJSON_AddArrayToObject(pca, "duty");
    for (size_t i = 0; i < 16; ++i) {
        cJSON_AddItemToArray(duties, cJSON_CreateNumber(s_snapshot.pwm.duty_cycle[i]));
    }
}

bool data_model_prepare_sensor_update(proto_buffer_t *out_buf) {
    if (out_buf == NULL) {
        return false;
    }
    uint64_t now_ms = time_sync_get_monotonic_ms();
    bool dirty = s_dirty_sensors || s_dirty_gpio || s_dirty_pwm;
    if (!dirty && (now_ms - s_last_publish_ms) < FORCE_PUBLISH_INTERVAL_MS) {
        return false;
    }

    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        return false;
    }

    append_sht20(payload);
    append_ds18b20(payload);
    append_gpio(payload);
    append_pwm(payload);

    uint32_t crc = 0;
    uint32_t seq = next_seq();
    bool ok = proto_encode_sensor_update(payload, now_ms, seq, out_buf, &crc);
    cJSON_Delete(payload);
    if (!ok) {
        return false;
    }

    s_snapshot.last_crc = crc;
    s_snapshot.last_seq_id = seq;
    s_last_publish_ms = now_ms;
    s_last_sent = s_snapshot;
    s_has_last_sent = true;
    s_dirty_sensors = false;
    s_dirty_gpio = false;
    s_dirty_pwm = false;
    return true;
}

bool data_model_build_heartbeat(proto_buffer_t *out_buf) {
    if (out_buf == NULL) {
        return false;
    }
    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        return false;
    }
    cJSON_AddNumberToObject(payload, "cpu", s_snapshot.metrics.cpu_load);
    cJSON_AddNumberToObject(payload, "rssi", s_snapshot.metrics.wifi_rssi);
    cJSON_AddNumberToObject(payload, "uptime", s_snapshot.metrics.uptime_ms);
    cJSON_AddNumberToObject(payload, "i2c_errors", s_snapshot.metrics.i2c_errors);
    cJSON_AddNumberToObject(payload, "onewire_errors", s_snapshot.metrics.onewire_errors);

    uint32_t crc = 0;
    uint32_t seq = next_seq();
    bool ok = proto_encode_heartbeat(payload, time_sync_get_monotonic_ms(), seq, out_buf, &crc);
    cJSON_Delete(payload);
    if (ok) {
        s_snapshot.last_crc = crc;
        s_snapshot.last_seq_id = seq;
    }
    return ok;
}

uint32_t data_model_next_sequence(void) {
    return next_seq();
}

bool data_model_parse_command(const uint8_t *buffer, size_t len, io_command_t *cmd) {
    if (buffer == NULL || len == 0 || cmd == NULL) {
        return false;
    }
    proto_envelope_t env;
    if (!proto_decode(buffer, len, &env)) {
        return false;
    }
    bool handled = false;
    if (env.type == PROTO_MSG_COMMAND && env.payload != NULL && env.crc_ok) {
        cJSON *set_pwm = cJSON_GetObjectItemCaseSensitive(env.payload, "set_pwm");
        cJSON *write_gpio = cJSON_GetObjectItemCaseSensitive(env.payload, "write_gpio");
        cJSON *set_pwm_freq = cJSON_GetObjectItemCaseSensitive(env.payload, "set_pwm_freq");
        if (cJSON_IsObject(set_pwm)) {
            cJSON *ch = cJSON_GetObjectItemCaseSensitive(set_pwm, "ch");
            cJSON *duty = cJSON_GetObjectItemCaseSensitive(set_pwm, "duty");
            if (cJSON_IsNumber(ch) && cJSON_IsNumber(duty)) {
                cmd->type = IO_CMD_SET_PWM;
                cmd->seq_id = env.seq_id;
                cmd->data.pwm.channel = (uint8_t)cJSON_GetNumberValue(ch);
                cmd->data.pwm.duty = (uint16_t)cJSON_GetNumberValue(duty);
                handled = true;
            }
        }
        if (!handled && cJSON_IsObject(write_gpio)) {
            cJSON *dev = cJSON_GetObjectItemCaseSensitive(write_gpio, "dev");
            cJSON *port = cJSON_GetObjectItemCaseSensitive(write_gpio, "port");
            cJSON *mask = cJSON_GetObjectItemCaseSensitive(write_gpio, "mask");
            cJSON *value = cJSON_GetObjectItemCaseSensitive(write_gpio, "value");
            if (cJSON_IsString(dev) && cJSON_IsString(port) && cJSON_IsNumber(mask) && cJSON_IsNumber(value)) {
                cmd->type = IO_CMD_WRITE_GPIO;
                cmd->seq_id = env.seq_id;
                cmd->data.gpio.device = (strcmp(dev->valuestring, "mcp1") == 0) ? 1 : 0;
                cmd->data.gpio.port = (port->valuestring[0] == 'B') ? 1 : 0;
                cmd->data.gpio.mask = (uint8_t)cJSON_GetNumberValue(mask);
                cmd->data.gpio.value = (uint8_t)cJSON_GetNumberValue(value);
                handled = true;
            }
        }
        if (!handled && cJSON_IsObject(set_pwm_freq)) {
            cJSON *freq = cJSON_GetObjectItemCaseSensitive(set_pwm_freq, "freq");
            if (cJSON_IsNumber(freq)) {
                cmd->type = IO_CMD_SET_PWM_FREQ;
                cmd->seq_id = env.seq_id;
                cmd->data.pwm_freq.frequency_hz = (uint16_t)cJSON_GetNumberValue(freq);
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

