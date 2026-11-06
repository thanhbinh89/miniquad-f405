/**
 * @file    angle_ctrl.h
 * @brief   Self-level outer loop: angle error -> body rate setpoint.
 */
#ifndef ANGLE_CTRL_H
#define ANGLE_CTRL_H

#include "maths.h"

/**
 * @param angle_sp  desired angle (deg)
 * @param angle     estimated angle (deg)
 * @param kp        (deg/s) per deg
 * @param rate_max  output limit (deg/s)
 */
static inline float angle_ctrl_rate(float angle_sp, float angle, float kp, float rate_max)
{
    return constrainf(kp * (angle_sp - angle), -rate_max, rate_max);
}

#endif /* ANGLE_CTRL_H */
