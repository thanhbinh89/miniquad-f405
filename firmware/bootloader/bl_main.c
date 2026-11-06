/**
 * @file    bl_main.c
 * @brief   MiniQuad-F405 serial bootloader (USART1 on J2, 460800 8N1).
 *
 * Boot flow:
 *   reset -> startup_early_hook(): a BOOT_APP token left in the shared RAM
 *            mailbox means "the previous run validated the app" -> jump to it
 *            on a pristine MCU (no peripheral touched yet).
 *         -> main(): stay resident if the app asked for it (firmware update),
 *            if the image is invalid, or if the reset button was pressed
 *            twice within the boot window. Otherwise listen 300 ms for a
 *            HELLO from the configurator, then reset into the application.
 *
 * LED: on during the boot window, fast blink while resident.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bl_core.h"
#include "board.h"
#include "clock.h"
#include "flash_if.h"
#include "fw_version.h"
#include "gpio.h"
#include "link.h"
#include "memory_map.h"
#include "shared_ram.h"
#include "uart_link.h"
#include "vectors.h"

#define BL_BOOT_WINDOW_MS   300u
#define BL_IDLE_BOOT_MS     30000u      /* resident on request + no traffic -> boot app */

static volatile uint32_t s_ms;

void SysTick_Handler(void)
{
    s_ms++;
}

/* ------------------------------------------------------------------------ */
/* Image jumps (only from a pristine reset state)                           */
/* ------------------------------------------------------------------------ */
__attribute__((noreturn)) static void jump_to(uint32_t vectors)
{
    uint32_t sp = ((const volatile uint32_t *)vectors)[0];
    uint32_t pc = ((const volatile uint32_t *)vectors)[1];

    SCB->VTOR = vectors;
    __DSB();
    __ISB();
    __set_MSP(sp);
    ((void (*)(void))pc)();
    for (;;) {
    }
}

void startup_early_hook(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    if (sh->magic != SHARED_MAGIC) {
        return;
    }
    if (sh->boot_token == BL_TOKEN_BOOT_APP) {
        sh->boot_token = 0u;
        jump_to(MM_APP_VECTORS);
    }
    if (sh->request == BL_REQ_ROM) {
        sh->request = BL_REQ_NONE;
        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        (void)RCC->APB2ENR;
        SYSCFG->MEMRMP = SYSCFG_MEMRMP_MEM_MODE_0;      /* system flash at 0x0 */
        jump_to(MM_SYSMEM_BASE);
    }
}

/* ------------------------------------------------------------------------ */
/* Flash backend                                                            */
/* ------------------------------------------------------------------------ */
static uint32_t target_erase(uint32_t size)
{
    uint32_t last = flash_sector_of((uint32_t)MM_APP_BASE + size - 1u);
    if (last >= FLASH_SECTOR_COUNT) {
        return 0u;
    }
    for (uint32_t s = MM_APP_FIRST_SECTOR; s <= last; s++) {
        uint32_t base = flash_sector_base(s);
        uint32_t len = flash_sector_size(s);
        if (!flash_is_erased(base, len) && flash_erase_sector(s) != FLASH_OK) {
            return 0u;
        }
    }
    return flash_sector_base(last) + flash_sector_size(last) - (uint32_t)MM_APP_BASE;
}

static bool target_program(uint32_t offset, const uint32_t *words, uint32_t len)
{
    return flash_program((uint32_t)MM_APP_BASE + offset, words, len / 4u) == FLASH_OK;
}

static const bl_flash_ops_t s_flash = {
    .erase = target_erase,
    .program = target_program,
    .region = (const uint8_t *)MM_APP_BASE,
    .region_size = MM_APP_MAX_SIZE,
};

/* ------------------------------------------------------------------------ */
static void led_set(bool on)
{
    gpio_write(LED_PORT, LED_PIN, LED_ACTIVE_LOW ? !on : on);
}

static void tx_flush(uint32_t timeout_ms)
{
    uint32_t t0 = s_ms;
    while (!uart_link_tx_idle() && (s_ms - t0) < timeout_ms) {
    }
}

__attribute__((noreturn)) static void reset_into_app(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    sh->dbl_token = 0u;
    sh->boot_token = BL_TOKEN_BOOT_APP;
    sh->boot_flags = BOOT_F_FROM_BL | ((clock_source() == CLOCK_SRC_HSI) ? BOOT_F_HSE_FAIL_BL : 0u);
    __DSB();
    NVIC_SystemReset();
}

__attribute__((noreturn)) static void reset_with_request(uint32_t request)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    sh->dbl_token = 0u;
    sh->request = request;
    __DSB();
    NVIC_SystemReset();
}

static uint8_t       s_frame[LINK_FRAME_MAX];
static link_parser_t s_parser;
static link_packet_t s_req;
static link_packet_t s_rsp;
static bl_core_t     s_bl;

int main(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    if (sh->magic != SHARED_MAGIC) {
        memset((void *)sh, 0, sizeof(*sh));
        sh->magic = SHARED_MAGIC;
    }
    sh->reset_csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    sh->boot_count++;
    sh->bl_version = FW_VERSION;
    sh->boot_flags = 0u;

    bool stay = false;
    bool stay_on_request = false;
    if (sh->request == BL_REQ_STAY) {
        sh->request = BL_REQ_NONE;
        stay = true;
        stay_on_request = true;
    }
    if (sh->dbl_token == BL_TOKEN_DBL_RESET) {      /* reset pressed during last window */
        sh->dbl_token = 0u;
        stay = true;
    }

    (void)clock_init(false);
    SysTick_Config(SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_SYSTICK);

    gpio_clock_enable(LED_PORT);
    gpio_output(LED_PORT, LED_PIN, GPIO_OTYPE_PP, GPIO_SPEED_LOW, LED_ACTIVE_LOW);   /* off */

    uart_link_init(LINK_BAUD);
    link_parser_init(&s_parser);

    const volatile uint32_t *uid = (const volatile uint32_t *)MM_UID_BASE;
    bl_identity_t id = {
        .uid = { uid[0], uid[1], uid[2] },
        .bl_version = FW_VERSION,
        .build_time = FW_BUILD_TIME,
        .git_hash = FW_GIT_HASH,
    };
    bl_core_init(&s_bl, &s_flash, &id);

    if (!bl_core_app_valid(&s_bl, NULL)) {
        stay = true;
    }
    if (!stay) {
        sh->dbl_token = BL_TOKEN_DBL_RESET;     /* armed for the boot window */
    }

    uint32_t t_start = s_ms;
    uint32_t t_last_rx = s_ms;
    bool host_seen = false;

    for (;;) {
        uint8_t rx[64];
        size_t n = uart_link_read(rx, sizeof(rx));
        for (size_t i = 0u; i < n; i++) {
            if (!link_parser_feed(&s_parser, rx[i], &s_req)) {
                continue;
            }
            t_last_rx = s_ms;
            host_seen = true;
            if (!stay) {
                stay = true;                    /* configurator caught the window */
                sh->dbl_token = 0u;
            }
            if (bl_core_handle(&s_bl, &s_req, &s_rsp)) {
                size_t len = link_encode(s_rsp.id, s_rsp.seq, s_rsp.payload, s_rsp.len, s_frame);
                while (uart_link_write(s_frame, len) == 0u) {
                }
            }
        }

        if (s_bl.boot_request) {
            tx_flush(50u);
            reset_into_app();
        }
        if (s_bl.reboot_request) {
            tx_flush(50u);
            switch (s_bl.reboot_target) {
            case REBOOT_BOOTLOADER: reset_with_request(BL_REQ_STAY);
            case REBOOT_ROM:        reset_with_request(BL_REQ_ROM);
            default:
                if (bl_core_app_valid(&s_bl, NULL)) {
                    reset_into_app();
                }
                reset_with_request(BL_REQ_NONE);
            }
        }

        uint32_t now = s_ms;
        if (!stay) {
            led_set(true);
            if ((now - t_start) >= BL_BOOT_WINDOW_MS) {
                reset_into_app();
            }
        } else {
            led_set(((now / 60u) & 1u) != 0u);
            /* resident because the app asked, but nobody talks to us: go back */
            if (stay_on_request && !host_seen && (now - t_last_rx) >= BL_IDLE_BOOT_MS &&
                bl_core_app_valid(&s_bl, NULL)) {
                reset_into_app();
            }
        }
    }
}
