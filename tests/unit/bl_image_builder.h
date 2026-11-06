/**
 * @file    bl_image_builder.h
 * @brief   Builds a valid application image (header + CRCs) in memory,
 *          exactly like tools/fwpack.py does after linking.
 */
#ifndef BL_IMAGE_BUILDER_H
#define BL_IMAGE_BUILDER_H

#include <stdint.h>
#include <string.h>

#include "app_header.h"
#include "board.h"
#include "crc.h"
#include "memory_map.h"

/** Fill @p buf with header + @p image_size bytes; returns total size. */
static inline uint32_t build_test_image(uint8_t *buf, uint32_t image_size, uint32_t version)
{
    memset(buf, 0, MM_APP_HEADER_SIZE + image_size);
    uint8_t *img = buf + MM_APP_HEADER_SIZE;
    for (uint32_t i = 8u; i < image_size; i++) {
        img[i] = (uint8_t)(i * 13u + 7u);
    }
    uint32_t sp = MM_CCM_BASE + MM_CCM_SIZE;
    uint32_t pc = MM_APP_VECTORS + 0x201u;          /* thumb */
    memcpy(img, &sp, 4);
    memcpy(img + 4, &pc, 4);

    app_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = APP_HEADER_MAGIC;
    h.header_version = APP_HEADER_VERSION;
    h.header_size = MM_APP_HEADER_SIZE;
    h.image_offset = MM_APP_HEADER_SIZE;
    h.image_size = image_size;
    h.image_crc32 = crc32(img, image_size);
    h.fw_version = version;
    h.board_id = BOARD_ID;
    strcpy(h.board_name, BOARD_NAME);
    memcpy(buf, &h, sizeof(h));
    uint32_t hc = crc32(buf, APP_HEADER_CRC_OFFSET);
    memcpy(buf + APP_HEADER_CRC_OFFSET, &hc, 4);
    return MM_APP_HEADER_SIZE + image_size;
}

#endif /* BL_IMAGE_BUILDER_H */
