/**
 * @file    crc.h
 * @brief   Small-footprint CRC-16/CCITT-FALSE and CRC-32 (zlib) routines.
 *
 * Nibble-table implementations (16-entry tables) keep the bootloader small
 * while still running at a few cycles per bit.
 *
 * Check values ("123456789"): crc16 = 0x29B1, crc32 = 0xCBF43926.
 */
#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT      0xFFFFu
#define CRC32_INIT      0xFFFFFFFFu

/** CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xor-out). */
uint16_t crc16_update(uint16_t crc, const void *data, size_t len);

/** Running CRC-32 update; start with CRC32_INIT, finish with crc32_final(). */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

static inline uint32_t crc32_final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

/** One-shot CRC-32 compatible with zlib / binascii.crc32 / JS implementations. */
static inline uint32_t crc32(const void *data, size_t len)
{
    return crc32_final(crc32_update(CRC32_INIT, data, len));
}

static inline uint16_t crc16(const void *data, size_t len)
{
    return crc16_update(CRC16_INIT, data, len);
}

#endif /* CRC_H */
