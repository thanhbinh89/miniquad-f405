/**
 * @file    test_control.c
 * @brief   Mixer geometry/desaturation, thrust curve, battery factor, PID.
 */
#include <string.h>

#include "alt_ctrl.h"
#include "mixer.h"
#include "motor_out.h"
#include "pid.h"
#include "tinytest.h"

static void test_geometry_default(void)
{
    mixer_geom_t g;
    mixer_init_geometry(&g, 0, false);
    /* M1 front-right, M2 front-left, M3 rear-left, M4 rear-right */
    CHECK(g.pos_x[0] > 0 && g.pos_y[0] > 0);
    CHECK(g.pos_x[1] > 0 && g.pos_y[1] < 0);
    CHECK(g.pos_x[2] < 0 && g.pos_y[2] < 0);
    CHECK(g.pos_x[3] < 0 && g.pos_y[3] > 0);
    /* +roll (right down) needs more thrust on the left motors */
    CHECK(g.roll[1] > 0 && g.roll[2] > 0 && g.roll[0] < 0 && g.roll[3] < 0);
    /* +pitch (nose up) needs more thrust in front */
    CHECK(g.pitch[0] > 0 && g.pitch[1] > 0 && g.pitch[2] < 0 && g.pitch[3] < 0);
    /* props-in: FR and RL CCW (+yaw) */
    CHECK(g.yaw[0] > 0 && g.yaw[2] > 0 && g.yaw[1] < 0 && g.yaw[3] < 0);

    mixer_init_geometry(&g, 0, true);
    CHECK(g.yaw[0] < 0 && g.yaw[2] < 0);

    /* nose = right edge: M4 (right-bottom corner) becomes front-right */
    mixer_init_geometry(&g, 1, false);
    CHECK(g.pos_x[3] > 0 && g.pos_y[3] > 0);
    CHECK(g.pos_x[0] > 0 && g.pos_y[0] < 0);   /* M1 front-left */
    /* nose = bottom edge: M3 (left-bottom) becomes front-right */
    mixer_init_geometry(&g, 2, false);
    CHECK(g.pos_x[2] > 0 && g.pos_y[2] > 0);
}

static void test_mix_basic(void)
{
    mixer_geom_t g;
    mixer_out_t o;
    mixer_init_geometry(&g, 0, false);
    mixer_mix(&g, 0.5f, 0.0f, 0.0f, 0.0f, false, &o);
    for (int i = 0; i < 4; i++) {
        CHECK_NEAR(o.motor[i], 0.5, 1e-6);
    }
    CHECK(!o.saturated);
    mixer_mix(&g, 0.5f, 0.1f, 0.0f, 0.0f, false, &o);
    CHECK_NEAR(o.motor[1], 0.6, 1e-6);
    CHECK_NEAR(o.motor[0], 0.4, 1e-6);
}

static void test_mix_desaturation(void)
{
    mixer_geom_t g;
    mixer_out_t o;
    mixer_init_geometry(&g, 0, false);
    /* huge roll: scaled to the motor range, yaw dropped */
    mixer_mix(&g, 0.5f, 2.0f, 0.0f, 0.3f, false, &o);
    CHECK(o.saturated);
    float mn = 1, mx = 0;
    for (int i = 0; i < 4; i++) {
        mn = fminf(mn, o.motor[i]);
        mx = fmaxf(mx, o.motor[i]);
    }
    CHECK_NEAR(mx - mn, 1.0, 1e-5);
    /* yaw limited first, roll keeps priority */
    mixer_mix(&g, 0.5f, 0.3f, 0.0f, 0.5f, false, &o);
    CHECK(o.yaw_scale < 1.0f);
    CHECK_NEAR(o.rp_scale, 1.0, 1e-6);
    /* full throttle: collective reduced to keep authority */
    mixer_mix(&g, 1.0f, 0.2f, 0.0f, 0.0f, false, &o);
    CHECK(o.collective < 1.0f);
    CHECK_NEAR(o.motor[1] - o.motor[0], 0.4, 1e-5);
    /* zero throttle without airmode: no torque; with airmode: full torque */
    mixer_mix(&g, 0.0f, 0.2f, 0.0f, 0.0f, false, &o);
    CHECK_NEAR(o.motor[1], 0.0, 1e-6);
    mixer_mix(&g, 0.0f, 0.2f, 0.0f, 0.0f, true, &o);
    CHECK_NEAR(o.motor[1] - o.motor[0], 0.4, 1e-5);
}

static void test_thrust_curve(void)
{
    for (float a = 0.0f; a <= 1.0f; a += 0.25f) {
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            float d = motor_thrust_to_duty(t, a);
            CHECK_NEAR(a * d * d + (1.0f - a) * d, t, 1e-4);
        }
    }
    CHECK_NEAR(motor_thrust_to_duty(1.5f, 0.5f), 1.0, 1e-6);
}

static void test_battery_factor(void)
{
    CHECK_NEAR(motor_battery_factor(3.9f, true, 3.9f, 0.85f, 1.35f), 1.0, 1e-6);
    CHECK_NEAR(motor_battery_factor(3.5f, true, 3.9f, 0.85f, 1.35f), 3.9 / 3.5, 1e-5);
    CHECK_NEAR(motor_battery_factor(2.5f, true, 3.9f, 0.85f, 1.35f), 1.35, 1e-6);
    CHECK_NEAR(motor_battery_factor(4.35f, true, 3.9f, 0.85f, 1.35f), 3.9 / 4.35, 1e-5);
    CHECK_NEAR(motor_battery_factor(0.0f, false, 3.9f, 0.85f, 1.35f), 1.0, 1e-6);

    float thrust[4] = { 0.0f, 0.25f, 0.5f, 1.0f }, duty[4];
    motor_output(thrust, 1.0f, 0.04f, 1.0f, 0.0f, duty);
    CHECK_NEAR(duty[0], 0.04, 1e-6);
    CHECK_NEAR(duty[3], 1.0, 1e-6);
    CHECK_NEAR(duty[2], 0.04 + 0.96 * 0.5, 1e-6);
}

static void test_pid(void)
{
    pid_axis_t p;
    pid_init(&p);
    pid_set_gains(&p, 50, 50, 0, 0, 0.3f);
    float out = pid_update(&p, 100.0f, 0.0f, 0.001f, true, false);
    CHECK_NEAR(p.p_out, 50 * PID_P_SCALE * 100.0, 1e-6);
    CHECK(out > p.p_out);
    /* integrator clamp */
    for (int i = 0; i < 100000; i++) {
        (void)pid_update(&p, 100.0f, 0.0f, 0.001f, true, false);
    }
    CHECK_NEAR(p.i, 0.3, 1e-6);
    /* saturation: only unwinding allowed */
    (void)pid_update(&p, 100.0f, 0.0f, 0.001f, true, true);
    CHECK_NEAR(p.i, 0.3, 1e-6);
    (void)pid_update(&p, -100.0f, 0.0f, 0.001f, true, true);
    CHECK(p.i < 0.3f);
    /* D on measurement: no kick on set point step */
    pid_init(&p);
    pid_set_gains(&p, 0, 0, 50, 0, 0.3f);
    (void)pid_update(&p, 0.0f, 0.0f, 0.001f, true, false);
    (void)pid_update(&p, 500.0f, 0.0f, 0.001f, true, false);
    CHECK_NEAR(p.d_out, 0.0, 1e-9);
    (void)pid_update(&p, 500.0f, 10.0f, 0.001f, true, false);
    CHECK(p.d_out < 0.0f);
}

static void test_alt_ctrl(void)
{
    alt_ctrl_t c;
    alt_ctrl_cfg_t cfg = { 1.5f, 0.25f, 0.15f, 0.8f, 2.5f };
    alt_ctrl_init(&c, 0.45f);
    alt_ctrl_engage(&c, 1.0f, 0.50f);
    CHECK_NEAR(c.out, 0.50, 1e-6);                  /* bumpless */
    float o = alt_ctrl_update(&c, &cfg, 0.8f, 0.0f, 0.0f, true, 0.001f);
    CHECK(o > 0.5f);                                /* below target -> more thrust */
    CHECK_NEAR(c.vz_sp, 0.3, 1e-5);
    /* ceiling */
    c.h_sp = 3.0f;
    (void)alt_ctrl_update(&c, &cfg, 2.4f, 0.0f, 0.0f, true, 0.001f);
    CHECK_NEAR(c.h_sp, 2.5, 1e-6);
    /* stick released while climbing: target = height + stopping distance */
    alt_ctrl_engage(&c, 1.0f, 0.45f);
    (void)alt_ctrl_update(&c, &cfg, 1.0f, 0.5f, 0.5f, false, 0.001f);
    CHECK(!c.holding);
    (void)alt_ctrl_update(&c, &cfg, 1.2f, 0.5f, 0.0f, true, 0.001f);
    CHECK_NEAR(c.h_sp, 1.2 + 0.15, 1e-5);
    /* hover thrust estimator: a = g (T / T_hover - 1) */
    alt_ctrl_init(&c, 0.60f);
    for (int i = 0; i < 20000; i++) {
        alt_ctrl_learn_hover(&c, 0.48f, 0.2f * 9.80665f, 3.0f, 0.001f);
    }
    CHECK_NEAR(c.hover, 0.40, 0.005);
}

int main(void)
{
    RUN(test_geometry_default);
    RUN(test_mix_basic);
    RUN(test_mix_desaturation);
    RUN(test_thrust_curve);
    RUN(test_battery_factor);
    RUN(test_pid);
    RUN(test_alt_ctrl);
    return TT_REPORT();
}
