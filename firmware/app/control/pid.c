/**
 * @file    pid.c
 * @brief   Rate PID axis.
 */
#include "pid.h"

#include <string.h>
#include "maths.h"

void pid_init(pid_axis_t *p)
{
    memset(p, 0, sizeof(*p));
    biquad_init_lpf(&p->d_lpf, 0.0f, 1000.0f);
    pt1_init(&p->d_lpf2, 0.0f, 0.001f);
}

void pid_set_gains(pid_axis_t *p, float P, float I, float D, float F, float i_limit)
{
    p->kp = P * PID_P_SCALE;
    p->ki = I * PID_I_SCALE;
    p->kd = D * PID_D_SCALE;
    p->kf = F * PID_F_SCALE;
    p->i_limit = i_limit;
    p->i = constrainf(p->i, -i_limit, i_limit);
}

void pid_set_dterm_filters(pid_axis_t *p, float lpf_hz, float lpf2_hz, float loop_hz, bool reset)
{
    if (reset) {
        biquad_init_lpf(&p->d_lpf, lpf_hz, loop_hz);
        pt1_init(&p->d_lpf2, lpf2_hz, 1.0f / loop_hz);
    } else {
        biquad_set_lpf(&p->d_lpf, lpf_hz, loop_hz);
        pt1_set_cutoff(&p->d_lpf2, lpf2_hz, 1.0f / loop_hz);
    }
}

float pid_update(pid_axis_t *p, float sp, float meas, float dt, bool integrate, bool saturated)
{
    if (dt <= 0.0f) {
        return p->sum;
    }
    if (!p->primed) {
        p->prev_meas = meas;
        p->prev_sp = sp;
        biquad_reset(&p->d_lpf, 0.0f);
        pt1_reset(&p->d_lpf2, 0.0f);
        p->primed = true;
    }

    float err = sp - meas;

    /* P */
    p->p_out = p->kp * err;

    /* I with conditional integration */
    if (integrate) {
        float di = p->ki * err * dt;
        if (!saturated || (di * p->i) < 0.0f) {
            p->i = constrainf(p->i + di, -p->i_limit, p->i_limit);
        }
    }
    p->i_out = p->i;

    /* D on measurement (no derivative kick on setpoint steps) */
    float d_raw = -(meas - p->prev_meas) / dt;
    float d_f = pt1_apply(&p->d_lpf2, biquad_apply(&p->d_lpf, d_raw));
    p->d_out = p->kd * d_f;
    p->prev_meas = meas;

    /* feed-forward on setpoint rate of change */
    p->f_out = p->kf * (sp - p->prev_sp) / dt;
    p->prev_sp = sp;

    p->sum = p->p_out + p->i_out + p->d_out + p->f_out;
    return p->sum;
}

void pid_reset_integrator(pid_axis_t *p)
{
    p->i = 0.0f;
}

void pid_reset(pid_axis_t *p)
{
    p->i = 0.0f;
    p->primed = false;
    p->p_out = p->i_out = p->d_out = p->f_out = p->sum = 0.0f;
}
