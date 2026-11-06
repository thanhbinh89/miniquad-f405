/**
 * @file    app_header.c
 * @brief   Application image header (see common/app_header.h).
 *
 * image_size, image_crc32 and header_crc32 stay zero here and are patched
 * into the binary by tools/fwpack.py after linking.
 */
#include "app_header.h"
#include "board.h"
#include "fw_version.h"

__attribute__((section(".app_header"), used))
const app_header_t g_app_header = {
    .magic = APP_HEADER_MAGIC,
    .header_version = APP_HEADER_VERSION,
    .header_size = MM_APP_HEADER_SIZE,
    .image_offset = MM_APP_HEADER_SIZE,
    .image_size = 0u,
    .image_crc32 = 0u,
    .fw_version = FW_VERSION,
    .build_time = FW_BUILD_TIME,
    .board_id = BOARD_ID,
    .flags = 0u,
    .git_hash = FW_GIT_HASH,
    .board_name = BOARD_NAME,
    .fw_name = "miniquad-app",
    .header_crc32 = 0u,
};
