/**
 * @file    test_emu.c
 * @brief   End-to-end flows through the device emulator: app <-> bootloader
 *          <-> ROM bootloader, firmware update, parameter persistence and a
 *          short flight with the virtual transmitter.
 *
 * Built for both board revisions (test_emu = MQ_BOARD_REV of the build,
 * test_emu_vN = the other one). On rev 2 the USB serial port disappears on
 * every MCU reset: the tests wait for it like the configurator does.
 */
#include <string.h>

#include "app_header.h"
#include "bl_image_builder.h"
#include "board.h"
#include "emu_device.h"
#include "link.h"
#include "memory_map.h"
#include "proto.h"
#include "tinytest.h"

static link_parser_t s_parser;
static link_packet_t s_pkt;
static uint8_t s_seq;

static void flush_rx(void)
{
    uint8_t b[4096];
    while (emu_tx(b, sizeof(b)) > 0u) {
    }
    link_parser_init(&s_parser);
}

/** Send a request and wait (emulated time) for its reply. */
static const link_packet_t *req(uint8_t id, const void *pl, size_t len, uint32_t timeout_ms)
{
    uint8_t frame[LINK_FRAME_MAX];
    s_seq++;
    size_t n = link_encode(id, s_seq, pl, len, frame);
    emu_rx(frame, n);
    for (uint32_t t = 0; t < timeout_ms; t++) {
        emu_run(1);
        uint8_t b[512];
        size_t k;
        while ((k = emu_tx(b, sizeof(b))) > 0u) {
            for (size_t i = 0; i < k; i++) {
                if (link_parser_feed(&s_parser, b[i], &s_pkt) && s_pkt.id == (uint8_t)(id | 0x80u) &&
                    s_pkt.seq == s_seq) {
                    return &s_pkt;
                }
            }
        }
    }
    return NULL;
}

static msg_hello_rsp_t hello(void)
{
    msg_hello_rsp_t h;
    memset(&h, 0xEE, sizeof(h));
    const link_packet_t *r = req(MSG_HELLO, NULL, 0, 100);
    if (r != NULL && r->len == sizeof(h)) {
        memcpy(&h, r->payload, sizeof(h));
    }
    return h;
}

static uint8_t status_of(const link_packet_t *r)
{
    return (r == NULL) ? 0xFFu : r->payload[0];
}

/** Wait until the host has the serial port again (rev 2 re-enumeration). */
static bool wait_link(uint32_t timeout_ms)
{
    for (uint32_t t = 0; t < timeout_ms; t++) {
        if (emu_link_up()) {
            flush_rx();
            return true;
        }
        emu_run(1);
    }
    return emu_link_up();
}

/** A request right after a reset/reboot: wait for the port first. */
static msg_hello_rsp_t hello_after_reset(void)
{
    if (BOARD_LINK_USB) {
        CHECK(!emu_link_up());              /* the port went away with the reset */
    }
    CHECK(wait_link(2000));
    return hello();
}

/* ------------------------------------------------------------------------ */
static void test_boot_and_hello(void)
{
    printf("    board rev %d (%s)\n", emu_board_rev(), BOARD_LINK_USB ? "USB CDC" : "UART J2");
    emu_init(true);
    CHECK(wait_link(1000));
    CHECK(emu_mode() == EMU_MODE_APP);
    msg_hello_rsp_t h = hello();
    CHECK(h.status == ST_OK && h.mode == DEV_MODE_APP);
    CHECK(h.app_valid == 1u);
    CHECK(h.board_id == BOARD_ID);
    CHECK(h.boot_reason == BLR_NONE);
    CHECK(strcmp(h.board_name, BOARD_NAME) == 0);

    /* power-on: 300 ms boot window first */
    emu_init(false);
    flush_rx();
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
    emu_run(150);
    CHECK(emu_link_up() == !BOARD_LINK_USB);    /* rev 2: no USB in a normal boot */
    emu_run(160);
    CHECK(emu_mode() == EMU_MODE_APP);
    CHECK(wait_link(1000));

    uint8_t t = REBOOT_NORMAL;
    if (!BOARD_LINK_USB) {
        /* rev 1: a packet inside the window keeps the bootloader */
        emu_reset_pin();
        flush_rx();
        emu_run(50);
        h = hello();
        CHECK(h.mode == DEV_MODE_BOOTLOADER && h.boot_reason == BLR_HOST);
        emu_run(1000);
        CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
        CHECK(status_of(req(MSG_REBOOT, &t, 1, 100)) == ST_OK);
        emu_run(20);
        CHECK(emu_mode() == EMU_MODE_APP);
    }

    /* reset pressed twice: the bootloader stays (and enumerates on rev 2) */
    emu_reset_pin();
    emu_run(100);
    emu_reset_pin();
    emu_run(10);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
    h = hello_after_reset();
    CHECK(h.mode == DEV_MODE_BOOTLOADER && h.boot_reason == BLR_DBL_RESET && h.app_valid == 1u);
    emu_run(2000);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);   /* no idle timeout for a deliberate stay */
    CHECK(status_of(req(MSG_REBOOT, &t, 1, 100)) == ST_OK);
    emu_run(20);
    CHECK(emu_mode() == EMU_MODE_APP);
    h = hello_after_reset();
    CHECK(h.mode == DEV_MODE_APP);
}

static void test_crash_loop(void)
{
    emu_init(false);
    emu_run(400);
    CHECK(emu_mode() == EMU_MODE_APP);
    /* the app faults soon after every start: after 3 unconfirmed starts
     * the bootloader keeps the board */
    int faults = 0;
    while (emu_mode() == EMU_MODE_APP && faults < 6) {
        emu_run(500);
        emu_inject_fault();
        faults++;
        emu_run(320);                       /* through the boot window */
    }
    CHECK(faults == 3);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
    msg_hello_rsp_t h = hello_after_reset();
    CHECK(h.mode == DEV_MODE_BOOTLOADER && h.boot_reason == BLR_CRASH_LOOP);
    emu_run(1000);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);

    /* the reset button starts it again (the user is in control) */
    emu_reset_pin();
    emu_run(320);
    CHECK(emu_mode() == EMU_MODE_APP);

    /* an app that runs past the confirmation is not counted */
    for (int i = 0; i < 4; i++) {
        emu_run(5200);
        emu_inject_fault();
        emu_run(320);
        CHECK(emu_mode() == EMU_MODE_APP);
    }
    /* nor a deliberate reboot */
    CHECK(wait_link(1000));
    for (int i = 0; i < 4; i++) {
        uint8_t t = REBOOT_NORMAL;
        CHECK(status_of(req(MSG_REBOOT, &t, 1, 200)) == ST_OK);
        emu_run(400);
        CHECK(emu_mode() == EMU_MODE_APP);
        CHECK(wait_link(1000));
    }
}

static void test_firmware_update(void)
{
    emu_init(true);
    CHECK(wait_link(1000));
    uint8_t t = REBOOT_BOOTLOADER;
    CHECK(status_of(req(MSG_REBOOT, &t, 1, 100)) == ST_OK);
    emu_run(150);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
    flush_rx();
    msg_hello_rsp_t h = hello_after_reset();
    CHECK(h.mode == DEV_MODE_BOOTLOADER && h.app_valid == 1u && h.boot_reason == BLR_REQUEST);

    static uint8_t img[MM_APP_HEADER_SIZE + 20000];
    uint32_t total = build_test_image(img, 20000u, 0x00020304u);
    uint32_t size = total;
    /* erase: 16 + 64 KB sectors -> 0.8 s, the reply comes afterwards */
    const link_packet_t *r = req(MSG_BL_ERASE, &size, 4, 2000);
    CHECK(status_of(r) == ST_OK);
    for (uint32_t off = 0; off < total; off += BL_WRITE_MAX) {
        uint8_t pl[4 + BL_WRITE_MAX];
        uint32_t n = total - off;
        n = (n > BL_WRITE_MAX) ? BL_WRITE_MAX : n;
        uint32_t padded = (n + 3u) & ~3u;
        memset(pl, 0xFF, sizeof(pl));
        memcpy(pl, &off, 4);
        memcpy(pl + 4, img + off, n);
        r = req(MSG_BL_WRITE, pl, 4u + padded, 100);
        if (status_of(r) != ST_OK) {
            CHECK(status_of(r) == ST_OK);
            break;
        }
    }
    r = req(MSG_BL_VERIFY, NULL, 0, 100);
    CHECK(status_of(r) == ST_OK);
    msg_bl_verify_rsp_t v;
    memcpy(&v, r->payload, sizeof(v));
    CHECK(v.valid == 1u && v.app_version == 0x00020304u);
    CHECK(v.crc_expected == v.crc_actual);

    /* read back a block */
    uint8_t rq[6];
    uint32_t off = 512u;
    uint16_t len = 64u;
    memcpy(rq, &off, 4);
    memcpy(rq + 4, &len, 2);
    r = req(MSG_BL_READ, rq, 6, 100);
    CHECK(status_of(r) == ST_OK && r->len == 5u + 64u && memcmp(&r->payload[5], img + 512, 64) == 0);

    r = req(MSG_BL_BOOT, NULL, 0, 100);
    CHECK(status_of(r) == ST_OK);
    emu_run(20);
    CHECK(emu_mode() == EMU_MODE_APP);
    flush_rx();
    h = hello_after_reset();
    CHECK(h.mode == DEV_MODE_APP && h.app_version == 0x00020304u);

    /* a broken image keeps the board in the bootloader */
    t = REBOOT_BOOTLOADER;
    CHECK(status_of(req(MSG_REBOOT, &t, 1, 100)) == ST_OK);
    emu_run(150);
    CHECK(wait_link(2000));
    size = 1024u;
    CHECK(status_of(req(MSG_BL_ERASE, &size, 4, 2000)) == ST_OK);
    emu_power_cycle();
    emu_run(1000);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
    CHECK(wait_link(1000));
    h = hello();
    CHECK(h.mode == DEV_MODE_BOOTLOADER && h.app_valid == 0u && h.boot_reason == BLR_NO_APP);
}

/* ---- AN3155 helpers ---- */
static int rom_xfer(const uint8_t *tx, size_t n, uint8_t *rx, size_t want, uint32_t timeout)
{
    emu_rx(tx, n);
    size_t got = 0;
    for (uint32_t t = 0; t < timeout && got < want; t++) {
        emu_run(1);
        got += emu_tx(rx + got, want - got);
    }
    return (int)got;
}

static void test_rom_bootloader(void)
{
    emu_init(true);
    CHECK(wait_link(1000));
    uint8_t t = REBOOT_ROM;
    CHECK(status_of(req(MSG_REBOOT, &t, 1, 100)) == ST_OK);
    emu_run(150);
    CHECK(emu_mode() == EMU_MODE_ROM);
    flush_rx();
    if (BOARD_LINK_USB) {
        /* rev 2: the ROM loader enumerates as USB DFU, the serial port is gone */
        emu_run(2000);
        CHECK(!emu_link_up());
        emu_power_cycle();
        emu_run(400);
        CHECK(emu_mode() == EMU_MODE_APP);
        CHECK(wait_link(1000));
        emu_set_boot0(true);
        emu_power_cycle();
        emu_run(1000);
        CHECK(emu_mode() == EMU_MODE_ROM && !emu_link_up());
        emu_set_boot0(false);
        emu_power_cycle();
        emu_run(400);
        CHECK(emu_mode() == EMU_MODE_APP);
        return;
    }

    uint8_t rx[300];
    uint8_t sync = 0x7F;
    CHECK(rom_xfer(&sync, 1, rx, 1, 50) == 1 && rx[0] == 0x79);
    const uint8_t get_id[2] = { 0x02, 0xFD };
    CHECK(rom_xfer(get_id, 2, rx, 5, 50) == 5);
    CHECK(rx[0] == 0x79 && rx[1] == 1 && rx[2] == 0x04 && rx[3] == 0x13 && rx[4] == 0x79);
    const uint8_t bad[2] = { 0x02, 0x00 };
    CHECK(rom_xfer(bad, 2, rx, 1, 50) == 1 && rx[0] == 0x1F);

    /* read 16 bytes of the app header */
    const uint8_t rd[2] = { 0x11, 0xEE };
    CHECK(rom_xfer(rd, 2, rx, 1, 50) == 1 && rx[0] == 0x79);
    const uint8_t addr[5] = { 0x08, 0x00, 0xC0, 0x00, 0x08 ^ 0x00 ^ 0xC0 ^ 0x00 };
    CHECK(rom_xfer(addr, 5, rx, 1, 50) == 1 && rx[0] == 0x79);
    const uint8_t cnt[2] = { 15, (uint8_t)~15 };
    CHECK(rom_xfer(cnt, 2, rx, 17, 50) == 17 && rx[0] == 0x79);
    CHECK(memcmp(&rx[1], &emu_flash()[MM_APP_BASE - MM_BL_BASE], 16) == 0);

    /* erase sector 7, write 8 bytes, read them back */
    const uint8_t er[2] = { 0x44, 0xBB };
    CHECK(rom_xfer(er, 2, rx, 1, 50) == 1 && rx[0] == 0x79);
    const uint8_t er_list[5] = { 0x00, 0x00, 0x00, 0x07, 0x07 };
    CHECK(rom_xfer(er_list, 5, rx, 1, 3000) == 1 && rx[0] == 0x79);
    const uint8_t wr[2] = { 0x31, 0xCE };
    CHECK(rom_xfer(wr, 2, rx, 1, 50) == 1 && rx[0] == 0x79);
    const uint8_t waddr[5] = { 0x08, 0x06, 0x00, 0x00, 0x08 ^ 0x06 };
    CHECK(rom_xfer(waddr, 5, rx, 1, 50) == 1 && rx[0] == 0x79);
    uint8_t wdata[11] = { 7, 1, 2, 3, 4, 5, 6, 7, 8, 0 };
    for (int i = 0; i < 9; i++) {
        wdata[9] ^= wdata[i];
    }
    CHECK(rom_xfer(wdata, 10, rx, 1, 50) == 1 && rx[0] == 0x79);
    CHECK(memcmp(&emu_flash()[0x60000], &wdata[1], 8) == 0);

    /* GO 0x08000000 -> our bootloader -> application */
    const uint8_t go[2] = { 0x21, 0xDE };
    CHECK(rom_xfer(go, 2, rx, 1, 50) == 1 && rx[0] == 0x79);
    const uint8_t gaddr[5] = { 0x08, 0x00, 0x00, 0x00, 0x08 };
    CHECK(rom_xfer(gaddr, 5, rx, 1, 50) == 1 && rx[0] == 0x79);
    CHECK(emu_mode() == EMU_MODE_BOOTLOADER);
    emu_run(400);
    CHECK(emu_mode() == EMU_MODE_APP);

    /* BOOT0 jumper: always the ROM loader */
    emu_set_boot0(true);
    emu_power_cycle();
    CHECK(emu_mode() == EMU_MODE_ROM);
    emu_set_boot0(false);
    emu_power_cycle();
    emu_run(400);
    CHECK(emu_mode() == EMU_MODE_APP);
}

static void test_params_persist(void)
{
    emu_init(true);
    CHECK(wait_link(1000));
    msg_param_info_rsp_t info;
    const link_packet_t *r = req(MSG_PARAM_INFO, NULL, 0, 100);
    CHECK(status_of(r) == ST_OK);
    memcpy(&info, r->payload, sizeof(info));
    CHECK(info.count > 50u);

    /* find "pid_roll_p" */
    int idx = -1;
    for (uint16_t i = 0; i < info.count && idx < 0; i++) {
        r = req(MSG_PARAM_DESC, &i, 2, 100);
        msg_param_desc_rsp_t d;
        memcpy(&d, r->payload, sizeof(d));
        if (strcmp(d.name, "pid_roll_p") == 0) {
            idx = i;
        }
    }
    CHECK(idx >= 0);
    float v = 71.5f;
    uint8_t set[6];
    set[0] = (uint8_t)idx;
    set[1] = (uint8_t)(idx >> 8);
    memcpy(&set[2], &v, 4);
    CHECK(status_of(req(MSG_PARAM_SET, set, 6, 100)) == ST_OK);
    /* first save erases a 16 KB bank: ~0.27 s */
    CHECK(status_of(req(MSG_PARAM_SAVE, NULL, 0, 1000)) == ST_OK);

    emu_power_cycle();
    emu_run(400);
    CHECK(emu_mode() == EMU_MODE_APP);
    CHECK(wait_link(1000));
    uint8_t q[3] = { (uint8_t)idx, (uint8_t)(idx >> 8), 1 };
    r = req(MSG_PARAM_GET, q, 3, 100);
    CHECK(status_of(r) == ST_OK);
    float got;
    memcpy(&got, &r->payload[4], 4);
    CHECK_NEAR(got, 71.5, 1e-6);
}

static void test_usb_cable(void)
{
    if (!BOARD_LINK_USB) {
        return;
    }
    emu_init(true);
    CHECK(wait_link(1000));
    uint8_t s[3] = { STREAM_STATUS, 0, 0 };
    const link_packet_t *r = req(MSG_STATUS, NULL, 0, 100);
    CHECK(status_of(r) == ST_OK);
    tlm_status_t st;
    memcpy(&st, &r->payload[1], sizeof(st));
    CHECK((st.flags & STF_USB) != 0u);
    CHECK((st.flags & STF_BAT_PRESENT) != 0u);

    uint32_t epoch = emu_link_epoch();
    emu_usb_cable(false);
    emu_run(5);
    CHECK(!emu_link_up() && emu_link_epoch() == epoch + 1u);
    emu_run(1000);
    CHECK(emu_mode() == EMU_MODE_APP);      /* the quad does not care */
    emu_usb_cable(true);
    emu_run(100);
    CHECK(!emu_link_up());                  /* enumeration takes a moment */
    CHECK(wait_link(1000));
    CHECK(status_of(req(MSG_STREAM, s, 3, 100)) == ST_OK);
}

static void test_flight(void)
{
    emu_init(true);
    CHECK(wait_link(1000));
    emu_run(3000);                          /* gyro calibration while still */
    emu_rc_set(4, 2000);                    /* arm */
    emu_run(500);
    float tr[14];
    emu_truth(tr, 14);
    CHECK(tr[8] == 1.0f);
    emu_rc_set(2, 1600);                    /* climb */
    emu_run(1000);
    emu_truth(tr, 14);
    printf("    climb: h %.2f m, roll %.1f, pitch %.1f, vbat %.2f V, %.2f A\n", tr[2], tr[3], tr[4], tr[6], tr[7]);
    CHECK(tr[2] > 0.3f);
    CHECK(tr[3] > -10.0f && tr[3] < 10.0f);
    /* the status stream sees the flight too */
    uint8_t s[3] = { STREAM_STATUS, 20, 0 };
    CHECK(status_of(req(MSG_STREAM, s, 3, 100)) == ST_OK);
    int frames = 0;
    for (int i = 0; i < 300; i++) {
        emu_run(1);
        uint8_t b[512];
        size_t k;
        while ((k = emu_tx(b, sizeof(b))) > 0u) {
            for (size_t j = 0; j < k; j++) {
                if (link_parser_feed(&s_parser, b[j], &s_pkt) && s_pkt.id == MSG_TLM_STATUS) {
                    tlm_status_t st;
                    memcpy(&st, s_pkt.payload, sizeof(st));
                    CHECK((st.flags & STF_ARMED) != 0u);
                    CHECK(st.vbat_mv > 3000u && st.vbat_mv < 4300u);
                    frames++;
                }
            }
        }
    }
    CHECK(frames >= 5 && frames <= 7);
    emu_rc_set(2, 1000);
    emu_rc_set(4, 1000);                    /* disarm in the air: drops */
    emu_run(5000);
    emu_truth(tr, 14);
    CHECK(tr[8] == 0.0f);
    CHECK(tr[2] < 0.05f);
}

int main(void)
{
    RUN(test_boot_and_hello);
    RUN(test_crash_loop);
    RUN(test_firmware_update);
    RUN(test_rom_bootloader);
    RUN(test_params_persist);
    RUN(test_usb_cable);
    RUN(test_flight);
    return TT_REPORT();
}
