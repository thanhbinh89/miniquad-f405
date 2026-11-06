/**
 * @file    spsc.h
 * @brief   Typed lock-free single-producer / single-consumer ring buffer.
 *
 * SPSC_DEFINE(name, type, size) defines name_t plus name_push() (producer,
 * e.g. an interrupt) and name_pop() (consumer, e.g. the main loop). One slot
 * is kept free; size must be a power of two.
 */
#ifndef SPSC_H
#define SPSC_H

#include <stdbool.h>
#include <stdint.h>

#define SPSC_DEFINE(name, type, size)                                              \
    _Static_assert(((size) & ((size) - 1u)) == 0u, #name " size: power of two");   \
    typedef struct {                                                               \
        type              buf[size];                                              \
        volatile uint32_t head;                                                   \
        volatile uint32_t tail;                                                   \
        volatile uint32_t dropped;                                                \
    } name##_t;                                                                    \
    __attribute__((unused)) static inline bool name##_push(name##_t *r, const type *v) \
    {                                                                              \
        uint32_t h = r->head;                                                      \
        uint32_t n = (h + 1u) & ((size) - 1u);                                     \
        if (n == r->tail) {                                                        \
            r->dropped++;                                                          \
            return false;                                                          \
        }                                                                          \
        r->buf[h] = *v;                                                            \
        __sync_synchronize();                                                      \
        r->head = n;                                                               \
        return true;                                                               \
    }                                                                              \
    __attribute__((unused)) static inline bool name##_pop(name##_t *r, type *v)  \
    {                                                                              \
        uint32_t t = r->tail;                                                      \
        if (t == r->head) {                                                        \
            return false;                                                          \
        }                                                                          \
        *v = r->buf[t];                                                            \
        __sync_synchronize();                                                      \
        r->tail = (t + 1u) & ((size) - 1u);                                        \
        return true;                                                               \
    }                                                                              \
    __attribute__((unused)) static inline void name##_clear(name##_t *r)         \
    {                                                                              \
        r->tail = r->head;                                                         \
    }

#endif /* SPSC_H */
