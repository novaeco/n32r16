#include "unity.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "proto_codec.h"

TEST_CASE("proto encode/decode sensor update", "[proto]") {
    cJSON *payload = cJSON_CreateObject();
    cJSON *sht = cJSON_AddArrayToObject(payload, "sht20");
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "id", "SHT20_1");
    cJSON_AddNumberToObject(entry, "t", 23.5);
    cJSON_AddNumberToObject(entry, "rh", 55.1);
    cJSON_AddItemToArray(sht, entry);

    proto_buffer_t encoded = {0};
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(proto_encode_sensor_update(payload, 1000, 1, &encoded, &crc));
    TEST_ASSERT_NOT_EQUAL(0, crc);
    TEST_ASSERT_NOT_NULL(encoded.data);

    proto_envelope_t env;
    TEST_ASSERT_TRUE(proto_decode(encoded.data, encoded.len, &env));
    TEST_ASSERT_EQUAL(PROTO_MSG_SENSOR_UPDATE, env.type);
    TEST_ASSERT_TRUE(env.crc_ok);
    TEST_ASSERT_EQUAL(1, env.seq_id);

    proto_envelope_free(&env);
    proto_buffer_free(&encoded);
    cJSON_Delete(payload);
}

