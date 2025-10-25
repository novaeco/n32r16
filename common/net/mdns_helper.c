#include "mdns_helper.h"

#include "esp_log.h"

/**
 * @brief Initialize the default mDNS subsystem.
 *
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t mdns_init_default(void)
{
    return mdns_init();
}

/**
 * @brief Assign the default mDNS hostname.
 *
 * @param hostname Hostname string to register.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t mdns_hostname_set_default(const char *hostname)
{
    return mdns_hostname_set(hostname);
}

/**
 * @brief Set the default mDNS instance name.
 *
 * @param instance Instance name string to publish.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t mdns_instance_name_set_default(const char *instance)
{
    return mdns_instance_name_set(instance);
}

/**
 * @brief Register an mDNS service using the default platform implementation.
 *
 * @param instance Instance name string (NULL for default).
 * @param service_type Service type label (e.g., "_http").
 * @param proto Transport protocol label (e.g., "_tcp").
 * @param port TCP/UDP port number.
 * @param txt Pointer to TXT record array.
 * @param num_items Number of TXT items in the array.
 * @return ESP_OK when the service is published successfully.
 */
static esp_err_t mdns_service_add_default(const char *instance, const char *service_type, const char *proto,
                                          uint16_t port, const mdns_txt_item_t *txt, size_t num_items)
{
    return mdns_service_add(instance, service_type, proto, port, txt, num_items);
}

/**
 * @brief Update a TXT record key/value pair for a service.
 *
 * @param service_type Service type label.
 * @param proto Transport protocol label.
 * @param key TXT record key.
 * @param value TXT record value.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t mdns_service_txt_item_set_default(const char *service_type, const char *proto, const char *key,
                                                   const char *value)
{
    return mdns_service_txt_item_set(service_type, proto, key, value);
}

/**
 * @brief Release the default mDNS resources.
 *
 * @return void
 */
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

/**
 * @brief Start the mDNS helper service with the configured identifiers.
 *
 * @param hostname Optional hostname to register.
 * @param instance Optional instance name to advertise.
 * @param port TCP port number to expose in the service record.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
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

/**
 * @brief Stop the mDNS helper service and release resources.
 *
 * @return void
 */
void mdns_helper_stop(void)
{
    s_platform->mdns_free();
}

/**
 * @brief Override the platform callbacks used by the mDNS helper.
 *
 * @param platform Pointer to the platform virtual table (NULL for defaults).
 * @return void
 */
void mdns_helper_set_platform(const mdns_helper_platform_t *platform)
{
    s_platform = platform ? platform : &s_default_platform;
}
