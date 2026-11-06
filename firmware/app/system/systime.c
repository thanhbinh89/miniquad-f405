/**
 * @file    systime.c
 * @brief   TIM2 microsecond counter + SysTick millisecond tick.
 */
#include "systime.h"
#include "vectors.h"

static volatile uint32_t s_ms;

void time_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIMEBASE_TIM->CR1 = 0u;
    TIMEBASE_TIM->PSC = (TIMCLK1_HZ / 1000000u) - 1u;      /* 1 MHz */
    TIMEBASE_TIM->ARR = 0xFFFFFFFFu;
    TIMEBASE_TIM->CNT = 0u;
    TIMEBASE_TIM->EGR = TIM_EGR_UG;                         /* load prescaler */
    TIMEBASE_TIM->CR1 = TIM_CR1_CEN;

    SysTick_Config(SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_SYSTICK);
}

uint32_t time_ms(void)
{
    return s_ms;
}

void delay_us(uint32_t us)
{
    uint32_t t0 = time_us();
    while ((time_us() - t0) < us) {
    }
}

void delay_ms(uint32_t ms)
{
    while (ms-- > 0u) {
        delay_us(1000u);
    }
}

__attribute__((weak)) void time_tick_hook(void)
{
}

void SysTick_Handler(void)
{
    s_ms++;
    time_tick_hook();
}
