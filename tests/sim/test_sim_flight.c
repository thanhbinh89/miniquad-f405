/**
 * @file    test_sim_flight.c
 * @brief   Closed-loop flight tests: the real flight core flies the model.
 *
 * Set SIM_LOG=1 to write sim_<test>.csv traces for plotting.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "fc.h"
#include "params.h"
#include "proto.h"
#include "sim_harness.h"
#include "tinytest.h"

static sim_t S;

/* human-like pilot holding a height with the throttle stick (uses truth) */
typedef struct {
    double h_ref;
    double thr;
    double integ;
} pilot_t;

static void pilot_update(pilot_t *p, sim_t *s, double roll, double pitch, double yaw)
{
    double h = sim_height(s);
    double vz = -s->quad.vel[2];
    p->integ += (p->h_ref - h) * 0.02;
    if (p->integ > 2.0) {
        p->integ = 2.0;
    } else if (p->integ < -2.0) {
        p->integ = -2.0;
    }
    p->thr = 0.36 + 0.35 * (p->h_ref - h) - 0.25 * vz + 0.08 * p->integ;
    if (p->thr < 0.0) {
        p->thr = 0.0;
    } else if (p->thr > 1.0) {
        p->thr = 1.0;
    }
    sim_sticks(s, roll, pitch, p->thr, yaw);
}

/* fly with the pilot for @p seconds (stick updates every 20 ms) */
static void pilot_fly(pilot_t *p, sim_t *s, double seconds, double roll, double pitch, double yaw)
{
    int n = (int)lround(seconds / 0.02);
    for (int i = 0; i < n; i++) {
        pilot_update(p, s, roll, pitch, yaw);
        sim_run(s, 0.02);
    }
}

static void open_log(const char *name)
{
    if (getenv("SIM_LOG") != NULL) {
        char fn[128];
        snprintf(fn, sizeof(fn), "sim_%s.csv", name);
        S.log = fopen(fn, "w");
        if (S.log != NULL) {
            fprintf(S.log, "t,roll,pitch,yaw,h,h_est,collective,hover,mode,armed,vbat,bat_factor\n");
        }
    }
}

static void close_log(void)
{
    if (S.log != NULL) {
        fclose(S.log);
        S.log = NULL;
    }
}

static void boot(const char *name)
{
    sim_init(&S);
    g_cfg.tof_offset_mm = 15.0f;
    open_log(name);
    sim_run(&S, 3.0);           /* gyro calibration + boot grace */
}

/* boot, arm on the ground in ANGLE, climb to @p h with the pilot */
static void takeoff(pilot_t *p, const char *name, double h)
{
    boot(name);
    CHECK(sim_arm(&S, 1.0));
    memset(p, 0, sizeof(*p));
    p->h_ref = h;
    pilot_fly(p, &S, 3.0, 0, 0, 0);
}

/* ------------------------------------------------------------------------ */
static void test_gyro_cal_and_alignment(void)
{
    boot("cal");
    fc_status_t st;
    fc_get_status(&st);
    CHECK((st.flags & STF_GYRO_CAL) != 0u);
    CHECK_NEAR(st.gyro_bias.x, S.gyro_bias[0], 0.2);
    CHECK_NEAR(st.gyro_bias.y, S.gyro_bias[1], 0.2);
    CHECK_NEAR(st.gyro_bias.z, S.gyro_bias[2], 0.2);
    CHECK_NEAR(st.roll, 0.0, 1.0);
    CHECK_NEAR(st.pitch, 0.0, 1.0);
    CHECK(!fc_is_armed());
    close_log();
}

static void test_arming_safety(void)
{
    /* receiver switched on with the arm switch already on -> must cycle */
    sim_init(&S);
    S.tx_on = false;
    sim_run(&S, 3.0);
    sim_channel(&S, SIM_CH_ARM, 2000);
    S.tx_on = true;
    S.rc_next_ms = S.t * 1000.0;
    sim_run(&S, 0.5);
    CHECK(!fc_is_armed());
    fc_status_t st;
    fc_get_status(&st);
    CHECK((st.arm_block & ARMB_SWITCH) != 0u);

    /* throttle up blocks arming */
    sim_channel(&S, SIM_CH_ARM, 1000);
    sim_sticks(&S, 0, 0, 0.5, 0);
    sim_run(&S, 0.3);
    sim_channel(&S, SIM_CH_ARM, 2000);
    sim_run(&S, 0.3);
    CHECK(!fc_is_armed());

    /* normal arming */
    sim_sticks(&S, 0, 0, 0.0, 0);
    CHECK(sim_arm(&S, 1.0));
    /* motors idle on the ground */
    CHECK_NEAR(S.out.duty[0], g_cfg.motor_idle, 0.02);
    sim_channel(&S, SIM_CH_ARM, 1000);
    sim_run(&S, 0.1);
    CHECK(!fc_is_armed());
    close_log();
}

static void test_angle_mode_level_and_step(void)
{
    pilot_t p;
    takeoff(&p, "angle", 1.0);
    double r, pi, y;
    sim_euler(&S, &r, &pi, &y);
    CHECK(fc_is_armed());
    CHECK_NEAR(sim_height(&S), 1.0, 0.3);
    CHECK_NEAR(r, 0.0, 3.0);
    CHECK_NEAR(pi, 0.0, 3.0);

    /* half roll stick -> half max angle */
    pilot_fly(&p, &S, 1.0, 0.5, 0, 0);
    sim_euler(&S, &r, &pi, &y);
    double want = 0.5 * g_cfg.angle_max;
    CHECK_NEAR(r, want, 4.0);

    /* release: back to level quickly without large overshoot */
    double max_neg = 0.0;
    for (int i = 0; i < 40; i++) {
        pilot_fly(&p, &S, 0.02, 0, 0, 0);
        sim_euler(&S, &r, &pi, &y);
        if (r < max_neg) {
            max_neg = r;
        }
    }
    CHECK_NEAR(r, 0.0, 2.5);
    CHECK(max_neg > -6.0);

    /* pitch forward stick = nose down (negative pitch in FRD) */
    pilot_fly(&p, &S, 0.6, 0, 0.5, 0);
    sim_euler(&S, &r, &pi, &y);
    CHECK_NEAR(pi, -want, 4.0);
    close_log();
}

static void test_disturbance_rejection(void)
{
    pilot_t p;
    takeoff(&p, "disturb", 1.0);
    S.quad.w[0] += 4.0;         /* ~230 deg/s roll kick */
    S.quad.w[1] -= 3.0;
    pilot_fly(&p, &S, 0.6, 0, 0, 0);
    double r, pi, y;
    sim_euler(&S, &r, &pi, &y);
    CHECK_NEAR(r, 0.0, 3.0);
    CHECK_NEAR(pi, 0.0, 3.0);
    CHECK(fc_is_armed());
    close_log();
}

static void test_acro_rate_tracking(void)
{
    pilot_t p;
    takeoff(&p, "acro", 1.2);
    sim_channel(&S, SIM_CH_MODE, 2000);     /* high = ACRO */
    pilot_fly(&p, &S, 0.2, 0, 0, 0);
    fc_status_t st;
    fc_get_status(&st);
    CHECK_NEAR(st.mode, FMODE_ACRO, 0);

    pilot_fly(&p, &S, 0.16, 0.3, 0, 0);     /* stick 0.3 -> ~200 deg/s */
    double sp = 0.3 * (1.0 - g_cfg.expo_rp + g_cfg.expo_rp * 0.09) * g_cfg.rate_max[0];
    double rate = S.quad.w[0] * 180.0 / M_PI;
    CHECK_NEAR(rate, sp, 0.2 * sp);

    /* yaw */
    pilot_fly(&p, &S, 0.2, 0, 0, 0);
    pilot_fly(&p, &S, 0.3, 0, 0, 0.5);
    double ysp = 0.5 * (1.0 - g_cfg.expo_yaw + g_cfg.expo_yaw * 0.25) * g_cfg.rate_max[2];
    double yrate = S.quad.w[2] * 180.0 / M_PI;
    CHECK_NEAR(yrate, ysp, 0.25 * ysp);
    close_log();
}

static void test_alt_hold(void)
{
    pilot_t p;
    takeoff(&p, "althold", 0.8);
    double h0 = sim_height(&S);

    sim_channel(&S, SIM_CH_MODE, 1500);     /* mid = ALT */
    sim_sticks(&S, 0, 0, 0.5, 0);
    sim_run(&S, 0.2);
    fc_status_t st;
    fc_get_status(&st);
    CHECK_NEAR(st.mode, FMODE_ALT, 0);

    double sq = 0.0, worst = 0.0;
    int n = 0;
    for (int i = 0; i < 250; i++) {         /* 5 s */
        sim_run(&S, 0.02);
        double e = sim_height(&S) - h0;
        sq += e * e;
        n++;
        if (fabs(e) > worst) {
            worst = fabs(e);
        }
    }
    double rms = sqrt(sq / n);
    printf("    alt hold: rms %.3f m, worst %.3f m\n", rms, worst);
    CHECK(rms < 0.06);
    CHECK(worst < 0.15);

    /* climb with the stick, then hold the new height */
    sim_sticks(&S, 0, 0, 0.85, 0);
    sim_run(&S, 1.5);
    double h1 = sim_height(&S);
    CHECK(h1 > h0 + 0.4);
    sim_sticks(&S, 0, 0, 0.5, 0);
    sim_run(&S, 1.0);
    double h2 = sim_height(&S);
    sim_run(&S, 3.0);
    double h3 = sim_height(&S);
    printf("    climb: %.2f -> %.2f, hold %.2f -> %.2f\n", h0, h1, h2, h3);
    CHECK_NEAR(h3, h2, 0.12);

    /* descend and land with the stick */
    sim_sticks(&S, 0, 0, 0.1, 0);
    sim_run(&S, 6.0);
    CHECK(sim_height(&S) < 0.02);
    fc_get_status(&st);
    CHECK((st.alt_flags & 4u) != 0u);       /* landed */
    close_log();
}

static void test_alt_takeoff_from_ground(void)
{
    boot("alt_takeoff");
    sim_channel(&S, SIM_CH_MODE, 1500);     /* ALT selected on the ground */
    CHECK(sim_arm(&S, 1.0));
    sim_run(&S, 0.5);
    CHECK(sim_height(&S) < 0.01);           /* stays down, motors idle */
    sim_sticks(&S, 0, 0, 0.75, 0);
    sim_run(&S, 2.0);
    sim_sticks(&S, 0, 0, 0.5, 0);
    sim_run(&S, 1.0);
    double h1 = sim_height(&S);
    sim_run(&S, 2.0);
    double h2 = sim_height(&S);
    fc_status_t st;
    fc_get_status(&st);
    printf("    takeoff: h %.2f -> %.2f, mode %u\n", h1, h2, st.mode);
    /* stick 0.75 commands (0.75 - 0.62) / 0.38 * 0.8 = 0.27 m/s for ~2 s */
    CHECK(h1 > 0.25 && h1 < 0.8);
    CHECK_NEAR(h2, h1, 0.06);
    CHECK_NEAR(st.mode, FMODE_ALT, 0);
    close_log();
}

static void test_failsafe_landing(void)
{
    pilot_t p;
    takeoff(&p, "failsafe", 1.0);
    sim_channel(&S, SIM_CH_MODE, 1500);
    sim_sticks(&S, 0, 0, 0.5, 0);
    sim_run(&S, 1.0);

    S.tx_on = false;                         /* transmitter off */
    sim_run(&S, 0.5);
    fc_status_t st;
    fc_get_status(&st);
    CHECK((st.flags & STF_FAILSAFE) != 0u);
    CHECK_NEAR(st.mode, FMODE_FAILSAFE, 0);
    double h_a = sim_height(&S);
    sim_run(&S, 1.0);
    double h_b = sim_height(&S);
    double vz = h_b - h_a;
    printf("    failsafe descent %.2f m/s\n", vz);
    CHECK(vz < -0.15 && vz > -0.6);
    sim_run(&S, 6.0);
    CHECK(sim_height(&S) < 0.02);
    CHECK(!fc_is_armed());
    double r, pi, y;
    sim_euler(&S, &r, &pi, &y);
    CHECK(fabs(r) < 10.0 && fabs(pi) < 10.0);
    close_log();
}

static void test_failsafe_without_tof(void)
{
    pilot_t p;
    takeoff(&p, "failsafe_notof", 1.2);
    S.tof_on = false;
    pilot_fly(&p, &S, 1.0, 0, 0, 0);
    S.tx_on = false;
    double h_max = sim_height(&S);
    for (int i = 0; i < 400; i++) {
        sim_run(&S, 0.02);
        if (sim_height(&S) > h_max) {
            h_max = sim_height(&S);
        }
    }
    CHECK(!fc_is_armed());                   /* disarmed after fs_land_s */
    CHECK(h_max < 1.6);                      /* no fly-away */
    close_log();
}

static double battery_run(bool comp, double *thr_start, double *thr_end)
{
    pilot_t p;
    takeoff(&p, comp ? "bat_comp" : "bat_nocomp", 1.0);
    g_cfg.bat_comp = comp ? 1 : 0;
    g_cfg_revision++;
    pilot_fly(&p, &S, 2.0, 0, 0, 0);
    double t0 = 0.0, t1 = 0.0;
    for (int i = 0; i < 50; i++) {
        pilot_fly(&p, &S, 0.02, 0, 0, 0);
        t0 += p.thr;
    }
    S.drain_v_per_s = 0.06;                  /* 4.15 V -> ~3.6 V */
    pilot_fly(&p, &S, 9.0, 0, 0, 0);
    S.drain_v_per_s = 0.0;
    for (int i = 0; i < 50; i++) {
        pilot_fly(&p, &S, 0.02, 0, 0, 0);
        t1 += p.thr;
    }
    *thr_start = t0 / 50.0;
    *thr_end = t1 / 50.0;
    close_log();
    return S.quad.v_bat;
}

static void test_battery_compensation(void)
{
    double a0, a1, b0, b1;
    double v1 = battery_run(true, &a0, &a1);
    double v2 = battery_run(false, &b0, &b1);
    printf("    pilot throttle with comp %.3f -> %.3f (vbat %.2f), without %.3f -> %.3f (vbat %.2f)\n",
           a0, a1, v1, b0, b1, v2);
    CHECK(fabs(a1 - a0) < 0.5 * fabs(b1 - b0));
    CHECK(b1 > b0 + 0.03);                   /* sag really needed more stick */
}

static void test_crash_detection(void)
{
    pilot_t p;
    takeoff(&p, "crash", 1.0);
    /* violent hit: the controller must recover, not disarm */
    S.quad.w[0] = 14.0;
    pilot_fly(&p, &S, 0.5, 0, 0, 0);
    CHECK(fc_is_armed());
    /* now it lies still on its side (IMU sees gravity along body y) */
    S.imu_override = true;
    S.override_acc[0] = 0.0;
    S.override_acc[1] = -1.0;
    S.override_acc[2] = 0.0;
    sim_run(&S, (g_cfg.crash_ms + 150) * 0.001);
    CHECK(!fc_is_armed());
    fc_status_t st;
    fc_get_status(&st);
    CHECK((st.flags & STF_CRASHED) != 0u);
    CHECK((st.arm_block & ARMB_CRASH) != 0u);
    S.imu_override = false;
    close_log();
}

static void test_board_rotated_props_out(void)
{
    /* board turned 90 deg (nose = right edge J2) and props-out: build the
     * physical motor layout independently of the firmware's mixer code */
    sim_init(&S);
    g_cfg.tof_offset_mm = 15.0f;
    g_cfg.board_yaw = 1;
    g_cfg.props_out = 1;
    g_cfg_revision++;
    const double a = 0.023;
    /* M1 front-left, M2 rear-left, M3 rear-right, M4 front-right */
    const double px[4] = { +a, -a, -a, +a };
    const double py[4] = { -a, -a, +a, +a };
    /* props-out: FL CCW, RL CW, RR CCW, FR CW */
    const int spin[4] = { +1, -1, +1, -1 };
    for (int i = 0; i < 4; i++) {
        S.quad.px[i] = px[i];
        S.quad.py[i] = py[i];
        S.quad.spin[i] = spin[i];
    }
    open_log("rotated");
    sim_run(&S, 3.0);
    CHECK(sim_arm(&S, 1.0));
    pilot_t p = { .h_ref = 1.0 };
    pilot_fly(&p, &S, 3.0, 0, 0, 0);
    double r, pi, y;
    sim_euler(&S, &r, &pi, &y);
    CHECK(fc_is_armed());
    CHECK_NEAR(sim_height(&S), 1.0, 0.3);
    CHECK_NEAR(r, 0.0, 3.0);
    CHECK_NEAR(pi, 0.0, 3.0);
    pilot_fly(&p, &S, 0.5, 0, 0, 0.5);       /* yaw right */
    CHECK(S.quad.w[2] > 1.0);
    close_log();
}

int main(void)
{
    RUN(test_gyro_cal_and_alignment);
    RUN(test_arming_safety);
    RUN(test_angle_mode_level_and_step);
    RUN(test_disturbance_rejection);
    RUN(test_acro_rate_tracking);
    RUN(test_alt_hold);
    RUN(test_alt_takeoff_from_ground);
    RUN(test_failsafe_landing);
    RUN(test_failsafe_without_tof);
    RUN(test_battery_compensation);
    RUN(test_crash_detection);
    RUN(test_board_rotated_props_out);
    return TT_REPORT();
}
