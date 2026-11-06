/**
 * @file    led.c
 * @brief   Status LED.
 */
#include "led.h"

#include "board.h"
#include "gpio.h"

void led_init(void)
{
    gpio_clock_enable(LED_PORT);
    gpio_output(LED_PORT, LED_PIN, GPIO_OTYPE_PP, GPIO_SPEED_LOW, LED_ACTIVE_LOW);    /* off */
}

void led_set(bool on)
{
    gpio_write(LED_PORT, LED_PIN, LED_ACTIVE_LOW ? !on : on);
}
