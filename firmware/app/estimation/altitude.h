/**
 * @file    altitude.h
 * @brief   Height above ground: 3-state Kalman filter fusing the vertical
 *          acceleration (earth frame) with the ToF range.
 *
 * State x = [h, v, b]: height (m, up), vertical speed (m/s, up) and
 * accelerometer bias (m/s^2). The ToF sample is ~1/2 timing budget old,
 * so the innovation is computed against the height estimate of that time
 * (ring buffer of past estimates) and the correction is applied now.
 * Innovations beyond 5 sigma are rejected; a long run of rejections
 * re-initialises the height on the measurement.
 */
#ifndef ALTITUDE_H
#define ALTITUDE_H

#include <stdbool.h>
#include <stdint.h>

#define ALT_HIST_LEN        128u        /* >= max delay in loop samples */

typedef struct {
    float    h;
    float    v;
    float    b;
    float    P[3][3];
    bool     initialized;
    float    hist[ALT_HIST_LEN];
    uint32_t hist_idx;
    float    last_innov;
    uint16_t reject_run;
    uint32_t updates;
    uint32_t rejects;
} alt_est_t;

void alt_est_reset(alt_est_t *e);
/** Time update. @p acc_up: earth vertical acceleration, gravity removed (m/s^2). */
void alt_est_predict(alt_est_t *e, float acc_up, float dt, float acc_noise, float bias_noise);
/**
 * Measurement update with height @p z (m) and std-dev @p sigma (m) measured
 * @p delay_samples predictions ago. Returns true if the sample was accepted.
 */
bool alt_est_update(alt_est_t *e, float z, float sigma, uint32_t delay_samples);

#endif /* ALTITUDE_H */
