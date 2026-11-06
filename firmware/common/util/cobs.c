/**
 * @file    cobs.c
 * @brief   COBS encoder / decoder.
 */
#include "cobs.h"

size_t cobs_encode(const uint8_t *src, size_t len, uint8_t *dst)
{
    size_t  out = 1u;      /* next write position             */
    size_t  code_pos = 0u; /* position of the current code byte */
    uint8_t code = 1u;

    for (size_t i = 0u; i < len; i++) {
        if (src[i] == 0u) {
            dst[code_pos] = code;
            code_pos = out++;
            code = 1u;
        } else {
            dst[out++] = src[i];
            code++;
            if (code == 0xFFu) {
                dst[code_pos] = code;
                code_pos = out++;
                code = 1u;
            }
        }
    }
    dst[code_pos] = code;
    return out;
}

size_t cobs_decode(const uint8_t *src, size_t len, uint8_t *dst)
{
    size_t in = 0u;
    size_t out = 0u;

    while (in < len) {
        uint8_t code = src[in++];
        if (code == 0u) {
            return 0u;                      /* delimiter inside frame */
        }
        size_t run = (size_t)code - 1u;
        if (in + run > len) {
            return 0u;                      /* truncated */
        }
        for (size_t k = 0u; k < run; k++) {
            uint8_t b = src[in++];
            if (b == 0u) {
                return 0u;
            }
            dst[out++] = b;
        }
        /* A code < 0xFF implies a zero byte, except after the last block. */
        if (code != 0xFFu && in < len) {
            dst[out++] = 0u;
        }
    }
    return out;
}
