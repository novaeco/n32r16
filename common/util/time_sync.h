#pragma once

#include "esp_err.h"

esp_err_t time_sync_start(const char *server);
int64_t time_sync_get_epoch_ms(void);
