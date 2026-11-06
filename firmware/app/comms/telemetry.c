/**
 * @file    telemetry.c
 * @brief   Telemetry stream scheduling.
 */
#include "telemetry.h"

#include "app_services.h"
#include "comms.h"
#include "proto.h"
#include "tlm_rings.h"

#define LOOP_HZ         1000u

static const uint16_t s_max_hz[STREAM_COUNT] = { 50u, 500u, 500u, 100u };
static uint16_t s_rate[STREAM_COUNT];
static uint32_t s_next_ms[STREAM_COUNT];

void telemetry_init(void)
{
    telemetry_stop_all();
}

uint16_t telemetry_set_rate(uint8_t stream, uint16_t hz)
{
    if (stream >= STREAM_COUNT) {
        return 0u;
    }
    if (hz > s_max_hz[stream]) {
        hz = s_max_hz[stream];
    }
    if (stream == STREAM_RATE || stream == STREAM_IMU) {
        uint16_t div = (hz == 0u) ? 0u : (uint16_t)(LOOP_HZ / hz);
        if (hz != 0u && div == 0u) {
            div = 1u;
        }
        hz = (div == 0u) ? 0u : (uint16_t)(LOOP_HZ / div);
        if (stream == STREAM_RATE) {
            tlm_set_rate_divider(div);
        } else {
            tlm_set_imu_divider(div);
        }
    }
    s_rate[stream] = hz;
    s_next_ms[stream] = 0u;
    return hz;
}

uint16_t telemetry_rate(uint8_t stream)
{
    return (stream < STREAM_COUNT) ? s_rate[stream] : 0u;
}

void telemetry_stop_all(void)
{
    for (uint8_t i = 0u; i < STREAM_COUNT; i++) {
        (void)telemetry_set_rate(i, 0u);
    }
    tlm_rings_reset();
}

static bool due(uint8_t stream, uint32_t now)
{
    if (s_rate[stream] == 0u) {
        return false;
    }
    uint32_t period = 1000u / s_rate[stream];
    if (s_next_ms[stream] == 0u || (int32_t)(now - s_next_ms[stream]) >= 0) {
        s_next_ms[stream] = now + period;
        return true;
    }
    return false;
}

void telemetry_task(uint32_t now_ms)
{
    tlm_rate_t r;
    while (tlm_pop_rate(&r)) {
        (void)comms_send(MSG_TLM_RATE, &r, sizeof(r));
    }
    tlm_imu_t im;
    while (tlm_pop_imu(&im)) {
        (void)comms_send(MSG_TLM_IMU, &im, sizeof(im));
    }
    if (due(STREAM_STATUS, now_ms)) {
        tlm_status_t st;
        svc_status(&st);
        (void)comms_send(MSG_TLM_STATUS, &st, sizeof(st));
    }
    if (due(STREAM_ALT, now_ms)) {
        tlm_alt_t a;
        svc_alt(&a);
        (void)comms_send(MSG_TLM_ALT, &a, sizeof(a));
    }
}
