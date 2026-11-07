/**
 * @file    bl_policy.h
 * @brief   Boot decision of the bootloader (hardware independent, unit
 *          tested in tests/unit/test_bootloader.c).
 *
 * The bootloader stays resident (and, on the USB board, enumerates) only
 * when there is a reason to:
 *
 *   BLR_REQUEST     the application asked for it (firmware update)
 *   BLR_NO_APP      no valid application image
 *   BLR_DBL_RESET   the reset button was pressed again during the 300 ms
 *                   window that follows a reset
 *   BLR_CRASH_LOOP  the application was started BL_CRASH_LIMIT times in a
 *                   row without confirming its boot (it does so after
 *                   SHARED_APP_CONFIRM_MS of uptime, or before a deliberate
 *                   reboot)
 *                   and the last reset came from software / a watchdog
 *
 * Otherwise (BLR_NONE) the application starts after the double-reset
 * window. A plain reset-pin reset (button) clears the crash counter: the
 * user is in control, not a crash.
 */
#ifndef BL_POLICY_H
#define BL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define BL_CRASH_LIMIT      3u

/* RCC_CSR reset flags (RM0090), also usable on the host */
#define BL_CSR_LPWRRSTF     (1u << 31)
#define BL_CSR_WWDGRSTF     (1u << 30)
#define BL_CSR_IWDGRSTF     (1u << 29)
#define BL_CSR_SFTRSTF      (1u << 28)
#define BL_CSR_PORRSTF      (1u << 27)
#define BL_CSR_PINRSTF      (1u << 26)
#define BL_CSR_BORRSTF      (1u << 25)

typedef struct {
    uint32_t request;       /* shared RAM request, BL_REQ_*            */
    bool     dbl_reset;     /* reset during the previous boot window   */
    bool     app_valid;     /* application image verifies              */
    uint32_t app_starts;    /* application starts not yet confirmed    */
} bl_boot_in_t;

/** BLR_* reason to stay resident, or BLR_NONE to start the application. */
uint8_t bl_boot_decide(const bl_boot_in_t *in);

/** True when the NRST pin alone caused the reset (button, debugger). On
 *  the STM32F4 every internal reset also pulls NRST, so PINRSTF is set
 *  with the other flags; only "PINRSTF and nothing else" is external. */
bool    bl_reset_is_external(uint32_t rcc_csr);

/** Crash counter as seen at bootloader entry. */
uint32_t bl_app_starts_at_entry(uint32_t stored, uint32_t rcc_csr);

#endif /* BL_POLICY_H */
