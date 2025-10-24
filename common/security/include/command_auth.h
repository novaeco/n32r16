#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "proto_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMMAND_AUTH_KEY_SIZE 32U
#define COMMAND_AUTH_MAC_SIZE 32U
#define COMMAND_AUTH_MAC_HEX_LEN (COMMAND_AUTH_MAC_SIZE * 2)
#define COMMAND_AUTH_NONCE_MAX_BYTES 16U

esp_err_t command_auth_init_from_nvs(const char *partition, const char *ns, const char *key_name);
esp_err_t command_auth_set_key(const uint8_t *key, size_t len);
void command_auth_reset_history(void);
void command_auth_forget_key(void);

esp_err_t command_auth_generate_mac(const cJSON *payload, uint32_t version, uint64_t timestamp_ms,
                                    const uint8_t *nonce, size_t nonce_len, char *mac_hex,
                                    size_t mac_hex_len);

bool command_auth_validate(const proto_envelope_t *env);

#ifdef __cplusplus
}
#endif

