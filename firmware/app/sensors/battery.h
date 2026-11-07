/**
 * @file    battery.h
 * @brief   1S battery monitor: voltage from the ADC (VREFINT-corrected),
 *          two low-pass outputs and a debounced warning state.
 *
 *   v_comp : bat_lpf_hz (default 2 Hz) - follows load sag, used by the
 *            thrust compensation
 *   v_slow : 0.2 Hz - used for the low / critical warnings (2 s debounce,
 *            0.15 V hysteresis, 5 s to recover)
 *
 * The divider (R2/R3 100k/100k) sits after the reverse-polarity MOSFET, so
 * the measured voltage is what the motors see. Below 2.3 V the board is
 * assumed to run from another supply (no battery).
 *
 * USB power (board rev 2): VBUS feeds a second 3.3 V LDO in parallel with
 * the battery one, and the battery regulator can then pass current
 * backwards into the VBAT rail (body diode / enabled pass transistor), so an
 * unplugged battery may read 2.7..3.3 V. With usb_power set a battery only
 * counts as present above 3.45 V (absent again below 3.35 V): no false
 * "critical battery" alarm on the bench, motors stay blocked.
 */
#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>
#include "filters.h"

typedef struct {
    float   v_raw;
    float   v_comp;
    float   v_slow;
    float   vdda;
    float   mcu_temp;
    bool    present;
    bool    usb_power;      /* input: VBUS present (battery_set_usb_power) */
    uint8_t state;          /* BAT_* (proto.h) */
    /* internal */
    pt1_t   lpf_comp;
    pt1_t   lpf_slow;
    float   t_warn;
    float   t_crit;
    float   t_ok;
} battery_t;

typedef struct {
    uint16_t vrefint_cal;   /* factory VREFINT reading at VDDA = 3.3 V */
    uint16_t ts_cal1;       /* temperature sensor at 30 C  */
    uint16_t ts_cal2;       /* temperature sensor at 110 C */
} battery_cal_t;

void battery_init(battery_t *b);
/** Board powered by USB as well (VBUS present). */
void battery_set_usb_power(battery_t *b, bool usb_power);
/**
 * @param raw_vbat  averaged ADC counts of the divider
 * @param raw_vref  averaged ADC counts of VREFINT
 * @param raw_temp  averaged ADC counts of the temperature sensor
 */
void battery_update(battery_t *b, const battery_cal_t *cal, uint16_t raw_vbat, uint16_t raw_vref,
                    uint16_t raw_temp, float dt);

#endif /* BATTERY_H */
