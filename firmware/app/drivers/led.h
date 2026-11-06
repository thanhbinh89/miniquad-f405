/**
 * @file    led.h
 * @brief   Green status LED D2 on PC12 (active low). The red LED D1 is the
 *          3V3 power indicator and is not controllable.
 */
#ifndef LED_H
#define LED_H

#include <stdbool.h>

void led_init(void);
void led_set(bool on);

#endif /* LED_H */
