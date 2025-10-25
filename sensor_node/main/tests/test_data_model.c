#include "data_model.h"

#include "unity.h"

#include <stdint.h>

static void seed_baseline(sensor_data_model_t *model)
{
    uint16_t duty[16] = {0};
    onewire_device_t device = {0};

    data_model_set_sht20(model, 0, "SHT20_A", 20.0f, 50.0f, true);
    data_model_set_sht20(model, 1, "SHT20_B", 21.5f, 48.5f, true);
    data_model_set_ds18b20(model, 0, &device, 19.75f);
    data_model_set_gpio(model, 0, 0xAAAA, 0x5555);
    data_model_set_pwm(model, duty, 16, 500);
    data_model_set_timestamp(model, 1000);
    data_model_increment_seq(model);
}

TEST_CASE("data_model rotates staging buffers", "[data_model]")
{
    sensor_data_model_t model;
    data_model_init(&model);

    for (int i = 0; i < 3; ++i) {
        seed_baseline(&model);
        onewire_device_t device = {0};
        data_model_set_ds18b20(&model, 0, &device, 19.75f + (float)i);

        size_t buffer_index = model.next_encode_index;
        uint8_t *payload = NULL;
        size_t payload_len = 0;
        uint32_t crc = 0;

        TEST_ASSERT_TRUE(data_model_build(&model, false, &payload, &payload_len, &crc));
        TEST_ASSERT_NOT_NULL(payload);
        TEST_ASSERT_TRUE(payload_len > 0);
        TEST_ASSERT_EQUAL_PTR(model.encode_buffers[buffer_index], payload);
        TEST_ASSERT_EQUAL(payload_len, model.encode_lengths[buffer_index]);
        TEST_ASSERT_EQUAL((buffer_index + 1U) % SENSOR_DATA_MODEL_BUFFER_COUNT, model.next_encode_index);
    }
}

TEST_CASE("data_model publish thresholding", "[data_model]")
{
    sensor_data_model_t model;
    data_model_init(&model);

    seed_baseline(&model);

    uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(data_model_build(&model, false, &payload, &payload_len, &crc));

    TEST_ASSERT_FALSE(data_model_should_publish(&model, 0.5f, 2.0f));

    data_model_set_sht20(&model, 0, "SHT20_A", 20.0f, 53.5f, true);
    TEST_ASSERT_TRUE(data_model_should_publish(&model, 0.5f, 2.0f));

    data_model_set_timestamp(&model, 2000);
    data_model_increment_seq(&model);
    TEST_ASSERT_TRUE(data_model_build(&model, false, &payload, &payload_len, &crc));

    onewire_device_t device = {0};
    data_model_set_ds18b20(&model, 0, &device, 20.1f);
    TEST_ASSERT_FALSE(data_model_should_publish(&model, 0.5f, 2.0f));

    data_model_set_ds18b20(&model, 0, &device, 21.0f);
    TEST_ASSERT_TRUE(data_model_should_publish(&model, 0.5f, 2.0f));
}

