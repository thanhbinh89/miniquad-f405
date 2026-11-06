/**
 * @file    bl_image.c
 * @brief   Application image validation.
 */
#include "bl_image.h"

#include <string.h>
#include "app_header.h"
#include "board.h"
#include "crc.h"
#include "memory_map.h"

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool in_range(uint32_t v, uint32_t base, uint32_t size)
{
    return v >= base && v <= base + size;   /* inclusive end: initial SP = top */
}

bool bl_image_check(const uint8_t *region, uint32_t region_size, img_info_t *info)
{
    img_info_t local;
    img_info_t *r = (info != NULL) ? info : &local;
    memset(r, 0, sizeof(*r));

    if (region_size < MM_APP_HEADER_SIZE + 8u) {
        r->result = IMG_ERR_SIZE;
        return false;
    }

    app_header_t h;
    memcpy(&h, region, sizeof(h));          /* avoid unaligned access on host */

    if (h.magic != APP_HEADER_MAGIC) {
        r->result = IMG_ERR_MAGIC;
        return false;
    }
    if (h.header_version != APP_HEADER_VERSION || h.header_size != MM_APP_HEADER_SIZE ||
        h.image_offset != MM_APP_HEADER_SIZE) {
        r->result = IMG_ERR_HEADER;
        return false;
    }
    if (crc32(region, APP_HEADER_CRC_OFFSET) != h.header_crc32) {
        r->result = IMG_ERR_HEADER_CRC;
        return false;
    }

    r->image_size = h.image_size;
    r->crc_expected = h.image_crc32;
    r->fw_version = h.fw_version;

    if (h.image_size < 8u || (h.image_size & 3u) != 0u ||
        h.image_size > region_size - MM_APP_HEADER_SIZE) {
        r->result = IMG_ERR_SIZE;
        return false;
    }
    if (h.board_id != BOARD_ID) {
        r->result = IMG_ERR_BOARD;
        return false;
    }

    const uint8_t *img = region + MM_APP_HEADER_SIZE;
    r->crc_actual = crc32(img, h.image_size);
    if (r->crc_actual != h.image_crc32) {
        r->result = IMG_ERR_CRC;
        return false;
    }

    uint32_t sp = rd32(img);
    uint32_t pc = rd32(img + 4);
    bool sp_ok = in_range(sp, MM_SRAM_BASE, MM_SRAM_SIZE) || in_range(sp, MM_CCM_BASE, MM_CCM_SIZE);
    bool pc_ok = ((pc & 1u) != 0u) && pc >= (uint32_t)MM_APP_VECTORS &&
                 pc < (uint32_t)MM_APP_VECTORS + h.image_size;
    if (!sp_ok || !pc_ok) {
        r->result = IMG_ERR_VECTORS;
        return false;
    }

    r->result = IMG_OK;
    return true;
}
