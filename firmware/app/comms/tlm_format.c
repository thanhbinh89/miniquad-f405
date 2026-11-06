/**
 * @file    tlm_format.c
 * @brief   Flight-core snapshot -> packed telemetry records.
 */
#include "tlm_format.h"

#include <string.h>

int16_t tlm_s16(float v, float scale)
{
    float x = v * scale;
    if (!(x == x)) {                        /* NaN */
        return 0;
    }
    return (int16_t)((x > 32767.0f) ? 32767.0f : ((x < -32768.0f) ? -32768.0f : x));
}

uint16_t tlm_u16(float v, float scale)
{
    float x = v * scale;
    if (!(x == x)) {
        return 0u;
    }
    return (uint16_t)((x > 65535.0f) ? 65535.0f : ((x < 0.0f) ? 0.0f : x + 0.5f));
}

static int8_t s8(float v)
{
    return (int8_t)((v > 127.0f) ? 127.0f : ((v < -128.0f) ? -128.0f : v));
}

void tlm_format_status(const fc_status_t *st, const tlm_extra_t *x, tlm_status_t *t)
{
    memset(t, 0, sizeof(*t));
    t->time_ms = x->time_ms;
    t->flags = (st->flags & ~x->flags_clear) | x->flags_set;
    t->arm_block = st->arm_block | x->arm_block_extra;
    t->mode = st->mode;
    t->fs_phase = st->fs_phase;
    t->bat_state = x->bat_state;
    t->cpu_load = x->cpu_load;
    t->roll_cd = tlm_s16(st->roll, 100.0f);
    t->pitch_cd = tlm_s16(st->pitch, 100.0f);
    t->yaw_cd = tlm_s16(st->yaw, 100.0f);
    t->vbat_mv = tlm_u16(x->v_comp, 1000.0f);
    t->vbat_slow_mv = tlm_u16(x->v_slow, 1000.0f);
    t->bat_comp_x1000 = tlm_u16(st->bat_factor, 1000.0f);
    t->loop_us_avg = x->loop_us_avg;
    t->loop_us_max = x->loop_us_max;
    t->loop_hz = x->loop_hz;
    t->mcu_temp_c = s8(x->mcu_temp);
    t->imu_temp_c = s8(st->imu_temp);

    if (x->rc != NULL) {
        t->rc_count = x->rc->count;
        t->rc_rate_hz = x->rc->rate_hz;
        for (int i = 0; i < 8; i++) {
            t->rc_us[i] = (i < (int)x->rc->count && x->rc->age_ms < 1000u) ? x->rc->ch[i] : 0u;
        }
    }
    for (int i = 0; i < 4; i++) {
        t->motor[i] = tlm_u16(st->duty[i], 1000.0f);
    }
    t->tof_mm = x->tof_have ? x->tof_mm : 0u;
    t->tof_status = x->tof_have ? x->tof_status : 255u;
    t->tof_rate_hz = x->tof_rate_hz;
    t->alt_cm = tlm_s16(st->h_est, 100.0f);
    t->vz_cms = tlm_s16(st->vz_est, 100.0f);
    t->alt_sp_cm = tlm_s16(st->h_sp, 100.0f);
    t->throttle_x1000 = tlm_u16(st->collective, 1000.0f);
    t->hover_x1000 = tlm_u16(st->hover, 1000.0f);
    t->imu_errors = (uint16_t)((x->imu_errors > 65535u) ? 65535u : x->imu_errors);
    t->tof_errors = (uint16_t)((x->tof_errors > 65535u) ? 65535u : x->tof_errors);
    t->rx_errors = (uint16_t)((x->rx_errors > 65535u) ? 65535u : x->rx_errors);
    t->link_drops = (uint16_t)((x->link_drops > 65535u) ? 65535u : x->link_drops);
}

void tlm_format_alt(const fc_status_t *st, uint32_t time_us, tlm_alt_t *a)
{
    memset(a, 0, sizeof(*a));
    a->time_us = time_us;
    a->h_est = st->h_est;
    a->vz_est = st->vz_est;
    a->h_sp = st->h_sp;
    a->vz_sp = st->vz_sp;
    a->h_tof = st->h_tof;
    a->acc_up = st->acc_up;
    a->thrust = st->collective;
    a->hover = st->hover;
    a->tof_status = st->tof_status;
    a->flags = (uint8_t)st->alt_flags;
}
