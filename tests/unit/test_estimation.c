/**
 * @file    test_estimation.c
 * @brief   Mahony attitude, altitude Kalman filter, IMU calibration.
 */
#include <string.h>

#include "altitude.h"
#include "attitude.h"
#include "imu_cal.h"
#include "maths.h"
#include "tinytest.h"

static uint32_t s_rng = 1u;
static float noise(float sigma)
{
    float sum = 0.0f;
    for (int i = 0; i < 12; i++) {
        s_rng = s_rng * 1664525u + 1013904223u;
        sum += (float)(s_rng >> 8) / 16777216.0f;
    }
    return (sum - 6.0f) * sigma;
}

/* accelerometer reading of a vehicle at rest with roll/pitch (deg) */
static vec3_t acc_at(float roll_deg, float pitch_deg)
{
    float r = roll_deg * DEG2RAD, p = pitch_deg * DEG2RAD;
    return v3(sinf(p), -cosf(p) * sinf(r), -cosf(p) * cosf(r));
}

static void test_align(void)
{
    attitude_t a;
    attitude_reset(&a);
    attitude_align(&a, acc_at(20.0f, -10.0f));
    CHECK_NEAR(a.roll * RAD2DEG, 20.0, 0.01);
    CHECK_NEAR(a.pitch * RAD2DEG, -10.0, 0.01);
}

static void test_accel_correction(void)
{
    attitude_t a;
    attitude_reset(&a);
    attitude_align(&a, acc_at(0, 0));
    a.fast_time = 0.0f;
    /* true attitude 15 deg roll, estimate starts level: must converge */
    for (int i = 0; i < 8000; i++) {
        attitude_update(&a, v3(0, 0, 0), acc_at(15.0f, 5.0f), 1.0f, 0.0f, 0.001f);
    }
    CHECK_NEAR(a.roll * RAD2DEG, 15.0, 0.3);
    CHECK_NEAR(a.pitch * RAD2DEG, 5.0, 0.3);
}

static void test_gyro_integration(void)
{
    attitude_t a;
    attitude_reset(&a);
    attitude_align(&a, acc_at(0, 0));
    /* 90 deg/s roll for 0.5 s with accel ignored (|a| far from 1 g) */
    for (int i = 0; i < 500; i++) {
        attitude_update(&a, v3(90.0f * DEG2RAD, 0, 0), v3(0, 0, -2.0f), 1.0f, 0.0f, 0.001f);
    }
    CHECK_NEAR(a.roll * RAD2DEG, 45.0, 0.2);
    /* yaw rate integrates into yaw */
    attitude_reset(&a);
    attitude_align(&a, acc_at(0, 0));
    for (int i = 0; i < 1000; i++) {
        attitude_update(&a, v3(0, 0, 30.0f * DEG2RAD), acc_at(0, 0), 0.5f, 0.0f, 0.001f);
    }
    CHECK_NEAR(a.yaw * RAD2DEG, 30.0, 0.3);
    /* body z axis in earth frame points down when level */
    vec3_t e = attitude_body_to_earth(&a, v3(0, 0, 1));
    CHECK_NEAR(e.z, 1.0, 1e-3);
}

static void test_altitude_kf(void)
{
    alt_est_t e;
    alt_est_reset(&e);
    const float bias = 0.3f;                /* accelerometer bias m/s^2 */
    float h = 0.5f, v = 0.0f;
    float sq = 0.0f;
    int n = 0;
    for (int k = 0; k < 20000; k++) {       /* 20 s at 1 kHz */
        float t = (float)k * 0.001f;
        float acc = 0.6f * sinf(0.8f * t);  /* true vertical acceleration */
        v += acc * 0.001f;
        h += v * 0.001f;
        alt_est_predict(&e, acc + bias + noise(0.3f), 0.001f, 0.6f, 0.02f);
        if ((k % 33) == 0) {
            (void)alt_est_update(&e, h + noise(0.01f), 0.02f, 0u);
        }
        if (k > 10000) {
            sq += (e.h - h) * (e.h - h);
            n++;
        }
    }
    float rms = sqrtf(sq / (float)n);
    printf("    kf height rms %.4f m, bias est %.3f\n", (double)rms, (double)e.b);
    CHECK(rms < 0.01f);
    CHECK_NEAR(e.b, bias, 0.1);
    CHECK_NEAR(e.v, v, 0.05);
}

static void test_altitude_outlier_and_delay(void)
{
    alt_est_t e;
    alt_est_reset(&e);
    (void)alt_est_update(&e, 1.0f, 0.02f, 0u);
    for (int k = 0; k < 2000; k++) {
        alt_est_predict(&e, 0.0f, 0.001f, 0.6f, 0.02f);
        if ((k % 33) == 0) {
            (void)alt_est_update(&e, 1.0f, 0.02f, 20u);
        }
    }
    CHECK(!alt_est_update(&e, 3.0f, 0.02f, 20u));      /* 2 m jump rejected */
    CHECK_NEAR(e.h, 1.0, 0.02);
    /* persistent new level -> re-initialised after a run of rejections */
    for (int k = 0; k < 20; k++) {
        (void)alt_est_update(&e, 2.0f, 0.02f, 20u);
    }
    CHECK_NEAR(e.h, 2.0, 0.05);
}

static void test_gyro_cal(void)
{
    imu_cal_t c;
    imu_cal_start_gyro(&c);
    /* moving first: no result */
    for (int i = 0; i < 800; i++) {
        float m = 20.0f * sinf((float)i * 0.05f);
        CHECK(imu_cal_update(&c, v3(m, 0, 0), 0.001f) == CAL_RUNNING);
    }
    cal_state_t st = CAL_RUNNING;
    for (int i = 0; i < 2000 && st == CAL_RUNNING; i++) {
        st = imu_cal_update(&c, v3(1.0f + noise(0.2f), -2.0f + noise(0.2f), 0.5f + noise(0.2f)), 0.001f);
    }
    CHECK(st == CAL_DONE);
    CHECK_NEAR(c.result.x, 1.0, 0.05);
    CHECK_NEAR(c.result.y, -2.0, 0.05);
    CHECK_NEAR(c.result.z, 0.5, 0.05);
}

static void test_accel_cal(void)
{
    imu_cal_t c;
    imu_cal_start_accel(&c);
    cal_state_t st = CAL_RUNNING;
    for (int i = 0; i < 3000 && st == CAL_RUNNING; i++) {
        st = imu_cal_update(&c, v3(0.02f + noise(0.003f), -0.01f, -0.97f), 0.001f);
    }
    CHECK(st == CAL_DONE);
    CHECK_NEAR(c.result.z + 1.0f, 0.03, 0.002);
    /* tilted board -> fails after the attempts */
    imu_cal_start_accel(&c);
    st = CAL_RUNNING;
    for (int i = 0; i < 20000 && st == CAL_RUNNING; i++) {
        st = imu_cal_update(&c, acc_at(30.0f, 0.0f), 0.001f);
    }
    CHECK(st == CAL_FAILED);
}

int main(void)
{
    RUN(test_align);
    RUN(test_accel_correction);
    RUN(test_gyro_integration);
    RUN(test_altitude_kf);
    RUN(test_altitude_outlier_and_delay);
    RUN(test_gyro_cal);
    RUN(test_accel_cal);
    return TT_REPORT();
}
