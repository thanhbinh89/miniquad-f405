/**
 * @file    ppm_decoder.h
 * @brief   PPM (CPPM / PPM-sum) frame decoder, hardware independent.
 *
 * Fed with edge time stamps (us) of one polarity. A gap > 2.7 ms is the
 * frame sync; pulses of 0.75..2.25 ms are channels. A frame is published
 * only when two consecutive frames have the same channel count (4..12);
 * any out-of-range interval drops sync until the next gap.
 */
#ifndef PPM_DECODER_H
#define PPM_DECODER_H

#include <stdbool.h>
#include <stdint.h>
#include "rc.h"

#define PPM_SYNC_MIN_US     2700u
#define PPM_SYNC_MAX_US     30000u
#define PPM_CH_MIN_US       750u
#define PPM_CH_MAX_US       2250u
#define PPM_MIN_CHANNELS    4u

typedef struct {
    /* decoding */
    uint16_t          ch[RC_MAX_CH];
    uint8_t           idx;
    bool              synced;
    bool              have_edge;
    uint32_t          last_edge_us;
    uint8_t           last_count;
    /* published frame (seqlock) */
    volatile uint32_t lock;
    uint16_t          frame[RC_MAX_CH];
    uint8_t           frame_count;
    uint32_t          frame_us;
    uint32_t          frame_seq;
    /* statistics */
    uint32_t          frames;
    uint32_t          glitches;
    uint32_t          prev_frame_us;
    float             rate_hz;
} ppm_decoder_t;

void ppm_decoder_init(ppm_decoder_t *d);
/** Call for every edge (interrupt context). */
void ppm_decoder_edge(ppm_decoder_t *d, uint32_t t_us);
/** Copy the newest frame into @p out (age computed against @p now_us). */
void ppm_decoder_get(ppm_decoder_t *d, rc_frame_t *out, uint32_t now_us);

#endif /* PPM_DECODER_H */
