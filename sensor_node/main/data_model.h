#pragma once

#include "common/proto/messages.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "onewire_bus.h"
#include <stdbool.h>

#define SENSOR_DATA_MODEL_MAX_MESSAGE_SIZE 2048U
#define SENSOR_DATA_MODEL_BUFFER_COUNT 2U

/**
 * @brief In-memory representation of the sensor node payload state.
 *
 * The model stores the most recent telemetry frame (`current`), the last
 * published frame (`last_published`) and a pair of double-buffered encoding
 * arenas used to stage serialized payloads for transmission.
 */
typedef struct {
    proto_sensor_update_t current; /**< Current working snapshot populated by tasks. */
    proto_sensor_update_t last_published; /**< Snapshot most recently acknowledged as published. */
    bool initialized; /**< Tracks whether ::data_model_init completed successfully. */
    SemaphoreHandle_t mutex; /**< Lightweight mutex guarding access to the structure. */
    StaticSemaphore_t mutex_storage; /**< Backing storage for #mutex. */
    uint8_t encode_buffers[SENSOR_DATA_MODEL_BUFFER_COUNT][SENSOR_DATA_MODEL_MAX_MESSAGE_SIZE];
    size_t encode_lengths[SENSOR_DATA_MODEL_BUFFER_COUNT]; /**< Length of each staged payload. */
    size_t next_encode_index; /**< Index of the staging buffer to fill on the next build. */
} sensor_data_model_t;

/**
 * @brief Initialise a ::sensor_data_model_t instance and allocate its mutex.
 *
 * The structure is zeroed, an internal FreeRTOS mutex is created, and default
 * PWM configuration values are seeded.
 */
void data_model_init(sensor_data_model_t *model);

/**
 * @brief Store an SHT20 reading inside the model.
 *
 * @param model Target data model, must be initialised.
 * @param index Index of the logical SHT20 sensor (0..1).
 * @param id Human readable identifier copied into the payload.
 * @param temp Temperature in degrees Celsius.
 * @param humidity Relative humidity in percent.
 */
void data_model_set_sht20(sensor_data_model_t *model, size_t index, const char *id, float temp,
                          float humidity);

/**
 * @brief Store a DS18B20 reading inside the model.
 *
 * @param model Target data model, must be initialised.
 * @param index Index of the logical DS18B20 sensor (0..3).
 * @param device Pointer to the 1-Wire device descriptor supplying the ROM code.
 * @param temp Temperature in degrees Celsius.
 */
void data_model_set_ds18b20(sensor_data_model_t *model, size_t index, const onewire_device_t *device,
                            float temp);

/**
 * @brief Update the cached GPIO port states from the MCP23017 expanders.
 */
void data_model_set_gpio(sensor_data_model_t *model, size_t dev_index, uint16_t porta, uint16_t portb);

/**
 * @brief Update PWM duty cycle and frequency telemetry.
 */
void data_model_set_pwm(sensor_data_model_t *model, const uint16_t *duty, size_t count,
                        uint16_t frequency_hz);

/**
 * @brief Record the monotonic timestamp attached to the frame.
 */
void data_model_set_timestamp(sensor_data_model_t *model, uint32_t timestamp_ms);

/**
 * @brief Determine whether the current model state differs sufficiently to publish.
 *
 * @param model Target data model.
 * @param temp_threshold Minimum delta (°C) for temperature-triggered publication.
 * @param humidity_threshold Minimum delta (%) for humidity-triggered publication.
 *
 * @return true if the frame should be transmitted, false otherwise.
 */
bool data_model_should_publish(sensor_data_model_t *model, float temp_threshold, float humidity_threshold);

/**
 * @brief Serialise the current snapshot into the double-buffered staging arenas.
 *
 * @param model Target data model.
 * @param use_cbor Encode as CBOR when true, otherwise JSON.
 * @param out_buf Output pointer to the staged payload buffer.
 * @param out_len Output payload length in bytes.
 * @param crc32 Optional pointer receiving the computed CRC32.
 *
 * @return true when encoding succeeds and the staging buffer has been updated.
 */
bool data_model_build(sensor_data_model_t *model, bool use_cbor, uint8_t **out_buf, size_t *out_len,
                      uint32_t *crc32);

/**
 * @brief Increment the monotonic sequence counter embedded in the payload.
 */
void data_model_increment_seq(sensor_data_model_t *model);

