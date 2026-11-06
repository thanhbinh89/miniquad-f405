/**
 * @file    watchdog.c
 * @brief   IWDG driver. Prescaler /64 -> 2 ms per count at 32 kHz LSI.
 */
#include "watchdog.h"

#include "stm32f4xx.h"

#define IWDG_KEY_ENABLE     0xCCCCu
#define IWDG_KEY_RELOAD     0xAAAAu
#define IWDG_KEY_ACCESS     0x5555u
#define IWDG_PR_DIV64       4u
#define IWDG_MS_PER_COUNT   2u

static uint32_t reload_for(uint32_t timeout_ms)
{
    uint32_t r = timeout_ms / IWDG_MS_PER_COUNT;
    if (r < 1u) {
        r = 1u;
    }
    if (r > 0xFFFu) {
        r = 0xFFFu;
    }
    return r;
}

static void load(uint32_t timeout_ms)
{
    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = IWDG_PR_DIV64;
    IWDG->RLR = reload_for(timeout_ms);
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u) {
    }
    IWDG->KR = IWDG_KEY_RELOAD;
}

void watchdog_init(uint32_t timeout_ms)
{
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    IWDG->KR = IWDG_KEY_ENABLE;         /* starts LSI + counter */
    load(timeout_ms);
}

void watchdog_kick(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

void watchdog_set_timeout(uint32_t timeout_ms)
{
    IWDG->KR = IWDG_KEY_RELOAD;
    load(timeout_ms);
}
