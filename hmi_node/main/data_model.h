#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

#define HMI_MAX_SHT20 2
#define HMI_MAX_DS18B20 4

typedef struct {
    char id[16];
    float temperature_c;
    float humidity_rh;
    bool valid;
} hmi_sht20_entry_t;

typedef struct {
    char rom[17];
    float temperature_c;
    bool valid;
} hmi_ds18b20_entry_t;

typedef struct {
    hmi_sht20_entry_t sht20[HMI_MAX_SHT20];
    hmi_ds18b20_entry_t ds18b20[HMI_MAX_DS18B20];
    uint8_t gpio_state[2][2];
    uint16_t pwm_duty[16];
    uint16_t pwm_freq;
    uint32_t last_crc;
    uint64_t last_ts_ms;
    struct {
        bool wifi_connected;
        bool ws_connected;
        uint8_t remote_clients;
        uint32_t reconnect_attempts;
    } network;
} hmi_data_snapshot_t;

void hmi_data_model_init(void);
bool hmi_data_model_apply_update(const uint8_t *payload, size_t len);
bool hmi_data_model_get_snapshot(hmi_data_snapshot_t *out_snapshot);
void hmi_data_model_set_wifi_connected(bool connected);
void hmi_data_model_set_ws_connected(bool connected);
void hmi_data_model_set_ws_retries(uint32_t attempts);

