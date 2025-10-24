#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    bool full;
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb, uint8_t *buffer, size_t capacity);
size_t ringbuf_capacity(const ringbuf_t *rb);
size_t ringbuf_size(const ringbuf_t *rb);
bool ringbuf_is_empty(const ringbuf_t *rb);
bool ringbuf_is_full(const ringbuf_t *rb);
size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t len);
size_t ringbuf_read(ringbuf_t *rb, uint8_t *data, size_t len);
void ringbuf_reset(ringbuf_t *rb);
