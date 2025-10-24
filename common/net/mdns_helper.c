#include "mdns_helper.h"

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "mdns";

esp_err_t mdns_helper_start(const char *hostname, const char *instance, uint16_t port)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (hostname) {
        mdns_hostname_set(hostname);
    }
    if (instance) {
        mdns_instance_name_set(instance);
    }
    mdns_service_add(NULL, "_hmi-sensor", "_tcp", port, NULL, 0);
    mdns_service_txt_item_set("_hmi-sensor", "_tcp", "proto", "wss");
    mdns_service_txt_item_set("_hmi-sensor", "_tcp", "auth", "bearer");
    return ESP_OK;
}

void mdns_helper_stop(void)
{
    mdns_free();
}
