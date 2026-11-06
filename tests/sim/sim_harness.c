/**
 * @file    sim_harness.c
 * @brief   Closed-loop simulation harness around fc_update().
 */
#include "sim_harness.h"

#include <math.h>
#include <string.h>
#include "params.h"
#include "proto.h"

#define PHYS_SUBSTEPS   4

static double rnd_uniform(sim_t *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return ((double)(s->rng >> 8) + 0.5) / 16777216.0;
}

static double rnd_gauss(sim_t *s)
{
    double u1 = rnd_uniform(s), u2 = rnd_uniform(s);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

void sim_init(sim_t *s)
{
    params_reset_defaults();
    fc_init(1000.0f);
    sim_init_env(s);
}

void sim_init_env(sim_t *s)
{
    memset(s, 0, sizeof(*s));
    quad_init(&s->quad);

    s->rng = 12345u;
    s->tx_on = true;
    for (int i = 0; i < 8; i++) {
        s->ch[i] = 1500;
    }
    s->ch[SIM_CH_THR] = 1000;
    s->ch[SIM_CH_ARM] = 1000;
    s->ch[SIM_CH_MODE] = 1000;          /* low = ANGLE */
    s->gyro_bias[0] = 0.8;
    s->gyro_bias[1] = -0.6;
    s->gyro_bias[2] = 0.4;
    s->gyro_noise = 0.15;
    s->acc_noise = 0.004;
    s->vib_gyro = 6.0;
    s->vib_acc = 0.25;
    s->vib_hz = 390.0;
    s->tof_on = true;
    s->tof_noise = 0.006;
    s->tof_period_ms = 33.0;
    s->tof_latency_ms = 17.0;
    s->v_comp = s->v_slow = s->quad.v_bat;
}

void sim_sticks(sim_t *s, double roll, double pitch, double thr, double yaw)
{
    s->ch[SIM_CH_ROLL] = (uint16_t)lround(1500.0 + 500.0 * roll);
    s->ch[SIM_CH_PITCH] = (uint16_t)lround(1500.0 + 500.0 * pitch);
    s->ch[SIM_CH_THR] = (uint16_t)lround(1000.0 + 1000.0 * thr);
    s->ch[SIM_CH_YAW] = (uint16_t)lround(1500.0 + 500.0 * yaw);
}

void sim_channel(sim_t *s, int ch, uint16_t us)
{
    s->ch[ch] = us;
}

void sim_step(sim_t *s)
{
    quadsim_t *q = &s->quad;

    /* --- physics at 4 kHz with the duty from the last loop --- */
    double duty[4];
    for (int i = 0; i < 4; i++) {
        duty[i] = s->props_removed ? 0.0 : s->out.duty[i];
    }
    for (int k = 0; k < PHYS_SUBSTEPS; k++) {
        quad_step(q, duty, 0.001 / PHYS_SUBSTEPS);
    }
    q->v_oc -= s->drain_v_per_s * 0.001;
    s->ms++;
    s->t = s->ms * 0.001;

    /* --- height history for ToF latency --- */
    /* ToF sits 15 mm above the ground when landed (tof_offset_mm = 15) */
    s->h_hist[s->h_idx % 256u] = (quad_height(q) + 0.015) / fmax(quad_cos_tilt(q), 0.2);
    s->h_idx++;

    fc_input_t *in = &s->in;
    in->now_ms = s->ms;

    /* --- IMU --- */
    double g[3], a[3];
    quad_imu(q, g, a);
    double thr_frac = (q->thrust[0] + q->thrust[1] + q->thrust[2] + q->thrust[3]) / (4.0 * q->t_max);
    double vib = sin(2.0 * M_PI * s->vib_hz * s->t);
    double vib2 = cos(2.0 * M_PI * s->vib_hz * 1.37 * s->t);
    in->imu.dt = 0.001f;
    in->imu.t_us = s->ms * 1000u;
    in->imu.gyro_dps.x = (float)(g[0] + s->gyro_bias[0] + s->gyro_noise * rnd_gauss(s) + s->vib_gyro * thr_frac * vib);
    in->imu.gyro_dps.y = (float)(g[1] + s->gyro_bias[1] + s->gyro_noise * rnd_gauss(s) + s->vib_gyro * thr_frac * vib2);
    in->imu.gyro_dps.z = (float)(g[2] + s->gyro_bias[2] + s->gyro_noise * rnd_gauss(s) + 0.5 * s->vib_gyro * thr_frac * vib);
    in->imu.acc_g.x = (float)(a[0] + s->acc_noise * rnd_gauss(s) + s->vib_acc * thr_frac * vib2);
    in->imu.acc_g.y = (float)(a[1] + s->acc_noise * rnd_gauss(s) + s->vib_acc * thr_frac * vib);
    in->imu.acc_g.z = (float)(a[2] + s->acc_noise * rnd_gauss(s) + s->vib_acc * thr_frac * vib);
    if (s->imu_override) {
        in->imu.gyro_dps = v3((float)s->gyro_bias[0], (float)s->gyro_bias[1], (float)s->gyro_bias[2]);
        in->imu.acc_g = v3((float)s->override_acc[0], (float)s->override_acc[1], (float)s->override_acc[2]);
    }
    in->imu.temp_c = 30.0f;
    in->imu.healthy = true;

    /* --- RC: one PPM frame every 20 ms --- */
    if (s->tx_on && s->t * 1000.0 >= s->rc_next_ms) {
        s->rc_next_ms += 20.0;
        s->rc_seq++;
        s->rc_last_ms = s->ms;
        in->rc.count = 8u;
        in->rc.rate_hz = 50u;
        for (int i = 0; i < 8; i++) {
            in->rc.ch[i] = s->ch[i];
        }
        in->rc.frame_seq = s->rc_seq;
    }
    in->rc.age_ms = s->ms - s->rc_last_ms;
    if (!s->tx_on && s->rc_seq == 0u) {
        in->rc.age_ms = 100000u;
    }

    /* --- battery --- */
    if (!s->bat_external) {
        double kc = 0.001 / (1.0 / (2.0 * M_PI * 2.0) + 0.001);
        double ks = 0.001 / (1.0 / (2.0 * M_PI * 0.2) + 0.001);
        s->v_comp += (q->v_bat - s->v_comp) * kc;
        s->v_slow += (q->v_bat - s->v_slow) * ks;
        in->bat.present = true;
        in->bat.v_comp = (float)s->v_comp;
        in->bat.v_slow = (float)s->v_slow;
        in->bat.state = (s->v_slow < g_cfg.bat_crit_v) ? BAT_CRIT
                      : ((s->v_slow < g_cfg.bat_warn_v) ? BAT_WARN : BAT_OK);
    }

    /* --- ToF --- */
    in->range.healthy = s->tof_on;
    if (s->tof_on && s->t * 1000.0 >= s->tof_next_ms) {
        s->tof_next_ms += s->tof_period_ms;
        unsigned lag = (unsigned)s->tof_latency_ms;
        double d = s->h_hist[(s->h_idx - 1u - lag) % 256u];
        d += s->tof_noise * (1.0 + d) * rnd_gauss(s);
        in->range.seq++;
        in->range.age_ms = 0u;
        in->range.dist_m = (float)d;
        in->range.status = (d < 0.04 || d > 3.5) ? 4u : 0u;
    }

    fc_update(in, &s->out);
    in->cal_request = 0u;

    if (s->log != NULL && (s->ms % 10u) == 0u) {
        double r, p, y;
        quad_euler_deg(q, &r, &p, &y);
        fc_status_t st;
        fc_get_status(&st);
        fprintf(s->log, "%.3f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%d,%d,%.3f,%.3f\n",
                s->t, r, p, y, quad_height(q), st.h_est, st.collective, st.hover,
                st.mode, (st.flags & STF_ARMED) ? 1 : 0, q->v_bat, st.bat_factor);
    }
}

void sim_run(sim_t *s, double seconds)
{
    uint32_t n = (uint32_t)lround(seconds * 1000.0);
    for (uint32_t i = 0; i < n; i++) {
        sim_step(s);
    }
}

bool sim_arm(sim_t *s, double timeout_s)
{
    s->ch[SIM_CH_ARM] = 1000;
    sim_run(s, 0.1);
    s->ch[SIM_CH_ARM] = 2000;
    double t_end = s->t + timeout_s;
    while (s->t < t_end) {
        sim_run(s, 0.01);
        if (fc_is_armed()) {
            return true;
        }
    }
    return false;
}

double sim_height(const sim_t *s)
{
    return quad_height(&s->quad);
}

void sim_euler(const sim_t *s, double *roll, double *pitch, double *yaw)
{
    quad_euler_deg(&s->quad, roll, pitch, yaw);
}
