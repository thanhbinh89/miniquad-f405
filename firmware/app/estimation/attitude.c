/**
 * @file    attitude.c
 * @brief   Mahony attitude filter.
 */
#include "attitude.h"

#include <string.h>

#define ALIGN_BOOST_TIME_S  2.0f
#define ALIGN_BOOST_KP      5.0f
#define I_ERR_LIMIT         0.10f       /* rad/s */
#define ACC_WINDOW_FULL     0.05f       /* g: full trust inside this band  */
#define ACC_WINDOW_ZERO     0.25f       /* g: no trust outside this band   */
#define RATE_NO_ACC         6.1f        /* rad/s (~350 deg/s)              */

static void refresh(attitude_t *a)
{
    quat_to_dcm(a->q, a->dcm);
    quat_to_euler(a->q, &a->roll, &a->pitch, &a->yaw);
}

void attitude_reset(attitude_t *a)
{
    memset(a, 0, sizeof(*a));
    a->q = quat_identity();
    refresh(a);
}

void attitude_align(attitude_t *a, vec3_t acc_g)
{
    /* at rest the accelerometer measures (g sin th, -g cos th sin ph, -g cos th cos ph) */
    float roll = atan2f(-acc_g.y, -acc_g.z);
    float pitch = atan2f(acc_g.x, sqrtf(acc_g.y * acc_g.y + acc_g.z * acc_g.z));
    a->q = quat_from_euler(roll, pitch, 0.0f);
    a->i_err = v3(0.0f, 0.0f, 0.0f);
    a->fast_time = ALIGN_BOOST_TIME_S;
    a->aligned = true;
    refresh(a);
}

void attitude_update(attitude_t *a, vec3_t gyro_rad, vec3_t acc_g, float kp, float ki, float dt)
{
    vec3_t w = gyro_rad;

    float acc_n = v3_norm(acc_g);
    float dev = fabsf(acc_n - 1.0f);
    float weight = 1.0f - (dev - ACC_WINDOW_FULL) / (ACC_WINDOW_ZERO - ACC_WINDOW_FULL);
    weight = constrainf(weight, 0.0f, 1.0f);
    if (v3_norm(gyro_rad) > RATE_NO_ACC) {
        weight = 0.0f;
    }
    a->acc_weight = weight;

    if (weight > 0.0f && acc_n > 0.3f) {
        /* measured "down" in body frame = -specific force */
        vec3_t down_meas = v3_scale(acc_g, -1.0f / acc_n);
        /* estimated "down" = third row of the body->earth DCM */
        vec3_t down_est = v3(a->dcm[2][0], a->dcm[2][1], a->dcm[2][2]);
        vec3_t e = v3_cross(down_meas, down_est);

        float kp_eff = kp;
        if (a->fast_time > 0.0f) {
            kp_eff = (kp > ALIGN_BOOST_KP) ? kp : ALIGN_BOOST_KP;
        } else if (ki > 0.0f) {
            a->i_err = v3_add(a->i_err, v3_scale(e, ki * weight * dt));
            a->i_err.x = constrainf(a->i_err.x, -I_ERR_LIMIT, I_ERR_LIMIT);
            a->i_err.y = constrainf(a->i_err.y, -I_ERR_LIMIT, I_ERR_LIMIT);
            a->i_err.z = constrainf(a->i_err.z, -I_ERR_LIMIT, I_ERR_LIMIT);
        }
        w = v3_add(w, v3_scale(e, kp_eff * weight));
    }
    w = v3_add(w, a->i_err);
    if (a->fast_time > 0.0f) {
        a->fast_time -= dt;
    }

    /* q_dot = 0.5 * q x (0, w) */
    quat_t q = a->q;
    float hx = 0.5f * dt * w.x, hy = 0.5f * dt * w.y, hz = 0.5f * dt * w.z;
    quat_t dq = {
        -q.x * hx - q.y * hy - q.z * hz,
         q.w * hx + q.y * hz - q.z * hy,
         q.w * hy - q.x * hz + q.z * hx,
         q.w * hz + q.x * hy - q.y * hx,
    };
    q.w += dq.w;
    q.x += dq.x;
    q.y += dq.y;
    q.z += dq.z;
    a->q = quat_normalize(q);
    refresh(a);
}

vec3_t attitude_body_to_earth(const attitude_t *a, vec3_t b)
{
    return v3(a->dcm[0][0] * b.x + a->dcm[0][1] * b.y + a->dcm[0][2] * b.z,
              a->dcm[1][0] * b.x + a->dcm[1][1] * b.y + a->dcm[1][2] * b.z,
              a->dcm[2][0] * b.x + a->dcm[2][1] * b.y + a->dcm[2][2] * b.z);
}

float attitude_cos_tilt(const attitude_t *a)
{
    return a->dcm[2][2];
}
