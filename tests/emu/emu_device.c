/**
 * @file    emu_device.c
 * @brief   Device emulator core: flash, UART, reset/boot logic, bootloader
 *          and application modes, svc_* services for comms.c.
 */
#include "emu_device.h"

#include <math.h>
#include <string.h>

#include "app_header.h"
#include "app_services.h"
#include "battery.h"
#include "bl_core.h"
#include "bl_image.h"
#include "board.h"
#include "comms.h"
#include "config.h"
#include "config_store.h"
#include "crc.h"
#include "emu_internal.h"
#include "fc.h"
#include "fc_events.h"
#include "fw_version.h"
#include "link.h"
#include "log.h"
#include "memory_map.h"
#include "notify.h"
#include "params.h"
#include "proto.h"
#include "shared_ram.h"
#include "sim_harness.h"
#include "telemetry.h"
#include "tlm_format.h"
#include "tlm_rings.h"

#define BL_WINDOW_MS        300u
#define BL_RESIDENT_IDLE_MS 30000u
#define MOTOR_TEST_HOLD_MS  500u
#define BAT_CAPACITY_AH     0.25
#define UID0                0x00454D55u     /* "UME" */
#define UID1                0x31303030u
#define UID2                0x20261105u

/* ======================================================================== */
/* flash                                                                    */
/* ======================================================================== */
static uint8_t s_flash[EMU_FLASH_SIZE];

static const uint32_t SECTOR_KB[EMU_SECTORS] = { 16, 16, 16, 16, 64, 128, 128, 128, 128, 128, 128, 128 };

uint32_t emu_flash_sector_base(int sector)
{
    uint32_t a = EMU_FLASH_BASE;
    for (int i = 0; i < sector; i++) {
        a += SECTOR_KB[i] * 1024u;
    }
    return a;
}

uint32_t emu_flash_sector_size(int sector)
{
    return SECTOR_KB[sector] * 1024u;
}

int emu_flash_sector(uint32_t addr)
{
    if (addr < EMU_FLASH_BASE || addr >= EMU_FLASH_BASE + EMU_FLASH_SIZE) {
        return -1;
    }
    for (int i = 0; i < EMU_SECTORS; i++) {
        uint32_t b = emu_flash_sector_base(i);
        if (addr >= b && addr < b + emu_flash_sector_size(i)) {
            return i;
        }
    }
    return -1;
}

uint32_t emu_flash_erase(int sector)
{
    if (sector < 0 || sector >= EMU_SECTORS) {
        return 0u;
    }
    memset(&s_flash[emu_flash_sector_base(sector) - EMU_FLASH_BASE], 0xFF, emu_flash_sector_size(sector));
    /* typical erase times of the F405 at x32 parallelism */
    return (SECTOR_KB[sector] == 16u) ? 250u : ((SECTOR_KB[sector] == 64u) ? 550u : 1000u);
}

bool emu_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr < EMU_FLASH_BASE || addr + len > EMU_FLASH_BASE + EMU_FLASH_SIZE) {
        return false;
    }
    uint8_t *p = &s_flash[addr - EMU_FLASH_BASE];
    bool ok = true;
    for (uint32_t i = 0; i < len; i++) {
        p[i] &= data[i];
        ok = ok && (p[i] == data[i]);
    }
    return ok;
}

uint8_t *emu_flash_mem(void)
{
    return s_flash;
}

const uint8_t *emu_flash(void)
{
    return s_flash;
}

/* ======================================================================== */
/* UART                                                                     */
/* ======================================================================== */
#define TX_RING_SIZE    4096u               /* same as the firmware */
#define WIRE_SIZE       65536u
#define RX_FIFO_SIZE    16384u

static uint8_t  s_txr[TX_RING_SIZE];
static uint32_t s_txr_head, s_txr_count;
static uint8_t  s_wire[WIRE_SIZE];
static uint32_t s_wire_head, s_wire_count;
static uint8_t  s_rx[RX_FIFO_SIZE];
static uint32_t s_rx_head, s_rx_count;
static double   s_tx_credit;
static uint32_t s_block_ms;

size_t emu_uart_read(uint8_t *buf, size_t max)
{
    size_t n = 0u;
    while (n < max && s_rx_count > 0u) {
        buf[n++] = s_rx[(s_rx_head + RX_FIFO_SIZE - s_rx_count) % RX_FIFO_SIZE];
        s_rx_count--;
    }
    return n;
}

size_t emu_uart_write(const void *data, size_t len)
{
    if (len > TX_RING_SIZE - s_txr_count) {
        return 0u;
    }
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0u; i < len; i++) {
        s_txr[s_txr_head] = p[i];
        s_txr_head = (s_txr_head + 1u) % TX_RING_SIZE;
    }
    s_txr_count += (uint32_t)len;
    return len;
}

size_t emu_uart_tx_free(void)
{
    return TX_RING_SIZE - s_txr_count;
}

bool emu_uart_tx_idle(void)
{
    return s_txr_count == 0u;
}

void emu_cpu_block(uint32_t ms)
{
    s_block_ms += ms;
}

bool emu_cpu_blocked(void)
{
    return s_block_ms > 0u;
}

/* bytes already handed to the UART leave even if the CPU jumps away */
static void uart_flush_to_wire(void)
{
    while (s_txr_count > 0u && s_wire_count < WIRE_SIZE) {
        s_wire[s_wire_head] = s_txr[(s_txr_head + TX_RING_SIZE - s_txr_count) % TX_RING_SIZE];
        s_wire_head = (s_wire_head + 1u) % WIRE_SIZE;
        s_wire_count++;
        s_txr_count--;
    }
}

static void uart_reset(void)
{
    s_txr_head = s_txr_count = 0u;
    s_rx_head = s_rx_count = 0u;
    s_tx_credit = 0.0;
}

/* one millisecond of wire time */
static void uart_tick(void)
{
    s_tx_credit += EMU_UART_BYTES_MS;
    while (s_tx_credit >= 1.0 && s_txr_count > 0u && s_wire_count < WIRE_SIZE) {
        uint8_t b = s_txr[(s_txr_head + TX_RING_SIZE - s_txr_count) % TX_RING_SIZE];
        s_txr_count--;
        s_wire[s_wire_head] = b;
        s_wire_head = (s_wire_head + 1u) % WIRE_SIZE;
        s_wire_count++;
        s_tx_credit -= 1.0;
    }
    if (s_txr_count == 0u && s_tx_credit > EMU_UART_BYTES_MS) {
        s_tx_credit = EMU_UART_BYTES_MS;    /* an idle line does not save up */
    }
}

void emu_rx(const uint8_t *data, size_t len)
{
    for (size_t i = 0u; i < len; i++) {
        if (s_rx_count >= RX_FIFO_SIZE) {
            return;                         /* overrun: bytes lost, like the DMA ring */
        }
        s_rx[s_rx_head] = data[i];
        s_rx_head = (s_rx_head + 1u) % RX_FIFO_SIZE;
        s_rx_count++;
    }
}

size_t emu_tx(uint8_t *out, size_t max)
{
    size_t n = 0u;
    while (n < max && s_wire_count > 0u) {
        out[n++] = s_wire[(s_wire_head + WIRE_SIZE - s_wire_count) % WIRE_SIZE];
        s_wire_count--;
    }
    return n;
}

/* ======================================================================== */
/* board state                                                              */
/* ======================================================================== */
static emu_mode_t s_mode;
static bool       s_boot0;
static bool       s_led;
static uint32_t   s_buzzer_hz;
static uint32_t   s_mode_ms;            /* ms since the last reset */

/* survives a reset (shared RAM / .noinit), lost at power cycle */
static struct {
    uint32_t request;
    uint32_t boot_count;
    uint32_t reset_csr;
} s_shared;

static struct {
    bool     valid;
    uint32_t type, pc, lr, psr, cfsr, hfsr, mmfar, bfar, uptime_ms;
} s_fault;

/* the vehicle and the transmitter are not reset with the MCU */
static sim_t    s_sim;
static bool     s_sim_ready;
static double   s_soc = 0.9;
static uint16_t s_rc[8];
static bool     s_rc_on = true;
static bool     s_tof_on = true;

/* LED / buzzer used by notify.c */
void buzzer_tone(uint32_t hz);
void led_set(bool on);
void buzzer_tone(uint32_t hz) { s_buzzer_hz = hz; }
void led_set(bool on) { s_led = on; }

static void reset_to(uint32_t request, bool start_app);

/* ======================================================================== */
/* bootloader mode                                                          */
/* ======================================================================== */
static bl_core_t     s_bl;
static link_parser_t s_bl_parser;
static link_packet_t s_bl_req, s_bl_rsp;
static uint8_t       s_bl_frame[LINK_FRAME_MAX];
static bool          s_bl_stay;
static bool          s_bl_on_request;
static uint32_t      s_bl_last_rx;
static uint32_t      s_bl_busy_acc;
static bool          s_bl_pending;       /* reply waiting for room in the TX ring */

static uint32_t bl_erase(uint32_t size)
{
    if (size == 0u || size > MM_APP_MAX_SIZE) {
        return 0u;
    }
    uint32_t end = MM_APP_BASE + size;
    uint32_t done = 0u;
    for (int sct = MM_APP_FIRST_SECTOR; sct < EMU_SECTORS; sct++) {
        uint32_t b = emu_flash_sector_base(sct);
        if (b >= end) {
            break;
        }
        s_bl_busy_acc += emu_flash_erase(sct);
        done = b + emu_flash_sector_size(sct) - MM_APP_BASE;
    }
    return done;
}

static bool bl_program(uint32_t offset, const uint32_t *words, uint32_t len)
{
    return emu_flash_write(MM_APP_BASE + offset, (const uint8_t *)words, len);
}

static const bl_flash_ops_t s_bl_ops = {
    bl_erase, bl_program, &s_flash[MM_APP_BASE - EMU_FLASH_BASE], MM_APP_MAX_SIZE,
};

static void bl_start(bool stay, bool on_request)
{
    s_mode = EMU_MODE_BOOTLOADER;
    bl_identity_t id = {
        .uid = { UID0, UID1, UID2 },
        .bl_version = FW_VERSION,
        .build_time = FW_BUILD_TIME,
        .git_hash = FW_GIT_HASH,
    };
    bl_core_init(&s_bl, &s_bl_ops, &id);
    link_parser_init(&s_bl_parser);
    s_bl_stay = stay || !bl_core_app_valid(&s_bl, NULL);
    s_bl_on_request = on_request;
    s_bl_last_rx = 0u;
    s_bl_pending = false;
}

static void bl_tick(void)
{
    uint32_t now = s_mode_ms;

    if (s_bl_pending) {
        size_t n = link_encode(s_bl_rsp.id, s_bl_rsp.seq, s_bl_rsp.payload, s_bl_rsp.len, s_bl_frame);
        if (emu_uart_write(s_bl_frame, n) == 0u) {
            return;
        }
        s_bl_pending = false;
    }

    uint8_t b;
    while (!s_bl_pending && !emu_cpu_blocked() && emu_uart_read(&b, 1u) == 1u) {
        if (!link_parser_feed(&s_bl_parser, b, &s_bl_req)) {
            continue;
        }
        s_bl_last_rx = now;
        s_bl_stay = true;                   /* the configurator caught the window */
        s_bl_busy_acc = 0u;
        if (bl_core_handle(&s_bl, &s_bl_req, &s_bl_rsp)) {
            size_t n = link_encode(s_bl_rsp.id, s_bl_rsp.seq, s_bl_rsp.payload, s_bl_rsp.len, s_bl_frame);
            if (emu_uart_write(s_bl_frame, n) == 0u) {
                s_bl_pending = true;
            }
        }
        if (s_bl_busy_acc > 0u) {
            emu_cpu_block(s_bl_busy_acc);   /* the reply leaves after the erase */
        }
    }

    if ((s_bl.boot_request || s_bl.reboot_request) && emu_uart_tx_idle() && !s_bl_pending) {
        if (s_bl.boot_request) {
            reset_to(BL_REQ_NONE, true);
            return;
        }
        switch (s_bl.reboot_target) {
        case REBOOT_BOOTLOADER: reset_to(BL_REQ_STAY, false); return;
        case REBOOT_ROM:        reset_to(BL_REQ_ROM, false); return;
        default:
            reset_to(BL_REQ_NONE, bl_core_app_valid(&s_bl, NULL));
            return;
        }
    }

    if (!s_bl_stay) {
        s_led = true;
        if (now >= BL_WINDOW_MS) {
            reset_to(BL_REQ_NONE, true);
        }
    } else {
        s_led = ((now / 250u) % 2u) == 0u;
        if (s_bl_on_request && (now - s_bl_last_rx) > BL_RESIDENT_IDLE_MS &&
            bl_core_app_valid(&s_bl, NULL)) {
            reset_to(BL_REQ_NONE, true);
        }
    }
}

/* ======================================================================== */
/* application mode                                                         */
/* ======================================================================== */
static battery_t  s_bat;
static const battery_cal_t s_bat_cal = { 1500u, 940u, 1210u };
static bool       s_mt_active;
static uint32_t   s_mt_until;
static uint16_t   s_mt_duty[4];
static uint8_t    s_cal_req;
static uint32_t   s_cfg_busy_ms;
static bool       s_reboot_pending;
static uint8_t    s_reboot_target;
static uint32_t   s_reboot_t0;
static uint32_t   s_rng = 0x1234567u;

static size_t port_read(uint8_t *buf, size_t max) { return emu_uart_read(buf, max); }
static size_t port_write(const void *d, size_t len) { return emu_uart_write(d, len); }
static size_t port_tx_free(void) { return emu_uart_tx_free(); }
static const comms_port_t s_port = { port_read, port_write, port_tx_free };

static bool cfg_erase(uint32_t bank)
{
    uint32_t ms = emu_flash_erase((bank == 0u) ? MM_CFG_A_SECTOR : MM_CFG_B_SECTOR);
    s_cfg_busy_ms += ms;
    return ms > 0u;
}

static bool cfg_program(uint32_t bank, uint32_t offset, const uint32_t *words, uint32_t count)
{
    uint32_t base = (bank == 0u) ? MM_CFG_A_BASE : MM_CFG_B_BASE;
    return emu_flash_write(base + offset, (const uint8_t *)words, count * 4u);
}

static const uint8_t *cfg_view(uint32_t bank)
{
    uint32_t base = (bank == 0u) ? MM_CFG_A_BASE : MM_CFG_B_BASE;
    return &s_flash[base - EMU_FLASH_BASE];
}

static const cfg_flash_ops_t s_cfg_ops = { cfg_erase, cfg_program, cfg_view, MM_CFG_BANK_SIZE };

static double ocv_from_soc(double soc)
{
    /* 1S LiPo open circuit voltage, piecewise linear */
    static const double S[] = { 0.00, 0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 0.95, 1.00 };
    static const double V[] = { 3.00, 3.45, 3.62, 3.70, 3.78, 3.87, 4.00, 4.12, 4.20 };
    if (soc <= 0.0) {
        return V[0];
    }
    for (int i = 1; i < 9; i++) {
        if (soc <= S[i]) {
            return V[i - 1] + (V[i] - V[i - 1]) * (soc - S[i - 1]) / (S[i] - S[i - 1]);
        }
    }
    return V[8];
}

static const app_header_t *app_hdr(void)
{
    return (const app_header_t *)&s_flash[MM_APP_BASE - EMU_FLASH_BASE];
}

static void drain_queues(void)
{
    log_msg_t m;
    while (log_pop(&m)) {
    }
    fc_event_t ev;
    while (fc_event_pop(&ev)) {
    }
}

static void app_start(void)
{
    s_mode = EMU_MODE_APP;

    /* the vehicle keeps flying (or lying) while the MCU restarts */
    quadsim_t keep = s_sim.quad;
    bool had = s_sim_ready;
    sim_init_env(&s_sim);
    if (had) {
        s_sim.quad = keep;
    }
    s_sim_ready = true;
    s_sim.bat_external = true;
    s_sim.tof_on = s_tof_on;
    s_sim.tx_on = s_rc_on;
    memcpy(s_sim.ch, s_rc, sizeof(s_rc));

    config_store_init(&s_cfg_ops);
    bool loaded = config_store_load();
    fc_init(1000.0f);
    battery_init(&s_bat);
    drain_queues();
    comms_init(&s_port);
    notify_init();
    notify_play(TONE_STARTUP);
    s_mt_active = false;
    s_cal_req = 0u;
    s_cfg_busy_ms = 0u;
    s_reboot_pending = false;

    LOG_I("MiniQuad-F405 %s (%s), clock HSE [emulator]", FW_VERSION_STR, FW_GIT_HASH);
    LOG_I("reset csr 0x%08x, boot #%u", s_shared.reset_csr, s_shared.boot_count);
    LOG_I("config: %s (%u entries)", loaded ? "loaded" : "defaults",
          (unsigned)config_store_info()->entries_loaded);
    if (s_fault.valid) {
        LOG_W("fault before reset: type %u pc 0x%08x", s_fault.type, s_fault.pc);
    }
}

static void house_10ms(void)
{
    /* events -> tones + log (main.c task_notify) */
    fc_event_t ev;
    while (fc_event_pop(&ev)) {
        notify_event(ev);
        log_printf((ev == FCEV_FAILSAFE || ev == FCEV_CRASH || ev == FCEV_EMERGENCY) ? LOG_LEVEL_WARN
                                                                                     : LOG_LEVEL_INFO,
                   "%s", fc_event_name(ev));
    }
    fc_status_t st;
    fc_get_status(&st);
    st.bat_state = s_bat.state;
    bool beeper = false;
    if (g_cfg.rc_ch_beeper > 0 && s_rc_on) {
        int ch = g_cfg.rc_ch_beeper - 1;
        beeper = ch < 8 && (float)s_rc[ch] >= g_cfg.rc_arm_us;
    }
    notify_update(&st, s_sim.ms, beeper);

    /* accelerometer calibration result (main.c task_housekeeping) */
    vec3_t off;
    bool ok;
    if (fc_take_acc_cal_result(&off, &ok)) {
        if (ok) {
            params_set((uint16_t)params_find("acc_off_x"), off.x);
            params_set((uint16_t)params_find("acc_off_y"), off.y);
            params_set((uint16_t)params_find("acc_off_z"), off.z);
            bool saved = svc_save_config();
            LOG_I("acc offsets %.3f %.3f %.3f g (%s)", (double)off.x, (double)off.y, (double)off.z,
                  saved ? "saved" : "NOT saved");
        } else {
            LOG_W("accelerometer calibration failed");
        }
    }
}

static void app_tick(void)
{
    sim_t *s = &s_sim;
    uint32_t now = s->ms + 1u;

    if (s_mt_active && (int32_t)(now - s_mt_until) >= 0) {
        s_mt_active = false;
    }
    s->in.motor_test_active = s_mt_active;
    memcpy(s->in.motor_test, s_mt_duty, sizeof(s_mt_duty));
    s->props_removed = s_mt_active;
    s->in.config_busy = s_cfg_busy_ms > 0u;
    if (s_cfg_busy_ms > 0u) {
        s_cfg_busy_ms--;
    }
    if (s_cal_req != 0u) {
        s->in.cal_request = s_cal_req;
        s_cal_req = 0u;
    }
    s->tx_on = s_rc_on;
    s->tof_on = s_tof_on;
    memcpy(s->ch, s_rc, sizeof(s_rc));

    /* battery: coulomb counting + the real monitor fed with ADC counts */
    s_soc -= s->quad.i_total * 0.001 / (3600.0 * BAT_CAPACITY_AH);
    if (s_soc < 0.0) {
        s_soc = 0.0;
    }
    s->quad.v_oc = ocv_from_soc(s_soc);
    if ((now % 5u) == 0u) {
        double v = s->quad.v_bat;
        uint16_t raw = (uint16_t)fmin(4095.0, v / VBAT_DIVIDER / 3.3 * 4095.0 + 0.5);
        battery_update(&s_bat, &s_bat_cal, raw, 1500u, 957u, 0.005f);   /* ~35 C */
        s->in.bat.present = s_bat.present;
        s->in.bat.v_comp = s_bat.v_comp;
        s->in.bat.v_slow = s_bat.v_slow;
        s->in.bat.state = s_bat.state;
    }

    sim_step(s);                            /* physics + fc_update() */

    if (!emu_cpu_blocked()) {
        comms_task();
        uint8_t target;
        if (comms_take_reboot(&target)) {
            s_reboot_pending = true;
            s_reboot_target = target;
            s_reboot_t0 = now;
        }
    }
    if (s_reboot_pending && (emu_uart_tx_idle() || (now - s_reboot_t0) >= 100u)) {
        s_reboot_pending = false;
        reset_to((s_reboot_target == REBOOT_BOOTLOADER) ? BL_REQ_STAY
                 : (s_reboot_target == REBOOT_ROM) ? BL_REQ_ROM : BL_REQ_NONE, false);
        return;
    }
    if ((now % 10u) == 0u && !emu_cpu_blocked()) {
        house_10ms();
    }
}

/* ---------------- services for comms.c ---------------- */
uint32_t svc_now_ms(void)
{
    return s_sim.ms;
}

bool svc_armed(void)
{
    return fc_is_armed();
}

void svc_identity(msg_hello_rsp_t *h)
{
    img_info_t info;
    bool valid = bl_image_check(&s_flash[MM_APP_BASE - EMU_FLASH_BASE], MM_APP_MAX_SIZE, &info);
    h->board_id = BOARD_ID;
    h->fw_version = valid ? info.fw_version : FW_VERSION;
    h->app_version = valid ? info.fw_version : 0u;
    h->app_valid = valid ? 1u : 0u;
    h->app_max_size = MM_APP_MAX_SIZE;
    h->uid[0] = UID0;
    h->uid[1] = UID1;
    h->uid[2] = UID2;
    h->build_time = valid ? app_hdr()->build_time : FW_BUILD_TIME;
    if (valid) {
        memcpy(h->git_hash, app_hdr()->git_hash, sizeof(h->git_hash) - 1u);
    } else {
        strncpy(h->git_hash, FW_GIT_HASH, sizeof(h->git_hash) - 1u);
    }
    strncpy(h->board_name, BOARD_NAME, sizeof(h->board_name) - 1u);
}

void svc_reboot(uint8_t target)
{
    s_reboot_pending = true;
    s_reboot_target = target;
    s_reboot_t0 = s_sim.ms;
}

bool svc_save_config(void)
{
    if (fc_is_armed()) {
        return false;
    }
    s_mt_active = false;
    uint32_t before = s_cfg_busy_ms;
    bool ok = config_store_save();
    uint32_t busy = (s_cfg_busy_ms - before) + 20u;     /* erase + programming */
    s_cfg_busy_ms = before + busy;
    emu_cpu_block(busy);
    if (ok) {
        notify_play(TONE_SAVE_OK);
    }
    return ok;
}

void svc_motor_test(const uint16_t duty[4])
{
    memcpy(s_mt_duty, duty, sizeof(s_mt_duty));
    s_mt_active = true;
    s_mt_until = s_sim.ms + MOTOR_TEST_HOLD_MS;
}

void svc_motor_test_stop(void)
{
    s_mt_active = false;
}

bool svc_calibrate(uint8_t what)
{
    switch (what) {
    case CAL_GYRO:
    case CAL_ACCEL_LEVEL:
        s_cal_req = what;
        return true;
    case CAL_ACCEL_RESET:
        params_set((uint16_t)params_find("acc_off_x"), 0.0f);
        params_set((uint16_t)params_find("acc_off_y"), 0.0f);
        params_set((uint16_t)params_find("acc_off_z"), 0.0f);
        return true;
    default:
        return false;
    }
}

void svc_beep(uint8_t pattern)
{
    notify_play((notify_tone_t)pattern);
}

static uint32_t rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 16;
}

void svc_status(tlm_status_t *t)
{
    fc_status_t st;
    fc_get_status(&st);
    const sim_t *s = &s_sim;

    tlm_extra_t x;
    memset(&x, 0, sizeof(x));
    x.time_ms = s->ms;
    x.flags_clear = STF_BAT_PRESENT | STF_MOTOR_TEST | (s_tof_on ? 0u : (STF_TOF_OK | STF_TOF_VALID));
    x.flags_set = STF_IMU_OK | (s_tof_on ? STF_TOF_OK : 0u) | (s_bat.present ? STF_BAT_PRESENT : 0u) |
                  (comms_host_alive() ? STF_HOST_ALIVE : 0u) | (s_mt_active ? STF_MOTOR_TEST : 0u);
    x.bat_state = s_bat.state;
    x.v_comp = s_bat.v_comp;
    x.v_slow = s_bat.v_slow;
    x.cpu_load = (uint8_t)(18u + rnd() % 5u);
    x.loop_us_avg = (uint16_t)(104u + rnd() % 8u);
    x.loop_us_max = (uint16_t)(150u + rnd() % 30u);
    x.loop_hz = 1000u;
    x.mcu_temp = s_bat.mcu_temp;
    x.rc = &s->in.rc;
    x.tof_have = s->in.range.seq != 0u;
    x.tof_mm = (uint16_t)fmin(65535.0, fmax(0.0, (double)s->in.range.dist_m * 1000.0));
    x.tof_status = s->in.range.status;
    x.tof_rate_hz = s_tof_on ? 30u : 0u;
    x.link_drops = comms_drops() + tlm_rings_dropped();
    tlm_format_status(&st, &x, t);
}

void svc_alt(tlm_alt_t *a)
{
    fc_status_t st;
    fc_get_status(&st);
    tlm_format_alt(&st, s_sim.ms * 1000u, a);
}

void svc_fault(msg_fault_info_rsp_t *f)
{
    if (s_fault.valid) {
        f->valid = 1u;
        f->type = (uint8_t)s_fault.type;
        f->pc = s_fault.pc;
        f->lr = s_fault.lr;
        f->psr = s_fault.psr;
        f->cfsr = s_fault.cfsr;
        f->hfsr = s_fault.hfsr;
        f->mmfar = s_fault.mmfar;
        f->bfar = s_fault.bfar;
        f->uptime_ms = s_fault.uptime_ms;
    }
    f->reset_csr = s_shared.reset_csr;
    f->boot_count = s_shared.boot_count;
}

/* ======================================================================== */
/* reset / boot                                                             */
/* ======================================================================== */
#define CSR_PIN     0x04000000u     /* PINRSTF */
#define CSR_POR     0x0C000000u     /* PORRSTF | PINRSTF */
#define CSR_SFT     0x14000000u     /* SFTRSTF | PINRSTF */

static void boot_flash(uint32_t request, bool start_app)
{
    if (s_flash[0] == 0xFFu && s_flash[1] == 0xFFu && s_flash[2] == 0xFFu && s_flash[3] == 0xFFu) {
        s_mode = EMU_MODE_DEAD;             /* no bootloader: nothing runs */
        s_led = false;
        return;
    }
    if (request == BL_REQ_ROM) {
        s_mode = EMU_MODE_ROM;
        rom_reset();
        return;
    }
    bl_start(request == BL_REQ_STAY, request == BL_REQ_STAY);
    if (start_app && !s_bl_stay) {
        app_start();                        /* boot token: skip the window */
    }
}

static void reset_to(uint32_t request, bool start_app)
{
    uart_reset();
    s_block_ms = 0u;
    s_mode_ms = 0u;
    s_led = false;
    s_buzzer_hz = 0u;
    s_shared.boot_count++;
    s_shared.reset_csr = CSR_SFT;
    s_shared.request = request;
    if (s_boot0) {
        s_mode = EMU_MODE_ROM;
        rom_reset();
        return;
    }
    boot_flash(request, start_app);
}

void emu_start_from_flash(void)
{
    uart_flush_to_wire();
    uart_reset();
    s_block_ms = 0u;
    s_mode_ms = 0u;
    boot_flash(BL_REQ_NONE, false);
}

/* ---------------- factory content ---------------- */
static void factory_flash(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));

    /* sector 0: a stand-in for the bootloader binary */
    uint32_t vec[2] = { MM_SHARED_BASE, MM_BL_BASE + 0x189u };
    memcpy(s_flash, vec, sizeof(vec));
    for (uint32_t i = 8u; i < 5120u; i++) {
        s_flash[i] = (uint8_t)(i * 37u + 11u);
    }

    /* a valid application image (header + image), like tools/fwpack.py */
    uint8_t *region = &s_flash[MM_APP_BASE - EMU_FLASH_BASE];
    const uint32_t image_size = 4096u;
    uint8_t *img = region + MM_APP_HEADER_SIZE;
    uint32_t sp = MM_CCM_BASE + 0x4000u;
    uint32_t pc = MM_APP_VECTORS + 0x189u;
    memcpy(img, &sp, 4);
    memcpy(img + 4, &pc, 4);
    for (uint32_t i = 8u; i < image_size; i++) {
        img[i] = (uint8_t)(i * 13u + 7u);
    }
    app_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = APP_HEADER_MAGIC;
    h.header_version = APP_HEADER_VERSION;
    h.header_size = MM_APP_HEADER_SIZE;
    h.image_offset = MM_APP_HEADER_SIZE;
    h.image_size = image_size;
    h.image_crc32 = crc32(img, image_size);
    h.fw_version = FW_VERSION;
    h.build_time = FW_BUILD_TIME;
    h.board_id = BOARD_ID;
    strncpy(h.git_hash, FW_GIT_HASH, sizeof(h.git_hash) - 1u);
    strncpy(h.board_name, BOARD_NAME, sizeof(h.board_name) - 1u);
    strncpy(h.fw_name, "miniquad-app", sizeof(h.fw_name) - 1u);
    memcpy(region, &h, sizeof(h));
    uint32_t hc = crc32(region, APP_HEADER_CRC_OFFSET);
    memcpy(region + APP_HEADER_CRC_OFFSET, &hc, 4);
}

static void power_on(bool skip_window)
{
    memset(&s_shared, 0, sizeof(s_shared));
    memset(&s_fault, 0, sizeof(s_fault));
    uart_reset();
    s_block_ms = 0u;
    s_mode_ms = 0u;
    s_shared.boot_count = 1u;
    s_shared.reset_csr = CSR_POR;
    if (s_boot0) {
        s_mode = EMU_MODE_ROM;
        rom_reset();
        return;
    }
    boot_flash(BL_REQ_NONE, skip_window);
}

void emu_init(bool skip_window)
{
    for (int i = 0; i < 8; i++) {
        s_rc[i] = 1500u;
    }
    s_rc[2] = 1000u;                        /* throttle low      */
    s_rc[4] = 1000u;                        /* arm switch off    */
    s_rc[5] = 1000u;                        /* mode low = ANGLE  */
    s_rc_on = true;
    s_tof_on = true;
    s_soc = 0.9;
    s_boot0 = false;
    s_sim_ready = false;
    s_wire_head = s_wire_count = 0u;
    factory_flash();
    power_on(skip_window);
}

void emu_power_cycle(void)
{
    power_on(false);
}

void emu_reset_pin(void)
{
    uart_reset();
    s_block_ms = 0u;
    s_mode_ms = 0u;
    s_shared.boot_count++;
    s_shared.reset_csr = CSR_PIN;
    if (s_boot0) {
        s_mode = EMU_MODE_ROM;
        rom_reset();
        return;
    }
    boot_flash(BL_REQ_NONE, false);
}

void emu_set_boot0(bool high)
{
    s_boot0 = high;
}

void emu_run(uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; i++) {
        s_mode_ms++;
        switch (s_mode) {
        case EMU_MODE_APP:        app_tick(); break;
        case EMU_MODE_BOOTLOADER: bl_tick(); break;
        case EMU_MODE_ROM:        rom_tick(); break;
        default: {
            uint8_t junk[64];
            while (emu_uart_read(junk, sizeof(junk)) > 0u) {
            }
            break;
        }
        }
        if (s_mode != EMU_MODE_APP && s_sim_ready) {
            /* MCU not flying: motors off, the vehicle still obeys physics */
            double zero[4] = { 0, 0, 0, 0 };
            quad_step(&s_sim.quad, zero, 0.001);
        }
        if (s_block_ms > 0u) {
            s_block_ms--;
        } else {
            uart_tick();
        }
    }
}

emu_mode_t emu_mode(void)
{
    return s_mode;
}

uint32_t emu_uptime_ms(void)
{
    return s_mode_ms;
}

/* ---------------- transmitter / environment ---------------- */
void emu_rc_set(int ch, uint16_t us)
{
    if (ch >= 0 && ch < 8) {
        s_rc[ch] = (us < 800u) ? 800u : ((us > 2200u) ? 2200u : us);
    }
}

uint16_t emu_rc_get(int ch)
{
    return (ch >= 0 && ch < 8) ? s_rc[ch] : 0u;
}

void emu_rc_enable(bool on)
{
    s_rc_on = on;
}

void emu_battery_set(float soc)
{
    s_soc = (soc < 0.0f) ? 0.0 : ((soc > 1.0f) ? 1.0 : (double)soc);
}

void emu_vehicle_reset(void)
{
    if (s_sim_ready) {
        double v_oc = s_sim.quad.v_oc;
        quad_init(&s_sim.quad);
        s_sim.quad.v_oc = v_oc;
    }
}

void emu_kick(float roll_dps, float pitch_dps, float yaw_dps)
{
    if (s_sim_ready) {
        const double k = M_PI / 180.0;
        s_sim.quad.w[0] += roll_dps * k;
        s_sim.quad.w[1] += pitch_dps * k;
        s_sim.quad.w[2] += yaw_dps * k;
    }
}

void emu_tof_enable(bool on)
{
    s_tof_on = on;
}

void emu_inject_fault(void)
{
    s_fault.valid = true;
    s_fault.type = 1u;                      /* FAULT_HARD */
    s_fault.pc = MM_APP_VECTORS + 0x1F2Au;
    s_fault.lr = MM_APP_VECTORS + 0x10E7u;
    s_fault.psr = 0x21000000u;
    s_fault.cfsr = 0x00000400u;             /* IMPRECISERR */
    s_fault.hfsr = 0x40000000u;             /* FORCED */
    s_fault.mmfar = 0xE000EDF8u;
    s_fault.bfar = 0xE000EDF8u;
    s_fault.uptime_ms = s_sim.ms;
    reset_to(BL_REQ_NONE, true);
}

/* ---------------- outputs ---------------- */
bool emu_led(void)
{
    return s_led;
}

uint32_t emu_buzzer_hz(void)
{
    return (s_mode == EMU_MODE_APP) ? s_buzzer_hz : 0u;
}

int emu_truth(float *out, int max)
{
    float v[14];
    memset(v, 0, sizeof(v));
    if (s_sim_ready) {
        const quadsim_t *q = &s_sim.quad;
        double r, p, y;
        quad_euler_deg(q, &r, &p, &y);
        v[0] = (float)q->pos[0];
        v[1] = (float)q->pos[1];
        v[2] = (float)quad_height(q);
        v[3] = (float)r;
        v[4] = (float)p;
        v[5] = (float)y;
        v[6] = (float)q->v_bat;
        v[7] = (float)q->i_total;
        v[8] = (s_mode == EMU_MODE_APP && fc_is_armed()) ? 1.0f : 0.0f;
        for (int i = 0; i < 4; i++) {
            v[9 + i] = (s_mode == EMU_MODE_APP) ? s_sim.out.duty[i] : 0.0f;
        }
        v[13] = (float)s_soc;
    }
    int n = (max < 14) ? max : 14;
    memcpy(out, v, (size_t)n * sizeof(float));
    return n;
}
