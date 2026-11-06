/**
 * @file    imu_cal.c
 * @brief   Still-window averaging for gyro bias / accelerometer level.
 *
 * Accumulators are double precision: 1000 samples of squares in float would
 * lose too much precision for the variance test.
 */
#include "imu_cal.h"

#include <math.h>
#include <string.h>

static void start(imu_cal_t *c, float window_s, float max_std, float max_abs, vec3_t expect,
                  uint32_t attempts)
{
    memset(c, 0, sizeof(*c));
    c->state = CAL_RUNNING;
    c->window_s = window_s;
    c->max_std = max_std;
    c->max_abs = max_abs;
    c->expect = expect;
    c->max_attempts = attempts;
}

void imu_cal_start_gyro(imu_cal_t *c)
{
    start(c, 0.75f, 1.5f, 25.0f, v3(0.0f, 0.0f, 0.0f), 0u);
}

void imu_cal_start_accel(imu_cal_t *c)
{
    /* level: the accelerometer reads -1 g on the body z axis (FRD) */
    start(c, 1.0f, 0.02f, 0.3f, v3(0.0f, 0.0f, -1.0f), 10u);
}

cal_state_t imu_cal_update(imu_cal_t *c, vec3_t s, float dt)
{
    if (c->state != CAL_RUNNING) {
        return c->state;
    }
    const float v[3] = { s.x, s.y, s.z };
    for (int i = 0; i < 3; i++) {
        c->sum[i] += (double)v[i];
        c->sum_sq[i] += (double)v[i] * (double)v[i];
    }
    c->n++;
    c->t += dt;
    if (c->t < c->window_s || c->n < 10u) {
        return c->state;
    }

    bool still = true;
    float mean[3];
    const float expect[3] = { c->expect.x, c->expect.y, c->expect.z };
    for (int i = 0; i < 3; i++) {
        double m = c->sum[i] / (double)c->n;
        double var = c->sum_sq[i] / (double)c->n - m * m;
        if (var < 0) {
            var = 0;        /* integer literals: -fsingle-precision-constant */
        }
        mean[i] = (float)m;
        if (sqrt(var) > (double)c->max_std || fabsf(mean[i] - expect[i]) > c->max_abs) {
            still = false;
        }
    }

    c->attempts++;
    if (still) {
        c->result = v3(mean[0], mean[1], mean[2]);
        c->state = CAL_DONE;
        return c->state;
    }

    if (c->max_attempts != 0u && c->attempts >= c->max_attempts) {
        c->state = CAL_FAILED;
        return c->state;
    }
    /* restart the window */
    memset(c->sum, 0, sizeof(c->sum));
    memset(c->sum_sq, 0, sizeof(c->sum_sq));
    c->n = 0u;
    c->t = 0.0f;
    return c->state;
}
