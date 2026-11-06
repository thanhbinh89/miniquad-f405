/**
 * @file    fc_events.c
 * @brief   Flight loop -> main loop event queue.
 */
#include "fc_events.h"

#include <stddef.h>

#include "spsc.h"

SPSC_DEFINE(ev_ring, uint8_t, 32u)

static ev_ring_t s_ring;

void fc_event_post(fc_event_t ev)
{
    uint8_t v = (uint8_t)ev;
    (void)ev_ring_push(&s_ring, &v);
}

bool fc_event_pop(fc_event_t *ev)
{
    uint8_t v;
    if (!ev_ring_pop(&s_ring, &v)) {
        return false;
    }
    *ev = (fc_event_t)v;
    return true;
}

const char *fc_event_name(fc_event_t ev)
{
    static const char *const names[FCEV_COUNT] = {
        [FCEV_NONE] = "none",
        [FCEV_ARMED] = "armed",
        [FCEV_DISARMED] = "disarmed",
        [FCEV_ARM_BLOCKED] = "arming blocked",
        [FCEV_FAILSAFE] = "FAILSAFE: RC signal lost",
        [FCEV_FS_RECOVERED] = "failsafe: RC recovered",
        [FCEV_FS_DISARM] = "failsafe: disarmed",
        [FCEV_GYRO_CAL_DONE] = "gyro calibrated",
        [FCEV_ACC_CAL_DONE] = "accelerometer level calibrated",
        [FCEV_ACC_CAL_FAILED] = "accelerometer calibration failed (keep still and level)",
        [FCEV_CRASH] = "crash detected: disarmed",
        [FCEV_MODE_ACRO] = "mode ACRO",
        [FCEV_MODE_ANGLE] = "mode ANGLE",
        [FCEV_MODE_ALT] = "mode ALT HOLD",
        [FCEV_ALT_UNAVAILABLE] = "ALT HOLD unavailable (no valid ToF height)",
        [FCEV_RX_LOST] = "RC signal lost",
        [FCEV_RX_OK] = "RC signal OK",
        [FCEV_EMERGENCY] = "EMERGENCY STOP (flight loop stalled)",
    };
    return ((unsigned)ev < FCEV_COUNT && names[ev] != NULL) ? names[ev] : "?";
}
