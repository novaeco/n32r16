#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Select the active downstream channel on a TCA9548A I2C multiplexer.
 *
 * Writing the same selection multiple times is avoided to reduce bus chatter.
 * Passing ::IO_MUX_CHANNEL_NONE disables all downstream channels.
 *
 * @param addr 7-bit I2C address of the multiplexer (0x70-0x77).
 * @param channel Channel index 0-7 to enable, or ::IO_MUX_CHANNEL_NONE to disable all.
 *
 * @return ESP_OK on success, an ESP-IDF error code otherwise.
 */
esp_err_t tca9548a_select(uint8_t addr, int8_t channel);
