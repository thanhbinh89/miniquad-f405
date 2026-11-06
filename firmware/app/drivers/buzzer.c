/**
 * @file    buzzer.c
 * @brief   TIM12 tone generator.
 */
#include "buzzer.h"

#include "board.h"
#include "gpio.h"

#define BUZZER_TICK_HZ  10500000u       /* 84 MHz / 8 */

static uint32_t s_hz;

void buzzer_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM12EN;
    (void)RCC->APB1ENR;
    TIM_TypeDef *t = BUZZER_TIM;
    t->CR1 = 0u;
    t->PSC = (TIMCLK1_HZ / BUZZER_TICK_HZ) - 1u;
    t->ARR = 0xFFFFu;
    t->CCR1 = 0u;
    t->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    t->CCER = TIM_CCER_CC1E;
    t->EGR = TIM_EGR_UG;
    t->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    gpio_clock_enable(BUZZER_PORT);
    gpio_alternate(BUZZER_PORT, BUZZER_PIN, BUZZER_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_LOW, GPIO_PULL_DOWN);
    s_hz = 0u;
}

void buzzer_tone(uint32_t hz)
{
    if (hz == s_hz) {
        return;
    }
    s_hz = hz;
    TIM_TypeDef *t = BUZZER_TIM;
    if (hz < 200u || hz > 20000u) {
        t->CCR1 = 0u;
        t->EGR = TIM_EGR_UG;
        return;
    }
    uint32_t period = BUZZER_TICK_HZ / hz;
    t->ARR = period - 1u;
    t->CCR1 = period / 2u;
    t->EGR = TIM_EGR_UG;
}
