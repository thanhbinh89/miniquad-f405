/**
 * @file    link.h
 * @brief   Packet framing for the serial link: COBS + CRC-16, 0x00 delimited.
 *
 * Pure C, no hardware access - used by the bootloader, the application, the
 * host emulators and the unit tests.
 */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cobs.h"
#include "proto.h"

#define LINK_RAW_MAX        (2u + LINK_MAX_PAYLOAD + 2u)            /* id seq payload crc */
#define LINK_FRAME_MAX      (COBS_MAX_ENCODED(LINK_RAW_MAX) + 1u)   /* + delimiter        */

typedef struct {
    uint8_t  id;
    uint8_t  seq;
    uint16_t len;
    uint8_t  payload[LINK_MAX_PAYLOAD];
} link_packet_t;

typedef struct {
    uint8_t  buf[LINK_FRAME_MAX];
    size_t   n;
    bool     overflow;
    uint32_t frames_ok;
    uint32_t err_crc;
    uint32_t err_cobs;
    uint32_t err_overflow;
} link_parser_t;

/**
 * Build a complete frame (including the trailing 0x00 delimiter).
 * @param out  buffer of at least LINK_FRAME_MAX bytes
 * @return frame length, or 0 if @p len exceeds LINK_MAX_PAYLOAD.
 */
size_t link_encode(uint8_t id, uint8_t seq, const void *payload, size_t len, uint8_t *out);

void link_parser_init(link_parser_t *p);

/**
 * Feed one received byte.
 * @return true when a complete, CRC-valid packet was decoded into @p out.
 */
bool link_parser_feed(link_parser_t *p, uint8_t byte, link_packet_t *out);

#endif /* LINK_H */
