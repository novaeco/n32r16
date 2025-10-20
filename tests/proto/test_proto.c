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

    uint32_t crc = 0;
    char *encoded = NULL;
    TEST_ASSERT_TRUE(proto_encode_sensor_update(payload, 1000, &encoded, &crc));
    TEST_ASSERT_NOT_NULL(encoded);
    TEST_ASSERT_NOT_EQUAL(0, crc);

    proto_envelope_t env;
    TEST_ASSERT_TRUE(proto_decode((const uint8_t *)encoded, strlen(encoded), &env));
    TEST_ASSERT_EQUAL(PROTO_MSG_SENSOR_UPDATE, env.type);
    TEST_ASSERT_EQUAL(1, env.version);

    proto_envelope_free(&env);
    free(encoded);
    cJSON_Delete(payload);
}

