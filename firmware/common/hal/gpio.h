/**
 * @file    gpio.h
 * @brief   Minimal register-level GPIO helpers (bare metal, header only).
 */
#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"

typedef enum {
    GPIO_MODE_INPUT  = 0u,
    GPIO_MODE_OUTPUT = 1u,
    GPIO_MODE_AF     = 2u,
    GPIO_MODE_ANALOG = 3u,
} gpio_mode_t;

typedef enum {
    GPIO_PULL_NONE = 0u,
    GPIO_PULL_UP   = 1u,
    GPIO_PULL_DOWN = 2u,
} gpio_pull_t;

typedef enum {
    GPIO_SPEED_LOW    = 0u,
    GPIO_SPEED_MEDIUM = 1u,
    GPIO_SPEED_FAST   = 2u,
    GPIO_SPEED_HIGH   = 3u,
} gpio_speed_t;

typedef enum {
    GPIO_OTYPE_PP = 0u,
    GPIO_OTYPE_OD = 1u,
} gpio_otype_t;

/** Enable the AHB1 clock of a GPIO port. */
static inline void gpio_clock_enable(GPIO_TypeDef *port)
{
    uint32_t idx = ((uint32_t)port - (uint32_t)GPIOA_BASE) / 0x400u;
    RCC->AHB1ENR |= (1u << idx);
    (void)RCC->AHB1ENR;                 /* read back: clock settled */
}

static inline void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, gpio_mode_t mode)
{
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | ((uint32_t)mode << (pin * 2u));
}

static inline void gpio_set_pull(GPIO_TypeDef *port, uint32_t pin, gpio_pull_t pull)
{
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | ((uint32_t)pull << (pin * 2u));
}

static inline void gpio_set_speed(GPIO_TypeDef *port, uint32_t pin, gpio_speed_t speed)
{
    port->OSPEEDR = (port->OSPEEDR & ~(3u << (pin * 2u))) | ((uint32_t)speed << (pin * 2u));
}

static inline void gpio_set_otype(GPIO_TypeDef *port, uint32_t pin, gpio_otype_t otype)
{
    port->OTYPER = (port->OTYPER & ~(1u << pin)) | ((uint32_t)otype << pin);
}

static inline void gpio_set_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    volatile uint32_t *afr = &port->AFR[pin >> 3];
    uint32_t shift = (pin & 7u) * 4u;
    *afr = (*afr & ~(0xFu << shift)) | ((af & 0xFu) << shift);
}

static inline void gpio_write(GPIO_TypeDef *port, uint32_t pin, bool high)
{
    port->BSRR = high ? (1u << pin) : (1u << (pin + 16u));
}

static inline bool gpio_read(const GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR & (1u << pin)) != 0u;
}

/** Configure a pin as push-pull/open-drain output with an initial level. */
static inline void gpio_output(GPIO_TypeDef *port, uint32_t pin, gpio_otype_t otype,
                               gpio_speed_t speed, bool initial)
{
    gpio_write(port, pin, initial);     /* set level before enabling the driver */
    gpio_set_otype(port, pin, otype);
    gpio_set_speed(port, pin, speed);
    gpio_set_pull(port, pin, GPIO_PULL_NONE);
    gpio_set_mode(port, pin, GPIO_MODE_OUTPUT);
}

/** Configure a pin for an alternate function. */
static inline void gpio_alternate(GPIO_TypeDef *port, uint32_t pin, uint32_t af,
                                  gpio_otype_t otype, gpio_speed_t speed, gpio_pull_t pull)
{
    gpio_set_af(port, pin, af);
    gpio_set_otype(port, pin, otype);
    gpio_set_speed(port, pin, speed);
    gpio_set_pull(port, pin, pull);
    gpio_set_mode(port, pin, GPIO_MODE_AF);
}

static inline void gpio_input(GPIO_TypeDef *port, uint32_t pin, gpio_pull_t pull)
{
    gpio_set_pull(port, pin, pull);
    gpio_set_mode(port, pin, GPIO_MODE_INPUT);
}

#endif /* GPIO_H */
