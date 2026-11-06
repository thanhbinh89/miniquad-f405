/**
 * @file    rc.h
 * @brief   RC channel processing: mapping, normalisation, dead band, expo,
 *          stick smoothing, switches and signal validity.
 *
 * Stick conventions (Betaflight compatible): high pulse = roll right,
 * pitch forward (nose down), yaw right, throttle up.
 */
#ifndef RC_H
#define RC_H

#include <stdbool.h>
#include <stdint.h>
#include "filters.h"

#define RC_MAX_CH       12u

/** Raw input as delivered by the PPM decoder. */
typedef struct {
    uint32_t frame_seq;         /* increments on every complete frame */
    uint32_t age_ms;            /* time since the last complete frame */
    uint8_t  count;             /* channels in the last frame          */
    uint8_t  rate_hz;           /* measured frame rate                 */
    uint16_t ch[RC_MAX_CH];     /* microseconds                        */
} rc_frame_t;

typedef struct {
    bool     valid;             /* fresh, plausible frames             */
    uint32_t valid_ms;          /* continuously valid for (ms)         */
    float    roll;              /* -1..1, smoothed                     */
    float    pitch;
    float    yaw;
    float    throttle;          /* 0..1, smoothed                      */
    float    throttle_raw;      /* 0..1, unsmoothed                    */
    bool     arm;               /* arm switch above threshold          */
    uint8_t  mode_pos;          /* 0 low, 1 mid, 2 high                */
    bool     beeper;
    /* internal */
    biquad_t lpf[4];
    uint32_t cfg_rev;
    float    loop_hz;
    uint32_t valid_since_ms;
    bool     primed;
} rc_state_t;

void  rc_init(rc_state_t *rc, float loop_hz);
void  rc_update(rc_state_t *rc, const rc_frame_t *f, uint32_t now_ms);

/** Cubic expo: x * (1 - e + e x^2), x in [-1, 1]. */
float rc_expo(float x, float expo);
/** Stick -> body rate (deg/s). */
float rc_stick_to_rate(float stick, float rate_max, float expo);
/** Throttle curve with mid point and expo, [0, 1] -> [0, 1]. */
float rc_throttle_curve(float t, float mid, float expo);

#endif /* RC_H */
