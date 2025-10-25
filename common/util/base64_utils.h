#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Decode a base64-encoded string into a binary buffer.
 *
 * @param encoded Null-terminated base64 string.
 * @param out Destination buffer for decoded bytes.
 * @param out_size Size of the destination buffer in bytes.
 * @param decoded_len Optional pointer receiving the number of decoded bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG when inputs are invalid,
 *         ESP_ERR_NO_MEM if the output buffer is too small, or ESP_FAIL on
 *         decoding errors.
 */
esp_err_t base64_utils_decode(const char *encoded, uint8_t *out, size_t out_size, size_t *decoded_len);
