#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the BME280 device at the specified I\u00b2C address.
 *
 * Loads factory calibration coefficients and configures baseline oversampling.
 */
esp_err_t bme280_init(uint8_t address);

/**
 * @brief Perform a forced measurement and return compensated readings.
 *
 * @param address I\u00b2C address of the target BME280.
 * @param temperature_c Optional pointer receiving the ambient temperature in \u00b0C.
 * @param humidity_percent Optional pointer receiving relative humidity in %RH.
 * @param pressure_pa Optional pointer receiving barometric pressure in pascals.
 */
esp_err_t bme280_read(uint8_t address, float *temperature_c, float *humidity_percent, float *pressure_pa);

#ifdef __cplusplus
}
#endif
