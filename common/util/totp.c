#include "totp.h"

#include "mbedtls/md.h"
#include <string.h>

static uint32_t pow10_u32(uint8_t digits)
{
    uint32_t value = 1;
    for (uint8_t i = 0; i < digits; ++i) {
        value *= 10U;
    }
    return value;
}

esp_err_t totp_compute(const totp_config_t *cfg, uint64_t unix_time, uint32_t *code)
{
    if (!cfg || !cfg->secret || cfg->secret_len == 0 || cfg->digits < 6 || cfg->digits > 8 || cfg->period_s == 0 || !code) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t counter = unix_time / cfg->period_s;
    unsigned char counter_bytes[8];
    for (int i = 7; i >= 0; --i) {
        counter_bytes[i] = (unsigned char)(counter & 0xFFU);
        counter >>= 8U;
    }

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!info) {
        return ESP_FAIL;
    }

    unsigned char digest[20];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int rc = mbedtls_md_setup(&ctx, info, 1);
    if (rc == 0) {
        rc = mbedtls_md_hmac_starts(&ctx, cfg->secret, cfg->secret_len);
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(&ctx, counter_bytes, sizeof(counter_bytes));
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_finish(&ctx, digest);
    }
    mbedtls_md_free(&ctx);
    if (rc != 0) {
        return ESP_FAIL;
    }

    uint8_t offset = digest[19] & 0x0FU;
    uint32_t binary = ((uint32_t)digest[offset] & 0x7FU) << 24U;
    binary |= ((uint32_t)digest[offset + 1U] & 0xFFU) << 16U;
    binary |= ((uint32_t)digest[offset + 2U] & 0xFFU) << 8U;
    binary |= (uint32_t)digest[offset + 3U] & 0xFFU;

    uint32_t mod = pow10_u32(cfg->digits);
    *code = binary % mod;
    return ESP_OK;
}

esp_err_t totp_verify(const totp_config_t *cfg, uint64_t unix_time, uint32_t window, uint32_t code, bool *match)
{
    if (!match) {
        return ESP_ERR_INVALID_ARG;
    }
    *match = false;
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cfg->secret || cfg->secret_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int32_t offset = -(int32_t)window; offset <= (int32_t)window; ++offset) {
        uint64_t adjusted_time = unix_time;
        int64_t delta = (int64_t)offset * (int64_t)cfg->period_s;
        if (offset < 0) {
            if ((uint64_t)(-delta) > adjusted_time) {
                continue;
            }
            adjusted_time -= (uint64_t)(-delta);
        } else {
            adjusted_time += (uint64_t)delta;
        }
        uint32_t expected = 0;
        esp_err_t err = totp_compute(cfg, adjusted_time, &expected);
        if (err != ESP_OK) {
            return err;
        }
        if (expected == code) {
            *match = true;
            return ESP_OK;
        }
    }
    return ESP_OK;
}

