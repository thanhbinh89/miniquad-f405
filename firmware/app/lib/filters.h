/**
 * @file    filters.h
 * @brief   Digital filters used by the flight code.
 *
 *  - pt1:     first order low-pass (RC), cheap, used for slow signals
 *  - biquad:  2nd order IIR (transposed direct form II):
 *               low-pass (Butterworth, Q = 1/sqrt(2)), notch, band-pass
 *  - median:  3/5 sample median for outlier rejection (ToF)
 *  - slew:    rate limiter
 *
 * A cutoff of 0 (or >= Nyquist) turns a filter into a pass-through, so
 * every filter can be disabled from the configurator.
 */
#ifndef FILTERS_H
#define FILTERS_H

#include <stdbool.h>
#include <stdint.h>

/* ---------------- PT1 ---------------- */
typedef struct {
    float k;
    float y;
} pt1_t;

void  pt1_init(pt1_t *f, float cutoff_hz, float dt);
/** Change the cutoff while keeping the filter state. */
void  pt1_set_cutoff(pt1_t *f, float cutoff_hz, float dt);
float pt1_apply(pt1_t *f, float x);
void  pt1_reset(pt1_t *f, float y);

/* ---------------- biquad ---------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float s1, s2;               /* transposed DF-II state */
    bool  bypass;
} biquad_t;

void  biquad_init_lpf(biquad_t *f, float cutoff_hz, float sample_hz);
void  biquad_init_notch(biquad_t *f, float center_hz, float q, float sample_hz);
void  biquad_init_bpf(biquad_t *f, float center_hz, float q, float sample_hz);
/** Update coefficients, keep state (for live tuning). */
void  biquad_set_lpf(biquad_t *f, float cutoff_hz, float sample_hz);
void  biquad_set_notch(biquad_t *f, float center_hz, float q, float sample_hz);
float biquad_apply(biquad_t *f, float x);
/** Put the filter in steady state for a constant input @p x. */
void  biquad_reset(biquad_t *f, float x);

/** Notch Q from center and lower -3 dB cutoff frequency. */
float notch_q_from_cutoff(float center_hz, float cutoff_hz);

/** Magnitude response |H(f)| (used by tests and the configurator). */
float biquad_gain(const biquad_t *f, float freq_hz, float sample_hz);

/* ---------------- median ---------------- */
typedef struct {
    float   buf[5];
    uint8_t idx;
    uint8_t count;
} median5_t;

void  median5_reset(median5_t *m);
float median5_apply(median5_t *m, float x);

/* ---------------- slew limiter ---------------- */
float slew_limit(float current, float target, float max_rate, float dt);

#endif /* FILTERS_H */
