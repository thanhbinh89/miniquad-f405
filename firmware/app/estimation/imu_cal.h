/**
 * @file    imu_cal.h
 * @brief   Gyro bias and accelerometer level calibration.
 *
 * Both routines collect a window of samples and accept it only if the
 * vehicle was still (low variance). The gyro calibration runs at every boot
 * and on request; the accelerometer level calibration only on request (the
 * result is stored as parameters acc_off_x/y/z).
 */
#ifndef IMU_CAL_H
#define IMU_CAL_H

#include <stdbool.h>
#include <stdint.h>
#include "maths.h"

typedef enum {
    CAL_IDLE = 0,
    CAL_RUNNING,
    CAL_DONE,
    CAL_FAILED,
} cal_state_t;

typedef struct {
    cal_state_t state;
    float       window_s;       /* length of one averaging window */
    float       max_std;        /* stillness threshold (std-dev)   */
    float       max_abs;        /* plausibility limit |mean - expect| */
    vec3_t      expect;         /* expected mean (0 for gyro, -1 g z for accel) */
    uint32_t    max_attempts;   /* 0 = retry forever               */
    float       t;
    uint32_t    n;
    uint32_t    attempts;
    double      sum[3];
    double      sum_sq[3];
    vec3_t      result;         /* mean of the accepted window     */
} imu_cal_t;

/** Gyro: 0.75 s windows, std < 1.5 dps, |bias| < 25 dps, retry forever. */
void imu_cal_start_gyro(imu_cal_t *c);
/** Accel level: 1 s window, std < 0.02 g, |offset| < 0.3 g after removing 1 g, 10 attempts. */
void imu_cal_start_accel(imu_cal_t *c);
/** Feed one sample; returns the state after the update. */
cal_state_t imu_cal_update(imu_cal_t *c, vec3_t sample, float dt);

#endif /* IMU_CAL_H */
