#include "net/ws_server.h"

#include "common/net/mdns_helper.h"
#include "common/net/wifi_manager.h"
#include "common/net/ws_server.h"
#include "common/net/ws_security.h"
#include "common/proto/messages.h"
#include "common/util/base64_utils.h"
#include "common/util/base32_utils.h"
#include "common/util/monotonic.h"
#include "cert_store.h"
#include "esp_log.h"
#include "io/io_map.h"
#include "tasks/t_io.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "sensor_ws";

static sensor_data_model_t *s_model;
static bool s_use_cbor;
static uint8_t s_tx_frame[SENSOR_DATA_MODEL_MAX_MESSAGE_SIZE + sizeof(uint32_t)];
static uint8_t s_sec2_salt[32];
static uint8_t s_sec2_verifier[384];
static uint8_t s_ws_secret[64];
static size_t s_ws_secret_len;
static uint8_t s_totp_secret[64];
static size_t s_totp_secret_len;
static wifi_manager_sec2_params_t s_sec2_params = {
    .salt = s_sec2_salt,
    .verifier = s_sec2_verifier,
};

static void ws_rx(const uint8_t *data, size_t len, uint32_t crc, void *ctx)
{
    (void)ctx;
    proto_command_t cmd;
    if (!proto_decode_command(data, len, s_use_cbor, &cmd, crc)) {
        ESP_LOGW(TAG, "Failed to decode command");
        return;
    }
    if (cmd.has_pwm_update) {
        io_task_set_pwm(cmd.pwm_update.channel, cmd.pwm_update.duty_cycle);
    }
    if (cmd.has_pwm_frequency) {
        io_task_set_pwm_frequency(cmd.pwm_frequency);
    }
    if (cmd.has_gpio_write) {
        io_task_write_gpio(cmd.gpio_write.device_index, cmd.gpio_write.port, cmd.gpio_write.mask,
                           cmd.gpio_write.value);
    }
}

void sensor_ws_server_start(sensor_data_model_t *model)
{
    s_model = model;
#if CONFIG_USE_CBOR
    s_use_cbor = true;
#else
    s_use_cbor = false;
#endif

    size_t salt_len = 0;
    size_t verifier_len = 0;
    ESP_ERROR_CHECK(base64_utils_decode(CONFIG_SENSOR_PROV_SEC2_SALT_BASE64, s_sec2_salt, sizeof(s_sec2_salt), &salt_len));
    ESP_ERROR_CHECK(base64_utils_decode(CONFIG_SENSOR_PROV_SEC2_VERIFIER_BASE64, s_sec2_verifier, sizeof(s_sec2_verifier),
                                        &verifier_len));
    s_sec2_params.salt_len = salt_len;
    s_sec2_params.verifier_len = verifier_len;

    s_ws_secret_len = 0;
    if (CONFIG_SENSOR_WS_CRYPTO_SECRET_BASE64[0] != '\0') {
        ESP_ERROR_CHECK(
            base64_utils_decode(CONFIG_SENSOR_WS_CRYPTO_SECRET_BASE64, s_ws_secret, sizeof(s_ws_secret), &s_ws_secret_len));
    }
    const bool enable_encryption = IS_ENABLED(CONFIG_SENSOR_WS_ENABLE_ENCRYPTION);
    const bool enable_handshake = IS_ENABLED(CONFIG_SENSOR_WS_ENABLE_HANDSHAKE);
    const bool enable_totp = IS_ENABLED(CONFIG_SENSOR_WS_ENABLE_TOTP);
    if ((enable_encryption || enable_handshake) && s_ws_secret_len == 0) {
        ESP_LOGE(TAG, "WebSocket security enabled but secret is empty");
        abort();
    }
    s_totp_secret_len = 0;
    if (CONFIG_SENSOR_WS_TOTP_SECRET_BASE32[0] != '\0') {
        ESP_ERROR_CHECK(
            base32_utils_decode(CONFIG_SENSOR_WS_TOTP_SECRET_BASE32, s_totp_secret, sizeof(s_totp_secret), &s_totp_secret_len));
    }
    if (enable_totp && s_totp_secret_len == 0) {
        ESP_LOGE(TAG, "TOTP enabled but secret is empty");
        abort();
    }

    wifi_manager_config_t wifi_cfg = {
        .power_save = false,
        .service_name_suffix = CONFIG_SENSOR_PROV_SERVICE_NAME,
        .pop = CONFIG_SENSOR_PROV_POP,
#ifdef CONFIG_SENSOR_PROV_PREFER_BLE
        .prefer_ble = true,
#else
        .prefer_ble = false,
#endif
        .force_provisioning = false,
        .provisioning_timeout_ms = CONFIG_SENSOR_PROV_TIMEOUT_MS,
        .connect_timeout_ms = CONFIG_SENSOR_PROV_CONNECT_TIMEOUT_MS,
        .max_connect_attempts = CONFIG_SENSOR_PROV_MAX_ATTEMPTS,
        .sec2_params = &s_sec2_params,
        .sec2_username = CONFIG_SENSOR_PROV_SEC2_USERNAME,
    };
    ESP_ERROR_CHECK(wifi_manager_start(&wifi_cfg));
    ESP_ERROR_CHECK(mdns_helper_start("sensor-node", "Sensor Node", CONFIG_SENSOR_WS_PORT));

    size_t cert_len = 0;
    size_t key_len = 0;
    const uint8_t *cert = cert_store_server_cert(&cert_len);
    const uint8_t *key = cert_store_server_key(&key_len);

    size_t rx_buffer_size = SENSOR_DATA_MODEL_MAX_MESSAGE_SIZE + sizeof(uint32_t);
    if (enable_encryption) {
        rx_buffer_size += WS_SECURITY_HEADER_LEN + WS_SECURITY_TAG_LEN;
    }

    ws_server_config_t ws_cfg = {
        .port = CONFIG_SENSOR_WS_PORT,
        .max_clients = 8,
        .auth_token = CONFIG_SENSOR_WS_AUTH_TOKEN,
        .server_cert = cert,
        .server_cert_len = cert_len,
        .server_key = key,
        .server_key_len = key_len,
        .ping_interval_ms = 10000,
        .pong_timeout_ms = 5000,
        .rx_buffer_size = rx_buffer_size,
        .crypto_secret = s_ws_secret_len > 0 ? s_ws_secret : NULL,
        .crypto_secret_len = s_ws_secret_len,
        .enable_frame_encryption = enable_encryption,
        .enable_handshake_token = enable_handshake,
        .handshake_replay_window_ms = CONFIG_SENSOR_WS_HANDSHAKE_TTL_MS,
        .handshake_cache_size = CONFIG_SENSOR_WS_HANDSHAKE_CACHE_SIZE,
        .enable_totp = enable_totp,
        .totp_secret = s_totp_secret_len > 0 ? s_totp_secret : NULL,
        .totp_secret_len = s_totp_secret_len,
        .totp_period_s = CONFIG_SENSOR_WS_TOTP_PERIOD_S,
        .totp_digits = CONFIG_SENSOR_WS_TOTP_DIGITS,
        .totp_window = CONFIG_SENSOR_WS_TOTP_WINDOW,
    };
    ESP_ERROR_CHECK(ws_server_start(&ws_cfg, ws_rx, NULL));
}

void sensor_ws_server_send_update(sensor_data_model_t *model, bool use_cbor)
{
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint32_t crc = 0;
    if (!data_model_build(model, use_cbor, &payload, &payload_len, &crc)) {
        return;
    }
    size_t frame_len = payload_len + sizeof(uint32_t);
    if (frame_len > sizeof(s_tx_frame)) {
        ESP_LOGE(TAG, "Frame too large (%zu)", frame_len);
        return;
    }
    memcpy(s_tx_frame, &crc, sizeof(uint32_t));
    memcpy(s_tx_frame + sizeof(uint32_t), payload, payload_len);
    ws_server_send(s_tx_frame, frame_len);
}
