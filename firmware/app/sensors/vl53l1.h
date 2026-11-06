/**
 * @file    vl53l1.h
 * @brief   ST VL53L1CB / VL53L1X time-of-flight ranging, non-blocking.
 *
 * Register sequence of ST's VL53L1X Ultra Lite Driver (STSW-IMG009, BSD-3);
 * ST confirms the L1CB gives the same performance as the L1X with the ULD.
 * Implemented as a state machine on top of the asynchronous I2C2 driver,
 * advanced from the main loop: no blocking, no busy waits.
 *
 * Board specifics: I/O and GPIO1 pull-ups are at 2V8 (= AVDD), GPIO1 is
 * configured active low and wired to PB13 (EXTI13, falling edge); XSHUT is
 * driven open-drain only (2V8 domain).
 */
#ifndef VL53L1_H
#define VL53L1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t seq;           /* increments with every new measurement */
    uint32_t t_ms;          /* time the result was read */
    uint16_t dist_mm;
    uint8_t  status;        /* ULD range status: 0 valid, 1 sigma, 2 signal, 4 out of bounds, 7 wrap ... */
    uint8_t  spads;
    uint16_t signal_kcps;
    uint16_t ambient_kcps;
} vl53l1_sample_t;

typedef enum {
    VL53L1_DIST_SHORT = 1,  /* up to ~1.3 m, better in sunlight */
    VL53L1_DIST_LONG  = 2,  /* up to ~4 m indoors */
} vl53l1_dist_mode_t;

/** Configure GPIO/EXTI and start the initialisation sequence. */
void     vl53l1_init(vl53l1_dist_mode_t mode, uint16_t timing_budget_ms, int16_t offset_mm);
/** Advance the state machine. Call every main-loop iteration. */
void     vl53l1_task(uint32_t now_ms);
bool     vl53l1_get(vl53l1_sample_t *out);
bool     vl53l1_healthy(void);
uint16_t vl53l1_model_id(void);
uint32_t vl53l1_errors(void);
uint8_t  vl53l1_rate_hz(void);
/** Timing budget actually configured (nearest supported value). */
uint16_t vl53l1_timing_budget(void);

/** Map a raw RESULT__RANGE_STATUS to the ULD status code (pure helper). */
uint8_t  vl53l1_map_status(uint8_t raw);
/** Index of the timing budget entry used for @p ms (pure helper). */
uint16_t vl53l1_pick_budget(vl53l1_dist_mode_t mode, uint16_t ms, uint16_t *macrop_a, uint16_t *macrop_b);

/* interrupt entry point (GPIO1 falling edge) */
void     vl53l1_gpio_isr(void);

#endif /* VL53L1_H */
