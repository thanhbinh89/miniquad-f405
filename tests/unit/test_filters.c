/**
 * @file    test_filters.c
 * @brief   Frequency response and behaviour of the filters.
 */
#include "filters.h"
#include "maths.h"
#include "tinytest.h"

static float measured_gain(biquad_t *f, float freq, float fs)
{
    float peak = 0.0f;
    for (int i = 0; i < 4000; i++) {
        float x = sinf(2.0f * M_PI_F * freq * (float)i / fs);
        float y = biquad_apply(f, x);
        if (i > 2000 && fabsf(y) > peak) {
            peak = fabsf(y);
        }
    }
    return peak;
}

static void test_biquad_lpf(void)
{
    biquad_t f;
    biquad_init_lpf(&f, 100.0f, 1000.0f);
    CHECK_NEAR(biquad_gain(&f, 0.0f, 1000.0f), 1.0, 1e-4);
    CHECK_NEAR(biquad_gain(&f, 100.0f, 1000.0f), 0.7071, 0.01);
    CHECK(biquad_gain(&f, 300.0f, 1000.0f) < 0.15f);
    CHECK_NEAR(measured_gain(&f, 100.0f, 1000.0f), biquad_gain(&f, 100.0f, 1000.0f), 0.02);
    /* steady state reset */
    biquad_reset(&f, 3.0f);
    CHECK_NEAR(biquad_apply(&f, 3.0f), 3.0, 1e-4);
    /* bypass */
    biquad_init_lpf(&f, 0.0f, 1000.0f);
    CHECK(f.bypass);
    CHECK_NEAR(biquad_apply(&f, 1.234f), 1.234, 1e-6);
}

static void test_biquad_notch(void)
{
    biquad_t f;
    float q = notch_q_from_cutoff(300.0f, 200.0f);
    CHECK(q > 0.5f && q < 2.0f);
    biquad_init_notch(&f, 300.0f, q, 1000.0f);
    CHECK(biquad_gain(&f, 300.0f, 1000.0f) < 0.01f);
    /* close to Nyquist the bilinear transform narrows the notch */
    float g200 = biquad_gain(&f, 200.0f, 1000.0f);
    CHECK(g200 > 0.6f && g200 < 0.9f);
    CHECK_NEAR(biquad_gain(&f, 20.0f, 1000.0f), 1.0, 0.02);
    CHECK(measured_gain(&f, 300.0f, 1000.0f) < 0.02f);
    /* far from Nyquist the -3 dB point lands on the requested cutoff */
    biquad_init_notch(&f, 100.0f, notch_q_from_cutoff(100.0f, 70.0f), 8000.0f);
    CHECK_NEAR(biquad_gain(&f, 70.0f, 8000.0f), 0.7071, 0.01);
    CHECK(notch_q_from_cutoff(300.0f, 0.0f) == 0.0f);
}

static void test_pt1(void)
{
    pt1_t f;
    pt1_init(&f, 10.0f, 0.001f);
    float y = 0.0f;
    /* time constant tau = 1/(2 pi 10) = 15.9 ms: 63 % after ~16 samples */
    for (int i = 0; i < 16; i++) {
        y = pt1_apply(&f, 1.0f);
    }
    CHECK_NEAR(y, 0.632, 0.03);
    pt1_init(&f, 0.0f, 0.001f);
    CHECK_NEAR(pt1_apply(&f, 5.0f), 5.0, 1e-6);
}

static void test_median(void)
{
    median5_t m;
    median5_reset(&m);
    CHECK_NEAR(median5_apply(&m, 1.0f), 1.0, 0);
    (void)median5_apply(&m, 100.0f);
    (void)median5_apply(&m, 1.1f);
    (void)median5_apply(&m, 0.9f);
    CHECK_NEAR(median5_apply(&m, 1.05f), 1.05, 1e-6);   /* outlier rejected */
}

static void test_slew(void)
{
    CHECK_NEAR(slew_limit(0.0f, 10.0f, 100.0f, 0.01f), 1.0, 1e-6);
    CHECK_NEAR(slew_limit(0.0f, 0.5f, 100.0f, 0.01f), 0.5, 1e-6);
}

int main(void)
{
    RUN(test_biquad_lpf);
    RUN(test_biquad_notch);
    RUN(test_pt1);
    RUN(test_median);
    RUN(test_slew);
    return TT_REPORT();
}
