/**
 * @file    test_rc.c
 * @brief   RC normalisation, expo, throttle curve, switches, validity.
 */
#include <string.h>

#include "config.h"
#include "params.h"
#include "rc.h"
#include "tinytest.h"

static rc_frame_t frame(uint16_t r, uint16_t p, uint16_t t, uint16_t y, uint16_t arm, uint16_t mode)
{
    rc_frame_t f;
    memset(&f, 0, sizeof(f));
    f.count = 8u;
    f.ch[0] = r;
    f.ch[1] = p;
    f.ch[2] = t;
    f.ch[3] = y;
    f.ch[4] = arm;
    f.ch[5] = mode;
    f.ch[6] = 1500;
    f.ch[7] = 1500;
    return f;
}

static void settle(rc_state_t *rc, const rc_frame_t *f)
{
    for (uint32_t i = 0; i < 500; i++) {
        rc_update(rc, f, i);
    }
}

static void test_normalisation(void)
{
    params_reset_defaults();
    rc_state_t rc;
    rc_init(&rc, 1000.0f);
    rc_frame_t f = frame(2000, 1000, 1500, 1500, 1000, 1000);
    settle(&rc, &f);
    CHECK(rc.valid);
    CHECK_NEAR(rc.roll, 1.0, 1e-3);
    CHECK_NEAR(rc.pitch, -1.0, 1e-3);
    CHECK_NEAR(rc.yaw, 0.0, 1e-6);
    CHECK_NEAR(rc.throttle, 0.5, 1e-3);
    CHECK(!rc.arm);
    CHECK(rc.mode_pos == 0u);

    f = frame(1505, 1495, 1000, 1750, 1900, 1500);   /* inside dead band (8 us) */
    settle(&rc, &f);
    CHECK_NEAR(rc.roll, 0.0, 1e-6);
    CHECK_NEAR(rc.pitch, 0.0, 1e-6);
    CHECK_NEAR(rc.yaw, (250.0 - 8.0) / (500.0 - 8.0), 1e-3);
    CHECK(rc.arm);
    CHECK(rc.mode_pos == 1u);
    f = frame(1500, 1500, 1000, 1500, 1900, 2000);
    settle(&rc, &f);
    CHECK(rc.mode_pos == 2u);
}

static void test_validity(void)
{
    params_reset_defaults();
    rc_state_t rc;
    rc_init(&rc, 1000.0f);
    rc_frame_t f = frame(1500, 1500, 1000, 1500, 1000, 1000);
    settle(&rc, &f);
    CHECK(rc.valid);
    f.age_ms = 400u;                        /* > rx_fs_ms (250) */
    rc_update(&rc, &f, 1000);
    CHECK(!rc.valid);
    f.age_ms = 0u;
    f.count = 3u;
    rc_update(&rc, &f, 1001);
    CHECK(!rc.valid);
    /* throttle based failsafe detection */
    g_cfg.rx_fs_thr_us = 950.0f;
    f = frame(1500, 1500, 900, 1500, 1000, 1000);
    rc_update(&rc, &f, 1002);
    CHECK(!rc.valid);
    /* channel map: throttle on channel 1 */
    params_reset_defaults();
    g_cfg.rc_map[2] = 1;
    g_cfg.rc_map[0] = 3;
    g_cfg_revision++;
    f = frame(2000, 1500, 1500, 1500, 1000, 1000);
    settle(&rc, &f);
    CHECK_NEAR(rc.throttle, 1.0, 1e-3);
    CHECK_NEAR(rc.roll, 0.0, 1e-3);
}

static void test_curves(void)
{
    CHECK_NEAR(rc_expo(1.0f, 0.5f), 1.0, 1e-6);
    CHECK_NEAR(rc_expo(0.5f, 0.0f), 0.5, 1e-6);
    CHECK(rc_expo(0.5f, 0.5f) < 0.5f);
    CHECK_NEAR(rc_stick_to_rate(-1.0f, 600.0f, 0.3f), -600.0, 1e-3);
    CHECK_NEAR(rc_throttle_curve(0.5f, 0.5f, 0.7f), 0.5, 1e-6);
    CHECK_NEAR(rc_throttle_curve(0.0f, 0.4f, 0.5f), 0.0, 1e-6);
    CHECK_NEAR(rc_throttle_curve(1.0f, 0.4f, 0.5f), 1.0, 1e-6);
    CHECK_NEAR(rc_throttle_curve(0.3f, 0.5f, 0.0f), 0.3, 1e-6);
}

int main(void)
{
    RUN(test_normalisation);
    RUN(test_validity);
    RUN(test_curves);
    return TT_REPORT();
}
