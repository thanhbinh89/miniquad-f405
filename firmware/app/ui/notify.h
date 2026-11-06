/**
 * @file    notify.h
 * @brief   Status LED patterns and buzzer tones / alarms.
 *
 * One-shot tones (events) have priority over repeating alarms (states).
 * Alarms, highest first: failsafe, critical battery, crash locator,
 * lost-model beeper (RC switch), low battery. Each category can be muted
 * with the beep_mask parameter (bit = NOTIFY_MASK_*).
 *
 * LED (50 ms steps, 1.6 s cycle), highest priority first:
 *   IMU failure        slow 0.8 s on/off
 *   failsafe           fast 5 Hz
 *   critical battery   2.5 Hz
 *   calibrating        10 Hz flicker
 *   motor test         alternating
 *   armed              solid (short gap every 1.6 s on low battery)
 *   disarmed, ready    single blink
 *   disarmed, blocked  double blink
 */
#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdint.h>
#include "fc.h"
#include "fc_events.h"

#define NOTIFY_MASK_STARTUP     (1u << 0)
#define NOTIFY_MASK_ARMING      (1u << 1)
#define NOTIFY_MASK_FAILSAFE    (1u << 2)
#define NOTIFY_MASK_BATTERY     (1u << 3)
#define NOTIFY_MASK_CAL         (1u << 4)
#define NOTIFY_MASK_BEEPER      (1u << 5)
#define NOTIFY_MASK_ERROR       (1u << 6)
#define NOTIFY_MASK_MODE        (1u << 7)
#define NOTIFY_MASK_CRASH       (1u << 8)

typedef enum {
    TONE_NONE = 0,
    TONE_STARTUP,
    TONE_ARM,
    TONE_DISARM,
    TONE_CAL_OK,
    TONE_CAL_FAIL,
    TONE_ERROR,
    TONE_MODE,
    TONE_SAVE_OK,
    TONE_TEST,
    TONE_COUNT
} notify_tone_t;

void notify_init(void);
void notify_play(notify_tone_t tone);
void notify_event(fc_event_t ev);
/** 100 Hz task: drives the LED and the buzzer. */
void notify_update(const fc_status_t *st, uint32_t now_ms, bool beeper_switch);

#endif /* NOTIFY_H */
