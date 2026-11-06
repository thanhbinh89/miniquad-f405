/**
 * @file    motor_pwm.c
 * @brief   TIM3 motor PWM.
 */
#include "motor_pwm.h"

#include "board.h"
#include "gpio.h"

#define PWM_PERIOD  (MOTOR_TIM_CLK_HZ / MOTOR_PWM_HZ)     /* 2625 */

void motor_pwm_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;

    TIM_TypeDef *t = MOTOR_TIM;
    t->CR1 = 0u;
    t->PSC = 0u;
    t->ARR = PWM_PERIOD - 1u;
    t->CCR1 = 0u;
    t->CCR2 = 0u;
    t->CCR3 = 0u;
    t->CCR4 = 0u;
    /* PWM mode 1 + preload on all channels */
    t->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE | (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    t->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE | (6u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    t->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    t->EGR = TIM_EGR_UG;
    t->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;

    /* pins only after the timer drives 0 % */
    gpio_clock_enable(MOTOR1_PORT);
    gpio_alternate(MOTOR1_PORT, MOTOR1_PIN, MOTOR_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_MEDIUM, GPIO_PULL_DOWN);
    gpio_alternate(MOTOR2_PORT, MOTOR2_PIN, MOTOR_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_MEDIUM, GPIO_PULL_DOWN);
    gpio_alternate(MOTOR3_PORT, MOTOR3_PIN, MOTOR_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_MEDIUM, GPIO_PULL_DOWN);
    gpio_alternate(MOTOR4_PORT, MOTOR4_PIN, MOTOR_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_MEDIUM, GPIO_PULL_DOWN);
}

static uint32_t to_ccr(float d)
{
    if (!(d > 0.0f)) {
        return 0u;                      /* also catches NaN */
    }
    if (d >= 1.0f) {
        return PWM_PERIOD;              /* CCR > ARR = 100 % */
    }
    return (uint32_t)(d * (float)PWM_PERIOD + 0.5f);
}

void motor_pwm_write(const float duty[4])
{
    TIM_TypeDef *t = MOTOR_TIM;
    t->CCR1 = to_ccr(duty[0]);
    t->CCR2 = to_ccr(duty[1]);
    t->CCR3 = to_ccr(duty[2]);
    t->CCR4 = to_ccr(duty[3]);
}

void motor_pwm_stop(void)
{
    TIM_TypeDef *t = MOTOR_TIM;
    t->CCR1 = 0u;
    t->CCR2 = 0u;
    t->CCR3 = 0u;
    t->CCR4 = 0u;
    t->EGR = TIM_EGR_UG;                /* apply now, not at the next period */
}
