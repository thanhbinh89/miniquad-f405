/**
 * @file    shared_ram.h
 * @brief   No-init RAM mailbox shared by the bootloader and the application.
 *
 * Located in the last 64 bytes of SRAM (section ".shared_ram", NOLOAD in both
 * linker scripts). SRAM keeps its content across a system reset, so the
 * application can ask the bootloader to stay resident (firmware update) and
 * the bootloader can tell the application why the MCU was reset.
 */
#ifndef SHARED_RAM_H
#define SHARED_RAM_H

#include <stdint.h>
#include "memory_map.h"

#define SHARED_MAGIC            0x4D514252u  /* "RBQM" */

/* app -> bootloader requests (field 'request') */
#define BL_REQ_NONE             0x00000000u
#define BL_REQ_STAY             0xB007B007u  /* stay in bootloader (update)   */
#define BL_REQ_ROM              0x5157E000u  /* jump to STM32 ROM bootloader  */

/* bootloader internal tokens */
#define BL_TOKEN_BOOT_APP       0xA9900A99u  /* jump to app right after reset */
#define BL_TOKEN_DBL_RESET      0xD0B1E5E7u  /* double reset detection window */

/* Crash-loop detection (field 'app_starts'): the bootloader counts every
 * start of the application, the application sets the counter back to 0 once
 * it has run SHARED_APP_CONFIRM_MS, and before any deliberate reboot. */
#define SHARED_APP_CONFIRM_MS   5000u

/* bootloader -> app flags (field 'boot_flags') */
#define BOOT_F_FROM_BL          (1u << 0)    /* app was started by our bootloader */
#define BOOT_F_HSE_FAIL_BL      (1u << 1)    /* bootloader ran on HSI             */

typedef struct {
    uint32_t magic;         /* SHARED_MAGIC when the block is initialised   */
    uint32_t request;       /* BL_REQ_*                                     */
    uint32_t boot_token;    /* BL_TOKEN_BOOT_APP                            */
    uint32_t dbl_token;     /* BL_TOKEN_DBL_RESET                           */
    uint32_t reset_csr;     /* RCC->CSR captured by the bootloader          */
    uint32_t boot_flags;    /* BOOT_F_*                                     */
    uint32_t bl_version;    /* bootloader version (major<<16|minor<<8|patch) */
    uint32_t boot_count;    /* incremented by the bootloader on every boot  */
    uint32_t app_starts;    /* app starts not yet confirmed (crash loop)    */
    uint32_t reserved[7];
} shared_ram_t;

_Static_assert(sizeof(shared_ram_t) == MM_SHARED_SIZE, "shared RAM block size");

#define SHARED_RAM              ((volatile shared_ram_t *)MM_SHARED_BASE)

#endif /* SHARED_RAM_H */
