/**
 * @file    clock.h
 * @brief   System clock tree: 168 MHz from HSE 8 MHz, HSI 16 MHz fallback.
 *
 * SYSCLK 168 MHz, AHB 168 MHz, APB1 42 MHz (timers 84 MHz),
 * APB2 84 MHz (timers 168 MHz), PLL48 48 MHz. Flash 5 wait states,
 * prefetch, I- and D-cache enabled.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CLOCK_SRC_HSE = 0,
    CLOCK_SRC_HSI = 1,
} clock_src_t;

/**
 * Configure the clock tree. Tries the 8 MHz crystal first and falls back to
 * the internal 16 MHz oscillator if it does not start. With @p enable_css the
 * clock security system is armed (HSE failure -> NMI -> clock_on_css()).
 */
clock_src_t clock_init(bool enable_css);

/** Source the PLL currently runs from. */
clock_src_t clock_source(void);

/**
 * To be called from the NMI handler when the clock security system fired:
 * clears the CSS flag and restarts the PLL from HSI at the same frequency.
 */
void clock_on_css(void);

/** Busy-wait using the DWT cycle counter (works before SysTick). */
void clock_delay_us(uint32_t us);

#endif /* CLOCK_H */
