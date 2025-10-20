#include "mdns_support.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "mdns";

esp_err_t mdns_start_service(const char *hostname, const char *instance_name, uint16_t port,
                             const char *service_type) {
    ESP_RETURN_ON_FALSE(hostname != NULL, ESP_ERR_INVALID_ARG, TAG, "hostname");
    ESP_RETURN_ON_FALSE(instance_name != NULL, ESP_ERR_INVALID_ARG, TAG, "instance");
    ESP_RETURN_ON_FALSE(service_type != NULL, ESP_ERR_INVALID_ARG, TAG, "service");

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set(instance_name));

    return mdns_service_add(instance_name, service_type, "tcp", port, NULL, 0);
}

esp_err_t mdns_find_service(const char *service_type, char *host_buf, size_t host_len,
                            uint16_t *port_out) {
    ESP_RETURN_ON_FALSE(service_type != NULL, ESP_ERR_INVALID_ARG, TAG, "service");
    ESP_RETURN_ON_FALSE(host_buf != NULL, ESP_ERR_INVALID_ARG, TAG, "host buf");
    ESP_RETURN_ON_FALSE(port_out != NULL, ESP_ERR_INVALID_ARG, TAG, "port out");

    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(service_type, "tcp", 3000, 5, &results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS query failed: %s", esp_err_to_name(err));
        return err;
    }

    if (results == NULL) {
        ESP_LOGW(TAG, "No mDNS results for %s", service_type);
        return ESP_ERR_NOT_FOUND;
    }

    mdns_ip_addr_t *addr = results->addr;
    while (addr != NULL) {
        if (addr->addr.type == MDNS_IP_PROTOCOL_V4) {
            esp_ip4_addr_t ip4 = addr->addr.u.addr4;
            snprintf(host_buf, host_len, IPSTR, IP2STR(&ip4));
            *port_out = results->port;
            mdns_query_results_free(results);
            return ESP_OK;
        }
        addr = addr->next;
    }

    mdns_query_results_free(results);
    return ESP_ERR_NOT_FOUND;
}

