/**
 * @file    test_link.c
 * @brief   CRC, COBS and packet framing.
 */
#include <string.h>

#include "cobs.h"
#include "crc.h"
#include "link.h"
#include "tinytest.h"

static void test_crc_check_values(void)
{
    const char *s = "123456789";
    CHECK(crc16(s, 9) == 0x29B1u);
    CHECK(crc32(s, 9) == 0xCBF43926u);
    /* incremental == one shot */
    uint32_t c = crc32_update(CRC32_INIT, s, 4);
    c = crc32_update(c, s + 4, 5);
    CHECK(crc32_final(c) == 0xCBF43926u);
}

static void roundtrip(const uint8_t *data, size_t n)
{
    uint8_t enc[COBS_MAX_ENCODED(600)];
    uint8_t dec[600];
    size_t e = cobs_encode(data, n, enc);
    CHECK(e <= COBS_MAX_ENCODED(n));
    for (size_t i = 0; i < e; i++) {
        CHECK(enc[i] != 0u);
    }
    size_t d = cobs_decode(enc, e, dec);
    CHECK(d == n);
    CHECK(memcmp(dec, data, n) == 0);
}

static void test_cobs(void)
{
    uint8_t buf[600];
    uint32_t r = 1u;
    for (size_t n = 1; n < 600; n += 7) {
        for (size_t i = 0; i < n; i++) {
            r = r * 1103515245u + 12345u;
            buf[i] = (uint8_t)((r >> 16) % 5u == 0u ? 0u : (r >> 8));
        }
        roundtrip(buf, n);
    }
    /* boundaries: 254 / 255 non-zero bytes, all zeros */
    memset(buf, 0x11, sizeof(buf));
    roundtrip(buf, 253);
    roundtrip(buf, 254);
    roundtrip(buf, 255);
    roundtrip(buf, 508);
    memset(buf, 0, 300);
    roundtrip(buf, 300);
    /* malformed input */
    uint8_t bad[3] = { 5u, 1u, 2u };
    CHECK(cobs_decode(bad, 3, buf) == 0u);
}

static void test_link_frames(void)
{
    uint8_t frame[LINK_FRAME_MAX];
    uint8_t payload[LINK_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 7u);
    }
    link_parser_t p;
    link_packet_t pkt;
    link_parser_init(&p);

    size_t n = link_encode(0x23, 42, payload, sizeof(payload), frame);
    CHECK(n > 0u && n <= LINK_FRAME_MAX);
    CHECK(frame[n - 1] == 0u);
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        got = link_parser_feed(&p, frame[i], &pkt);
    }
    CHECK(got);
    CHECK(pkt.id == 0x23 && pkt.seq == 42 && pkt.len == LINK_MAX_PAYLOAD);
    CHECK(memcmp(pkt.payload, payload, pkt.len) == 0);

    /* too long */
    CHECK(link_encode(1, 1, payload, LINK_MAX_PAYLOAD + 1u, frame) == 0u);

    /* corrupted byte -> CRC or COBS error, parser recovers on the next frame */
    n = link_encode(0x01, 7, "abc", 3, frame);
    frame[2] ^= 0x40u;
    got = false;
    for (size_t i = 0; i < n; i++) {
        got |= link_parser_feed(&p, frame[i], &pkt);
    }
    CHECK(!got);
    CHECK(p.err_crc + p.err_cobs >= 1u);
    n = link_encode(0x01, 8, NULL, 0, frame);
    got = false;
    for (size_t i = 0; i < n; i++) {
        got |= link_parser_feed(&p, frame[i], &pkt);
    }
    CHECK(got && pkt.seq == 8 && pkt.len == 0u);

    /* garbage longer than a frame -> overflow, then recovery */
    for (int i = 0; i < 1000; i++) {
        (void)link_parser_feed(&p, 0x55, &pkt);
    }
    (void)link_parser_feed(&p, 0x00, &pkt);
    CHECK(p.err_overflow == 1u);
    n = link_encode(0x02, 9, "x", 1, frame);
    got = false;
    for (size_t i = 0; i < n; i++) {
        got |= link_parser_feed(&p, frame[i], &pkt);
    }
    CHECK(got && pkt.id == 0x02);
}

/* Reference vector shared with web/test/proto.test.mjs (computed independently in Python) */
static void test_reference_frame(void)
{
    uint8_t frame[LINK_FRAME_MAX];
    const uint8_t pl[4] = { 0x00, 0x01, 0x02, 0x00 };
    const uint8_t expect[10] = { 0x03, 0x23, 0x05, 0x03, 0x01, 0x02, 0x03, 0xFD, 0x87, 0x00 };
    size_t n = link_encode(0x23, 0x05, pl, sizeof(pl), frame);
    CHECK(n == sizeof(expect));
    CHECK(memcmp(frame, expect, sizeof(expect)) == 0);
}

int main(void)
{
    RUN(test_crc_check_values);
    RUN(test_cobs);
    RUN(test_link_frames);
    RUN(test_reference_frame);
    return TT_REPORT();
}
