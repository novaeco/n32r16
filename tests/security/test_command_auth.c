#include "unity.h"

#include <string.h>

#include "cJSON.h"
#include "command_auth.h"
#include "esp_err.h"
#include "proto_codec.h"

static const uint8_t TEST_KEY[COMMAND_AUTH_KEY_SIZE] = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
    0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0xf0,
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11,
};

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len) {
    static const char table[] = "0123456789abcdef";
    TEST_ASSERT(len * 2 + 1 <= out_len);
    for (size_t i = 0; i < len; ++i) {
        out[2 * i] = table[(bytes[i] >> 4) & 0x0F];
        out[2 * i + 1] = table[bytes[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

TEST_CASE("command auth accepts valid signature", "[command_auth]") {
    command_auth_forget_key();
    TEST_ASSERT_EQUAL(ESP_OK, command_auth_set_key(TEST_KEY, sizeof(TEST_KEY)));
    command_auth_reset_history();

    cJSON *payload = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(payload);
    cJSON_AddItemToObject(payload, "set_pwm", cJSON_CreateObject());
    cJSON *set_pwm = cJSON_GetObjectItemCaseSensitive(payload, "set_pwm");
    TEST_ASSERT_NOT_NULL(set_pwm);
    cJSON_AddNumberToObject(set_pwm, "ch", 3);
    cJSON_AddNumberToObject(set_pwm, "duty", 1024);

    cJSON *auth = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(auth);

    proto_envelope_t env = {
        .version = 1,
        .type = PROTO_MSG_COMMAND,
        .timestamp_ms = 12345678ULL,
        .payload = payload,
        .auth = auth,
    };

    uint8_t nonce[COMMAND_AUTH_NONCE_MAX_BYTES] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };
    char nonce_hex[COMMAND_AUTH_NONCE_MAX_BYTES * 2 + 1];
    bytes_to_hex(nonce, 8, nonce_hex, sizeof(nonce_hex));

    char mac_hex[COMMAND_AUTH_MAC_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(ESP_OK, command_auth_generate_mac(payload, env.version, env.timestamp_ms,
                                                        nonce, 8, mac_hex, sizeof(mac_hex)));

    cJSON_AddStringToObject(auth, "nonce", nonce_hex);
    cJSON_AddStringToObject(auth, "mac", mac_hex);
    cJSON_AddStringToObject(auth, "alg", "HS256");

    TEST_ASSERT_TRUE(command_auth_validate(&env));
    TEST_ASSERT_FALSE(command_auth_validate(&env));

    cJSON_Delete(payload);
    cJSON_Delete(auth);
    command_auth_forget_key();
}

TEST_CASE("command auth rejects tampered payload", "[command_auth]") {
    command_auth_forget_key();
    TEST_ASSERT_EQUAL(ESP_OK, command_auth_set_key(TEST_KEY, sizeof(TEST_KEY)));

    cJSON *payload = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(payload);
    cJSON_AddStringToObject(payload, "cmd", "noop");

    cJSON *auth = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(auth);

    proto_envelope_t env = {
        .version = 1,
        .type = PROTO_MSG_COMMAND,
        .timestamp_ms = 98765432ULL,
        .payload = payload,
        .auth = auth,
    };

    uint8_t nonce[COMMAND_AUTH_NONCE_MAX_BYTES] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    };
    char nonce_hex[COMMAND_AUTH_NONCE_MAX_BYTES * 2 + 1];
    bytes_to_hex(nonce, 8, nonce_hex, sizeof(nonce_hex));
    char mac_hex[COMMAND_AUTH_MAC_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(ESP_OK, command_auth_generate_mac(payload, env.version, env.timestamp_ms,
                                                        nonce, 8, mac_hex, sizeof(mac_hex)));
    mac_hex[0] = (mac_hex[0] == '0') ? '1' : '0';
    cJSON_AddStringToObject(auth, "nonce", nonce_hex);
    cJSON_AddStringToObject(auth, "mac", mac_hex);

    TEST_ASSERT_FALSE(command_auth_validate(&env));

    cJSON_Delete(payload);
    cJSON_Delete(auth);
    command_auth_forget_key();
}

