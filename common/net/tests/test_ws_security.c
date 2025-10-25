#include "ws_security.h"

#include "unity.h"
#include <stdint.h>
#include <string.h>

TEST_CASE("ws security encrypts and decrypts payloads", "[net][ws]")
{
    const uint8_t secret[] = {
        0x4a, 0x92, 0x13, 0x6f, 0x54, 0x27, 0x90, 0x1a,
        0x3b, 0x6c, 0x5d, 0x2e, 0xfe, 0x81, 0xa1, 0x19,
        0x72, 0x4b, 0x37, 0xaa, 0x88, 0x93, 0xcd, 0x0f,
        0x24, 0x5a, 0x8c, 0xbe, 0x11, 0x42, 0x77, 0x90,
    };
    ws_security_config_t cfg = {
        .secret = secret,
        .secret_len = sizeof(secret),
        .enable_encryption = true,
        .enable_handshake = true,
    };
    ws_security_context_t tx_ctx = {0};
    ws_security_context_t rx_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&tx_ctx, &cfg));
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&rx_ctx, &cfg));

    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t frame[128] = {0};
    size_t frame_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_encrypt(&tx_ctx, payload, sizeof(payload), frame, sizeof(frame), &frame_len));
    TEST_ASSERT_GREATER_THAN(sizeof(payload), frame_len);

    size_t plaintext_len = 0;
    uint64_t counter_state = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_decrypt(&rx_ctx, frame, frame_len, &plaintext_len, &counter_state));
    TEST_ASSERT_EQUAL(sizeof(payload), plaintext_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame, sizeof(payload));

    // Replay of the same frame must be rejected.
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      ws_security_decrypt(&rx_ctx, frame, frame_len, &plaintext_len, &counter_state));
}

TEST_CASE("ws security rejects tampered ciphertext", "[net][ws]")
{
    const uint8_t secret[32] = {0};
    ws_security_config_t cfg = {
        .secret = secret,
        .secret_len = sizeof(secret),
        .enable_encryption = true,
    };
    ws_security_context_t tx_ctx = {0};
    ws_security_context_t rx_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&tx_ctx, &cfg));
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&rx_ctx, &cfg));

    const uint8_t payload[] = {0x01, 0x02, 0x03};
    uint8_t frame[128] = {0};
    size_t frame_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_encrypt(&tx_ctx, payload, sizeof(payload), frame, sizeof(frame), &frame_len));
    TEST_ASSERT_GREATER_THAN(sizeof(payload), frame_len);

    frame[frame_len - 1] ^= 0xAA;
    size_t plaintext_len = 0;
    uint64_t counter = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      ws_security_decrypt(&rx_ctx, frame, frame_len, &plaintext_len, &counter));
}

TEST_CASE("ws security encrypts burst of frames", "[net][ws]")
{
    const uint8_t secret[32] = {0};
    ws_security_config_t cfg = {
        .secret = secret,
        .secret_len = sizeof(secret),
        .enable_encryption = true,
    };
    ws_security_context_t tx_ctx = {0};
    ws_security_context_t rx_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&tx_ctx, &cfg));
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&rx_ctx, &cfg));

    uint8_t payload[16] = {0};
    for (uint32_t i = 0; i < 256; ++i) {
        for (size_t j = 0; j < sizeof(payload); ++j) {
            payload[j] = (uint8_t)(i + j);
        }
        uint8_t frame[128] = {0};
        size_t frame_len = 0;
        TEST_ASSERT_EQUAL(ESP_OK, ws_security_encrypt(&tx_ctx, payload, sizeof(payload), frame, sizeof(frame), &frame_len));
        size_t plaintext_len = 0;
        uint64_t counter = 0;
        TEST_ASSERT_EQUAL(ESP_OK, ws_security_decrypt(&rx_ctx, frame, frame_len, &plaintext_len, &counter));
        TEST_ASSERT_EQUAL(sizeof(payload), plaintext_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame, sizeof(payload));
    }
}

TEST_CASE("ws security validates handshake signatures", "[net][ws]")
{
    const uint8_t secret[] = {
        0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18,
        0x29, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x90,
        0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
        0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0xf0,
    };
    ws_security_config_t cfg = {
        .secret = secret,
        .secret_len = sizeof(secret),
        .enable_encryption = false,
        .enable_handshake = true,
    };
    ws_security_context_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&ctx, &cfg));

    uint8_t nonce[WS_SECURITY_NONCE_LEN];
    for (size_t i = 0; i < sizeof(nonce); ++i) {
        nonce[i] = (uint8_t)i;
    }
    const char token[] = "Bearer token";
    uint8_t signature[32];
    TEST_ASSERT_EQUAL(ESP_OK,
                      ws_security_compute_handshake_signature(&ctx, nonce, sizeof(nonce), token, signature,
                                                             sizeof(signature)));
    TEST_ASSERT_EQUAL(ESP_OK,
                      ws_security_verify_handshake(&ctx, nonce, sizeof(nonce), token, signature, sizeof(signature)));

    signature[0] ^= 0xFF;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE,
                      ws_security_verify_handshake(&ctx, nonce, sizeof(nonce), token, signature, sizeof(signature)));
}

TEST_CASE("ws security computes and verifies totp codes", "[net][ws]")
{
    static const uint8_t secret[] = "12345678901234567890";
    ws_security_config_t cfg = {
        .secret = NULL,
        .secret_len = 0,
        .enable_encryption = false,
        .enable_handshake = false,
        .enable_totp = true,
        .totp_secret = secret,
        .totp_secret_len = sizeof(secret) - 1U,
        .totp_period_s = 30,
        .totp_digits = 8,
        .totp_window = 1,
    };
    ws_security_context_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_context_init(&ctx, &cfg));

    uint32_t code = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_compute_totp(&ctx, 59, &code));
    TEST_ASSERT_EQUAL_UINT32(94287082, code);

    bool match = false;
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_verify_totp(&ctx, 59, code, &match));
    TEST_ASSERT_TRUE(match);

    match = true;
    TEST_ASSERT_EQUAL(ESP_OK, ws_security_verify_totp(&ctx, 59, 11111111, &match));
    TEST_ASSERT_FALSE(match);
}
