#include "security/command_auth.h"

#include "command_auth.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "sensor_auth";

esp_err_t sensor_command_auth_init(void) {
    esp_err_t err = command_auth_init_from_nvs(CONFIG_SENSOR_NODE_CMD_AUTH_PARTITION,
                                               CONFIG_SENSOR_NODE_CMD_AUTH_NAMESPACE,
                                               CONFIG_SENSOR_NODE_CMD_AUTH_KEY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load HMAC key (%s)", esp_err_to_name(err));
    }
    return err;
}

void sensor_command_auth_shutdown(void) {
    command_auth_forget_key();
}

