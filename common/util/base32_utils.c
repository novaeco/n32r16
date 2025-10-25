#include "base32_utils.h"

#include <ctype.h>
#include <string.h>

static int8_t decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (int8_t)(c - 'A');
    }
    if (c >= 'a' && c <= 'z') {
        return (int8_t)(c - 'a');
    }
    if (c >= '2' && c <= '7') {
        return (int8_t)(c - '2' + 26);
    }
    return -1;
}

esp_err_t base32_utils_decode(const char *input, uint8_t *output, size_t out_size, size_t *out_len)
{
    if (!input || !output) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t input_len = strlen(input);
    size_t buffer = 0;
    int bits = 0;
    size_t produced = 0;

    for (size_t i = 0; i < input_len; ++i) {
        char c = input[i];
        if (c == '=') {
            break;
        }
        if (isspace((unsigned char)c)) {
            continue;
        }
        int8_t value = decode_char(c);
        if (value < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        buffer = (buffer << 5U) | (uint8_t)value;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (produced >= out_size) {
                return ESP_ERR_NO_MEM;
            }
            output[produced++] = (uint8_t)((buffer >> bits) & 0xFFU);
        }
    }

    if (bits > 0 && ((buffer & ((1U << bits) - 1U)) != 0U)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (out_len) {
        *out_len = produced;
    }
    return ESP_OK;
}

