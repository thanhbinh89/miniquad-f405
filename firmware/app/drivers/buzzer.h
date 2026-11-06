/**
 * @file    buzzer.h
 * @brief   Magnetic buzzer (MLT-5030, resonant at 4 kHz) on TIM12 CH1 (PB14),
 *          50 % square wave through the MMBT3904 driver.
 */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

void buzzer_init(void);
/** Start a tone (Hz); 0 = silence. */
void buzzer_tone(uint32_t hz);

#endif /* BUZZER_H */
