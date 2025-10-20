#include "ringbuf.h"

#include <string.h>

bool ringbuf_init(ringbuf_t *rb, uint8_t *storage, size_t size) {
    if (rb == NULL || storage == NULL || size == 0) {
        return false;
    }
    rb->buffer = storage;
    rb->size = size;
    ringbuf_reset(rb);
    return true;
}

size_t ringbuf_capacity(const ringbuf_t *rb) {
    return rb == NULL ? 0 : rb->size;
}

size_t ringbuf_size(const ringbuf_t *rb) {
    if (rb == NULL) {
        return 0;
    }
    if (rb->full) {
        return rb->size;
    }
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }
    return rb->size + rb->head - rb->tail;
}

void ringbuf_reset(ringbuf_t *rb) {
    if (rb == NULL) {
        return;
    }
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}

static void advance_pointer(ringbuf_t *rb, size_t count) {
    if (count >= rb->size) {
        count = rb->size;
    }
    rb->head = (rb->head + count) % rb->size;
    if (rb->full) {
        rb->tail = (rb->tail + count) % rb->size;
    }
    rb->full = rb->head == rb->tail;
}

static void retreat_pointer(ringbuf_t *rb, size_t count) {
    rb->full = false;
    rb->tail = (rb->tail + count) % rb->size;
}

size_t ringbuf_put(ringbuf_t *rb, const uint8_t *data, size_t len) {
    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    size_t space = rb->size - ringbuf_size(rb);
    if (len > space) {
        len = space;
    }
    for (size_t i = 0; i < len; ++i) {
        rb->buffer[rb->head] = data[i];
        advance_pointer(rb, 1);
    }
    return len;
}

size_t ringbuf_get(ringbuf_t *rb, uint8_t *data, size_t len) {
    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }
    size_t stored = ringbuf_size(rb);
    if (len > stored) {
        len = stored;
    }
    for (size_t i = 0; i < len; ++i) {
        data[i] = rb->buffer[rb->tail];
        retreat_pointer(rb, 1);
    }
    return len;
}

