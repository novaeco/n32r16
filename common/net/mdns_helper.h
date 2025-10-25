#pragma once

#include "esp_err.h"
#include "mdns.h"

typedef struct {
    esp_err_t (*mdns_init)(void);
    esp_err_t (*mdns_hostname_set)(const char *hostname);
    esp_err_t (*mdns_instance_name_set)(const char *instance);
    esp_err_t (*mdns_service_add)(const char *instance, const char *service_type, const char *proto,
                                 uint16_t port, const mdns_txt_item_t *txt, size_t num_items);
    esp_err_t (*mdns_service_txt_item_set)(const char *service_type, const char *proto, const char *key,
                                           const char *value);
    void (*mdns_free)(void);
} mdns_helper_platform_t;

esp_err_t mdns_helper_start(const char *hostname, const char *instance, uint16_t port);
void mdns_helper_stop(void);
void mdns_helper_set_platform(const mdns_helper_platform_t *platform);
