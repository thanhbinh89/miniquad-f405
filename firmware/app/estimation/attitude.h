/**
 * @file    attitude.h
 * @brief   Mahony complementary filter (quaternion) - roll/pitch/yaw.
 *
 * The gyro is integrated every loop; the accelerometer corrects roll and
 * pitch only when the measured specific force is close to 1 g (weight fades
 * to 0 at +/-0.25 g or above 350 deg/s). Yaw is gyro-only (no magnetometer)
 * and therefore relative.
 */
#ifndef ATTITUDE_H
#define ATTITUDE_H

#include <stdbool.h>
#include "maths.h"

typedef struct {
    quat_t q;               /* body -> earth (NED) */
    vec3_t i_err;           /* integral correction, rad/s */
    float  dcm[3][3];       /* body -> earth rotation */
    float  roll;            /* rad */
    float  pitch;
    float  yaw;
    float  acc_weight;      /* 0..1, last accelerometer trust */
    float  fast_time;       /* s left with boosted gain after alignment */
    bool   aligned;
} attitude_t;

void   attitude_reset(attitude_t *a);
/** Initialise roll/pitch from a (still) accelerometer sample, yaw = 0. */
void   attitude_align(attitude_t *a, vec3_t acc_g);
void   attitude_update(attitude_t *a, vec3_t gyro_rad, vec3_t acc_g, float kp, float ki, float dt);
/** Rotate a body vector into the earth frame. */
vec3_t attitude_body_to_earth(const attitude_t *a, vec3_t b);
/** cos(roll) * cos(pitch): vertical component of the body z axis. */
float  attitude_cos_tilt(const attitude_t *a);

#endif /* ATTITUDE_H */
