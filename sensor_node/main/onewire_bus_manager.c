#include "onewire_bus_manager.h"

#include "esp_log.h"

static const char *TAG = "onewire";

esp_err_t onewire_bus_manager_init(gpio_num_t pin, onewire_bus_handle_t *out_handle)
{
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = pin,
        .max_rx_bytes = 16,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1 * 1000 * 1000,
        .with_dma = false,
    };
    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, out_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init 1-Wire bus: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t onewire_bus_scan(onewire_bus_handle_t bus, onewire_device_t *devices, size_t max_devices,
                           size_t *found)
{
    if (!bus || !devices || !found) {
        return ESP_ERR_INVALID_ARG;
    }
    onewire_device_iter_handle_t iter;
    onewire_device_iter_config_t config = {
        .bus = bus,
    };
    esp_err_t err = onewire_new_device_iter(&config, &iter);
    if (err != ESP_OK) {
        return err;
    }
    size_t count = 0;
    while (count < max_devices && onewire_device_iter_get_next(iter, &devices[count]) == ESP_OK) {
        ++count;
    }
    *found = count;
    onewire_del_device_iter(iter);
    return ESP_OK;
}
