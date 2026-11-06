/**
 * @file    tlm_rings.h
 * @brief   High-rate telemetry samples produced by the flight loop and
 *          drained by the telemetry task (exact, jitter-free sampling).
 *
 * The flight loop pushes one sample every 'divider' loops (0 = off).
 */
#ifndef TLM_RINGS_H
#define TLM_RINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "proto.h"

void     tlm_rings_reset(void);

void     tlm_set_rate_divider(uint16_t div);
uint16_t tlm_rate_divider(void);
bool     tlm_push_rate(const tlm_rate_t *s);
bool     tlm_pop_rate(tlm_rate_t *s);

void     tlm_set_imu_divider(uint16_t div);
uint16_t tlm_imu_divider(void);
bool     tlm_push_imu(const tlm_imu_t *s);
bool     tlm_pop_imu(tlm_imu_t *s);

uint32_t tlm_rings_dropped(void);

#endif /* TLM_RINGS_H */
