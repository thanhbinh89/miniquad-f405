/**
 * @file    bl_core.h
 * @brief   Bootloader protocol state machine (hardware independent).
 *
 * The flash is abstracted by bl_flash_ops_t so the very same code runs on
 * the target and inside the device emulator (tests/emu), where the web flasher is
 * exercised end-to-end against it.
 */
#ifndef BL_CORE_H
#define BL_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include "link.h"

typedef struct {
    /** Erase every sector needed for @p size bytes starting at MM_APP_BASE.
     *  @return bytes now erased (sector aligned, >= size) or 0 on failure. */
    uint32_t (*erase)(uint32_t size);
    /** Program @p len bytes (multiple of 4) at MM_APP_BASE + @p offset. */
    bool     (*program)(uint32_t offset, const uint32_t *words, uint32_t len);
    /** Memory view of the application region (header + image). */
    const uint8_t *region;
    uint32_t       region_size;
} bl_flash_ops_t;

typedef struct {
    uint32_t uid[3];
    uint32_t bl_version;
    uint32_t build_time;
    const char *git_hash;
} bl_identity_t;

typedef struct {
    const bl_flash_ops_t *flash;
    bl_identity_t         id;
    uint32_t erased;            /* bytes erased by the last BL_ERASE      */
    bool     boot_request;      /* host asked to start the application    */
    bool     reboot_request;
    uint8_t  reboot_target;     /* REBOOT_*                                */
    uint32_t packets;           /* requests handled                       */
} bl_core_t;

void bl_core_init(bl_core_t *bl, const bl_flash_ops_t *flash, const bl_identity_t *id);

/**
 * Handle one request.
 * @return true if @p rsp holds a reply to send (always for valid requests).
 */
bool bl_core_handle(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp);

/** True if the stored application image is valid. */
bool bl_core_app_valid(const bl_core_t *bl, uint32_t *version);

#endif /* BL_CORE_H */
