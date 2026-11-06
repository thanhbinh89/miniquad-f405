/**
 * @file    tlm_format.h
 * @brief   Conversion of the flight-core snapshot into the packed telemetry
 *          records of proto.h (scaling, saturation, flag merging).
 *
 * Hardware independent: used by the target services (app_services.c) and
 * by the device emulator (tests/emu), so both report identical units.
 */
#ifndef TLM_FORMAT_H
#define TLM_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include "fc.h"
#include "proto.h"
#include "rc.h"

/** Live state that is not part of the flight-core snapshot. */
typedef struct {
    uint32_t          time_ms;
    uint32_t          flags_set;        /* STF_* forced on                  */
    uint32_t          flags_clear;      /* STF_* forced off (applied first) */
    uint32_t          arm_block_extra;  /* ARMB_* added                     */
    uint8_t           bat_state;        /* BAT_*                            */
    float             v_comp, v_slow;   /* V                                */
    uint8_t           cpu_load;         /* %                                */
    uint16_t          loop_us_avg, loop_us_max, loop_hz;
    float             mcu_temp;         /* deg C                            */
    const rc_frame_t *rc;               /* newest PPM frame, may be NULL    */
    bool              tof_have;         /* a ToF sample exists              */
    uint16_t          tof_mm;
    uint8_t           tof_status;
    uint8_t           tof_rate_hz;
    uint32_t          imu_errors, tof_errors, rx_errors, link_drops;
} tlm_extra_t;

void tlm_format_status(const fc_status_t *st, const tlm_extra_t *x, tlm_status_t *out);
void tlm_format_alt(const fc_status_t *st, uint32_t time_us, tlm_alt_t *out);

/* saturating scalers (also used by the flight loop telemetry) */
int16_t  tlm_s16(float v, float scale);
uint16_t tlm_u16(float v, float scale);

#endif /* TLM_FORMAT_H */
