#pragma once

#include "esp_err.h"

esp_err_t mdns_helper_start(const char *hostname, const char *instance, uint16_t port);
void mdns_helper_stop(void);
