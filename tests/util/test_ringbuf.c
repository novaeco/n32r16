#include "ringbuf.h"

#include "unity.h"

TEST_CASE("ring buffer stores and retrieves data", "[ringbuf]") {
    uint8_t storage[8];
    ringbuf_t rb;
    TEST_ASSERT_TRUE(ringbuf_init(&rb, storage, sizeof(storage)));

    const uint8_t data_in[] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_UINT32(sizeof(data_in), ringbuf_put(&rb, data_in, sizeof(data_in)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data_in), ringbuf_size(&rb));

    uint8_t out[5] = {0};
    TEST_ASSERT_EQUAL_UINT32(sizeof(out), ringbuf_get(&rb, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_in, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(0, ringbuf_size(&rb));
}

TEST_CASE("ring buffer overwrites when full", "[ringbuf]") {
    uint8_t storage[4];
    ringbuf_t rb;
    TEST_ASSERT_TRUE(ringbuf_init(&rb, storage, sizeof(storage)));

    const uint8_t first[] = {10, 11, 12, 13};
    TEST_ASSERT_EQUAL_UINT32(sizeof(first), ringbuf_put(&rb, first, sizeof(first)));
    const uint8_t second[] = {20, 21, 22};
    TEST_ASSERT_EQUAL_UINT32(sizeof(second), ringbuf_put(&rb, second, sizeof(second)));

    uint8_t out[4] = {0};
    TEST_ASSERT_EQUAL_UINT32(sizeof(out), ringbuf_get(&rb, out, sizeof(out)));
    const uint8_t expected[] = {11, 12, 13, 20};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, sizeof(out));
}
