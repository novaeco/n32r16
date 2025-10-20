#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t mdns_start_service(const char *hostname, const char *instance_name, uint16_t port,
                             const char *service_type);
esp_err_t mdns_find_service(const char *service_type, char *host_buf, size_t host_len,
                            uint16_t *port_out);

