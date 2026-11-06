/**
 * @file    clock.c
 * @brief   Clock tree setup for STM32F405 at 168 MHz.
 */
#include "clock.h"

#include "board.h"
#include "stm32f4xx.h"

uint32_t SystemCoreClock = 16000000u;      /* CMSIS, HSI after reset */

static clock_src_t s_src = CLOCK_SRC_HSI;

#define PLL_N       336u
#define PLL_P_DIV2  0u      /* PLLP field 00 -> /2 */
#define PLL_Q       7u

static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void clock_delay_us(uint32_t us)
{
    uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    uint32_t start = DWT->CYCCNT;
    uint32_t wait = us * cycles_per_us;
    while ((DWT->CYCCNT - start) < wait) {
    }
}

static bool wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t loops)
{
    while ((*reg & mask) == 0u) {
        if (loops-- == 0u) {
            return false;
        }
    }
    return true;
}

/* Start the PLL from the given source and switch SYSCLK to it. */
static bool pll_start(clock_src_t src)
{
    uint32_t pllm = (src == CLOCK_SRC_HSE) ? (BOARD_HSE_HZ / 1000000u) : 16u;

    /* keep reserved bits (31:28, 23, 21:18, 15) at their reset value */
    RCC->PLLCFGR = (RCC->PLLCFGR & 0xF0BC8000u)
                 | (pllm << RCC_PLLCFGR_PLLM_Pos)
                 | (PLL_N << RCC_PLLCFGR_PLLN_Pos)
                 | (PLL_P_DIV2 << RCC_PLLCFGR_PLLP_Pos)
                 | (PLL_Q << RCC_PLLCFGR_PLLQ_Pos)
                 | ((src == CLOCK_SRC_HSE) ? RCC_PLLCFGR_PLLSRC_HSE : RCC_PLLCFGR_PLLSRC_HSI);

    RCC->CR |= RCC_CR_PLLON;
    if (!wait_flag(&RCC->CR, RCC_CR_PLLRDY, 200000u)) {
        return false;
    }

    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    uint32_t loops = 200000u;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
        if (loops-- == 0u) {
            return false;
        }
    }
    return true;
}

static void switch_to_hsi(void)
{
    RCC->CR |= RCC_CR_HSION;
    (void)wait_flag(&RCC->CR, RCC_CR_HSIRDY, 100000u);
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
    }
    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
    }
}

clock_src_t clock_init(bool enable_css)
{
    /* Voltage scale 1 is required for 168 MHz. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_VOS;

    /* Run from HSI while (re)configuring. */
    switch_to_hsi();

    /* 5 wait states @ 168 MHz / 3.3 V, reset and enable caches + prefetch. */
    FLASH->ACR = FLASH_ACR_LATENCY_5WS;
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR |= FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_5WS) {
    }

    /* Try the crystal (typ. 2-10 ms, ~100 ms timeout at 16 MHz). */
    RCC->CR |= RCC_CR_HSEON;
    bool hse_ok = wait_flag(&RCC->CR, RCC_CR_HSERDY, 400000u);

    s_src = CLOCK_SRC_HSI;
    if (hse_ok && pll_start(CLOCK_SRC_HSE)) {
        s_src = CLOCK_SRC_HSE;
        if (enable_css) {
            RCC->CR |= RCC_CR_CSSON;
        }
    } else {
        switch_to_hsi();
        RCC->CR &= ~RCC_CR_HSEON;
        (void)pll_start(CLOCK_SRC_HSI);
    }

    SystemCoreClock = SYSCLK_HZ;
    dwt_enable();
    return s_src;
}

clock_src_t clock_source(void)
{
    return s_src;
}

void clock_on_css(void)
{
    /* Hardware already switched SYSCLK to HSI and stopped the PLL. */
    RCC->CIR = RCC_CIR_CSSC;
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON);
    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
    }
    s_src = CLOCK_SRC_HSI;
    (void)pll_start(CLOCK_SRC_HSI);
    SystemCoreClock = SYSCLK_HZ;
}
