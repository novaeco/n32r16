#include "command_auth.h"

#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/constant_time.h"
#include "nvs.h"
#include "nvs_flash.h"

#define COMMAND_AUTH_REPLAY_WINDOW_MS (60ULL * 1000ULL)
#define COMMAND_AUTH_HISTORY_DEPTH 8U

static const char *TAG = "cmd_auth";

static uint8_t s_key[COMMAND_AUTH_KEY_SIZE];
static size_t s_key_len;
static bool s_key_ready;

typedef struct {
    uint8_t nonce[COMMAND_AUTH_NONCE_MAX_BYTES];
    size_t len;
    uint64_t timestamp_ms;
    bool used;
} command_auth_history_entry_t;

static command_auth_history_entry_t s_history[COMMAND_AUTH_HISTORY_DEPTH];
static uint64_t s_latest_timestamp;

static void to_be32(uint32_t value, uint8_t out[4]) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static void to_be64(uint64_t value, uint8_t out[8]) {
    for (int i = 7; i >= 0; --i) {
        out[i] = (uint8_t)(value & 0xFFU);
        value >>= 8;
    }
}

static bool hex_to_bytes(const char *hex, uint8_t *out, size_t max_len, size_t *out_len) {
    if (hex == NULL || out == NULL || out_len == NULL) {
        return false;
    }
    size_t hex_len = strlen(hex);
    if ((hex_len % 2U) != 0) {
        return false;
    }
    size_t byte_len = hex_len / 2U;
    if (byte_len == 0 || byte_len > max_len) {
        return false;
    }
    for (size_t i = 0; i < byte_len; ++i) {
        char hi = hex[2U * i];
        char lo = hex[2U * i + 1U];
        uint8_t value = 0;
        for (int shift = 0; shift < 2; ++shift) {
            char c = (shift == 0) ? hi : lo;
            uint8_t nibble;
            if (c >= '0' && c <= '9') {
                nibble = (uint8_t)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = (uint8_t)(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                nibble = (uint8_t)(c - 'A' + 10);
            } else {
                return false;
            }
            value = (uint8_t)((value << 4) | nibble);
        }
        out[i] = value;
    }
    *out_len = byte_len;
    return true;
}

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out) {
    static const char hex_table[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[2U * i] = hex_table[(bytes[i] >> 4) & 0x0F];
        out[2U * i + 1U] = hex_table[bytes[i] & 0x0F];
    }
    out[2U * len] = '\0';
}

static bool nonce_seen(const uint8_t *nonce, size_t len) {
    for (size_t i = 0; i < COMMAND_AUTH_HISTORY_DEPTH; ++i) {
        if (s_history[i].used && s_history[i].len == len &&
            memcmp(s_history[i].nonce, nonce, len) == 0) {
            return true;
        }
    }
    return false;
}

static void store_nonce(uint64_t timestamp_ms, const uint8_t *nonce, size_t len) {
    size_t slot = COMMAND_AUTH_HISTORY_DEPTH;
    uint64_t oldest_ts = UINT64_MAX;
    for (size_t i = 0; i < COMMAND_AUTH_HISTORY_DEPTH; ++i) {
        if (!s_history[i].used) {
            slot = i;
            break;
        }
        if (s_history[i].timestamp_ms < oldest_ts) {
            oldest_ts = s_history[i].timestamp_ms;
            slot = i;
        }
    }
    if (slot >= COMMAND_AUTH_HISTORY_DEPTH) {
        slot = 0;
    }
    memcpy(s_history[slot].nonce, nonce, len);
    s_history[slot].len = len;
    s_history[slot].timestamp_ms = timestamp_ms;
    s_history[slot].used = true;
}

static bool timestamp_valid(uint64_t ts) {
    if (s_latest_timestamp == 0) {
        s_latest_timestamp = ts;
        return true;
    }
    if (ts + COMMAND_AUTH_REPLAY_WINDOW_MS < s_latest_timestamp) {
        return false;
    }
    if (ts > s_latest_timestamp) {
        s_latest_timestamp = ts;
    }
    return true;
}

static esp_err_t ensure_key_ready(void) {
    return s_key_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t command_auth_set_key(const uint8_t *key, size_t len) {
    if (key == NULL || len != COMMAND_AUTH_KEY_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    command_auth_forget_key();
    memcpy(s_key, key, len);
    s_key_len = len;
    s_key_ready = true;
    return ESP_OK;
}

esp_err_t command_auth_init_from_nvs(const char *partition, const char *ns, const char *key_name) {
    if (partition == NULL || ns == NULL || key_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(partition, ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS partition %s: %s", partition, esp_err_to_name(err));
        return err;
    }
    uint8_t key_buf[COMMAND_AUTH_KEY_SIZE];
    size_t required = sizeof(key_buf);
    err = nvs_get_blob(handle, key_name, key_buf, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read key '%s': %s", key_name, esp_err_to_name(err));
        return err;
    }
    if (required != COMMAND_AUTH_KEY_SIZE) {
        ESP_LOGE(TAG, "Invalid key length %zu (expected %u)", required, COMMAND_AUTH_KEY_SIZE);
        mbedtls_platform_zeroize(key_buf, sizeof(key_buf));
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t set_res = command_auth_set_key(key_buf, required);
    mbedtls_platform_zeroize(key_buf, sizeof(key_buf));
    return set_res;
}

void command_auth_reset_history(void) {
    memset(s_history, 0, sizeof(s_history));
    s_latest_timestamp = 0;
}

void command_auth_forget_key(void) {
    mbedtls_platform_zeroize(s_key, sizeof(s_key));
    s_key_len = 0;
    s_key_ready = false;
    command_auth_reset_history();
}

esp_err_t command_auth_generate_mac(const cJSON *payload, uint32_t version, uint64_t timestamp_ms,
                                    const uint8_t *nonce, size_t nonce_len, char *mac_hex,
                                    size_t mac_hex_len) {
    if (payload == NULL || nonce == NULL || mac_hex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (nonce_len == 0 || nonce_len > COMMAND_AUTH_NONCE_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (mac_hex_len < COMMAND_AUTH_MAC_HEX_LEN + 1U) {
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(ensure_key_ready(), TAG, "key not ready");

    char *payload_str = cJSON_PrintUnformatted(payload);
    if (payload_str == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t payload_len = strlen(payload_str);

    uint8_t header[12];
    to_be32(version, header);
    to_be64(timestamp_ms, header + 4);

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        cJSON_free(payload_str);
        return ESP_ERR_INVALID_STATE;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int ret = mbedtls_md_setup(&ctx, info, 1);
    if (ret == 0) {
        ret = mbedtls_md_hmac_starts(&ctx, s_key, s_key_len);
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_update(&ctx, header, sizeof(header));
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_update(&ctx, nonce, nonce_len);
    }
    if (ret == 0) {
        ret = mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload_str, payload_len);
    }
    uint8_t mac_bin[COMMAND_AUTH_MAC_SIZE];
    if (ret == 0) {
        ret = mbedtls_md_hmac_finish(&ctx, mac_bin);
    }
    mbedtls_md_free(&ctx);
    cJSON_free(payload_str);
    if (ret != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    bytes_to_hex(mac_bin, sizeof(mac_bin), mac_hex);
    return ESP_OK;
}

bool command_auth_validate(const proto_envelope_t *env) {
    if (env == NULL || env->payload == NULL || env->auth == NULL) {
        return false;
    }
    if (ensure_key_ready() != ESP_OK) {
        ESP_LOGE(TAG, "Shared key not loaded");
        return false;
    }
    if (!timestamp_valid(env->timestamp_ms)) {
        ESP_LOGW(TAG, "Rejected replayed timestamp %llu", (unsigned long long)env->timestamp_ms);
        return false;
    }
    cJSON *nonce_item = cJSON_GetObjectItemCaseSensitive(env->auth, "nonce");
    cJSON *mac_item = cJSON_GetObjectItemCaseSensitive(env->auth, "mac");
    cJSON *alg_item = cJSON_GetObjectItemCaseSensitive(env->auth, "alg");
    if (!cJSON_IsString(nonce_item) || !cJSON_IsString(mac_item)) {
        return false;
    }
    if (alg_item != NULL && (!cJSON_IsString(alg_item) || strcmp(alg_item->valuestring, "HS256") != 0)) {
        return false;
    }
    if (strlen(mac_item->valuestring) != COMMAND_AUTH_MAC_HEX_LEN) {
        return false;
    }
    uint8_t nonce_bin[COMMAND_AUTH_NONCE_MAX_BYTES];
    size_t nonce_len = 0;
    if (!hex_to_bytes(nonce_item->valuestring, nonce_bin, sizeof(nonce_bin), &nonce_len)) {
        return false;
    }
    if (nonce_seen(nonce_bin, nonce_len)) {
        ESP_LOGW(TAG, "Rejected replayed nonce");
        return false;
    }
    char expected_mac[COMMAND_AUTH_MAC_HEX_LEN + 1U];
    if (command_auth_generate_mac(env->payload, env->version, env->timestamp_ms, nonce_bin, nonce_len,
                                  expected_mac, sizeof(expected_mac)) != ESP_OK) {
        return false;
    }
    if (mbedtls_ct_memcmp(mac_item->valuestring, expected_mac, COMMAND_AUTH_MAC_HEX_LEN) != 0) {
        ESP_LOGW(TAG, "Invalid MAC for command");
        return false;
    }
    store_nonce(env->timestamp_ms, nonce_bin, nonce_len);
    return true;
}

