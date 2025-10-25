#include "tasks/t_ui.h"

#include "display/lvgl_port.h"
#include "display/ui_screens.h"
#include "common/proto/messages.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/ws_client.h"
#include "prefs_store.h"

static hmi_data_model_t *s_model;
static hmi_user_preferences_t s_prefs;
static const char *TAG = "ui_task";

static void ui_cb_set_pwm(uint8_t channel, uint16_t duty, void *ctx)
{
    (void)ctx;
    proto_command_t cmd = {
        .has_pwm_update = true,
        .pwm_update = {
            .channel = channel,
            .duty_cycle = duty,
        },
    };
    hmi_ws_client_send_command(&cmd);
}

static void ui_cb_set_pwm_freq(uint16_t freq, void *ctx)
{
    (void)ctx;
    proto_command_t cmd = {
        .has_pwm_frequency = true,
        .pwm_frequency = freq,
    };
    hmi_ws_client_send_command(&cmd);
}

static void ui_cb_write_gpio(uint8_t device_index, uint8_t port, uint16_t mask, uint16_t value, void *ctx)
{
    (void)ctx;
    proto_command_t cmd = {
        .has_gpio_write = true,
        .gpio_write = {
            .device_index = device_index,
            .port = port,
            .mask = mask,
            .value = value,
        },
    };
    hmi_ws_client_send_command(&cmd);
}

static void ui_cb_apply_prefs(const hmi_user_preferences_t *prefs, void *ctx)
{
    (void)ctx;
    if (!prefs) {
        return;
    }
    s_prefs = *prefs;
    hmi_data_model_set_preferences(s_model, &s_prefs);
    esp_err_t err = hmi_prefs_store_save(&s_prefs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save preferences: %s", esp_err_to_name(err));
    }
    ui_apply_preferences(&s_prefs);
}

static void ui_cb_reset_prefs(void *ctx)
{
    (void)ctx;
    hmi_data_model_reset_preferences(s_model);
    hmi_data_model_get_preferences(s_model, &s_prefs);
    ui_apply_preferences(&s_prefs);
    esp_err_t err = hmi_prefs_store_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase preferences: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Preferences cleared");
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    ui_callbacks_t callbacks = {
        .set_pwm = ui_cb_set_pwm,
        .set_pwm_frequency = ui_cb_set_pwm_freq,
        .write_gpio = ui_cb_write_gpio,
        .apply_preferences = ui_cb_apply_prefs,
        .reset_preferences = ui_cb_reset_prefs,
    };

    lvgl_port_init();
    ui_init(&callbacks, NULL);
    hmi_data_model_get_preferences(s_model, &s_prefs);
    ui_apply_preferences(&s_prefs);

    proto_sensor_update_t update = {0};
    if (hmi_data_model_peek_update(s_model, &update)) {
        ui_update_sensor_data(&update, s_prefs.use_fahrenheit);
    }

    while (true) {
        if (hmi_data_model_get_update(s_model, &update)) {
            ui_update_sensor_data(&update, s_prefs.use_fahrenheit);
        }
        ui_update_connection_status(hmi_data_model_is_connected(s_model));
        ui_update_crc_status(hmi_data_model_get_crc_status(s_model));
        ui_process();
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void ui_task_start(hmi_data_model_t *model)
{
    s_model = model;
    xTaskCreatePinnedToCore(ui_task, "t_ui", 8192, NULL, 4, NULL, 1);
}
