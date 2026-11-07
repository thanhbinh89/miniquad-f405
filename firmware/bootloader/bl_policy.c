/**
 * @file    bl_policy.c
 * @brief   Boot decision (see bl_policy.h).
 */
#include "bl_policy.h"

#include "proto.h"
#include "shared_ram.h"

uint8_t bl_boot_decide(const bl_boot_in_t *in)
{
    if (in->request == BL_REQ_STAY) {
        return BLR_REQUEST;
    }
    if (!in->app_valid) {
        return BLR_NO_APP;
    }
    if (in->dbl_reset) {
        return BLR_DBL_RESET;
    }
    if (in->app_starts >= BL_CRASH_LIMIT) {
        return BLR_CRASH_LOOP;
    }
    return BLR_NONE;
}

bool bl_reset_is_external(uint32_t rcc_csr)
{
    const uint32_t internal = BL_CSR_LPWRRSTF | BL_CSR_WWDGRSTF | BL_CSR_IWDGRSTF | BL_CSR_SFTRSTF |
                              BL_CSR_PORRSTF | BL_CSR_BORRSTF;
    return (rcc_csr & BL_CSR_PINRSTF) != 0u && (rcc_csr & internal) == 0u;
}

uint32_t bl_app_starts_at_entry(uint32_t stored, uint32_t rcc_csr)
{
    if (bl_reset_is_external(rcc_csr) || (rcc_csr & (BL_CSR_PORRSTF | BL_CSR_BORRSTF)) != 0u) {
        return 0u;              /* button, power-up or brown-out: not a software crash */
    }
    return stored;
}
