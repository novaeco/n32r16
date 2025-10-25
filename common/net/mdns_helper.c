#include "mdns_helper.h"

#include "esp_log.h"

static esp_err_t mdns_init_default(void)
{
    return mdns_init();
}

static esp_err_t mdns_hostname_set_default(const char *hostname)
{
    return mdns_hostname_set(hostname);
}

static esp_err_t mdns_instance_name_set_default(const char *instance)
{
    return mdns_instance_name_set(instance);
}

static esp_err_t mdns_service_add_default(const char *instance, const char *service_type, const char *proto,
                                          uint16_t port, const mdns_txt_item_t *txt, size_t num_items)
{
    return mdns_service_add(instance, service_type, proto, port, txt, num_items);
}

static esp_err_t mdns_service_txt_item_set_default(const char *service_type, const char *proto, const char *key,
                                                   const char *value)
{
    return mdns_service_txt_item_set(service_type, proto, key, value);
}

static void mdns_free_default(void)
{
    mdns_free();
}

static const mdns_helper_platform_t s_default_platform = {
    .mdns_init = mdns_init_default,
    .mdns_hostname_set = mdns_hostname_set_default,
    .mdns_instance_name_set = mdns_instance_name_set_default,
    .mdns_service_add = mdns_service_add_default,
    .mdns_service_txt_item_set = mdns_service_txt_item_set_default,
    .mdns_free = mdns_free_default,
};

static const mdns_helper_platform_t *s_platform = &s_default_platform;

static const char *TAG = "mdns";

esp_err_t mdns_helper_start(const char *hostname, const char *instance, uint16_t port)
{
    esp_err_t err = s_platform->mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (hostname) {
        s_platform->mdns_hostname_set(hostname);
    }
    if (instance) {
        s_platform->mdns_instance_name_set(instance);
    }
    s_platform->mdns_service_add(NULL, "_hmi-sensor", "_tcp", port, NULL, 0);
    s_platform->mdns_service_txt_item_set("_hmi-sensor", "_tcp", "proto", "wss");
    s_platform->mdns_service_txt_item_set("_hmi-sensor", "_tcp", "auth", "bearer");
    return ESP_OK;
}

void mdns_helper_stop(void)
{
    s_platform->mdns_free();
}

void mdns_helper_set_platform(const mdns_helper_platform_t *platform)
{
    s_platform = platform ? platform : &s_default_platform;
}
