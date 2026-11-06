/**
 * @file    fc_events.h
 * @brief   Events from the flight loop (interrupt) to the main loop
 *          (notifications, log). Lock-free single-producer queue.
 */
#ifndef FC_EVENTS_H
#define FC_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FCEV_NONE = 0,
    FCEV_ARMED,
    FCEV_DISARMED,
    FCEV_ARM_BLOCKED,       /* arm switch moved while arming is not allowed */
    FCEV_FAILSAFE,
    FCEV_FS_RECOVERED,
    FCEV_FS_DISARM,
    FCEV_GYRO_CAL_DONE,
    FCEV_ACC_CAL_DONE,
    FCEV_ACC_CAL_FAILED,
    FCEV_CRASH,
    FCEV_MODE_ACRO,
    FCEV_MODE_ANGLE,
    FCEV_MODE_ALT,
    FCEV_ALT_UNAVAILABLE,   /* ALT selected but no valid height */
    FCEV_RX_LOST,
    FCEV_RX_OK,
    FCEV_EMERGENCY,         /* flight loop stall / forced disarm */
    FCEV_COUNT
} fc_event_t;

void fc_event_post(fc_event_t ev);
bool fc_event_pop(fc_event_t *ev);
const char *fc_event_name(fc_event_t ev);

#endif /* FC_EVENTS_H */
