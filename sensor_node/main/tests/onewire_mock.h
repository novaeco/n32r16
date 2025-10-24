#pragma once

#include "esp_err.h"
#include "onewire_bus.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void onewire_mock_set_scratch(const uint8_t *data, size_t len);
void onewire_mock_set_ready(bool ready);
