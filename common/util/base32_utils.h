#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Decode an RFC 4648 Base32 string into raw bytes.
 *
 * @param input     Null-terminated Base32 string (case-insensitive, padding optional).
 * @param output    Destination buffer for the decoded bytes.
 * @param out_size  Size of the destination buffer in bytes.
 * @param out_len   Optional pointer receiving the number of decoded bytes.
 * @return ESP_OK on success or an ESP-IDF error code on malformed input or insufficient space.
 */
esp_err_t base32_utils_decode(const char *input, uint8_t *output, size_t out_size, size_t *out_len);

