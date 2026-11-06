/**
 * @file    quad_model.c
 * @brief   Quad rigid-body model (double precision).
 */
#include "quad_model.h"

#include <math.h>
#include <string.h>

#define G 9.80665

static void q_normalize(double q[4])
{
    double n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) {
        q[i] /= n;
    }
}

static void q_to_dcm(const double q[4], double r[3][3])
{
    double w = q[0], x = q[1], y = q[2], z = q[3];
    r[0][0] = 1 - 2 * (y * y + z * z); r[0][1] = 2 * (x * y - w * z);     r[0][2] = 2 * (x * z + w * y);
    r[1][0] = 2 * (x * y + w * z);     r[1][1] = 1 - 2 * (x * x + z * z); r[1][2] = 2 * (y * z - w * x);
    r[2][0] = 2 * (x * z - w * y);     r[2][1] = 2 * (y * z + w * x);     r[2][2] = 1 - 2 * (x * x + y * y);
}

static void q_from_euler(double roll, double pitch, double yaw, double q[4])
{
    double cr = cos(roll / 2), sr = sin(roll / 2);
    double cp = cos(pitch / 2), sp = sin(pitch / 2);
    double cy = cos(yaw / 2), sy = sin(yaw / 2);
    q[0] = cr * cp * cy + sr * sp * sy;
    q[1] = sr * cp * cy - cr * sp * sy;
    q[2] = cr * sp * cy + sr * cp * sy;
    q[3] = cr * cp * sy - sr * sp * cy;
}

void quad_init(quadsim_t *m)
{
    memset(m, 0, sizeof(*m));
    m->mass = 0.030;
    m->inertia[0] = 1.2e-5;
    m->inertia[1] = 1.2e-5;
    m->inertia[2] = 2.2e-5;
    m->t_max = 0.22;            /* ~22 g per motor: thrust/weight ~3 */
    m->v_ref = 3.9;
    m->lin_a = 0.58;            /* firmware assumes 0.5 */
    m->tau_motor = 0.030;
    m->kq = 0.006;
    m->drag = 0.02;
    m->rot_damp = 2.0e-6;
    m->r_int = 0.12;
    m->i_max = 1.8;

    /* default nose = top edge: M1 FR, M2 FL, M3 RL, M4 RR; props-in */
    const double a = 0.023;
    const double px[4] = { +a, +a, -a, -a };
    const double py[4] = { +a, -a, -a, +a };
    const int spin[4] = { +1, -1, +1, -1 };
    for (int i = 0; i < 4; i++) {
        m->px[i] = px[i];
        m->py[i] = py[i];
        m->spin[i] = spin[i];
    }

    m->q[0] = 1.0;
    m->v_oc = 4.15;
    m->v_bat = m->v_oc;
    m->on_ground = true;
}

void quad_place(quadsim_t *m, double h, double roll_deg, double pitch_deg)
{
    m->pos[0] = m->pos[1] = 0.0;
    m->pos[2] = -h;
    memset(m->vel, 0, sizeof(m->vel));
    memset(m->w, 0, sizeof(m->w));
    q_from_euler(roll_deg * M_PI / 180.0, pitch_deg * M_PI / 180.0, 0.0, m->q);
    m->on_ground = (h <= 0.0);
}

void quad_step(quadsim_t *m, const double duty[4], double dt)
{
    /* motors */
    double f_total = 0.0, tq[3] = { 0, 0, 0 };
    m->i_total = 0.0;
    for (int i = 0; i < 4; i++) {
        double d = duty[i] < 0 ? 0 : (duty[i] > 1 ? 1 : duty[i]);
        double de = d * m->v_bat / m->v_ref;
        double t_cmd = m->t_max * (m->lin_a * de * de + (1.0 - m->lin_a) * de);
        m->thrust[i] += (t_cmd - m->thrust[i]) * (dt / m->tau_motor);
        double t = m->thrust[i];
        f_total += t;
        tq[0] += -m->py[i] * t;
        tq[1] += m->px[i] * t;
        tq[2] += m->spin[i] * m->kq * t;
        m->i_total += m->i_max * pow(de, 1.5);
    }
    m->v_bat = m->v_oc - m->r_int * m->i_total;

    /* rotation */
    double *I = m->inertia, *w = m->w;
    double Iw[3] = { I[0] * w[0], I[1] * w[1], I[2] * w[2] };
    double gyro[3] = { w[1] * Iw[2] - w[2] * Iw[1], w[2] * Iw[0] - w[0] * Iw[2], w[0] * Iw[1] - w[1] * Iw[0] };
    for (int k = 0; k < 3; k++) {
        double wd = (tq[k] - gyro[k] - m->rot_damp * w[k]) / I[k];
        w[k] += wd * dt;
    }

    /* translation */
    double r[3][3];
    q_to_dcm(m->q, r);
    double acc[3];
    for (int k = 0; k < 3; k++) {
        acc[k] = r[k][2] * (-f_total) / m->mass - m->drag * m->vel[k] / m->mass;
    }
    acc[2] += G;

    if (m->on_ground) {
        /* stays on the ground until thrust overcomes gravity */
        if (acc[2] < -0.05) {
            m->on_ground = false;
        } else {
            memset(m->vel, 0, sizeof(m->vel));
            memset(acc, 0, sizeof(acc));
            w[0] = w[1] = 0.0;
            w[2] *= 0.9;
            double yaw = atan2(r[1][0], r[0][0]);
            q_from_euler(0.0, 0.0, yaw, m->q);
        }
    }
    if (!m->on_ground) {
        for (int k = 0; k < 3; k++) {
            m->vel[k] += acc[k] * dt;
            m->pos[k] += m->vel[k] * dt;
        }
        if (m->pos[2] >= 0.0) {             /* touched the ground */
            m->pos[2] = 0.0;
            m->on_ground = true;
            memset(m->vel, 0, sizeof(m->vel));
        }
    }
    memcpy(m->acc_world, acc, sizeof(acc));

    /* attitude integration: q_dot = 0.5 q x (0, w) */
    double *q = m->q;
    double hx = 0.5 * dt * w[0], hy = 0.5 * dt * w[1], hz = 0.5 * dt * w[2];
    double dq[4] = {
        -q[1] * hx - q[2] * hy - q[3] * hz,
         q[0] * hx + q[2] * hz - q[3] * hy,
         q[0] * hy - q[1] * hz + q[3] * hx,
         q[0] * hz + q[1] * hy - q[2] * hx,
    };
    for (int i = 0; i < 4; i++) {
        q[i] += dq[i];
    }
    q_normalize(q);
}

void quad_imu(const quadsim_t *m, double gyro_dps[3], double acc_g[3])
{
    double r[3][3];
    q_to_dcm(m->q, r);
    for (int k = 0; k < 3; k++) {
        gyro_dps[k] = m->w[k] * 180.0 / M_PI;
    }
    /* specific force f = a - g, rotated into the body frame */
    double f[3] = { m->acc_world[0], m->acc_world[1], m->acc_world[2] - G };
    for (int k = 0; k < 3; k++) {
        acc_g[k] = (r[0][k] * f[0] + r[1][k] * f[1] + r[2][k] * f[2]) / G;
    }
}

void quad_euler_deg(const quadsim_t *m, double *roll, double *pitch, double *yaw)
{
    const double *q = m->q;
    *roll = atan2(2 * (q[0] * q[1] + q[2] * q[3]), 1 - 2 * (q[1] * q[1] + q[2] * q[2])) * 180.0 / M_PI;
    double s = 2 * (q[0] * q[2] - q[3] * q[1]);
    s = s > 1 ? 1 : (s < -1 ? -1 : s);
    *pitch = asin(s) * 180.0 / M_PI;
    *yaw = atan2(2 * (q[0] * q[3] + q[1] * q[2]), 1 - 2 * (q[2] * q[2] + q[3] * q[3])) * 180.0 / M_PI;
}

double quad_height(const quadsim_t *m)
{
    return -m->pos[2];
}

double quad_cos_tilt(const quadsim_t *m)
{
    double r[3][3];
    q_to_dcm(m->q, r);
    return r[2][2];
}
