#include "net/ws_server.h"

#include "common/net/mdns_helper.h"
#include "common/net/wifi_manager.h"
#include "common/net/ws_server.h"
#include "common/proto/messages.h"
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

    wifi_manager_config_t wifi_cfg = {
        .power_save = false,
        .service_name_suffix = CONFIG_SENSOR_PROV_SERVICE_NAME,
        .pop = CONFIG_SENSOR_PROV_POP,
    };
    ESP_ERROR_CHECK(wifi_manager_start(&wifi_cfg));
    ESP_ERROR_CHECK(mdns_helper_start("sensor-node", "Sensor Node", CONFIG_SENSOR_WS_PORT));

    size_t cert_len = 0;
    size_t key_len = 0;
    const uint8_t *cert = cert_store_server_cert(&cert_len);
    const uint8_t *key = cert_store_server_key(&key_len);

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
        .rx_buffer_size = SENSOR_DATA_MODEL_MAX_MESSAGE_SIZE + sizeof(uint32_t),
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
