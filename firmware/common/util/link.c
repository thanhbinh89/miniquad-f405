/**
 * @file    link.c
 * @brief   COBS/CRC-16 packet framing.
 */
#include "link.h"

#include <string.h>
#include "crc.h"

size_t link_encode(uint8_t id, uint8_t seq, const void *payload, size_t len, uint8_t *out)
{
    uint8_t raw[LINK_RAW_MAX];

    if (len > LINK_MAX_PAYLOAD) {
        return 0u;
    }
    raw[0] = id;
    raw[1] = seq;
    if (len > 0u) {
        memcpy(&raw[2], payload, len);
    }
    uint16_t crc = crc16(raw, len + 2u);
    raw[len + 2u] = (uint8_t)(crc & 0xFFu);
    raw[len + 3u] = (uint8_t)(crc >> 8);

    size_t n = cobs_encode(raw, len + 4u, out);
    out[n++] = 0u;
    return n;
}

void link_parser_init(link_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

bool link_parser_feed(link_parser_t *p, uint8_t byte, link_packet_t *out)
{
    if (byte != 0u) {
        if (p->n < sizeof(p->buf)) {
            p->buf[p->n++] = byte;
        } else {
            p->overflow = true;
        }
        return false;
    }

    /* delimiter: evaluate the frame collected so far */
    size_t n = p->n;
    bool overflow = p->overflow;
    p->n = 0u;
    p->overflow = false;

    if (n == 0u) {
        return false;                       /* idle / back-to-back delimiters */
    }
    if (overflow) {
        p->err_overflow++;
        return false;
    }

    size_t raw_len = cobs_decode(p->buf, n, p->buf);   /* in place */
    if (raw_len < 4u || raw_len > LINK_RAW_MAX) {
        p->err_cobs++;
        return false;
    }

    uint16_t rx_crc = (uint16_t)(p->buf[raw_len - 2u] | ((uint16_t)p->buf[raw_len - 1u] << 8));
    if (crc16(p->buf, raw_len - 2u) != rx_crc) {
        p->err_crc++;
        return false;
    }

    out->id = p->buf[0];
    out->seq = p->buf[1];
    out->len = (uint16_t)(raw_len - 4u);
    if (out->len > 0u) {
        memcpy(out->payload, &p->buf[2], out->len);
    }
    p->frames_ok++;
    return true;
}
