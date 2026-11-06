/**
 * @file    altitude.c
 * @brief   Height / vertical speed / accel bias Kalman filter.
 */
#include "altitude.h"

#include <math.h>
#include <string.h>

#define GATE_SIGMA2         25.0f       /* 5 sigma */
#define REINIT_REJECTS      15u
#define BIAS_LIMIT          2.0f        /* m/s^2 */

static void init_on(alt_est_t *e, float z, float sigma)
{
    e->h = z;
    e->v = 0.0f;
    memset(e->P, 0, sizeof(e->P));
    e->P[0][0] = sigma * sigma;
    e->P[1][1] = 0.25f;
    e->P[2][2] = 0.25f;
    for (uint32_t i = 0u; i < ALT_HIST_LEN; i++) {
        e->hist[i] = z;
    }
    e->reject_run = 0u;
    e->initialized = true;
}

void alt_est_reset(alt_est_t *e)
{
    memset(e, 0, sizeof(*e));
}

void alt_est_predict(alt_est_t *e, float acc_up, float dt, float acc_noise, float bias_noise)
{
    if (!e->initialized) {
        return;
    }
    float a = acc_up - e->b;
    float dt2 = 0.5f * dt * dt;
    e->h += e->v * dt + a * dt2;
    e->v += a * dt;

    /* P = F P F' + Q, F = [1 dt -dt2; 0 1 -dt; 0 0 1] */
    float (*P)[3] = e->P;
    float FP[3][3];
    for (int j = 0; j < 3; j++) {
        FP[0][j] = P[0][j] + dt * P[1][j] - dt2 * P[2][j];
        FP[1][j] = P[1][j] - dt * P[2][j];
        FP[2][j] = P[2][j];
    }
    float N[3][3];
    for (int i = 0; i < 3; i++) {
        N[i][0] = FP[i][0] + dt * FP[i][1] - dt2 * FP[i][2];
        N[i][1] = FP[i][1] - dt * FP[i][2];
        N[i][2] = FP[i][2];
    }
    /* acceleration noise enters through G = [dt2, dt, 0] */
    float qa = acc_noise * acc_noise;
    N[0][0] += qa * dt2 * dt2;
    N[0][1] += qa * dt2 * dt;
    N[1][0] += qa * dt2 * dt;
    N[1][1] += qa * dt * dt;
    N[2][2] += bias_noise * bias_noise * dt;

    for (int i = 0; i < 3; i++) {
        for (int j = i; j < 3; j++) {
            float s = 0.5f * (N[i][j] + N[j][i]);
            P[i][j] = s;
            P[j][i] = s;
        }
    }

    e->hist[e->hist_idx % ALT_HIST_LEN] = e->h;
    e->hist_idx++;
}

bool alt_est_update(alt_est_t *e, float z, float sigma, uint32_t delay_samples)
{
    if (!isfinite(z)) {
        return false;
    }
    if (!e->initialized) {
        init_on(e, z, sigma);
        e->updates++;
        return true;
    }

    if (delay_samples >= ALT_HIST_LEN) {
        delay_samples = ALT_HIST_LEN - 1u;
    }
    float h_then = e->h;
    if (delay_samples > 0u && e->hist_idx > delay_samples) {
        h_then = e->hist[(e->hist_idx - 1u - delay_samples) % ALT_HIST_LEN];
    }

    float (*P)[3] = e->P;
    float y = z - h_then;
    float S = P[0][0] + sigma * sigma;
    e->last_innov = y;

    if (y * y > GATE_SIGMA2 * S) {
        e->rejects++;
        if (++e->reject_run >= REINIT_REJECTS) {
            init_on(e, z, sigma);
        }
        return false;
    }
    e->reject_run = 0u;

    float K0 = P[0][0] / S;
    float K1 = P[1][0] / S;
    float K2 = P[2][0] / S;
    /* shift the stored past estimates by the same correction so the next
     * delayed innovation does not count this one twice */
    for (uint32_t i = 0u; i < ALT_HIST_LEN; i++) {
        e->hist[i] += K0 * y;
    }
    e->h += K0 * y;
    e->v += K1 * y;
    e->b += K2 * y;
    if (e->b > BIAS_LIMIT) {
        e->b = BIAS_LIMIT;
    } else if (e->b < -BIAS_LIMIT) {
        e->b = -BIAS_LIMIT;
    }

    /* P = (I - K H) P, H = [1 0 0] */
    float r0[3] = { P[0][0], P[0][1], P[0][2] };
    for (int j = 0; j < 3; j++) {
        P[0][j] -= K0 * r0[j];
        P[1][j] -= K1 * r0[j];
        P[2][j] -= K2 * r0[j];
    }
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            float s = 0.5f * (P[i][j] + P[j][i]);
            P[i][j] = s;
            P[j][i] = s;
        }
    }
    e->updates++;
    return true;
}
