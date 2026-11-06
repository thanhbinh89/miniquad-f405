/**
 * @file    notify.c
 * @brief   LED / buzzer pattern engine (100 Hz).
 */
#include "notify.h"

#include "buzzer.h"
#include "config.h"
#include "led.h"
#include "proto.h"

/* tone step: frequency in % of beep_hz (0 = silence), duration in 10 ms */
typedef struct {
    uint8_t pct;
    uint8_t ticks;
} step_t;

typedef struct {
    const step_t *steps;
    uint8_t       count;
    uint16_t      mask;
} seq_t;

#define SEQ(arr, m) { arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), (m) }

static const step_t s_startup[] = { { 75, 8 }, { 0, 3 }, { 100, 8 }, { 0, 3 }, { 125, 14 } };
static const step_t s_arm[]     = { { 100, 30 } };
static const step_t s_disarm[]  = { { 100, 8 }, { 0, 6 }, { 100, 8 } };
static const step_t s_cal_ok[]  = { { 125, 6 }, { 0, 4 }, { 125, 6 } };
static const step_t s_cal_bad[] = { { 60, 40 } };
static const step_t s_error[]   = { { 60, 10 }, { 0, 6 }, { 60, 10 }, { 0, 6 }, { 60, 10 } };
static const step_t s_mode[]    = { { 125, 4 } };
static const step_t s_save[]    = { { 100, 4 }, { 0, 3 }, { 125, 4 } };
static const step_t s_test[]    = { { 75, 10 }, { 100, 10 }, { 125, 10 } };

static const seq_t s_tones[TONE_COUNT] = {
    [TONE_NONE]     = { 0, 0, 0 },
    [TONE_STARTUP]  = SEQ(s_startup, NOTIFY_MASK_STARTUP),
    [TONE_ARM]      = SEQ(s_arm, NOTIFY_MASK_ARMING),
    [TONE_DISARM]   = SEQ(s_disarm, NOTIFY_MASK_ARMING),
    [TONE_CAL_OK]   = SEQ(s_cal_ok, NOTIFY_MASK_CAL),
    [TONE_CAL_FAIL] = SEQ(s_cal_bad, NOTIFY_MASK_CAL),
    [TONE_ERROR]    = SEQ(s_error, NOTIFY_MASK_ERROR),
    [TONE_MODE]     = SEQ(s_mode, NOTIFY_MASK_MODE),
    [TONE_SAVE_OK]  = SEQ(s_save, 0xFFFFu),
    [TONE_TEST]     = SEQ(s_test, 0xFFFFu),
};

static notify_tone_t s_tone;
static uint8_t       s_step;
static uint8_t       s_left;
static uint32_t      s_tick;

void notify_init(void)
{
    s_tone = TONE_NONE;
    s_tick = 0u;
}

void notify_play(notify_tone_t tone)
{
    if (tone <= TONE_NONE || tone >= TONE_COUNT) {
        return;
    }
    if (((uint32_t)g_cfg.beep_mask & s_tones[tone].mask) == 0u) {
        return;
    }
    s_tone = tone;
    s_step = 0u;
    s_left = s_tones[tone].steps[0].ticks;
}

void notify_event(fc_event_t ev)
{
    switch (ev) {
    case FCEV_ARMED:            notify_play(TONE_ARM); break;
    case FCEV_DISARMED:
    case FCEV_FS_DISARM:
    case FCEV_CRASH:
    case FCEV_EMERGENCY:        notify_play(TONE_DISARM); break;
    case FCEV_GYRO_CAL_DONE:
    case FCEV_ACC_CAL_DONE:     notify_play(TONE_CAL_OK); break;
    case FCEV_ACC_CAL_FAILED:   notify_play(TONE_CAL_FAIL); break;
    case FCEV_ARM_BLOCKED:
    case FCEV_ALT_UNAVAILABLE:  notify_play(TONE_ERROR); break;
    case FCEV_MODE_ACRO:
    case FCEV_MODE_ANGLE:
    case FCEV_MODE_ALT:         notify_play(TONE_MODE); break;
    default:                    break;
    }
}

static uint32_t tone_hz(uint8_t pct)
{
    return ((uint32_t)g_cfg.beep_hz * pct) / 100u;
}

/* repeating alarms: on/off in 10 ms ticks, period in ticks */
static uint32_t alarm_hz(const fc_status_t *st, bool beeper_switch)
{
    uint32_t mask = (uint32_t)g_cfg.beep_mask;
    uint32_t t = s_tick;
    if ((st->flags & STF_FAILSAFE) != 0u && (mask & NOTIFY_MASK_FAILSAFE) != 0u) {
        return ((t % 20u) < 10u) ? tone_hz(100) : 0u;
    }
    if (st->bat_state == BAT_CRIT && (mask & NOTIFY_MASK_BATTERY) != 0u) {
        return ((t % 50u) < 10u) ? tone_hz(100) : 0u;
    }
    if ((st->flags & STF_CRASHED) != 0u && (st->flags & STF_ARMED) == 0u &&
        (mask & NOTIFY_MASK_CRASH) != 0u) {
        return ((t % 100u) < 30u) ? tone_hz(75) : 0u;  /* help to find it */
    }
    if (beeper_switch && (mask & NOTIFY_MASK_BEEPER) != 0u) {
        uint32_t p = t % 120u;
        return (p < 20u) ? tone_hz(125) : ((p >= 40u && p < 60u) ? tone_hz(100) : 0u);
    }
    if (st->bat_state == BAT_WARN && (mask & NOTIFY_MASK_BATTERY) != 0u) {
        return ((t % 200u) < 6u) ? tone_hz(100) : 0u;
    }
    return 0u;
}

static uint32_t led_pattern(const fc_status_t *st)
{
    uint32_t f = st->flags;
    if ((f & STF_IMU_OK) == 0u) {
        return 0x0000FFFFu;
    }
    if ((f & STF_FAILSAFE) != 0u) {
        return 0x33333333u;
    }
    if (st->bat_state == BAT_CRIT) {
        return 0x0F0F0F0Fu;
    }
    if ((f & STF_CALIBRATING) != 0u) {
        return 0x55555555u;
    }
    if ((f & STF_MOTOR_TEST) != 0u) {
        return 0x00FF00FFu;
    }
    if ((f & STF_ARMED) != 0u) {
        return (st->bat_state == BAT_WARN) ? 0xFFFFFFF0u : 0xFFFFFFFFu;
    }
    return (st->arm_block == 0u) ? 0x00000003u : 0x00000033u;
}

void notify_update(const fc_status_t *st, uint32_t now_ms, bool beeper_switch)
{
    (void)now_ms;
    s_tick++;

    /* LED: 50 ms per bit */
    uint32_t bit = (s_tick / 5u) & 31u;
    led_set(((led_pattern(st) >> bit) & 1u) != 0u);

    /* buzzer: one-shot tone first, then alarms */
    uint32_t hz = 0u;
    if (s_tone != TONE_NONE) {
        const seq_t *q = &s_tones[s_tone];
        hz = tone_hz(q->steps[s_step].pct);
        if (s_left > 0u) {
            s_left--;
        }
        if (s_left == 0u) {
            if (++s_step >= q->count) {
                s_tone = TONE_NONE;
            } else {
                s_left = q->steps[s_step].ticks;
            }
        }
    } else {
        hz = alarm_hz(st, beeper_switch);
    }
    buzzer_tone(hz);
}
