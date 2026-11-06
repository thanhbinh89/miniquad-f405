/**
 * @file    quad_model.h
 * @brief   Rigid-body model of a ~30 g 65 mm brushed (coreless) quad for
 *          closed-loop tests of the flight core.
 *
 * Frames: body FRD, world NED (z down, ground at z = 0).
 * Includes motor lag, a thrust curve that differs from the firmware's
 * assumption, voltage dependent thrust, battery internal resistance (sag)
 * and a crude ground contact model.
 */
#ifndef QUAD_MODEL_H
#define QUAD_MODEL_H

#include <stdbool.h>

typedef struct {
    /* parameters */
    double mass;            /* kg */
    double inertia[3];      /* kg m^2 */
    double t_max;           /* N per motor at full duty and v_ref */
    double v_ref;           /* V */
    double lin_a;           /* true quadratic share of the thrust curve */
    double tau_motor;       /* s */
    double kq;              /* yaw torque / thrust (m) */
    double drag;            /* N per m/s */
    double rot_damp;        /* N m per rad/s */
    double r_int;           /* battery internal resistance (ohm) */
    double i_max;           /* A per motor at full duty */
    double px[4], py[4];    /* motor positions (body, m) */
    int    spin[4];         /* +1 CCW (positive yaw torque), -1 CW */

    /* state */
    double pos[3];
    double vel[3];
    double q[4];            /* body -> world */
    double w[3];            /* body rates rad/s */
    double thrust[4];       /* N */
    double v_oc;            /* open circuit battery voltage */
    double v_bat;
    double i_total;
    double acc_world[3];    /* last world acceleration (m/s^2) */
    bool   on_ground;
} quadsim_t;

void   quad_init(quadsim_t *m);
/** Integrate @p dt seconds with PWM duties @p duty[0..3] (0..1). */
void   quad_step(quadsim_t *m, const double duty[4], double dt);
/** Ideal IMU readings: body rates (deg/s) and specific force (g). */
void   quad_imu(const quadsim_t *m, double gyro_dps[3], double acc_g[3]);
void   quad_euler_deg(const quadsim_t *m, double *roll, double *pitch, double *yaw);
double quad_height(const quadsim_t *m);
double quad_cos_tilt(const quadsim_t *m);
/** Place the vehicle at height @p h with the given attitude (deg), at rest. */
void   quad_place(quadsim_t *m, double h, double roll_deg, double pitch_deg);

#endif /* QUAD_MODEL_H */
