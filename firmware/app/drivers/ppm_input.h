/**
 * @file    ppm_input.h
 * @brief   PPM receiver input on PC11 (J3 pin 4): EXTI11 edge interrupt
 *          time-stamped with the 1 MHz TIM2 counter.
 *
 * PC10/PC11 have no timer channel on the F405, so hardware input capture
 * is not possible on J3. The EXTI handler takes the time stamp as its first
 * instruction and runs at the highest application priority (jitter < 1 us).
 * An edge storm (floating or noisy input) masks the line for 200 ms.
 */
#ifndef PPM_INPUT_H
#define PPM_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "rc.h"

void     ppm_input_init(bool falling_edge);
/** Re-enable after a storm, update edge polarity. Call from the main loop. */
void     ppm_input_task(uint32_t now_ms, bool falling_edge);
void     ppm_input_get(rc_frame_t *out);
uint32_t ppm_input_glitches(void);

/* interrupt entry point */
void     ppm_input_edge_isr(uint32_t t_us);

#endif /* PPM_INPUT_H */
