#pragma once

#include "sdkconfig.h"
#include "common/proto/messages.h"
#include "data_model.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void (*set_pwm)(uint8_t channel, uint16_t duty, void *ctx);
    void (*set_pwm_frequency)(uint16_t frequency, void *ctx);
    void (*write_gpio)(uint8_t device_index, uint8_t port, uint16_t mask, uint16_t value, void *ctx);
    void (*apply_preferences)(const hmi_user_preferences_t *prefs, void *ctx);
    void (*reset_preferences)(void *ctx);
} ui_callbacks_t;

void ui_init(const ui_callbacks_t *callbacks, void *ctx);
void ui_update_sensor_data(const proto_sensor_update_t *update, bool use_fahrenheit);
void ui_update_connection_status(bool connected);
void ui_update_crc_status(bool crc_ok);
void ui_apply_preferences(const hmi_user_preferences_t *prefs);
void ui_process(void);

#if CONFIG_BUILD_UNIT_TESTS
void ui_set_callbacks_for_test(const ui_callbacks_t *callbacks, void *ctx);
void ui_set_active_prefs_for_test(const hmi_user_preferences_t *prefs);
void ui_test_handle_gpio_switch(uint8_t device_index, uint8_t pin_index, bool on);
void ui_test_handle_pwm_slider(uint8_t channel, uint16_t value);
void ui_test_handle_pwm_frequency(uint16_t freq);
void ui_test_apply_preferences_inputs(const char *ssid, const char *password, const char *mdns, bool dark,
                                      bool fahrenheit);
void ui_test_trigger_reset_preferences(void);
#endif
