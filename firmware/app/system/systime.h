/**
 * @file    systime.h
 * @brief   Time base: TIM2 free-running 32-bit microsecond counter and a
 *          1 kHz SysTick millisecond counter.
 *
 * time_us() wraps after ~71 minutes; always compare with unsigned
 * subtraction: (time_us() - t0) >= dt.
 */
#ifndef APP_SYSTIME_H
#define APP_SYSTIME_H

#include <stdint.h>
#include "board.h"

void time_init(void);

static inline uint32_t time_us(void)
{
    return TIMEBASE_TIM->CNT;
}

uint32_t time_ms(void);
void     delay_us(uint32_t us);
void     delay_ms(uint32_t ms);

/** Called from the SysTick interrupt every millisecond (weak, override). */
void     time_tick_hook(void);

#endif /* APP_SYSTIME_H */
