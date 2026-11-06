/**
 * @file    vl53l1.c
 * @brief   VL53L1CB / VL53L1X driver: asynchronous state machine.
 *
 * The default configuration block and the timing tables below are taken
 * from ST's VL53L1X Ultra Lite Driver (STSW-IMG009, BSD-3-Clause,
 * (c) STMicroelectronics); see THIRD_PARTY.md.
 */
#include "vl53l1.h"

#include <string.h>
#include "board.h"
#include "exti.h"
#include "gpio.h"
#include "i2c2.h"

/* ---------------- registers ---------------- */
#define REG_VHV_TIMEOUT_MACROP_LOOP_BOUND   0x0008u
#define REG_VHV_INIT                        0x000Bu
#define REG_PART_TO_PART_RANGE_OFFSET_MM    0x001Eu
#define REG_DEFAULT_CONFIG_START            0x002Du
#define REG_GPIO_TIO_HV_STATUS              0x0031u
#define REG_PHASECAL_TIMEOUT_MACROP         0x004Bu
#define REG_RANGE_TIMEOUT_MACROP_A_HI       0x005Eu
#define REG_RANGE_VCSEL_PERIOD_A            0x0060u
#define REG_RANGE_TIMEOUT_MACROP_B_HI       0x0061u
#define REG_RANGE_VCSEL_PERIOD_B            0x0063u
#define REG_RANGE_VALID_PHASE_HIGH          0x0069u
#define REG_INTERMEASUREMENT_PERIOD         0x006Cu
#define REG_SD_CONFIG_WOI_SD0               0x0078u
#define REG_SD_CONFIG_INITIAL_PHASE_SD0     0x007Au
#define REG_SYSTEM_INTERRUPT_CLEAR          0x0086u
#define REG_SYSTEM_MODE_START               0x0087u
#define REG_RESULT_RANGE_STATUS             0x0089u
#define REG_RESULT_OSC_CALIBRATE_VAL        0x00DEu
#define REG_FIRMWARE_SYSTEM_STATUS          0x00E5u
#define REG_IDENTIFICATION_MODEL_ID         0x010Fu

#define RESULT_LEN          17u
#define DEFAULT_CONFIG_LEN  91u             /* 0x2D .. 0x87 */

/* ULD default configuration (0x2D..0x87). Board-specific changes are
 * applied at run time: 0x2E/0x2F bit0 = 1 (pull-ups at AVDD 2V8),
 * 0x30 = 0x11 (GPIO1 active low). */
static const uint8_t s_default_config[DEFAULT_CONFIG_LEN] = {
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21,
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x38, 0xff, 0x01, 0x00, 0x08, 0x00,
    0x00, 0x01, 0xcc, 0x0f, 0x01, 0xf1, 0x0d, 0x01, 0x68, 0x00, 0x80, 0x08, 0xb8, 0x00, 0x00, 0x00,
    0x00, 0x0f, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x0d, 0x0e, 0x0e, 0x00,
    0x00, 0x02, 0xc7, 0xff, 0x9B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
};
_Static_assert(sizeof(s_default_config) == DEFAULT_CONFIG_LEN, "ULD default configuration size");

/* timing budget -> RANGE_CONFIG__TIMEOUT_MACROP_A_HI / _B_HI */
typedef struct {
    uint16_t ms;
    uint16_t a;
    uint16_t b;
} tb_entry_t;

static const tb_entry_t s_tb_short[] = {
    { 15u, 0x001Du, 0x0027u }, { 20u, 0x0051u, 0x006Eu }, { 33u, 0x00D6u, 0x006Eu },
    { 50u, 0x01AEu, 0x01E8u }, { 100u, 0x02E1u, 0x0388u }, { 200u, 0x03E1u, 0x0496u },
    { 500u, 0x0591u, 0x05C1u },
};
static const tb_entry_t s_tb_long[] = {
    { 20u, 0x001Eu, 0x0022u }, { 33u, 0x0060u, 0x006Eu }, { 50u, 0x00ADu, 0x00C6u },
    { 100u, 0x01CCu, 0x01EAu }, { 200u, 0x02D9u, 0x02F8u }, { 500u, 0x048Fu, 0x04A4u },
};

/* RESULT__RANGE_STATUS -> ULD status */
static const uint8_t s_status_map[24] = {
    255, 255, 255, 5, 2, 4, 1, 7, 3, 0, 255, 255, 9, 13, 255, 255, 255, 255, 10, 6, 255, 255, 11, 12,
};

/* ---------------- state machine ---------------- */
typedef enum {
    VL_OFF = 0,
    VL_RESET_LOW,
    VL_RESET_RELEASE,
    VL_BOOT_POLL,
    VL_READ_ID,
    VL_WRITE_DEFAULT,
    VL_VHV_START,
    VL_VHV_POLL,
    VL_VHV_CLEAR,
    VL_VHV_STOP,
    VL_VHV_BOUND,
    VL_VHV_INIT,
    VL_CONFIG_LIST,
    VL_OSC_READ,
    VL_IMP_WRITE,
    VL_START,
    VL_RUN_WAIT,
    VL_RUN_POLL,
    VL_READ_RESULT,
    VL_CLEAR,
    VL_BACKOFF,
} vl_state_t;

typedef struct {
    uint16_t reg;
    uint8_t  len;
    uint32_t val;
} reg_write_t;

#define CONFIG_LIST_MAX     12u
#define BOOT_TIMEOUT_MS     100u
#define POLL_PERIOD_MS      5u
#define MAX_RETRIES         5u

static vl_state_t        s_state;
static uint32_t          s_t_state;         /* time the current state was entered */
static bool              s_pending;         /* an I2C transaction is in flight */
static uint8_t           s_retries;
static uint32_t          s_errors;
static uint16_t          s_model_id;
static bool              s_healthy;
static volatile bool     s_irq;

static vl53l1_dist_mode_t s_mode;
static uint16_t          s_tb_ms;
static uint16_t          s_tb_a, s_tb_b;
static int16_t           s_offset_mm;
static uint16_t          s_osc;

static reg_write_t       s_cfg[CONFIG_LIST_MAX];
static uint8_t           s_cfg_n;
static uint8_t           s_cfg_i;

/* DMA buffers: static (SRAM), never on the stack (CCM) */
static uint8_t           s_tx[2u + DEFAULT_CONFIG_LEN];
static uint8_t           s_rx[RESULT_LEN];

static vl53l1_sample_t   s_sample;
static volatile uint32_t s_sample_lock;
static uint32_t          s_rate_t0;
static uint32_t          s_rate_n;
static uint8_t           s_rate_hz;

uint8_t vl53l1_map_status(uint8_t raw)
{
    raw &= 0x1Fu;
    return (raw < sizeof(s_status_map)) ? s_status_map[raw] : 255u;
}

uint16_t vl53l1_pick_budget(vl53l1_dist_mode_t mode, uint16_t ms, uint16_t *macrop_a, uint16_t *macrop_b)
{
    const tb_entry_t *t = (mode == VL53L1_DIST_SHORT) ? s_tb_short : s_tb_long;
    size_t n = (mode == VL53L1_DIST_SHORT) ? sizeof(s_tb_short) / sizeof(s_tb_short[0])
                                           : sizeof(s_tb_long) / sizeof(s_tb_long[0]);
    size_t best = 0u;
    for (size_t i = 0u; i < n; i++) {
        int32_t d_best = (int32_t)t[best].ms - (int32_t)ms;
        int32_t d_i = (int32_t)t[i].ms - (int32_t)ms;
        if ((d_i < 0 ? -d_i : d_i) < (d_best < 0 ? -d_best : d_best)) {
            best = i;
        }
    }
    *macrop_a = t[best].a;
    *macrop_b = t[best].b;
    return t[best].ms;
}

static void xshut(bool run)
{
    /* open-drain: low = shutdown, released = pulled up to 2V8 by R19 */
    gpio_write(TOF_XSHUT_PORT, TOF_XSHUT_PIN, run);
}

static void enter(vl_state_t st, uint32_t now)
{
    s_state = st;
    s_t_state = now;
    s_pending = false;
}

static bool wr(uint16_t reg, const uint8_t *data, uint16_t len)
{
    s_tx[0] = (uint8_t)(reg >> 8);
    s_tx[1] = (uint8_t)(reg & 0xFFu);
    memcpy(&s_tx[2], data, len);
    return i2c2_start(TOF_I2C_ADDR7, s_tx, (uint16_t)(len + 2u), NULL, 0u);
}

static bool wr_val(uint16_t reg, uint32_t val, uint8_t len)
{
    uint8_t b[4];
    for (uint8_t i = 0u; i < len; i++) {
        b[i] = (uint8_t)(val >> (8u * (len - 1u - i)));      /* big endian */
    }
    return wr(reg, b, len);
}

static bool rd(uint16_t reg, uint16_t len)
{
    s_tx[0] = (uint8_t)(reg >> 8);
    s_tx[1] = (uint8_t)(reg & 0xFFu);
    return i2c2_start(TOF_I2C_ADDR7, s_tx, 2u, s_rx, len);
}

/* Issue a transaction once per state; returns +1 done/ok, 0 running, -1 failed */
static int transact(bool (*issue)(void))
{
    if (!s_pending) {
        if (i2c2_busy()) {
            return 0;
        }
        if (!issue()) {
            return 0;
        }
        s_pending = true;
        return 0;
    }
    if (i2c2_busy()) {
        return 0;
    }
    s_pending = false;
    return (i2c2_result() == I2C_RES_OK) ? 1 : -1;
}

/* issue helpers for the current state (one transaction each) */
static bool issue_boot(void)    { return rd(REG_FIRMWARE_SYSTEM_STATUS, 1u); }
static bool issue_id(void)      { return rd(REG_IDENTIFICATION_MODEL_ID, 2u); }
static bool issue_status(void)  { return rd(REG_GPIO_TIO_HV_STATUS, 1u); }
static bool issue_osc(void)     { return rd(REG_RESULT_OSC_CALIBRATE_VAL, 2u); }
static bool issue_result(void)  { return rd(REG_RESULT_RANGE_STATUS, RESULT_LEN); }
static bool issue_start(void)   { return wr_val(REG_SYSTEM_MODE_START, 0x40u, 1u); }
static bool issue_stop(void)    { return wr_val(REG_SYSTEM_MODE_START, 0x00u, 1u); }
static bool issue_clear(void)   { return wr_val(REG_SYSTEM_INTERRUPT_CLEAR, 0x01u, 1u); }
static bool issue_vhv_b(void)   { return wr_val(REG_VHV_TIMEOUT_MACROP_LOOP_BOUND, 0x09u, 1u); }
static bool issue_vhv_i(void)   { return wr_val(REG_VHV_INIT, 0x00u, 1u); }

static bool issue_default(void)
{
    uint8_t cfg[DEFAULT_CONFIG_LEN];
    memcpy(cfg, s_default_config, sizeof(cfg));
    cfg[0x2Eu - REG_DEFAULT_CONFIG_START] |= 0x01u;   /* I2C pull-up at AVDD (2V8) */
    cfg[0x2Fu - REG_DEFAULT_CONFIG_START] |= 0x01u;   /* GPIO pull-up at AVDD (2V8) */
    cfg[0x30u - REG_DEFAULT_CONFIG_START] = 0x11u;    /* GPIO1 active low */
    return wr(REG_DEFAULT_CONFIG_START, cfg, DEFAULT_CONFIG_LEN);
}

static bool issue_cfg_item(void)
{
    const reg_write_t *w = &s_cfg[s_cfg_i];
    return wr_val(w->reg, w->val, w->len);
}

static bool issue_imp(void)
{
    uint32_t period_ms = (uint32_t)s_tb_ms + 4u;      /* inter-measurement >= timing budget */
    uint32_t v = (uint32_t)((float)(s_osc & 0x3FFu) * (float)period_ms * 1.075f);
    return wr_val(REG_INTERMEASUREMENT_PERIOD, v, 4u);
}

static void build_config_list(void)
{
    s_cfg_n = 0u;
#define CFG(r, l, v) s_cfg[s_cfg_n++] = (reg_write_t){ (r), (l), (v) }
    if (s_mode == VL53L1_DIST_SHORT) {
        CFG(REG_PHASECAL_TIMEOUT_MACROP, 1u, 0x14u);
        CFG(REG_RANGE_VCSEL_PERIOD_A, 1u, 0x07u);
        CFG(REG_RANGE_VCSEL_PERIOD_B, 1u, 0x05u);
        CFG(REG_RANGE_VALID_PHASE_HIGH, 1u, 0x38u);
        CFG(REG_SD_CONFIG_WOI_SD0, 2u, 0x0705u);
        CFG(REG_SD_CONFIG_INITIAL_PHASE_SD0, 2u, 0x0606u);
    } else {
        CFG(REG_PHASECAL_TIMEOUT_MACROP, 1u, 0x0Au);
        CFG(REG_RANGE_VCSEL_PERIOD_A, 1u, 0x0Fu);
        CFG(REG_RANGE_VCSEL_PERIOD_B, 1u, 0x0Du);
        CFG(REG_RANGE_VALID_PHASE_HIGH, 1u, 0xB8u);
        CFG(REG_SD_CONFIG_WOI_SD0, 2u, 0x0F0Du);
        CFG(REG_SD_CONFIG_INITIAL_PHASE_SD0, 2u, 0x0E0Eu);
    }
    CFG(REG_RANGE_TIMEOUT_MACROP_A_HI, 2u, s_tb_a);
    CFG(REG_RANGE_TIMEOUT_MACROP_B_HI, 2u, s_tb_b);
    /* part-to-part offset (mm x 4), inner/outer offsets 0 */
    CFG(REG_PART_TO_PART_RANGE_OFFSET_MM, 2u, (uint16_t)(int16_t)(s_offset_mm * 4));
    CFG(REG_PART_TO_PART_RANGE_OFFSET_MM + 2u, 2u, 0u);
    CFG(REG_PART_TO_PART_RANGE_OFFSET_MM + 4u, 2u, 0u);
#undef CFG
}

static void fail(uint32_t now)
{
    s_errors++;
    if (++s_retries >= MAX_RETRIES) {
        s_healthy = false;
        enter(VL_BACKOFF, now);
    } else if (s_state >= VL_RUN_WAIT) {
        enter(VL_RUN_WAIT, now);            /* transient error while ranging */
    } else {
        enter(VL_RESET_LOW, now);
    }
}

static void publish(uint32_t now)
{
    vl53l1_sample_t smp;
    smp.t_ms = now;
    smp.status = vl53l1_map_status(s_rx[0]);
    smp.spads = s_rx[3];
    smp.ambient_kcps = (uint16_t)(((uint32_t)s_rx[7] << 8 | s_rx[8]) * 8u / 1000u);
    smp.dist_mm = (uint16_t)((uint16_t)s_rx[13] << 8 | s_rx[14]);
    smp.signal_kcps = (uint16_t)(((uint32_t)s_rx[15] << 8 | s_rx[16]) * 8u / 1000u);

    s_sample_lock++;
    __DMB();
    smp.seq = s_sample.seq + 1u;
    s_sample = smp;
    __DMB();
    s_sample_lock++;

    s_rate_n++;
    if ((now - s_rate_t0) >= 1000u) {
        s_rate_hz = (uint8_t)((s_rate_n > 255u) ? 255u : s_rate_n);
        s_rate_n = 0u;
        s_rate_t0 = now;
    }
}

void vl53l1_init(vl53l1_dist_mode_t mode, uint16_t timing_budget_ms, int16_t offset_mm)
{
    s_mode = mode;
    s_tb_ms = vl53l1_pick_budget(mode, timing_budget_ms, &s_tb_a, &s_tb_b);
    s_offset_mm = offset_mm;

    gpio_clock_enable(TOF_XSHUT_PORT);
    gpio_output(TOF_XSHUT_PORT, TOF_XSHUT_PIN, GPIO_OTYPE_OD, GPIO_SPEED_LOW, false);
    gpio_clock_enable(TOF_GPIO1_PORT);
    gpio_input(TOF_GPIO1_PORT, TOF_GPIO1_PIN, GPIO_PULL_NONE);   /* 47k pull-up to 2V8 on board */
    exti_config(TOF_GPIO1_PORT, TOF_GPIO1_PIN, EXTI_EDGE_FALLING);
    NVIC_SetPriority(PPM_EXTI_IRQn, IRQ_PRIO_PPM);               /* shared EXTI15_10 vector */
    NVIC_EnableIRQ(PPM_EXTI_IRQn);
    exti_enable(TOF_GPIO1_PIN, true);

    i2c2_init(TOF_I2C_HZ);
    s_healthy = false;
    s_retries = 0u;
    enter(VL_RESET_LOW, 0u);
}

void vl53l1_gpio_isr(void)
{
    s_irq = true;
}

void vl53l1_task(uint32_t now)
{
    i2c2_poll(now);
    uint32_t dt = now - s_t_state;
    int r;

    switch (s_state) {
    case VL_OFF:
        break;

    case VL_RESET_LOW:
        xshut(false);
        if (dt >= 5u) {
            xshut(true);
            enter(VL_RESET_RELEASE, now);
        }
        break;

    case VL_RESET_RELEASE:
        if (dt >= 2u) {                     /* tBOOT 1.2 ms */
            enter(VL_BOOT_POLL, now);
        }
        break;

    case VL_BOOT_POLL:
        r = transact(issue_boot);
        if (r > 0 && (s_rx[0] & 0x01u) != 0u) {
            enter(VL_READ_ID, now);
        } else if (dt > BOOT_TIMEOUT_MS) {
            fail(now);
        }
        break;

    case VL_READ_ID:
        r = transact(issue_id);
        if (r > 0) {
            s_model_id = (uint16_t)((uint16_t)s_rx[0] << 8 | s_rx[1]);
            if ((s_model_id >> 8) != 0xEAu) {
                fail(now);                  /* not a VL53L1 */
            } else {
                enter(VL_WRITE_DEFAULT, now);
            }
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_WRITE_DEFAULT:
        r = transact(issue_default);
        if (r > 0) {
            enter(VL_VHV_START, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_VHV_START:
        r = transact(issue_start);
        if (r > 0) {
            enter(VL_VHV_POLL, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_VHV_POLL:
        r = transact(issue_status);
        if (r > 0 && (s_rx[0] & 0x01u) == 0u) {     /* active low: 0 = ready */
            enter(VL_VHV_CLEAR, now);
        } else if (r < 0 || dt > 500u) {
            fail(now);
        }
        break;

    case VL_VHV_CLEAR:
        r = transact(issue_clear);
        if (r > 0) {
            enter(VL_VHV_STOP, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_VHV_STOP:
        r = transact(issue_stop);
        if (r > 0) {
            enter(VL_VHV_BOUND, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_VHV_BOUND:
        r = transact(issue_vhv_b);
        if (r > 0) {
            enter(VL_VHV_INIT, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_VHV_INIT:
        r = transact(issue_vhv_i);
        if (r > 0) {
            build_config_list();
            s_cfg_i = 0u;
            enter(VL_CONFIG_LIST, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_CONFIG_LIST:
        r = transact(issue_cfg_item);
        if (r > 0) {
            if (++s_cfg_i >= s_cfg_n) {
                enter(VL_OSC_READ, now);
            }
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_OSC_READ:
        r = transact(issue_osc);
        if (r > 0) {
            s_osc = (uint16_t)((uint16_t)s_rx[0] << 8 | s_rx[1]);
            enter(VL_IMP_WRITE, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_IMP_WRITE:
        r = transact(issue_imp);
        if (r > 0) {
            enter(VL_START, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_START:
        r = transact(issue_start);
        if (r > 0) {
            s_irq = false;
            s_healthy = true;
            s_retries = 0u;
            s_rate_t0 = now;
            enter(VL_RUN_WAIT, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_RUN_WAIT:
        /* interrupt first, polling as a fall-back */
        if (s_irq) {
            s_irq = false;
            enter(VL_READ_RESULT, now);
        } else if (dt >= POLL_PERIOD_MS) {
            enter(VL_RUN_POLL, now);
        }
        if (dt > 4u * (uint32_t)s_tb_ms + 100u) {
            s_healthy = false;              /* no data for a long time */
        }
        break;

    case VL_RUN_POLL:
        r = transact(issue_status);
        if (r > 0) {
            if ((s_rx[0] & 0x01u) == 0u) {
                enter(VL_READ_RESULT, now);
            } else {
                s_state = VL_RUN_WAIT;      /* keep the original wait start time */
            }
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_READ_RESULT:
        r = transact(issue_result);
        if (r > 0) {
            publish(now);
            s_healthy = true;
            s_retries = 0u;
            enter(VL_CLEAR, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_CLEAR:
        r = transact(issue_clear);
        if (r > 0) {
            s_irq = false;
            enter(VL_RUN_WAIT, now);
        } else if (r < 0) {
            fail(now);
        }
        break;

    case VL_BACKOFF:
        xshut(false);
        if (dt >= 500u) {
            s_retries = 0u;
            enter(VL_RESET_LOW, now);
        }
        break;

    default:
        enter(VL_RESET_LOW, now);
        break;
    }
}

bool vl53l1_get(vl53l1_sample_t *out)
{
    uint32_t a, b;
    do {
        a = s_sample_lock;
        __DMB();
        *out = s_sample;
        __DMB();
        b = s_sample_lock;
    } while (a != b || (a & 1u) != 0u);
    return out->seq != 0u;
}

bool vl53l1_healthy(void)
{
    return s_healthy;
}

uint16_t vl53l1_model_id(void)
{
    return s_model_id;
}

uint32_t vl53l1_errors(void)
{
    return s_errors;
}

uint8_t vl53l1_rate_hz(void)
{
    return s_rate_hz;
}

uint16_t vl53l1_timing_budget(void)
{
    return s_tb_ms;
}
