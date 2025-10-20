#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t head;
    size_t tail;
    bool full;
} ringbuf_t;

bool ringbuf_init(ringbuf_t *rb, uint8_t *storage, size_t size);
size_t ringbuf_capacity(const ringbuf_t *rb);
size_t ringbuf_size(const ringbuf_t *rb);
void ringbuf_reset(ringbuf_t *rb);
size_t ringbuf_put(ringbuf_t *rb, const uint8_t *data, size_t len);
size_t ringbuf_get(ringbuf_t *rb, uint8_t *data, size_t len);

