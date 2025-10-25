#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WS_SECURITY_VERSION 1U
#define WS_SECURITY_IV_LEN 12U
#define WS_SECURITY_TAG_LEN 16U
#define WS_SECURITY_COUNTER_LEN 8U
#define WS_SECURITY_FIXED_HEADER_LEN (1U + 1U + WS_SECURITY_COUNTER_LEN)
#define WS_SECURITY_HEADER_LEN (WS_SECURITY_FIXED_HEADER_LEN + WS_SECURITY_IV_LEN)
#define WS_SECURITY_NONCE_LEN 16U

typedef struct {
    const uint8_t *secret;
    size_t secret_len;
    bool enable_encryption;
    bool enable_handshake;
    bool enable_totp;
    const uint8_t *totp_secret;
    size_t totp_secret_len;
    uint32_t totp_period_s;
    uint8_t totp_digits;
    uint32_t totp_window;
} ws_security_config_t;

typedef struct {
    bool handshake_enabled;
    bool encryption_enabled;
    bool totp_enabled;
    uint8_t handshake_key[32];
    uint8_t frame_key[32];
    uint64_t tx_counter;
    uint8_t totp_secret[64];
    size_t totp_secret_len;
    uint32_t totp_period_s;
    uint8_t totp_digits;
    uint32_t totp_window;
} ws_security_context_t;

esp_err_t ws_security_context_init(ws_security_context_t *ctx, const ws_security_config_t *config);
size_t ws_security_encrypted_size(const ws_security_context_t *ctx, size_t plaintext_len);
esp_err_t ws_security_encrypt(ws_security_context_t *ctx, const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *out, size_t out_size, size_t *out_len);
esp_err_t ws_security_decrypt(const ws_security_context_t *ctx, uint8_t *buffer, size_t buffer_len,
                              size_t *plaintext_len, uint64_t *counter_state);
esp_err_t ws_security_compute_handshake_signature(const ws_security_context_t *ctx, const uint8_t *nonce,
                                                  size_t nonce_len, const char *auth_token, uint8_t *signature,
                                                  size_t signature_len);
esp_err_t ws_security_verify_handshake(const ws_security_context_t *ctx, const uint8_t *nonce, size_t nonce_len,
                                       const char *auth_token, const uint8_t *signature, size_t signature_len);
bool ws_security_is_encryption_enabled(const ws_security_context_t *ctx);
bool ws_security_is_handshake_enabled(const ws_security_context_t *ctx);
void ws_security_reset_counters(ws_security_context_t *ctx);
bool ws_security_is_totp_enabled(const ws_security_context_t *ctx);
uint8_t ws_security_totp_digits(const ws_security_context_t *ctx);
esp_err_t ws_security_compute_totp(const ws_security_context_t *ctx, uint64_t unix_time, uint32_t *code);
esp_err_t ws_security_verify_totp(const ws_security_context_t *ctx, uint64_t unix_time, uint32_t code, bool *match);
