/**
 * @file    flash_if.h
 * @brief   STM32F405 internal flash: sector erase and 32-bit programming.
 *
 * Programming uses PSIZE = x32 (VDD 2.7-3.6 V). While an erase or program
 * operation runs, any code fetch from flash stalls the CPU - callers must
 * only use these functions when the motors are stopped.
 */
#ifndef FLASH_IF_H
#define FLASH_IF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLASH_SECTOR_COUNT  12u

typedef enum {
    FLASH_OK = 0,
    FLASH_ERR_ARG,
    FLASH_ERR_WRP,      /* write protection */
    FLASH_ERR_PGA,      /* alignment        */
    FLASH_ERR_PGP,      /* parallelism      */
    FLASH_ERR_PGS,      /* sequence         */
    FLASH_ERR_VERIFY,
} flash_status_t;

uint32_t       flash_sector_base(uint32_t sector);
uint32_t       flash_sector_size(uint32_t sector);
/** Sector containing @p addr, or FLASH_SECTOR_COUNT if outside the flash. */
uint32_t       flash_sector_of(uint32_t addr);

flash_status_t flash_erase_sector(uint32_t sector);
/** Program @p count words at word-aligned @p addr (target must be erased). */
flash_status_t flash_program(uint32_t addr, const uint32_t *words, size_t count);
/** True if @p len bytes at @p addr are all 0xFF. */
bool           flash_is_erased(uint32_t addr, size_t len);

#endif /* FLASH_IF_H */
