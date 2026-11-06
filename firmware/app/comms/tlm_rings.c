/**
 * @file    tlm_rings.c
 * @brief   SPSC rings for rate-loop and IMU telemetry samples.
 */
#include "tlm_rings.h"

#include "spsc.h"

SPSC_DEFINE(rate_ring, tlm_rate_t, 32u)
SPSC_DEFINE(imu_ring, tlm_imu_t, 32u)

static rate_ring_t        s_rate;
static imu_ring_t         s_imu;
static volatile uint16_t  s_rate_div;
static volatile uint16_t  s_imu_div;

void tlm_rings_reset(void)
{
    s_rate_div = 0u;
    s_imu_div = 0u;
    rate_ring_clear(&s_rate);
    imu_ring_clear(&s_imu);
}

void tlm_set_rate_divider(uint16_t div)
{
    s_rate_div = div;
}

uint16_t tlm_rate_divider(void)
{
    return s_rate_div;
}

bool tlm_push_rate(const tlm_rate_t *s)
{
    return rate_ring_push(&s_rate, s);
}

bool tlm_pop_rate(tlm_rate_t *s)
{
    return rate_ring_pop(&s_rate, s);
}

void tlm_set_imu_divider(uint16_t div)
{
    s_imu_div = div;
}

uint16_t tlm_imu_divider(void)
{
    return s_imu_div;
}

bool tlm_push_imu(const tlm_imu_t *s)
{
    return imu_ring_push(&s_imu, s);
}

bool tlm_pop_imu(tlm_imu_t *s)
{
    return imu_ring_pop(&s_imu, s);
}

uint32_t tlm_rings_dropped(void)
{
    return s_rate.dropped + s_imu.dropped;
}
