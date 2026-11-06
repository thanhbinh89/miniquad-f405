/**
 * @file    flight_task.h
 * @brief   Glue between the drivers and the flight core.
 *
 * The flight loop runs in FLIGHT_SWI_IRQHandler, pended by the IMU DMA
 * completion (1 kHz). It collects the latest RC frame, battery state and
 * ToF sample, runs fc_update() and writes the motor PWM.
 *
 * Supervision (SysTick, higher priority): if the loop does not run for
 * 20 ms the motors are stopped; if that happens while armed the vehicle is
 * disarmed as soon as the loop resumes (arm switch must be cycled).
 */
#ifndef FLIGHT_TASK_H
#define FLIGHT_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "battery.h"

typedef struct {
    uint16_t loop_us_avg;
    uint16_t loop_us_max;
    uint16_t loop_hz;
    uint32_t loops;
    bool     running;       /* loop executed during the last 20 ms */
    bool     emergency;     /* stall detected while armed */
} flight_stats_t;

void flight_task_init(void);
/** Battery state produced by the main loop, consumed by the flight loop. */
void flight_task_set_battery(const battery_t *b);
void flight_task_motor_test(const uint16_t duty[4], uint32_t until_ms);
void flight_task_motor_test_stop(void);
bool flight_task_motor_test_active(void);
void flight_task_request_cal(uint8_t what);
void flight_task_set_config_busy(bool busy);
void flight_task_stats(flight_stats_t *out);
/** Reset the max execution time statistic. */
void flight_task_stats_reset_max(void);

#endif /* FLIGHT_TASK_H */
