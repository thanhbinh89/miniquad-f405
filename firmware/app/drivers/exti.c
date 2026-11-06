/**
 * @file    exti.c
 * @brief   EXTI configuration and the shared interrupt handlers.
 */
#include "exti.h"

#include "board.h"
#include "icm42688.h"
#include "ppm_input.h"
#include "vl53l1.h"
#include "vectors.h"

void exti_config(GPIO_TypeDef *port, uint32_t pin, exti_edge_t edge)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    uint32_t port_idx = ((uint32_t)port - (uint32_t)GPIOA_BASE) / 0x400u;
    uint32_t reg = pin >> 2;
    uint32_t shift = (pin & 3u) * 4u;
    SYSCFG->EXTICR[reg] = (SYSCFG->EXTICR[reg] & ~(0xFu << shift)) | (port_idx << shift);

    uint32_t m = 1u << pin;
    EXTI->IMR &= ~m;
    EXTI->RTSR = ((edge & EXTI_EDGE_RISING) != 0) ? (EXTI->RTSR | m) : (EXTI->RTSR & ~m);
    EXTI->FTSR = ((edge & EXTI_EDGE_FALLING) != 0) ? (EXTI->FTSR | m) : (EXTI->FTSR & ~m);
    EXTI->PR = m;
}

void exti_enable(uint32_t pin, bool enable)
{
    uint32_t m = 1u << pin;
    if (enable) {
        EXTI->PR = m;
        EXTI->IMR |= m;
    } else {
        EXTI->IMR &= ~m;
    }
}

void EXTI3_IRQHandler(void)
{
    if ((EXTI->PR & (1u << IMU_INT_PIN)) != 0u) {
        EXTI->PR = 1u << IMU_INT_PIN;
        icm42688_drdy_isr();
    }
}

void EXTI15_10_IRQHandler(void)
{
    uint32_t t = TIMEBASE_TIM->CNT;         /* time stamp first: PPM jitter */
    uint32_t pr = EXTI->PR;
    if ((pr & (1u << PPM_PIN)) != 0u) {
        EXTI->PR = 1u << PPM_PIN;
        ppm_input_edge_isr(t);
    }
    if ((pr & (1u << TOF_GPIO1_PIN)) != 0u) {
        EXTI->PR = 1u << TOF_GPIO1_PIN;
        vl53l1_gpio_isr();
    }
}
