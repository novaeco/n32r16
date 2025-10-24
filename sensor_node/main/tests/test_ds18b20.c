#include "drivers/ds18b20.h"

#include "onewire_mock.h"
#include "unity.h"

TEST_CASE("ds18b20 conversion timing", "[ds18b20]")
{
    TEST_ASSERT_EQUAL_UINT32(94, ds18b20_conversion_time_ms(9));
    TEST_ASSERT_EQUAL_UINT32(188, ds18b20_conversion_time_ms(10));
    TEST_ASSERT_EQUAL_UINT32(375, ds18b20_conversion_time_ms(11));
    TEST_ASSERT_EQUAL_UINT32(750, ds18b20_conversion_time_ms(12));
}

TEST_CASE("ds18b20 decodes temperature", "[ds18b20]")
{
    uint8_t scratch[9] = {0x50, 0x05, 0, 0, 0, 0, 0, 0, 0};
    onewire_mock_set_scratch(scratch, sizeof(scratch));
    onewire_mock_set_ready(true);

    onewire_device_t device = {0};
    float temp = 0.0f;
    TEST_ASSERT_EQUAL(ESP_OK, ds18b20_read_temperature((onewire_bus_handle_t)1, &device, &temp));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 85.0f, temp);
}
