#include "app_main.h"

#include <assert.h>
#include <string.h>

#include "cert_store.h"
#include "common/net/wifi_manager.h"
#include "common/ota/ota_update.h"
#include "common/util/memory_profile.h"
#include "data_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tasks/t_heartbeat.h"
#include "tasks/t_net_rx.h"
#include "tasks/t_ui.h"

static const char *TAG = "hmi_main";

#if !CONFIG_HMI_ALLOW_PLACEHOLDER_SECRETS
#define HMI_STATIC_ASSERT_NOT_PLACEHOLDER(value, literal, message) _Static_assert(__builtin_strcmp(value, literal) != 0, message)

HMI_STATIC_ASSERT_NOT_PLACEHOLDER(CONFIG_HMI_WS_AUTH_TOKEN, "replace-me",
    "CONFIG_HMI_WS_AUTH_TOKEN must be replaced with a production token");
HMI_STATIC_ASSERT_NOT_PLACEHOLDER(CONFIG_HMI_PROV_POP, "hmi-pop",
    "CONFIG_HMI_PROV_POP must be replaced before building");
HMI_STATIC_ASSERT_NOT_PLACEHOLDER(CONFIG_HMI_OTA_URL, "https://example.com/hmi/ota.json",
    "CONFIG_HMI_OTA_URL must target the production manifest");
#undef HMI_STATIC_ASSERT_NOT_PLACEHOLDER
#endif

static inline void hmi_validate_runtime_secrets(void)
{
#if !CONFIG_HMI_ALLOW_PLACEHOLDER_SECRETS
    assert(strlen(CONFIG_HMI_WS_AUTH_TOKEN) > 16 && "CONFIG_HMI_WS_AUTH_TOKEN is too short");
    assert(strlen(CONFIG_HMI_PROV_POP) > 8 && "CONFIG_HMI_PROV_POP is too short");
    assert(strlen(CONFIG_HMI_OTA_URL) > 12 && "CONFIG_HMI_OTA_URL is invalid");
#endif
}

static bool wait_for_wifi(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t step = 200;
    while (elapsed < timeout_ms) {
        if (wifi_manager_is_connected()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
    return wifi_manager_is_connected();
}

void app_main(void)
{
    hmi_validate_runtime_secrets();

    ESP_LOGI(TAG, "Booting HMI node");
    ESP_ERROR_CHECK(memory_profile_init());
    memory_profile_log();
    hmi_data_model_t model;
    hmi_data_model_init(&model);

    heartbeat_task_start();
    net_task_start(&model);
    ui_task_start(&model);

    size_t ca_len = 0;
    const uint8_t *ca = cert_store_ca_cert(&ca_len);
    ota_update_config_t ota_cfg = {
        .url = CONFIG_HMI_OTA_URL,
        .cert_pem = ca,
        .cert_len = ca_len,
        .auto_reboot = true,
        .check_delay_ms = 5000,
        .wait_for_connectivity = wait_for_wifi,
    };
    ESP_ERROR_CHECK(ota_update_schedule(&ota_cfg));
}
