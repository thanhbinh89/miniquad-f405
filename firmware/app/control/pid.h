/**
 * @file    pid.h
 * @brief   Rate-loop PID axis: P on error, I with anti-windup, D on
 *          measurement (filtered), feed-forward on setpoint derivative.
 *
 * Gains use Betaflight-like scaling so familiar numbers apply:
 *   out = P*3.2e-5*err + I*2.44e-4*integral(err) - D*5.29e-7*d(gyro)/dt
 *         + F*1.375e-5*d(setpoint)/dt
 * with err in deg/s and out in normalised thrust (1.0 = full motor range).
 */
#ifndef PID_H
#define PID_H

#include <stdbool.h>
#include "filters.h"

#define PID_P_SCALE     3.2029e-5f
#define PID_I_SCALE     2.44381e-4f
#define PID_D_SCALE     5.29e-7f
#define PID_F_SCALE     1.3754e-5f

typedef struct {
    float    kp, ki, kd, kf;    /* scaled gains */
    float    i_limit;
    float    i;                 /* integrator (normalised output units) */
    float    prev_meas;
    float    prev_sp;
    bool     primed;
    biquad_t d_lpf;
    pt1_t    d_lpf2;
    float    p_out, i_out, d_out, f_out, sum;
} pid_axis_t;

void  pid_init(pid_axis_t *p);
void  pid_set_gains(pid_axis_t *p, float P, float I, float D, float F, float i_limit);
void  pid_set_dterm_filters(pid_axis_t *p, float lpf_hz, float lpf2_hz, float loop_hz, bool reset);
/**
 * @param integrate  false freezes the integrator (landed, throttle cut...)
 * @param saturated  mixer clipped last loop: integrate only towards zero
 */
float pid_update(pid_axis_t *p, float sp, float meas, float dt, bool integrate, bool saturated);
void  pid_reset_integrator(pid_axis_t *p);
void  pid_reset(pid_axis_t *p);

#endif /* PID_H */
