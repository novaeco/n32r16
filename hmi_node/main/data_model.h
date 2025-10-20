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
    float cpu_percent;
    int8_t wifi_rssi;
    uint32_t uptime_ms;
    uint32_t i2c_errors;
    uint32_t onewire_errors;
    uint32_t last_crc;
    uint32_t last_seq;
    uint32_t last_ack_seq;
    bool last_ack_ok;
    bool crc_ok;
    bool link_up;
    uint64_t last_sensor_update_ms;
    uint64_t last_heartbeat_ms;
} hmi_data_snapshot_t;

void hmi_data_model_init(void);
bool hmi_data_model_ingest(const uint8_t *payload, size_t len);
const hmi_data_snapshot_t *hmi_data_model_peek(void);
uint32_t hmi_data_model_next_command_seq(void);
void hmi_data_model_register_command(uint32_t seq_id);

