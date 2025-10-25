#include "base64_utils.h"

#include "mbedtls/base64.h"
#include <string.h>

esp_err_t base64_utils_decode(const char *encoded, uint8_t *out, size_t out_size, size_t *decoded_len)
{
    if (!encoded || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t input_len = strlen(encoded);
    if (input_len == 0) {
        if (decoded_len) {
            *decoded_len = 0;
        }
        return ESP_ERR_INVALID_ARG;
    }
    size_t out_len = 0;
    int rc = mbedtls_base64_decode(out, out_size, &out_len, (const unsigned char *)encoded, input_len);
    if (rc == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return ESP_ERR_NO_MEM;
    }
    if (rc != 0) {
        return ESP_FAIL;
    }
    if (decoded_len) {
        *decoded_len = out_len;
    }
    return ESP_OK;
}
