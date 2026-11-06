/**
 * @file    cobs.h
 * @brief   Consistent Overhead Byte Stuffing (COBS).
 *
 * Encoded data never contains 0x00, so 0x00 is used as frame delimiter on the
 * serial link. Worst-case overhead is 1 byte per 254 bytes (+1).
 */
#ifndef COBS_H
#define COBS_H

#include <stddef.h>
#include <stdint.h>

/** Maximum encoded size for @p n input bytes (without the 0x00 delimiter). */
#define COBS_MAX_ENCODED(n)     ((n) + ((n) / 254u) + 1u)

/**
 * Encode @p len bytes from @p src into @p dst.
 * @return number of bytes written (no trailing delimiter).
 * @note   @p dst must hold at least COBS_MAX_ENCODED(len) bytes and must not
 *         overlap @p src.
 */
size_t cobs_encode(const uint8_t *src, size_t len, uint8_t *dst);

/**
 * Decode @p len COBS bytes (delimiter excluded). Decoding in place
 * (dst == src) is allowed.
 * @return decoded length, or 0 if the input is malformed or contains 0x00.
 */
size_t cobs_decode(const uint8_t *src, size_t len, uint8_t *dst);

#endif /* COBS_H */
