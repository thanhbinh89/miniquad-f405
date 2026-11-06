/**
 * @file    config_store_target.c
 * @brief   Flash backend of the config store: banks = sectors 1 and 2.
 */
#include "config_store.h"

#include "flash_if.h"
#include "memory_map.h"

static uint32_t bank_base(uint32_t bank)
{
    return (bank == 0u) ? (uint32_t)MM_CFG_A_BASE : (uint32_t)MM_CFG_B_BASE;
}

static bool t_erase(uint32_t bank)
{
    uint32_t sector = (bank == 0u) ? MM_CFG_A_SECTOR : MM_CFG_B_SECTOR;
    return flash_erase_sector(sector) == FLASH_OK;
}

static bool t_program(uint32_t bank, uint32_t offset, const uint32_t *words, uint32_t count)
{
    return flash_program(bank_base(bank) + offset, words, count) == FLASH_OK;
}

static const uint8_t *t_view(uint32_t bank)
{
    return (const uint8_t *)bank_base(bank);
}

static const cfg_flash_ops_t s_target_ops = {
    .erase = t_erase,
    .program = t_program,
    .view = t_view,
    .bank_size = MM_CFG_BANK_SIZE,
};

const cfg_flash_ops_t *config_store_target_ops(void)
{
    return &s_target_ops;
}
