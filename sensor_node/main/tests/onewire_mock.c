#include "onewire_mock.h"

#include "unity.h"
#include <string.h>

static uint8_t s_scratch[9];
static size_t s_scratch_len;
static bool s_ready;

void onewire_mock_set_scratch(const uint8_t *data, size_t len)
{
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(s_scratch), len);
    memcpy(s_scratch, data, len);
    s_scratch_len = len;
}

void onewire_mock_set_ready(bool ready)
{
    s_ready = ready;
}

esp_err_t onewire_bus_reset(onewire_bus_handle_t bus)
{
    (void)bus;
    return ESP_OK;
}

esp_err_t onewire_bus_write_bytes(onewire_bus_handle_t bus, const uint8_t *data, size_t len)
{
    (void)bus;
    (void)data;
    (void)len;
    return ESP_OK;
}

esp_err_t onewire_bus_read_bytes(onewire_bus_handle_t bus, uint8_t *data, size_t len)
{
    (void)bus;
    TEST_ASSERT_LESS_OR_EQUAL(s_scratch_len, len);
    memcpy(data, s_scratch, len);
    return ESP_OK;
}

esp_err_t onewire_bus_read_bit(onewire_bus_handle_t bus, bool *bit)
{
    (void)bus;
    if (bit) {
        *bit = s_ready;
    }
    return ESP_OK;
}
