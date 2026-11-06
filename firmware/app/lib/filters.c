/**
 * @file    filters.c
 * @brief   PT1, biquad, median and slew filters.
 */
#include "filters.h"

#include <math.h>
#include <string.h>
#include "maths.h"

/* ---------------- PT1 ---------------- */
static float pt1_gain(float cutoff_hz, float dt)
{
    if (cutoff_hz <= 0.0f || dt <= 0.0f) {
        return 1.0f;
    }
    float rc = 1.0f / (2.0f * M_PI_F * cutoff_hz);
    return dt / (rc + dt);
}

void pt1_init(pt1_t *f, float cutoff_hz, float dt)
{
    f->k = pt1_gain(cutoff_hz, dt);
    f->y = 0.0f;
}

void pt1_set_cutoff(pt1_t *f, float cutoff_hz, float dt)
{
    f->k = pt1_gain(cutoff_hz, dt);
}

float pt1_apply(pt1_t *f, float x)
{
    f->y += f->k * (x - f->y);
    return f->y;
}

void pt1_reset(pt1_t *f, float y)
{
    f->y = y;
}

/* ---------------- biquad ---------------- */
typedef enum { BQ_LPF, BQ_NOTCH, BQ_BPF } bq_kind_t;

static void biquad_coeffs(biquad_t *f, bq_kind_t kind, float fc, float q, float fs)
{
    if (fc <= 0.0f || fs <= 0.0f || fc >= 0.48f * fs || q <= 0.0f) {
        f->bypass = true;
        f->b0 = 1.0f;
        f->b1 = f->b2 = f->a1 = f->a2 = 0.0f;
        return;
    }
    float w0 = 2.0f * M_PI_F * fc / fs;
    float sn = sinf(w0);
    float cs = cosf(w0);
    float alpha = sn / (2.0f * q);
    float a0 = 1.0f + alpha;
    float b0, b1, b2;

    switch (kind) {
    case BQ_LPF:
        b0 = (1.0f - cs) * 0.5f;
        b1 = 1.0f - cs;
        b2 = (1.0f - cs) * 0.5f;
        break;
    case BQ_NOTCH:
        b0 = 1.0f;
        b1 = -2.0f * cs;
        b2 = 1.0f;
        break;
    case BQ_BPF:
    default:
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
        break;
    }
    f->bypass = false;
    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = (-2.0f * cs) / a0;
    f->a2 = (1.0f - alpha) / a0;
}

void biquad_init_lpf(biquad_t *f, float cutoff_hz, float sample_hz)
{
    memset(f, 0, sizeof(*f));
    biquad_coeffs(f, BQ_LPF, cutoff_hz, 0.70710678f, sample_hz);
}

void biquad_init_notch(biquad_t *f, float center_hz, float q, float sample_hz)
{
    memset(f, 0, sizeof(*f));
    biquad_coeffs(f, BQ_NOTCH, center_hz, q, sample_hz);
}

void biquad_init_bpf(biquad_t *f, float center_hz, float q, float sample_hz)
{
    memset(f, 0, sizeof(*f));
    biquad_coeffs(f, BQ_BPF, center_hz, q, sample_hz);
}

void biquad_set_lpf(biquad_t *f, float cutoff_hz, float sample_hz)
{
    biquad_coeffs(f, BQ_LPF, cutoff_hz, 0.70710678f, sample_hz);
}

void biquad_set_notch(biquad_t *f, float center_hz, float q, float sample_hz)
{
    biquad_coeffs(f, BQ_NOTCH, center_hz, q, sample_hz);
}

float biquad_apply(biquad_t *f, float x)
{
    if (f->bypass) {
        return x;
    }
    float y = f->b0 * x + f->s1;
    f->s1 = f->b1 * x - f->a1 * y + f->s2;
    f->s2 = f->b2 * x - f->a2 * y;
    return y;
}

void biquad_reset(biquad_t *f, float x)
{
    float den = 1.0f + f->a1 + f->a2;
    float g = (fabsf(den) > 1e-9f) ? (f->b0 + f->b1 + f->b2) / den : 1.0f;
    float y = g * x;
    f->s1 = y - f->b0 * x;
    f->s2 = f->b2 * x - f->a2 * y;
}

float notch_q_from_cutoff(float center_hz, float cutoff_hz)
{
    if (cutoff_hz <= 0.0f || cutoff_hz >= center_hz) {
        return 0.0f;
    }
    return center_hz * cutoff_hz / (center_hz * center_hz - cutoff_hz * cutoff_hz);
}

float biquad_gain(const biquad_t *f, float freq_hz, float sample_hz)
{
    if (f->bypass) {
        return 1.0f;
    }
    float w = 2.0f * M_PI_F * freq_hz / sample_hz;
    float c1 = cosf(w), s1 = sinf(w);
    float c2 = cosf(2.0f * w), s2 = sinf(2.0f * w);
    float nr = f->b0 + f->b1 * c1 + f->b2 * c2;
    float ni = -(f->b1 * s1 + f->b2 * s2);
    float dr = 1.0f + f->a1 * c1 + f->a2 * c2;
    float di = -(f->a1 * s1 + f->a2 * s2);
    return sqrtf((nr * nr + ni * ni) / (dr * dr + di * di));
}

/* ---------------- median ---------------- */
void median5_reset(median5_t *m)
{
    memset(m, 0, sizeof(*m));
}

float median5_apply(median5_t *m, float x)
{
    m->buf[m->idx] = x;
    m->idx = (uint8_t)((m->idx + 1u) % 5u);
    if (m->count < 5u) {
        m->count++;
    }

    float s[5];
    uint8_t n = m->count;
    memcpy(s, m->buf, sizeof(s));
    if (n < 5u) {
        /* not full yet: use the n most recent samples (positions 0..n-1) */
        for (uint8_t i = 0u; i < n; i++) {
            s[i] = m->buf[i];
        }
    }
    for (uint8_t i = 1u; i < n; i++) {
        float v = s[i];
        int j = (int)i - 1;
        while (j >= 0 && s[j] > v) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = v;
    }
    return s[n / 2u];
}

/* ---------------- slew ---------------- */
float slew_limit(float current, float target, float max_rate, float dt)
{
    float step = max_rate * dt;
    return current + constrainf(target - current, -step, step);
}
