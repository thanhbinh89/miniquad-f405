/**
 * @file    bl_main.c
 * @brief   MiniQuad-F405 bootloader.
 *
 * Link to the configurator: USB CDC on the USB-C port (board rev 2, polled
 * from the main loop) or USART1 on J2 at 460800 8N1 (rev 1).
 *
 * Boot flow:
 *   reset -> startup_early_hook(): a BOOT_APP token left in the shared RAM
 *            mailbox means "the previous run validated the app": clear the
 *            reset flags and jump to it on a pristine MCU (no peripheral
 *            touched yet). A ROM request jumps to the STM32 system memory
 *            bootloader (USART1 / USB DFU).
 *         -> main(): bl_boot_decide() (bl_policy.h).
 *            Normal boot: arm the double-reset token, LED on for 300 ms, then
 *            reset into the application. Rev 1 also listens to the UART
 *            during that window (the configurator may catch it); rev 2 does
 *            not enumerate on USB at all in a normal boot - the host would
 *            only see a device appear and vanish.
 *            Resident (update requested, no valid app, reset pressed twice,
 *            crash loop): link up - on rev 2 the board enumerates as soon as
 *            VBUS is present - and serve the configurator. Resident on
 *            request without any host traffic for 30 s: back to the app.
 *
 * LED: on during the boot window; resident: fast blink while waiting for a
 * host, slow blink once a USB host has configured the port.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bl_core.h"
#include "bl_policy.h"
#include "board.h"
#include "clock.h"
#include "flash_if.h"
#include "fw_version.h"
#include "gpio.h"
#include "link.h"
#include "link_port.h"
#include "memory_map.h"
#include "shared_ram.h"
#include "vectors.h"

#define BL_BOOT_WINDOW_MS   300u
#define BL_IDLE_BOOT_MS     30000u      /* resident on request + no traffic -> boot app */
#define BL_DETACH_MS        20u         /* D+ released before a reset (rev 2)           */

static volatile uint32_t s_ms;
static bool s_link_up;

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

/* The STM32 ROM bootloader probes USART1_RX (PA10), USART3_RX (PC11, PB11),
 * CAN2_RX (PB5) and USB. AN2606: while it looks for a host over USB DFU, the
 * RX pins of the interfaces not used must sit at a fixed level - noise on a
 * floating one can make it lock onto a UART instead. PB11 has the I2C pull-up
 * and PB5 the motor gate pull-down; PA10 (not connected on rev 2, UART RX on
 * rev 1) and PC11 (J3.4 = PPM, floating without a receiver) get the internal
 * pull-ups here. Only helps when the ROM is entered through this path (not
 * with JP1/BOOT0), and a receiver sending PPM on J3.4 is still activity. */
#define ROM_USART1_RX_PIN       10u         /* PA10, fixed by the ROM */

static void rom_detect_pins_quiet(void)
{
    gpio_clock_enable(GPIOA);
    gpio_clock_enable(PPM_PORT);
    gpio_set_pull(GPIOA, ROM_USART1_RX_PIN, GPIO_PULL_UP);
    gpio_set_pull(PPM_PORT, PPM_PIN, GPIO_PULL_UP);     /* PC11 = ROM USART3_RX */
}

void startup_early_hook(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    if (sh->magic != SHARED_MAGIC) {
        return;
    }
    if (sh->boot_token == BL_TOKEN_BOOT_APP) {
        sh->boot_token = 0u;
        /* the next bootloader entry must see only the resets that happen
         * while the application runs (crash-loop detection) */
        RCC->CSR |= RCC_CSR_RMVF;
        jump_to(MM_APP_VECTORS);
    }
    if (sh->request == BL_REQ_ROM) {
        sh->request = BL_REQ_NONE;
        rom_detect_pins_quiet();
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

static void link_task(void)
{
    if (s_link_up) {
        link_port_task(s_ms);
    }
}

static void tx_flush(uint32_t timeout_ms)
{
    uint32_t t0 = s_ms;
    while (s_link_up && !link_port_tx_idle() && (s_ms - t0) < timeout_ms) {
        link_task();
    }
}

__attribute__((noreturn)) static void system_reset(void)
{
    if (s_link_up) {
        link_port_detach(BL_DETACH_MS);         /* host sees the device leave */
    }
    __DSB();
    NVIC_SystemReset();
}

__attribute__((noreturn)) static void reset_into_app(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    sh->dbl_token = 0u;
    sh->boot_token = BL_TOKEN_BOOT_APP;
    sh->app_starts++;                           /* the app clears it once it runs */
    sh->boot_flags = BOOT_F_FROM_BL | ((clock_source() == CLOCK_SRC_HSI) ? BOOT_F_HSE_FAIL_BL : 0u);
    system_reset();
}

__attribute__((noreturn)) static void reset_with_request(uint32_t request)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    sh->dbl_token = 0u;
    sh->request = request;
    system_reset();
}

static uint8_t       s_frame[LINK_FRAME_MAX];
static link_parser_t s_parser;
static link_packet_t s_req;
static link_packet_t s_rsp;
static bl_core_t     s_bl;

static void send_frame(size_t len)
{
    uint32_t t0 = s_ms;
    while (link_port_write(s_frame, len) == 0u) {
        link_task();
        if ((s_ms - t0) > 250u) {
            return;                             /* host stopped reading: drop the reply */
        }
    }
}

int main(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    if (sh->magic != SHARED_MAGIC) {
        memset((void *)sh, 0, sizeof(*sh));
        sh->magic = SHARED_MAGIC;
    }
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    sh->reset_csr = csr;
    sh->boot_count++;
    sh->bl_version = FW_VERSION;
    sh->boot_flags = 0u;
    sh->app_starts = bl_app_starts_at_entry(sh->app_starts, csr);

    bl_boot_in_t in = {
        .request = sh->request,
        .dbl_reset = (sh->dbl_token == BL_TOKEN_DBL_RESET),     /* reset during last window */
        .app_valid = false,
        .app_starts = sh->app_starts,
    };
    sh->request = BL_REQ_NONE;
    sh->dbl_token = 0u;

    (void)clock_init(false);
    SysTick_Config(SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_SYSTICK);

    gpio_clock_enable(LED_PORT);
    gpio_output(LED_PORT, LED_PIN, GPIO_OTYPE_PP, GPIO_SPEED_LOW, LED_ACTIVE_LOW);   /* off */

    const volatile uint32_t *uid = (const volatile uint32_t *)MM_UID_BASE;
    bl_identity_t id = {
        .uid = { uid[0], uid[1], uid[2] },
        .bl_version = FW_VERSION,
        .build_time = FW_BUILD_TIME,
        .git_hash = FW_GIT_HASH,
    };
    bl_core_init(&s_bl, &s_flash, &id);
    in.app_valid = bl_core_app_valid(&s_bl, NULL);
    s_bl.boot_reason = bl_boot_decide(&in);
    bool resident = (s_bl.boot_reason != BLR_NONE);

    if (!resident) {
        sh->dbl_token = BL_TOKEN_DBL_RESET;     /* armed for the boot window */
#if BOARD_LINK_USB
        led_set(true);
        uint32_t t0 = s_ms;
        while ((s_ms - t0) < BL_BOOT_WINDOW_MS) {
        }
        reset_into_app();
#endif
    }

    link_port_init(LINK_PORT_BOOTLOADER);
    s_link_up = true;
    link_parser_init(&s_parser);

    uint32_t t_start = s_ms;
    uint32_t t_last_rx = s_ms;
    bool host_seen = false;

    for (;;) {
        link_task();
        uint8_t rx[64];
        size_t n = link_port_read(rx, sizeof(rx));
        for (size_t i = 0u; i < n; i++) {
            if (!link_parser_feed(&s_parser, rx[i], &s_req)) {
                continue;
            }
            t_last_rx = s_ms;
            host_seen = true;
            if (!resident) {                    /* rev 1: configurator caught the window */
                resident = true;
                sh->dbl_token = 0u;
                s_bl.boot_reason = BLR_HOST;
            }
            if (bl_core_handle(&s_bl, &s_req, &s_rsp)) {
                send_frame(link_encode(s_rsp.id, s_rsp.seq, s_rsp.payload, s_rsp.len, s_frame));
            }
        }

        if (s_bl.boot_request) {
            tx_flush(100u);
            sh->app_starts = 0u;                /* explicit user action, not a crash */
            reset_into_app();
        }
        if (s_bl.reboot_request) {
            tx_flush(100u);
            switch (s_bl.reboot_target) {
            case REBOOT_BOOTLOADER: reset_with_request(BL_REQ_STAY);
            case REBOOT_ROM:        reset_with_request(BL_REQ_ROM);
            default:
                if (bl_core_app_valid(&s_bl, NULL)) {
                    sh->app_starts = 0u;
                    reset_into_app();
                }
                reset_with_request(BL_REQ_NONE);
            }
        }

        uint32_t now = s_ms;
        if (!resident) {
            led_set(true);
            if ((now - t_start) >= BL_BOOT_WINDOW_MS) {
                reset_into_app();
            }
        } else {
            bool connected = BOARD_LINK_USB ? link_port_ready() : host_seen;
            uint32_t period = connected ? 250u : 60u;
            led_set(((now / period) & 1u) != 0u);
            /* resident because the app asked, but nobody talks to us: go back */
            if (s_bl.boot_reason == BLR_REQUEST && !host_seen && (now - t_last_rx) >= BL_IDLE_BOOT_MS &&
                bl_core_app_valid(&s_bl, NULL)) {
                reset_into_app();
            }
        }
    }
}
