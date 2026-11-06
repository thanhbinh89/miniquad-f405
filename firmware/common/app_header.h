/**
 * @file    app_header.h
 * @brief   512-byte header placed at the start of the application region.
 *
 * The application is linked so that this header sits at MM_APP_BASE and the
 * vector table at MM_APP_VECTORS (= base + 512, which satisfies the VTOR
 * alignment). image_size, image_crc32 and header_crc32 are zero in the ELF and
 * are filled in by tools/fwpack.py after linking. The bootloader refuses to
 * start an image whose header or CRC does not verify.
 *
 * Layout is little-endian and fixed: tools/fwpack.py and web/js/firmware.js
 * use the byte offsets noted below.
 */
#ifndef APP_HEADER_H
#define APP_HEADER_H

#include <stdint.h>
#include "memory_map.h"

#define APP_HEADER_MAGIC        0x5041514Du     /* "MQAP" */
#define APP_HEADER_VERSION      1u
#define APP_HEADER_CRC_OFFSET   0x1FCu

typedef struct {
    uint32_t magic;             /* 0x000 APP_HEADER_MAGIC                       */
    uint32_t header_version;    /* 0x004 APP_HEADER_VERSION                     */
    uint32_t header_size;       /* 0x008 MM_APP_HEADER_SIZE                     */
    uint32_t image_offset;      /* 0x00C offset of the vector table (= 0x200)   */
    uint32_t image_size;        /* 0x010 bytes after the header (fwpack)        */
    uint32_t image_crc32;       /* 0x014 CRC-32 of those bytes (fwpack)         */
    uint32_t fw_version;        /* 0x018 major<<16 | minor<<8 | patch           */
    uint32_t build_time;        /* 0x01C unix time of the commit / build        */
    uint32_t board_id;          /* 0x020 BOARD_ID                               */
    uint32_t flags;             /* 0x024 reserved, 0                            */
    char     git_hash[16];      /* 0x028 NUL terminated                         */
    char     board_name[16];    /* 0x038 NUL terminated                         */
    char     fw_name[16];       /* 0x048 NUL terminated                         */
    uint32_t reserved[105];     /* 0x058                                        */
    uint32_t header_crc32;      /* 0x1FC CRC-32 of bytes 0x000..0x1FB (fwpack)  */
} app_header_t;

_Static_assert(sizeof(app_header_t) == MM_APP_HEADER_SIZE, "app header must be 512 bytes");
_Static_assert(__builtin_offsetof(app_header_t, header_crc32) == APP_HEADER_CRC_OFFSET,
               "header_crc32 offset");

#define APP_HEADER              ((const app_header_t *)MM_APP_BASE)

#endif /* APP_HEADER_H */
