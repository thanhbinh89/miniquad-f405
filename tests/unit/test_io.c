/**
 * @file    test_io.c
 * @brief   Pure parts of the I/O path: PPM decoder, battery monitor,
 *          IMU frame rotation and the mini printf / log queue.
 */
#include <string.h>

#include "battery.h"
#include "config.h"
#include "imu_frame.h"
#include "log.h"
#include "params.h"
#include "ppm_decoder.h"
#include "proto.h"
#include "tinytest.h"

/* the printf tests feed deliberately odd formats ("%q", NULL for %s) */
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-overflow"

/* ------------------------------------------------------------------------ */
/* PPM                                                                       */
/* ------------------------------------------------------------------------ */

/* Emit one PPM frame (edges of one polarity) starting at *t. */
static void ppm_frame(ppm_decoder_t *d, uint32_t *t, const uint16_t *ch, int n, uint32_t frame_us)
{
    uint32_t used = 0u;
    for (int i = 0; i < n; i++) {
        *t += ch[i];
        used += ch[i];
        ppm_decoder_edge(d, *t);
    }
    *t += frame_us - used;          /* sync gap, closed by the next edge */
    ppm_decoder_edge(d, *t);
}

static void test_ppm_decode(void)
{
    ppm_decoder_t d;
    ppm_decoder_init(&d);
    const uint16_t ch[8] = { 1000, 1250, 1500, 1750, 2000, 1100, 1900, 1500 };
    uint32_t t = 1000u;
    ppm_decoder_edge(&d, t);        /* first edge only primes the decoder */

    rc_frame_t f;
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == 0u);
    CHECK(f.age_ms == 0xFFFFFFFFu);

    /* the first gap only synchronises, frames 1 and 2 establish the count */
    t += 9000u;
    ppm_decoder_edge(&d, t);
    ppm_frame(&d, &t, ch, 8, 22500u);
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == 0u);
    ppm_frame(&d, &t, ch, 8, 22500u);
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == 1u);
    CHECK(f.count == 8u);
    for (int i = 0; i < 8; i++) {
        CHECK(f.ch[i] == ch[i]);
    }
    CHECK(f.age_ms == 0u);

    for (int k = 0; k < 50; k++) {
        ppm_frame(&d, &t, ch, 8, 22500u);
    }
    ppm_decoder_get(&d, &f, t + 30000u);
    CHECK(f.frame_seq == 51u);
    CHECK(f.age_ms == 30u);
    CHECK_NEAR(f.rate_hz, 44, 1);
    CHECK(d.glitches == 0u);
}

static void test_ppm_glitch(void)
{
    ppm_decoder_t d;
    ppm_decoder_init(&d);
    const uint16_t ch[6] = { 1500, 1500, 1000, 1500, 1000, 1000 };
    uint32_t t = 0u;
    ppm_decoder_edge(&d, t);
    t += 5000u;
    ppm_decoder_edge(&d, t);
    for (int k = 0; k < 5; k++) {
        ppm_frame(&d, &t, ch, 6, 20000u);
    }
    rc_frame_t f;
    ppm_decoder_get(&d, &f, t);
    uint32_t seq = f.frame_seq;
    CHECK(seq == 4u);

    /* a 300 us spike inside a frame drops sync until the next gap ... */
    t += 1500u;
    ppm_decoder_edge(&d, t);
    t += 300u;
    ppm_decoder_edge(&d, t);
    CHECK(d.glitches == 1u);
    t += 15000u;                    /* ... which ends the broken frame */
    ppm_decoder_edge(&d, t);
    /* ... and two complete frames are needed again */
    ppm_frame(&d, &t, ch, 6, 20000u);
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == seq);
    ppm_frame(&d, &t, ch, 6, 20000u);
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == seq + 1u);

    /* a frame with a different channel count is not published */
    const uint16_t ch5[5] = { 1500, 1500, 1000, 1500, 1000 };
    ppm_frame(&d, &t, ch5, 5, 20000u);
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == seq + 1u);
    CHECK(f.count == 6u);
}

static void test_ppm_too_few_channels(void)
{
    ppm_decoder_t d;
    ppm_decoder_init(&d);
    const uint16_t ch[3] = { 1500, 1500, 1500 };
    uint32_t t = 0u;
    ppm_decoder_edge(&d, t);
    t += 5000u;
    ppm_decoder_edge(&d, t);
    for (int k = 0; k < 10; k++) {
        ppm_frame(&d, &t, ch, 3, 20000u);
    }
    rc_frame_t f;
    ppm_decoder_get(&d, &f, t);
    CHECK(f.frame_seq == 0u);
}

/* ------------------------------------------------------------------------ */
/* battery                                                                   */
/* ------------------------------------------------------------------------ */

static const battery_cal_t CAL = { 1500u, 940u, 1210u };

/* ADC counts for a battery voltage at VDDA = 3.3 V (VREFINT reads 1500) */
static uint16_t vbat_counts(float v)
{
    return (uint16_t)(v / 2.0f / 3.3f * 4095.0f + 0.5f);
}

static void run_bat(battery_t *b, float v, float seconds)
{
    int n = (int)(seconds * 200.0f);
    for (int i = 0; i < n; i++) {
        battery_update(b, &CAL, vbat_counts(v), 1500u, 1000u, 0.005f);
    }
}

static void test_battery_scaling(void)
{
    params_reset_defaults();
    battery_t b;
    battery_init(&b);
    CHECK(b.state == BAT_NONE);

    run_bat(&b, 4.0f, 0.5f);
    CHECK(b.present);
    CHECK(b.state == BAT_OK);
    CHECK_NEAR(b.vdda, 3.3, 0.001);
    CHECK_NEAR(b.v_raw, 4.0, 0.005);
    CHECK_NEAR(b.v_comp, 4.0, 0.005);
    CHECK_NEAR(b.v_slow, 4.0, 0.005);
    /* 1000 counts between TS_CAL1 (30 C) and TS_CAL2 (110 C) */
    CHECK_NEAR(b.mcu_temp, 30.0 + (1000.0 - 940.0) * 80.0 / 270.0, 0.05);

    /* VDDA drops to 3.0 V: VREFINT reads more counts, the battery fewer */
    uint16_t vref = (uint16_t)(1500.0f * 3.3f / 3.0f + 0.5f);
    uint16_t raw = (uint16_t)(4.0f / 2.0f / 3.0f * 4095.0f + 0.5f);
    for (int i = 0; i < 2000; i++) {
        battery_update(&b, &CAL, raw, vref, 1000u, 0.005f);
    }
    CHECK_NEAR(b.vdda, 3.0, 0.005);
    CHECK_NEAR(b.v_raw, 4.0, 0.01);

    /* the compensation filter follows a sag much faster than the slow one */
    battery_init(&b);
    run_bat(&b, 4.0f, 1.0f);
    run_bat(&b, 3.6f, 0.25f);
    CHECK(b.v_comp < 3.80f);
    CHECK(b.v_slow > 3.85f);
}

static void test_battery_states(void)
{
    params_reset_defaults();
    battery_t b;
    battery_init(&b);
    run_bat(&b, 4.0f, 1.0f);

    /* below the warning level: WARN only after the slow filter + 2 s */
    float warn = g_cfg.bat_warn_v - 0.05f;
    run_bat(&b, warn, 3.0f);
    CHECK(b.state == BAT_OK);
    run_bat(&b, warn, 20.0f);
    CHECK(b.state == BAT_WARN);

    /* critical */
    float crit = g_cfg.bat_crit_v - 0.05f;
    run_bat(&b, crit, 30.0f);
    CHECK(b.state == BAT_CRIT);

    /* a short rise (landing, sag gone) does not clear the alarm */
    run_bat(&b, g_cfg.bat_warn_v + 0.3f, 4.0f);
    CHECK(b.state == BAT_CRIT);
    /* a fresh battery recovers in two steps: CRIT -> WARN -> OK */
    run_bat(&b, 4.1f, 40.0f);
    CHECK(b.state == BAT_OK);

    /* unplugged (debug connector only) */
    run_bat(&b, 1.0f, 0.1f);
    CHECK(!b.present);
    CHECK(b.state == BAT_NONE);
    /* hysteresis: 2.45 V is not enough to become present again */
    run_bat(&b, 2.45f, 0.1f);
    CHECK(!b.present);
}

/* ------------------------------------------------------------------------ */
/* IMU frame                                                                 */
/* ------------------------------------------------------------------------ */

static void test_imu_frame(void)
{
    vec3_t s = v3(1.0f, 2.0f, 3.0f);    /* board x (right edge), y (top edge), z (up) */

    vec3_t b0 = imu_frame_to_body(s, 0);  /* nose = top edge */
    CHECK_NEAR(b0.x, 2.0, 1e-6);
    CHECK_NEAR(b0.y, 1.0, 1e-6);
    CHECK_NEAR(b0.z, -3.0, 1e-6);

    vec3_t b1 = imu_frame_to_body(s, 1);  /* nose = right edge: right = bottom edge */
    CHECK_NEAR(b1.x, 1.0, 1e-6);
    CHECK_NEAR(b1.y, -2.0, 1e-6);
    CHECK_NEAR(b1.z, -3.0, 1e-6);

    vec3_t b2 = imu_frame_to_body(s, 2);
    CHECK_NEAR(b2.x, -2.0, 1e-6);
    CHECK_NEAR(b2.y, -1.0, 1e-6);

    vec3_t b3 = imu_frame_to_body(s, 3);
    CHECK_NEAR(b3.x, -1.0, 1e-6);
    CHECK_NEAR(b3.y, 2.0, 1e-6);

    /* level board at rest: the accelerometer reads +1 g on sensor Z, which
       must become -1 g on body Z (FRD, specific force points up) */
    vec3_t g = imu_frame_to_body(v3(0.0f, 0.0f, 1.0f), 0);
    CHECK_NEAR(g.z, -1.0, 1e-6);
    /* right-handed for every orientation: x cross y = z */
    for (int yaw = 0; yaw < 4; yaw++) {
        vec3_t ex = imu_frame_to_body(v3(1, 0, 0), yaw);
        vec3_t ey = imu_frame_to_body(v3(0, 1, 0), yaw);
        vec3_t ez = imu_frame_to_body(v3(0, 0, 1), yaw);
        vec3_t c = v3(ex.y * ey.z - ex.z * ey.y, ex.z * ey.x - ex.x * ey.z, ex.x * ey.y - ex.y * ey.x);
        CHECK_NEAR(c.x, ez.x, 1e-6);
        CHECK_NEAR(c.y, ez.y, 1e-6);
        CHECK_NEAR(c.z, ez.z, 1e-6);
    }
}

/* ------------------------------------------------------------------------ */
/* mini printf + log queue                                                   */
/* ------------------------------------------------------------------------ */

#define FMT_EQ(expect, ...)                                                       \
    do {                                                                          \
        char _b[96];                                                              \
        size_t _n = mini_snprintf(_b, sizeof(_b), __VA_ARGS__);                   \
        CHECK(strcmp(_b, expect) == 0);                                           \
        CHECK(_n == strlen(expect));                                              \
        if (strcmp(_b, expect) != 0) {                                            \
            printf("    got '%s' expected '%s'\n", _b, expect);                   \
        }                                                                         \
    } while (0)

static void test_printf(void)
{
    FMT_EQ("x=42 y=-7", "x=%d y=%i", 42, -7);
    FMT_EQ("-2147483648", "%d", (int32_t)INT32_MIN);
    FMT_EQ("4294967295", "%u", 4294967295u);
    FMT_EQ("0x00ff BEEF", "0x%04x %X", 0xFFu, 0xBEEFu);
    FMT_EQ("[  12][12  ][0012][-012]", "[%4d][%-4d][%04d][%04d]", 12, 12, 12, -12);
    FMT_EQ("abc|ab|  ab", "%s|%.2s|%4.2s", "abc", "abcdef", "abc");
    FMT_EQ("(null)", "%s", (const char *)NULL);
    FMT_EQ("c=Z 100%", "c=%c 100%%", 'Z');
    FMT_EQ("3.142 -0.50 2", "%.3f %.2f %.0f", 3.14159, -0.5, 2.4);
    FMT_EQ("1.000", "%f", 0.9999999);
    FMT_EQ("  1.5", "%5.1f", 1.5);
    FMT_EQ("nan", "%f", (double)NAN);
    FMT_EQ("big", "%.3f", 1.0e12);
    FMT_EQ("%q", "%q");

    /* truncation: always terminated, returns the full length */
    char small[6];
    size_t n = mini_snprintf(small, sizeof(small), "%s", "0123456789");
    CHECK(n == 10u);
    CHECK(strcmp(small, "01234") == 0);
}

static void test_log_queue(void)
{
    log_msg_t m;
    while (log_pop(&m)) {
    }
    LOG_I("boot %d", 1);
    LOG_W("vbat %.2f", 3.3);
    CHECK(log_pop(&m));
    CHECK(m.level == LOG_LEVEL_INFO);
    CHECK(m.len == 6u && memcmp(m.text, "boot 1", 6) == 0);
    CHECK(log_pop(&m));
    CHECK(m.level == LOG_LEVEL_WARN);
    CHECK(strcmp(m.text, "vbat 3.30") == 0);
    CHECK(!log_pop(&m));

    /* overflow drops the oldest messages (queue holds 7) */
    for (int i = 0; i < 20; i++) {
        LOG_D("msg %d", i);
    }
    int count = 0;
    int first = -1;
    while (log_pop(&m)) {
        if (first < 0) {
            first = m.text[4] == '1' ? 10 + (m.text[5] - '0') : (m.text[4] - '0');
        }
        count++;
    }
    CHECK(count == 7);
    CHECK(first == 13);

    /* long messages are cut, not overflowed */
    char longs[300];
    memset(longs, 'a', sizeof(longs) - 1u);
    longs[sizeof(longs) - 1u] = '\0';
    LOG_E("%s", longs);
    CHECK(log_pop(&m));
    CHECK(m.len == LOG_MSG_MAX - 1u);
    CHECK(m.text[LOG_MSG_MAX - 1u] == '\0');
}

int main(void)
{
    RUN(test_ppm_decode);
    RUN(test_ppm_glitch);
    RUN(test_ppm_too_few_channels);
    RUN(test_battery_scaling);
    RUN(test_battery_states);
    RUN(test_imu_frame);
    RUN(test_printf);
    RUN(test_log_queue);
    return TT_REPORT();
}
