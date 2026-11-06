/**
 * @file    exti.h
 * @brief   External interrupt lines and their dispatch.
 *
 *   EXTI3   PA3  ICM-42688-P INT1 (data ready)   -> icm42688_drdy_isr()
 *   EXTI11  PC11 PPM input                       -> ppm_input_edge_isr()
 *   EXTI13  PB13 VL53L1 GPIO1 (range ready)      -> vl53l1_gpio_isr()
 */
#ifndef EXTI_H
#define EXTI_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"

typedef enum {
    EXTI_EDGE_RISING  = 1,
    EXTI_EDGE_FALLING = 2,
    EXTI_EDGE_BOTH    = 3,
} exti_edge_t;

/** Route @p port/@p pin to its EXTI line and select the edge (line masked). */
void exti_config(GPIO_TypeDef *port, uint32_t pin, exti_edge_t edge);
void exti_enable(uint32_t pin, bool enable);

#endif /* EXTI_H */
