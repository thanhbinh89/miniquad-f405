/**
 * @file    flash_if.c
 * @brief   Internal flash erase / program.
 */
#include "flash_if.h"

#include "memory_map.h"
#include "stm32f4xx.h"

#define FLASH_KEY1      0x45670123u
#define FLASH_KEY2      0xCDEF89ABu
#define FLASH_ERR_MASK  (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)

uint32_t flash_sector_base(uint32_t sector)
{
    if (sector < 4u) {
        return (uint32_t)MM_FLASH_BASE + sector * 0x4000u;
    }
    if (sector == 4u) {
        return (uint32_t)MM_FLASH_BASE + 0x10000u;
    }
    return (uint32_t)MM_FLASH_BASE + 0x20000u + (sector - 5u) * 0x20000u;
}

uint32_t flash_sector_size(uint32_t sector)
{
    if (sector < 4u) {
        return 0x4000u;
    }
    if (sector == 4u) {
        return 0x10000u;
    }
    return 0x20000u;
}

uint32_t flash_sector_of(uint32_t addr)
{
    for (uint32_t s = 0u; s < FLASH_SECTOR_COUNT; s++) {
        uint32_t b = flash_sector_base(s);
        if (addr >= b && addr < b + flash_sector_size(s)) {
            return s;
        }
    }
    return FLASH_SECTOR_COUNT;
}

static void flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static flash_status_t flash_wait(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
    }
    uint32_t sr = FLASH->SR;
    FLASH->SR = FLASH_ERR_MASK | FLASH_SR_EOP;  /* rc_w1 */
    if ((sr & FLASH_SR_WRPERR) != 0u) {
        return FLASH_ERR_WRP;
    }
    if ((sr & FLASH_SR_PGAERR) != 0u) {
        return FLASH_ERR_PGA;
    }
    if ((sr & FLASH_SR_PGPERR) != 0u) {
        return FLASH_ERR_PGP;
    }
    if ((sr & FLASH_SR_PGSERR) != 0u) {
        return FLASH_ERR_PGS;
    }
    return FLASH_OK;
}

/* Data cache may hold stale lines of a sector that was just modified. */
static void flash_flush_caches(void)
{
    uint32_t acr = FLASH->ACR;
    FLASH->ACR = acr & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR = acr;
}

flash_status_t flash_erase_sector(uint32_t sector)
{
    if (sector >= FLASH_SECTOR_COUNT) {
        return FLASH_ERR_ARG;
    }
    flash_unlock();
    (void)flash_wait();
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB))
              | FLASH_CR_PSIZE_1 | (sector << FLASH_CR_SNB_Pos) | FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;
    flash_status_t st = flash_wait();
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    flash_lock();
    flash_flush_caches();
    return st;
}

flash_status_t flash_program(uint32_t addr, const uint32_t *words, size_t count)
{
    if ((addr & 3u) != 0u || addr < (uint32_t)MM_FLASH_BASE ||
        addr + count * 4u > (uint32_t)MM_FLASH_BASE + (uint32_t)MM_FLASH_SIZE) {
        return FLASH_ERR_ARG;
    }
    flash_unlock();
    (void)flash_wait();
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1;

    flash_status_t st = FLASH_OK;
    for (size_t i = 0u; i < count; i++) {
        volatile uint32_t *dst = (volatile uint32_t *)(addr + i * 4u);
        FLASH->CR |= FLASH_CR_PG;
        *dst = words[i];
        __DSB();
        st = flash_wait();
        FLASH->CR &= ~FLASH_CR_PG;
        if (st != FLASH_OK) {
            break;
        }
        if (*dst != words[i]) {
            st = FLASH_ERR_VERIFY;
            break;
        }
    }
    flash_lock();
    flash_flush_caches();
    return st;
}

bool flash_is_erased(uint32_t addr, size_t len)
{
    const uint32_t *p = (const uint32_t *)addr;
    for (size_t i = 0u; i < len / 4u; i++) {
        if (p[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}
