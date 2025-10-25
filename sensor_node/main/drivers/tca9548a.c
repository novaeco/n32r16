#include "drivers/tca9548a.h"

#include "esp_log.h"
#include "i2c_bus.h"
#include "io/io_map.h"

static const char *TAG = "tca9548a";

static uint8_t s_last_addr;
static int8_t s_last_channel = IO_MUX_CHANNEL_NONE;
static bool s_has_state;

esp_err_t tca9548a_select(uint8_t addr, int8_t channel)
{
    if (channel < IO_MUX_CHANNEL_NONE || channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (addr < 0x08 || addr > 0x77) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel == s_last_channel && addr == s_last_addr && s_has_state) {
        return ESP_OK;
    }
    uint8_t payload = 0;
    if (channel != IO_MUX_CHANNEL_NONE) {
        payload = (uint8_t)(1U << channel);
    }
    esp_err_t err = i2c_bus_write(addr, &payload, sizeof(payload));
    if (err == ESP_OK) {
        s_last_addr = addr;
        s_last_channel = channel;
        s_has_state = true;
        ESP_LOGD(TAG, "Selected channel %d on 0x%02X", channel, addr);
    } else {
        ESP_LOGW(TAG, "Failed to select channel %d on 0x%02X: %s", channel, addr, esp_err_to_name(err));
    }
    return err;
}
