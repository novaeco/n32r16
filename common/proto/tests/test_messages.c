#include "messages.h"

#include "unity.h"
#include <string.h>

TEST_CASE("proto encode/decode sensor update json", "[proto]")
{
    proto_sensor_update_t update = {
        .timestamp_ms = 1234,
        .sequence_id = 42,
        .sht20_count = 1,
        .ds18b20_count = 1,
        .pwm = {
            .frequency_hz = 500,
        },
    };
    strcpy(update.sht20[0].id, "SHT20_1");
    update.sht20[0].temperature_c = 25.5f;
    update.sht20[0].humidity_percent = 48.2f;
    memcpy(update.ds18b20[0].rom_code, "ABCDEFGH", 8);
    update.ds18b20[0].temperature_c = 22.75f;
    update.mcp[0].port_a = 0x0F;
    update.mcp[0].port_b = 0xF0;
    update.pwm.duty_cycle[0] = 1234;

    uint8_t buffer[512];
    size_t len = sizeof(buffer);
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(proto_encode_sensor_update_into(&update, false, buffer, &len, &crc));
    TEST_ASSERT_GREATER_THAN(0, len);

    proto_sensor_update_t decoded = {0};
    TEST_ASSERT_TRUE(proto_decode_sensor_update(buffer, len, false, &decoded, crc));
    TEST_ASSERT_EQUAL_UINT32(update.sequence_id, decoded.sequence_id);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, update.sht20[0].temperature_c, decoded.sht20[0].temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, update.ds18b20[0].temperature_c, decoded.ds18b20[0].temperature_c);
    TEST_ASSERT_EQUAL_UINT16(update.mcp[0].port_a, decoded.mcp[0].port_a);
}

TEST_CASE("proto encode/decode command json", "[proto]")
{
    proto_command_t cmd = {
        .timestamp_ms = 4321,
        .sequence_id = 7,
        .has_pwm_update = true,
        .pwm_update = {
            .channel = 2,
            .duty_cycle = 2048,
        },
        .has_pwm_frequency = true,
        .pwm_frequency = 800,
        .has_gpio_write = true,
        .gpio_write = {
            .device_index = 1,
            .port = 1,
            .mask = 0x03,
            .value = 0x02,
        },
    };
    uint8_t buffer[256];
    size_t len = sizeof(buffer);
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(proto_encode_command_into(&cmd, false, buffer, &len, &crc));
    proto_command_t decoded = {0};
    TEST_ASSERT_TRUE(proto_decode_command(buffer, len, false, &decoded, crc));
    TEST_ASSERT_EQUAL_UINT8(cmd.pwm_update.channel, decoded.pwm_update.channel);
    TEST_ASSERT_EQUAL_UINT16(cmd.pwm_frequency, decoded.pwm_frequency);
    TEST_ASSERT_TRUE(decoded.has_gpio_write);
    TEST_ASSERT_EQUAL_UINT8(1, decoded.gpio_write.port);
    TEST_ASSERT_EQUAL_UINT16(cmd.gpio_write.value, decoded.gpio_write.value);
}
