/**
 * @file    rc.c
 * @brief   RC processing.
 */
#include "rc.h"

#include <string.h>
#include "config.h"
#include "maths.h"

#define SWITCH_BAND_US      200.0f      /* mode switch: mid +/- 200 us = middle */
#define PLAUSIBLE_MARGIN_US 200.0f

static float channel_us(const rc_frame_t *f, int32_t ch1)
{
    if (ch1 < 1 || ch1 > (int32_t)f->count) {
        return -1.0f;
    }
    return (float)f->ch[ch1 - 1];
}

static float stick_norm(float us)
{
    float mid = g_cfg.rc_mid_us;
    float d = deadbandf(us - mid, g_cfg.rc_deadband_us);
    float half = (d >= 0.0f) ? (g_cfg.rc_max_us - mid) : (mid - g_cfg.rc_min_us);
    half -= g_cfg.rc_deadband_us;
    if (half < 50.0f) {
        half = 50.0f;
    }
    return constrainf(d / half, -1.0f, 1.0f);
}

static float throttle_norm(float us)
{
    float span = g_cfg.rc_max_us - g_cfg.rc_min_us;
    if (span < 100.0f) {
        span = 100.0f;
    }
    return constrainf((us - g_cfg.rc_min_us) / span, 0.0f, 1.0f);
}

static void configure(rc_state_t *rc)
{
    rc->cfg_rev = g_cfg_revision;
    for (int i = 0; i < 4; i++) {
        biquad_set_lpf(&rc->lpf[i], g_cfg.rc_smooth_hz, rc->loop_hz);
    }
}

void rc_init(rc_state_t *rc, float loop_hz)
{
    memset(rc, 0, sizeof(*rc));
    rc->loop_hz = loop_hz;
    for (int i = 0; i < 4; i++) {
        biquad_init_lpf(&rc->lpf[i], g_cfg.rc_smooth_hz, loop_hz);
    }
    rc->cfg_rev = g_cfg_revision;
}

void rc_update(rc_state_t *rc, const rc_frame_t *f, uint32_t now_ms)
{
    if (rc->cfg_rev != g_cfg_revision) {
        configure(rc);
    }

    float r_us = channel_us(f, g_cfg.rc_map[0]);
    float p_us = channel_us(f, g_cfg.rc_map[1]);
    float t_us = channel_us(f, g_cfg.rc_map[2]);
    float y_us = channel_us(f, g_cfg.rc_map[3]);

    bool valid = f->count >= 4u && f->age_ms < (uint32_t)g_cfg.rx_fs_ms &&
                 r_us > 0.0f && p_us > 0.0f && t_us > 0.0f && y_us > 0.0f;
    float lo = g_cfg.rc_min_us - PLAUSIBLE_MARGIN_US;
    float hi = g_cfg.rc_max_us + PLAUSIBLE_MARGIN_US;
    if (valid && (t_us < lo || t_us > hi || r_us < lo || r_us > hi)) {
        valid = false;
    }
    if (valid && g_cfg.rx_fs_thr_us > 0.0f && t_us < g_cfg.rx_fs_thr_us) {
        valid = false;                  /* receiver signals failsafe via low throttle */
    }

    if (valid) {
        if (!rc->valid) {
            rc->valid_since_ms = now_ms;
        }
        rc->valid_ms = now_ms - rc->valid_since_ms;

        float raw[4] = { stick_norm(r_us), stick_norm(p_us), stick_norm(y_us), throttle_norm(t_us) };
        if (!rc->primed) {
            for (int i = 0; i < 4; i++) {
                biquad_reset(&rc->lpf[i], raw[i]);
            }
            rc->primed = true;
        }
        rc->roll = constrainf(biquad_apply(&rc->lpf[0], raw[0]), -1.0f, 1.0f);
        rc->pitch = constrainf(biquad_apply(&rc->lpf[1], raw[1]), -1.0f, 1.0f);
        rc->yaw = constrainf(biquad_apply(&rc->lpf[2], raw[2]), -1.0f, 1.0f);
        rc->throttle = constrainf(biquad_apply(&rc->lpf[3], raw[3]), 0.0f, 1.0f);
        rc->throttle_raw = raw[3];

        float arm_us = channel_us(f, g_cfg.rc_ch_arm);
        rc->arm = arm_us >= g_cfg.rc_arm_us;

        float mode_us = channel_us(f, g_cfg.rc_ch_mode);
        if (mode_us < 0.0f) {
            rc->mode_pos = 0u;
        } else if (mode_us < g_cfg.rc_mid_us - SWITCH_BAND_US) {
            rc->mode_pos = 0u;
        } else if (mode_us > g_cfg.rc_mid_us + SWITCH_BAND_US) {
            rc->mode_pos = 2u;
        } else {
            rc->mode_pos = 1u;
        }

        float beep_us = channel_us(f, g_cfg.rc_ch_beeper);
        rc->beeper = beep_us >= g_cfg.rc_arm_us;
    } else {
        rc->valid_ms = 0u;
        rc->arm = false;
        rc->beeper = false;
        rc->primed = false;
    }
    rc->valid = valid;
}

float rc_expo(float x, float expo)
{
    return x * (1.0f - expo + expo * x * x);
}

float rc_stick_to_rate(float stick, float rate_max, float expo)
{
    return rc_expo(constrainf(stick, -1.0f, 1.0f), expo) * rate_max;
}

float rc_throttle_curve(float t, float mid, float expo)
{
    t = constrainf(t, 0.0f, 1.0f);
    mid = constrainf(mid, 0.05f, 0.95f);
    float range = (t < mid) ? mid : (1.0f - mid);
    float x = (t - mid) / range;
    return constrainf(mid + rc_expo(x, expo) * range, 0.0f, 1.0f);
}
