#include "net/ws_server.h"

#include "common/net/mdns_helper.h"
#include "common/net/wifi_manager.h"
#include "common/net/ws_server.h"
#include "common/proto/messages.h"
#include "common/util/monotonic.h"
#include "esp_log.h"
#include "io/io_map.h"
#include "tasks/t_io.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sensor_ws";

static sensor_data_model_t *s_model;
static bool s_use_cbor;

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
        io_task_write_gpio(cmd.gpio_write.device_index, cmd.gpio_write.mask, cmd.gpio_write.value);
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
        .ssid = CONFIG_SENSOR_WIFI_SSID,
        .password = CONFIG_SENSOR_WIFI_PASSWORD,
        .power_save = false,
    };
    ESP_ERROR_CHECK(wifi_manager_start_sta(&wifi_cfg));
    ESP_ERROR_CHECK(mdns_helper_start("sensor-node", "Sensor Node", CONFIG_SENSOR_WS_PORT));
    ESP_ERROR_CHECK(ws_server_start(CONFIG_SENSOR_WS_PORT, ws_rx, NULL));
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
    uint8_t *frame = malloc(frame_len);
    if (!frame) {
        free(payload);
        return;
    }
    memcpy(frame, &crc, sizeof(uint32_t));
    memcpy(frame + sizeof(uint32_t), payload, payload_len);
    ws_server_send(frame, frame_len);
    free(frame);
    free(payload);
}
