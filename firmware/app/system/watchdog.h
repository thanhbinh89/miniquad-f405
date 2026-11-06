/**
 * @file    watchdog.h
 * @brief   Independent watchdog (IWDG, LSI ~32 kHz). Frozen while the core
 *          is halted by a debugger.
 *
 * If the firmware hangs, the reset turns every motor pin into a floating
 * input -> gates pulled low by the 10k resistors -> motors off.
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

#define WATCHDOG_TIMEOUT_MS         250u
#define WATCHDOG_LONG_TIMEOUT_MS    4000u   /* flash erase in progress */

void watchdog_init(uint32_t timeout_ms);
void watchdog_kick(void);
/** Change the timeout of the running watchdog (e.g. before a flash erase). */
void watchdog_set_timeout(uint32_t timeout_ms);

#endif /* WATCHDOG_H */
