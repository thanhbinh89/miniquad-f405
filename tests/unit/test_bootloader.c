/**
 * @file    test_bootloader.c
 * @brief   Image validation and the bootloader protocol state machine.
 */
#include <string.h>

#include "bl_core.h"
#include "bl_image.h"
#include "bl_policy.h"
#include "bl_image_builder.h"
#include "crc.h"
#include "link.h"
#include "proto.h"
#include "shared_ram.h"
#include "tinytest.h"

#define REGION_SIZE (256u * 1024u)
static uint8_t s_flash[REGION_SIZE];
static uint8_t s_image[REGION_SIZE];

static uint32_t f_erase(uint32_t size)
{
    uint32_t n = (size + 0x3FFFu) & ~0x3FFFu;   /* 16 KB granularity */
    if (n > REGION_SIZE) {
        return 0u;
    }
    memset(s_flash, 0xFF, n);
    return n;
}

static bool f_program(uint32_t off, const uint32_t *w, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = ((const uint8_t *)w)[i];
        if ((s_flash[off + i] & b) != b) {
            return false;
        }
        s_flash[off + i] = b;
    }
    return true;
}

static const bl_flash_ops_t s_ops = { f_erase, f_program, s_flash, REGION_SIZE };

static bl_core_t     s_bl;
static link_packet_t s_rsp;

static uint8_t req(uint8_t id, const void *pl, size_t len)
{
    link_packet_t r;
    memset(&r, 0, sizeof(r));
    r.id = id;
    r.seq = 7;
    r.len = (uint16_t)len;
    if (len) {
        memcpy(r.payload, pl, len);
    }
    CHECK(bl_core_handle(&s_bl, &r, &s_rsp));
    CHECK(s_rsp.id == (id | MSG_REPLY_FLAG));
    CHECK(s_rsp.seq == 7);
    return s_rsp.payload[0];
}

static void test_image_check(void)
{
    uint32_t total = build_test_image(s_image, 20000u, 0x000102u);
    img_info_t info;
    CHECK(bl_image_check(s_image, total, &info));
    CHECK(info.fw_version == 0x000102u);
    s_image[MM_APP_HEADER_SIZE + 5000] ^= 1u;               /* corrupt image */
    CHECK(!bl_image_check(s_image, total, &info));
    CHECK(info.result == IMG_ERR_CRC);
    build_test_image(s_image, 20000u, 1u);
    s_image[0x30] ^= 1u;                                     /* corrupt header */
    CHECK(!bl_image_check(s_image, total, &info));
    CHECK(info.result == IMG_ERR_HEADER_CRC);
    memset(s_image, 0xFF, 1024);
    CHECK(!bl_image_check(s_image, total, &info));
    CHECK(info.result == IMG_ERR_MAGIC);
}

static void test_protocol_flash_cycle(void)
{
    bl_identity_t id = { { 1, 2, 3 }, 0x000100u, 1700000000u, "abcdef12" };
    memset(s_flash, 0xFF, sizeof(s_flash));
    bl_core_init(&s_bl, &s_ops, &id);

    CHECK(req(MSG_HELLO, NULL, 0) == ST_OK);
    msg_hello_rsp_t h;
    memcpy(&h, s_rsp.payload, sizeof(h));
    CHECK(h.mode == DEV_MODE_BOOTLOADER && h.app_valid == 0u);
    CHECK(strcmp(h.git_hash, "abcdef12") == 0);

    /* write before erase -> state error */
    uint8_t wr[4 + 16];
    memset(wr, 0, sizeof(wr));
    CHECK(req(MSG_BL_WRITE, wr, sizeof(wr)) == ST_ERR_STATE);

    uint32_t total = build_test_image(s_image, 40000u, 0x000203u);
    CHECK(req(MSG_BL_ERASE, &total, 4) == ST_OK);
    /* unaligned length rejected */
    CHECK(req(MSG_BL_WRITE, wr, 4 + 6) == ST_ERR_LENGTH);

    for (uint32_t off = 0; off < total; off += BL_WRITE_MAX) {
        uint32_t n = total - off;
        if (n > BL_WRITE_MAX) {
            n = BL_WRITE_MAX;
        }
        uint8_t buf[4 + BL_WRITE_MAX];
        memcpy(buf, &off, 4);
        memcpy(buf + 4, s_image + off, n);
        uint32_t padded = (n + 3u) & ~3u;
        memset(buf + 4 + n, 0xFF, padded - n);
        CHECK(req(MSG_BL_WRITE, buf, 4 + padded) == ST_OK);
    }
    /* beyond the erased range */
    uint32_t far = 0x10000u;
    uint8_t buf2[8];
    memcpy(buf2, &far, 4);
    memset(buf2 + 4, 0, 4);
    CHECK(req(MSG_BL_WRITE, buf2, 8) == ST_ERR_ARG);

    CHECK(req(MSG_BL_VERIFY, NULL, 0) == ST_OK);
    msg_bl_verify_rsp_t v;
    memcpy(&v, s_rsp.payload, sizeof(v));
    CHECK(v.valid == 1u && v.app_version == 0x000203u && v.crc_actual == v.crc_expected);

    /* read back */
    msg_bl_read_req_t rr = { 0x200u, 64u };
    CHECK(req(MSG_BL_READ, &rr, sizeof(rr)) == ST_OK);
    CHECK(s_rsp.len == 5u + 64u);
    CHECK(memcmp(&s_rsp.payload[5], s_image + 0x200, 64) == 0);

    CHECK(req(MSG_BL_BOOT, NULL, 0) == ST_OK);
    CHECK(s_bl.boot_request);

    uint8_t tgt = REBOOT_ROM;
    CHECK(req(MSG_REBOOT, &tgt, 1) == ST_OK);
    CHECK(s_bl.reboot_request && s_bl.reboot_target == REBOOT_ROM);
    CHECK(req(0x3Eu, NULL, 0) == ST_ERR_UNKNOWN);
}

static void test_boot_refused_when_invalid(void)
{
    bl_identity_t id = { { 0, 0, 0 }, 1u, 0u, NULL };
    memset(s_flash, 0xFF, sizeof(s_flash));
    bl_core_init(&s_bl, &s_ops, &id);
    CHECK(req(MSG_BL_BOOT, NULL, 0) == ST_ERR_VERIFY);
    CHECK(!s_bl.boot_request);
    CHECK(req(MSG_BL_VERIFY, NULL, 0) == ST_ERR_VERIFY);
}

static void test_hello_reports_reason(void)
{
    bl_identity_t id = { { 1, 2, 3 }, 0x00010203u, 0u, "abc" };
    bl_core_init(&s_bl, &s_ops, &id);
    s_bl.boot_reason = BLR_CRASH_LOOP;
    CHECK(req(MSG_HELLO, NULL, 0) == ST_OK);
    msg_hello_rsp_t h;
    memcpy(&h, s_rsp.payload, sizeof(h));
    CHECK(h.mode == DEV_MODE_BOOTLOADER);
    CHECK(h.boot_reason == BLR_CRASH_LOOP);
    CHECK(h.board_id == BOARD_ID);
    CHECK(strcmp(h.board_name, BOARD_NAME) == 0);
}

static void test_boot_policy(void)
{
    bl_boot_in_t in = { BL_REQ_NONE, false, true, 0u };
    CHECK(bl_boot_decide(&in) == BLR_NONE);
    in.request = BL_REQ_STAY;
    CHECK(bl_boot_decide(&in) == BLR_REQUEST);
    in.request = BL_REQ_NONE;
    in.app_valid = false;
    CHECK(bl_boot_decide(&in) == BLR_NO_APP);
    in.app_valid = true;
    in.dbl_reset = true;
    CHECK(bl_boot_decide(&in) == BLR_DBL_RESET);
    in.dbl_reset = false;
    in.app_starts = BL_CRASH_LIMIT - 1u;
    CHECK(bl_boot_decide(&in) == BLR_NONE);
    in.app_starts = BL_CRASH_LIMIT;
    CHECK(bl_boot_decide(&in) == BLR_CRASH_LOOP);
    in.request = BL_REQ_STAY;                       /* an update request wins */
    CHECK(bl_boot_decide(&in) == BLR_REQUEST);

    /* reset causes (on the F4 internal resets also set PINRSTF) */
    CHECK(bl_reset_is_external(BL_CSR_PINRSTF));
    CHECK(!bl_reset_is_external(BL_CSR_PINRSTF | BL_CSR_SFTRSTF));
    CHECK(!bl_reset_is_external(BL_CSR_PINRSTF | BL_CSR_IWDGRSTF));
    CHECK(!bl_reset_is_external(BL_CSR_PINRSTF | BL_CSR_PORRSTF));
    CHECK(!bl_reset_is_external(0u));
    CHECK(bl_app_starts_at_entry(2u, BL_CSR_PINRSTF) == 0u);                     /* button */
    CHECK(bl_app_starts_at_entry(2u, BL_CSR_PINRSTF | BL_CSR_PORRSTF) == 0u);    /* power */
    CHECK(bl_app_starts_at_entry(2u, BL_CSR_PINRSTF | BL_CSR_BORRSTF) == 0u);    /* brown-out */
    CHECK(bl_app_starts_at_entry(2u, BL_CSR_PINRSTF | BL_CSR_SFTRSTF) == 2u);    /* fault */
    CHECK(bl_app_starts_at_entry(2u, BL_CSR_PINRSTF | BL_CSR_IWDGRSTF) == 2u);   /* watchdog */
}

int main(void)
{
    RUN(test_hello_reports_reason);
    RUN(test_boot_policy);
    RUN(test_image_check);
    RUN(test_protocol_flash_cycle);
    RUN(test_boot_refused_when_invalid);
    return TT_REPORT();
}
