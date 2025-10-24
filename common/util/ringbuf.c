#include "ringbuf.h"

#include <string.h>

void ringbuf_init(ringbuf_t *rb, uint8_t *buffer, size_t capacity)
{
    rb->buffer = buffer;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}

size_t ringbuf_capacity(const ringbuf_t *rb)
{
    return rb->capacity;
}

size_t ringbuf_size(const ringbuf_t *rb)
{
    if (rb->full) {
        return rb->capacity;
    }

    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }
    return rb->capacity - rb->tail + rb->head;
}

bool ringbuf_is_empty(const ringbuf_t *rb)
{
    return (!rb->full && (rb->head == rb->tail));
}

bool ringbuf_is_full(const ringbuf_t *rb)
{
    return rb->full;
}

static void advance_pointer(ringbuf_t *rb)
{
    if (rb->full) {
        rb->tail = (rb->tail + 1) % rb->capacity;
    }

    rb->head = (rb->head + 1) % rb->capacity;
    rb->full = rb->head == rb->tail;
}

static void retreat_pointer(ringbuf_t *rb)
{
    rb->full = false;
    rb->tail = (rb->tail + 1) % rb->capacity;
}

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t len)
{
    size_t written = 0;
    while (written < len && rb->capacity > 0) {
        rb->buffer[rb->head] = data[written++];
        advance_pointer(rb);
    }
    return written;
}

size_t ringbuf_read(ringbuf_t *rb, uint8_t *data, size_t len)
{
    size_t read = 0;
    while (read < len && !ringbuf_is_empty(rb)) {
        data[read++] = rb->buffer[rb->tail];
        retreat_pointer(rb);
    }
    return read;
}

void ringbuf_reset(ringbuf_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}
