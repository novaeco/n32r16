#pragma once

#include "data_model.h"

typedef struct {
    const char *tab_dashboard;
    const char *tab_gpio;
    const char *tab_pwm;
    const char *tab_traces;
    const char *tab_settings;
    const char *tab_accessibility;
    const char *label_wifi_ssid;
    const char *label_wifi_password;
    const char *label_mdns_target;
    const char *label_dark_theme;
    const char *label_use_fahrenheit;
    const char *label_apply;
    const char *label_reset;
    const char *label_wifi_connected;
    const char *label_wifi_disconnected;
    const char *label_crc_ok;
    const char *label_crc_error;
    const char *label_crc_unknown;
    const char *label_temperature_prefix;
    const char *label_humidity_prefix;
    const char *label_sensor_fallback;
    const char *label_sequence_prefix;
    const char *label_language;
    const char *label_high_contrast;
    const char *label_text_scale;
    const char *label_touch_targets;
    const char *dropdown_languages;
    const char *label_gpio_on;
    const char *label_gpio_off;
    const char *label_frequency;
    const char *label_channel_prefix;
} ui_locale_pack_t;

const ui_locale_pack_t *ui_locale_get_pack(hmi_language_t language);
