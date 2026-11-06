/**
 * @file    ppm_input.c
 * @brief   PPM input glue (EXTI11 + TIM2) around ppm_decoder.
 */
#include "ppm_input.h"

#include "board.h"
#include "exti.h"
#include "gpio.h"
#include "ppm_decoder.h"
#include "systime.h"

#define STORM_EDGES_PER_MS  10u     /* > 10 edges in 1 ms is not PPM */
#define STORM_MASK_MS       200u

static ppm_decoder_t     s_dec;
static volatile uint32_t s_edges_window;
static volatile uint32_t s_window_start_us;
static volatile bool     s_masked;
static uint32_t          s_mask_ms;
static bool              s_falling;

void ppm_input_init(bool falling_edge)
{
    ppm_decoder_init(&s_dec);
    s_falling = falling_edge;
    gpio_clock_enable(PPM_PORT);
    gpio_input(PPM_PORT, PPM_PIN, GPIO_PULL_UP);
    exti_config(PPM_PORT, PPM_PIN, falling_edge ? EXTI_EDGE_FALLING : EXTI_EDGE_RISING);
    NVIC_SetPriority(PPM_EXTI_IRQn, IRQ_PRIO_PPM);
    NVIC_EnableIRQ(PPM_EXTI_IRQn);
    exti_enable(PPM_PIN, true);
}

void ppm_input_edge_isr(uint32_t t_us)
{
    if ((t_us - s_window_start_us) >= 1000u) {
        s_window_start_us = t_us;
        s_edges_window = 0u;
    }
    if (++s_edges_window > STORM_EDGES_PER_MS) {
        exti_enable(PPM_PIN, false);
        s_masked = true;
        return;
    }
    ppm_decoder_edge(&s_dec, t_us);
}

void ppm_input_task(uint32_t now_ms, bool falling_edge)
{
    if (falling_edge != s_falling) {
        s_falling = falling_edge;
        exti_config(PPM_PORT, PPM_PIN, falling_edge ? EXTI_EDGE_FALLING : EXTI_EDGE_RISING);
        exti_enable(PPM_PIN, true);
    }
    if (s_masked) {
        if (s_mask_ms == 0u) {
            s_mask_ms = now_ms;
        } else if ((now_ms - s_mask_ms) >= STORM_MASK_MS) {
            s_mask_ms = 0u;
            s_masked = false;
            s_edges_window = 0u;
            exti_enable(PPM_PIN, true);
        }
    }
}

void ppm_input_get(rc_frame_t *out)
{
    ppm_decoder_get(&s_dec, out, time_us());
}

uint32_t ppm_input_glitches(void)
{
    return s_dec.glitches;
}
