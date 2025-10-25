#include "ws_security.h"

#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include <inttypes.h>
#include <string.h>

#define TAG "ws_security"

static esp_err_t derive_key(const uint8_t *secret, size_t secret_len, const char *label, uint8_t *out, size_t out_len)
{
    if (!secret || secret_len == 0 || !label || !out || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        return ESP_FAIL;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int rc = mbedtls_md_setup(&ctx, info, 1);
    if (rc != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }
    rc = mbedtls_md_hmac_starts(&ctx, secret, secret_len);
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(&ctx, (const unsigned char *)label, strlen(label));
    }
    unsigned char digest[32] = {0};
    if (rc == 0) {
        rc = mbedtls_md_hmac_finish(&ctx, digest);
    }
    mbedtls_md_free(&ctx);
    if (rc != 0) {
        return ESP_FAIL;
    }
    if (out_len > sizeof(digest)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, digest, out_len);
    return ESP_OK;
}

esp_err_t ws_security_context_init(ws_security_context_t *ctx, const ws_security_config_t *config)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (!config) {
        return ESP_OK;
    }
    if ((!config->secret || config->secret_len == 0) && (config->enable_encryption || config->enable_handshake)) {
        ESP_LOGE(TAG, "Security secret missing while features are enabled");
        return ESP_ERR_INVALID_ARG;
    }
    if (config->enable_handshake) {
        esp_err_t err = derive_key(config->secret, config->secret_len, "ws-handshake", ctx->handshake_key,
                                   sizeof(ctx->handshake_key));
        if (err != ESP_OK) {
            return err;
        }
        ctx->handshake_enabled = true;
    }
    if (config->enable_encryption) {
        esp_err_t err = derive_key(config->secret, config->secret_len, "ws-frame", ctx->frame_key,
                                   sizeof(ctx->frame_key));
        if (err != ESP_OK) {
            return err;
        }
        ctx->encryption_enabled = true;
        ctx->tx_counter = 0;
    }
    return ESP_OK;
}

size_t ws_security_encrypted_size(const ws_security_context_t *ctx, size_t plaintext_len)
{
    if (!ctx || !ctx->encryption_enabled) {
        return plaintext_len;
    }
    return WS_SECURITY_HEADER_LEN + plaintext_len + WS_SECURITY_TAG_LEN;
}

static void generate_iv(uint8_t *iv, size_t len)
{
    esp_fill_random(iv, len);
}

esp_err_t ws_security_encrypt(ws_security_context_t *ctx, const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *out, size_t out_size, size_t *out_len)
{
    if (!ctx || !plaintext || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->encryption_enabled) {
        if (out_size < plaintext_len) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(out, plaintext, plaintext_len);
        if (out_len) {
            *out_len = plaintext_len;
        }
        return ESP_OK;
    }
    size_t required = ws_security_encrypted_size(ctx, plaintext_len);
    if (out_size < required) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *cursor = out;
    cursor[0] = WS_SECURITY_VERSION;
    cursor[1] = 0U;
    uint64_t counter = ++ctx->tx_counter;
    memcpy(cursor + 2, &counter, sizeof(counter));
    uint8_t *iv = cursor + WS_SECURITY_FIXED_HEADER_LEN;
    generate_iv(iv, WS_SECURITY_IV_LEN);
    uint8_t *ciphertext = iv + WS_SECURITY_IV_LEN;
    uint8_t *tag = ciphertext + plaintext_len;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, ctx->frame_key, (unsigned int)sizeof(ctx->frame_key) * 8U);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plaintext_len, iv, WS_SECURITY_IV_LEN, cursor,
                                       WS_SECURITY_FIXED_HEADER_LEN, plaintext, ciphertext, WS_SECURITY_TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    if (rc != 0) {
        ESP_LOGE(TAG, "AES-GCM encrypt failed (%d)", rc);
        return ESP_FAIL;
    }
    if (out_len) {
        *out_len = required;
    }
    return ESP_OK;
}

esp_err_t ws_security_decrypt(const ws_security_context_t *ctx, uint8_t *buffer, size_t buffer_len,
                              size_t *plaintext_len, uint64_t *counter_state)
{
    if (!ctx || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->encryption_enabled) {
        if (plaintext_len) {
            *plaintext_len = buffer_len;
        }
        return ESP_OK;
    }
    if (buffer_len < WS_SECURITY_HEADER_LEN + WS_SECURITY_TAG_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (buffer[0] != WS_SECURITY_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    uint8_t flags = buffer[1];
    if (flags != 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t counter = 0;
    memcpy(&counter, buffer + 2, sizeof(counter));
    if (counter_state) {
        if (counter <= *counter_state) {
            ESP_LOGW(TAG, "Replay detected (counter=%" PRIu64 ")", counter);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    uint8_t *iv = buffer + WS_SECURITY_FIXED_HEADER_LEN;
    uint8_t *ciphertext = iv + WS_SECURITY_IV_LEN;
    size_t ciphertext_len = buffer_len - WS_SECURITY_HEADER_LEN - WS_SECURITY_TAG_LEN;
    uint8_t *tag = ciphertext + ciphertext_len;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, ctx->frame_key, (unsigned int)sizeof(ctx->frame_key) * 8U);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, ciphertext_len, iv, WS_SECURITY_IV_LEN, buffer, WS_SECURITY_FIXED_HEADER_LEN,
                                      ciphertext, ciphertext, WS_SECURITY_TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    if (rc != 0) {
        ESP_LOGW(TAG, "AES-GCM decrypt failed (%d)", rc);
        return ESP_ERR_INVALID_RESPONSE;
    }
    memmove(buffer, ciphertext, ciphertext_len);
    if (plaintext_len) {
        *plaintext_len = ciphertext_len;
    }
    if (counter_state) {
        *counter_state = counter;
    }
    return ESP_OK;
}

static esp_err_t handshake_hmac(const ws_security_context_t *ctx, const uint8_t *nonce, size_t nonce_len,
                                const char *auth_token, uint8_t *signature, size_t signature_len)
{
    if (!ctx || !nonce || nonce_len == 0 || !auth_token || !signature || signature_len < 32) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ctx->handshake_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        return ESP_FAIL;
    }
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    int rc = mbedtls_md_setup(&md, info, 1);
    if (rc == 0) {
        rc = mbedtls_md_hmac_starts(&md, ctx->handshake_key, sizeof(ctx->handshake_key));
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(&md, nonce, nonce_len);
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_update(&md, (const unsigned char *)auth_token, strlen(auth_token));
    }
    if (rc == 0) {
        rc = mbedtls_md_hmac_finish(&md, signature);
    }
    mbedtls_md_free(&md);
    if (rc != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ws_security_compute_handshake_signature(const ws_security_context_t *ctx, const uint8_t *nonce,
                                                  size_t nonce_len, const char *auth_token, uint8_t *signature,
                                                  size_t signature_len)
{
    return handshake_hmac(ctx, nonce, nonce_len, auth_token, signature, signature_len);
}

esp_err_t ws_security_verify_handshake(const ws_security_context_t *ctx, const uint8_t *nonce, size_t nonce_len,
                                       const char *auth_token, const uint8_t *signature, size_t signature_len)
{
    if (!signature || signature_len < 32) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t expected[32] = {0};
    esp_err_t err = handshake_hmac(ctx, nonce, nonce_len, auth_token, expected, sizeof(expected));
    if (err != ESP_OK) {
        return err;
    }
    if (mbedtls_ct_memcmp(expected, signature, sizeof(expected)) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

bool ws_security_is_encryption_enabled(const ws_security_context_t *ctx)
{
    return ctx && ctx->encryption_enabled;
}

bool ws_security_is_handshake_enabled(const ws_security_context_t *ctx)
{
    return ctx && ctx->handshake_enabled;
}

void ws_security_reset_counters(ws_security_context_t *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->tx_counter = 0;
}
