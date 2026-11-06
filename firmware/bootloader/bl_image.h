/**
 * @file    bl_image.h
 * @brief   Application image validation (header, CRCs, vector table sanity).
 *
 * Pure function of a memory buffer, so it runs unchanged on the target
 * (memory mapped flash) and in host tests / the bootloader emulator.
 */
#ifndef BL_IMAGE_H
#define BL_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMG_OK = 0,
    IMG_ERR_MAGIC,
    IMG_ERR_HEADER,         /* version / size / offset fields  */
    IMG_ERR_HEADER_CRC,
    IMG_ERR_SIZE,
    IMG_ERR_BOARD,
    IMG_ERR_CRC,
    IMG_ERR_VECTORS,        /* stack pointer / reset vector    */
} img_result_t;

typedef struct {
    img_result_t result;
    uint32_t     image_size;
    uint32_t     crc_expected;
    uint32_t     crc_actual;
    uint32_t     fw_version;
} img_info_t;

/**
 * Validate the image stored at @p region (which represents MM_APP_BASE).
 * @param region_size bytes readable from @p region
 */
bool bl_image_check(const uint8_t *region, uint32_t region_size, img_info_t *info);

#endif /* BL_IMAGE_H */
