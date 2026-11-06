/**
 * @file    ppm_decoder.c
 * @brief   PPM frame decoder.
 */
#include "ppm_decoder.h"

#include <string.h>

void ppm_decoder_init(ppm_decoder_t *d)
{
    memset(d, 0, sizeof(*d));
}

static void publish(ppm_decoder_t *d, uint32_t t_us)
{
    d->lock++;
    __sync_synchronize();
    memcpy(d->frame, d->ch, sizeof(d->frame));
    d->frame_count = d->idx;
    d->frame_us = t_us;
    d->frame_seq++;
    __sync_synchronize();
    d->lock++;

    d->frames++;
    if (d->prev_frame_us != 0u) {
        uint32_t period = t_us - d->prev_frame_us;
        if (period > 0u && period < 100000u) {
            float hz = 1.0e6f / (float)period;
            d->rate_hz += (hz - d->rate_hz) * 0.1f;
        }
    }
    d->prev_frame_us = t_us;
}

void ppm_decoder_edge(ppm_decoder_t *d, uint32_t t_us)
{
    if (!d->have_edge) {
        d->have_edge = true;
        d->last_edge_us = t_us;
        return;
    }
    uint32_t dt = t_us - d->last_edge_us;
    d->last_edge_us = t_us;

    if (dt >= PPM_SYNC_MIN_US && dt <= PPM_SYNC_MAX_US) {
        if (d->synced && d->idx >= PPM_MIN_CHANNELS) {
            if (d->idx == d->last_count) {
                publish(d, t_us);
            }
            d->last_count = d->idx;
        }
        d->synced = true;
        d->idx = 0u;
    } else if (dt >= PPM_CH_MIN_US && dt <= PPM_CH_MAX_US) {
        if (d->synced) {
            if (d->idx < RC_MAX_CH) {
                d->ch[d->idx++] = (uint16_t)dt;
            } else {
                d->synced = false;      /* more channels than supported */
                d->glitches++;
            }
        }
    } else {
        if (d->synced) {
            d->glitches++;
        }
        d->synced = false;
        d->last_count = 0u;
    }
}

void ppm_decoder_get(ppm_decoder_t *d, rc_frame_t *out, uint32_t now_us)
{
    uint32_t a, b;
    do {
        a = d->lock;
        __sync_synchronize();
        memcpy(out->ch, d->frame, sizeof(out->ch));
        out->count = d->frame_count;
        out->frame_seq = d->frame_seq;
        uint32_t fus = d->frame_us;
        out->age_ms = (d->frame_seq == 0u) ? 0xFFFFFFFFu : (now_us - fus) / 1000u;
        __sync_synchronize();
        b = d->lock;
    } while (a != b || (a & 1u) != 0u);
    float r = d->rate_hz;
    out->rate_hz = (uint8_t)((r > 255.0f) ? 255.0f : ((r < 0.0f) ? 0.0f : r));
}
