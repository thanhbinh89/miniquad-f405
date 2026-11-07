/**
 * @file    battery.c
 * @brief   Battery voltage, VDDA and MCU temperature.
 */
#include "battery.h"

#include <string.h>
#include "board.h"
#include "config.h"
#include "maths.h"
#include "proto.h"

#define PRESENT_ON_V    2.6f
#define PRESENT_OFF_V   2.3f
#define USB_PRESENT_ON_V    3.45f   /* above anything the 3.3 V rail can back-feed */
#define USB_PRESENT_OFF_V   3.35f
#define SLOW_LPF_HZ     0.2f
#define DEBOUNCE_S      2.0f
#define RECOVER_S       5.0f
#define HYSTERESIS_V    0.15f

void battery_init(battery_t *b)
{
    memset(b, 0, sizeof(*b));
    b->state = BAT_NONE;
    b->vdda = 3.3f;
}

void battery_set_usb_power(battery_t *b, bool usb_power)
{
    b->usb_power = usb_power;
}

void battery_update(battery_t *b, const battery_cal_t *cal, uint16_t raw_vbat, uint16_t raw_vref,
                    uint16_t raw_temp, float dt)
{
    /* VDDA from the factory VREFINT calibration (the LDO is only +/-2 %) */
    if (raw_vref > 500u && cal->vrefint_cal > 500u) {
        b->vdda = constrainf(3.3f * (float)cal->vrefint_cal / (float)raw_vref, 2.8f, 3.6f);
    }
    float v_pin = (float)raw_vbat * b->vdda / 4095.0f;
    b->v_raw = v_pin * VBAT_DIVIDER * g_cfg.bat_vscale;

    /* temperature: two-point factory calibration, scaled to the actual VDDA */
    if (cal->ts_cal2 > cal->ts_cal1) {
        float t_raw = (float)raw_temp * b->vdda / 3.3f;
        b->mcu_temp = 30.0f + (t_raw - (float)cal->ts_cal1) * 80.0f /
                              (float)(cal->ts_cal2 - cal->ts_cal1);
    }

    bool was_present = b->present;
    float on_v = b->usb_power ? USB_PRESENT_ON_V : PRESENT_ON_V;
    float off_v = b->usb_power ? USB_PRESENT_OFF_V : PRESENT_OFF_V;
    if (!b->present && b->v_raw > on_v) {
        b->present = true;
    } else if (b->present && b->v_raw < off_v) {
        b->present = false;
    }
    if (!b->present) {
        b->state = BAT_NONE;
        b->v_comp = b->v_slow = b->v_raw;
        b->t_warn = b->t_crit = b->t_ok = 0.0f;
        return;
    }
    if (!was_present) {
        pt1_init(&b->lpf_comp, g_cfg.bat_lpf_hz, dt);
        pt1_init(&b->lpf_slow, SLOW_LPF_HZ, dt);
        pt1_reset(&b->lpf_comp, b->v_raw);
        pt1_reset(&b->lpf_slow, b->v_raw);
        b->state = BAT_OK;
    }
    pt1_set_cutoff(&b->lpf_comp, g_cfg.bat_lpf_hz, dt);
    b->v_comp = pt1_apply(&b->lpf_comp, b->v_raw);
    b->v_slow = pt1_apply(&b->lpf_slow, b->v_raw);

    float v = b->v_slow;
    b->t_crit = (v < g_cfg.bat_crit_v) ? b->t_crit + dt : 0.0f;
    b->t_warn = (v < g_cfg.bat_warn_v) ? b->t_warn + dt : 0.0f;

    if (b->t_crit >= DEBOUNCE_S) {
        b->state = BAT_CRIT;
    } else if (b->t_warn >= DEBOUNCE_S && b->state == BAT_OK) {
        b->state = BAT_WARN;
    }

    /* recovery needs a clear margin for a while (sag disappears on landing) */
    float recover_v = (b->state == BAT_CRIT) ? g_cfg.bat_crit_v : g_cfg.bat_warn_v;
    if (b->state != BAT_OK && v > recover_v + HYSTERESIS_V) {
        b->t_ok += dt;
        if (b->t_ok >= RECOVER_S) {
            b->state = (b->state == BAT_CRIT) ? BAT_WARN : BAT_OK;
            b->t_ok = 0.0f;
        }
    } else {
        b->t_ok = 0.0f;
    }
}
