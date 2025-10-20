#pragma once

#include <stdbool.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

typedef void (*wifi_sta_event_cb_t)(system_event_id_t event_id);

typedef struct {
    const char *ssid;
    const char *password;
    bool auto_reconnect;
} wifi_sta_config_t;

esp_err_t wifi_sta_init(const wifi_sta_config_t *cfg);
void wifi_sta_wait_connected(void);
const esp_netif_ip_info_t *wifi_sta_get_ip_info(void);

