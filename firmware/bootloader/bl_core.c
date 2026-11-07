/**
 * @file    bl_core.c
 * @brief   Bootloader request handlers.
 */
#include "bl_core.h"

#include <string.h>
#include "bl_image.h"
#include "board.h"
#include "memory_map.h"
#include "proto.h"

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static bool reply(link_packet_t *rsp, const link_packet_t *req, const void *data, size_t len)
{
    rsp->id = (uint8_t)(req->id | MSG_REPLY_FLAG);
    rsp->seq = req->seq;
    rsp->len = (uint16_t)len;
    memcpy(rsp->payload, data, len);
    return true;
}

static bool reply_status(link_packet_t *rsp, const link_packet_t *req, uint8_t status)
{
    return reply(rsp, req, &status, 1u);
}

void bl_core_init(bl_core_t *bl, const bl_flash_ops_t *flash, const bl_identity_t *id)
{
    memset(bl, 0, sizeof(*bl));
    bl->flash = flash;
    bl->id = *id;
}

bool bl_core_app_valid(const bl_core_t *bl, uint32_t *version)
{
    img_info_t info;
    bool ok = bl_image_check(bl->flash->region, bl->flash->region_size, &info);
    if (version != NULL) {
        *version = ok ? info.fw_version : 0u;
    }
    return ok;
}

static bool on_hello(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    msg_hello_rsp_t h;
    memset(&h, 0, sizeof(h));
    h.status = ST_OK;
    h.mode = DEV_MODE_BOOTLOADER;
    h.proto_version = PROTO_VERSION;
    h.board_id = BOARD_ID;
    h.fw_version = bl->id.bl_version;
    uint32_t app_version = 0u;
    h.app_valid = bl_core_app_valid(bl, &app_version) ? 1u : 0u;
    h.boot_reason = bl->boot_reason;
    h.app_version = app_version;
    h.app_max_size = bl->flash->region_size;
    memcpy(h.uid, bl->id.uid, sizeof(h.uid));
    h.build_time = bl->id.build_time;
    if (bl->id.git_hash != NULL) {
        strncpy(h.git_hash, bl->id.git_hash, sizeof(h.git_hash) - 1u);
    }
    _Static_assert(sizeof(BOARD_NAME) <= sizeof(h.board_name), "BOARD_NAME too long");
    memcpy(h.board_name, BOARD_NAME, sizeof(BOARD_NAME));
    return reply(rsp, req, &h, sizeof(h));
}

static bool on_reboot(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    if (req->len < 1u || req->payload[0] > REBOOT_ROM) {
        return reply_status(rsp, req, ST_ERR_ARG);
    }
    bl->reboot_target = req->payload[0];
    bl->reboot_request = true;
    return reply_status(rsp, req, ST_OK);
}

static bool on_erase(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    if (req->len != sizeof(msg_bl_erase_req_t)) {
        return reply_status(rsp, req, ST_ERR_LENGTH);
    }
    uint32_t size = rd32(req->payload);
    if (size < MM_APP_HEADER_SIZE + 8u || size > bl->flash->region_size) {
        return reply_status(rsp, req, ST_ERR_ARG);
    }
    bl->erased = 0u;
    uint32_t erased = bl->flash->erase(size);
    if (erased < size) {
        return reply_status(rsp, req, ST_ERR_FLASH);
    }
    bl->erased = erased;
    return reply_status(rsp, req, ST_OK);
}

static bool on_write(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    msg_bl_write_rsp_t r;
    r.status = ST_OK;
    r.offset = (req->len >= 4u) ? rd32(req->payload) : 0u;

    uint32_t data_len = (req->len >= 4u) ? (uint32_t)req->len - 4u : 0u;
    if (req->len < 8u || data_len > BL_WRITE_MAX || (data_len & 3u) != 0u) {
        r.status = ST_ERR_LENGTH;
    } else if ((r.offset & 3u) != 0u || r.offset + data_len > bl->erased) {
        r.status = (bl->erased == 0u) ? ST_ERR_STATE : ST_ERR_ARG;
    } else {
        uint32_t words[BL_WRITE_MAX / 4u];
        memcpy(words, &req->payload[4], data_len);      /* aligned copy */
        if (!bl->flash->program(r.offset, words, data_len)) {
            r.status = ST_ERR_FLASH;
        }
    }
    return reply(rsp, req, &r, sizeof(r));
}

static bool on_read(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    if (req->len != sizeof(msg_bl_read_req_t)) {
        return reply_status(rsp, req, ST_ERR_LENGTH);
    }
    uint32_t offset = rd32(req->payload);
    uint16_t length = rd16(&req->payload[4]);
    if (length > BL_WRITE_MAX || offset > bl->flash->region_size ||
        length > bl->flash->region_size - offset) {
        return reply_status(rsp, req, ST_ERR_ARG);
    }
    rsp->id = (uint8_t)(req->id | MSG_REPLY_FLAG);
    rsp->seq = req->seq;
    rsp->payload[0] = ST_OK;
    memcpy(&rsp->payload[1], &offset, 4u);
    memcpy(&rsp->payload[5], bl->flash->region + offset, length);
    rsp->len = (uint16_t)(5u + length);
    return true;
}

static bool on_verify(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    img_info_t info;
    msg_bl_verify_rsp_t v;
    v.valid = bl_image_check(bl->flash->region, bl->flash->region_size, &info) ? 1u : 0u;
    v.status = v.valid ? ST_OK : ST_ERR_VERIFY;
    v.image_size = info.image_size;
    v.crc_expected = info.crc_expected;
    v.crc_actual = info.crc_actual;
    v.app_version = info.fw_version;
    return reply(rsp, req, &v, sizeof(v));
}

static bool on_boot(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    if (!bl_core_app_valid(bl, NULL)) {
        return reply_status(rsp, req, ST_ERR_VERIFY);
    }
    bl->boot_request = true;
    return reply_status(rsp, req, ST_OK);
}

bool bl_core_handle(bl_core_t *bl, const link_packet_t *req, link_packet_t *rsp)
{
    if (req->id >= 0x40u) {
        return false;                       /* not a request: ignore */
    }
    bl->packets++;

    switch (req->id) {
    case MSG_HELLO:     return on_hello(bl, req, rsp);
    case MSG_REBOOT:    return on_reboot(bl, req, rsp);
    case MSG_BL_ERASE:  return on_erase(bl, req, rsp);
    case MSG_BL_WRITE:  return on_write(bl, req, rsp);
    case MSG_BL_READ:   return on_read(bl, req, rsp);
    case MSG_BL_VERIFY: return on_verify(bl, req, rsp);
    case MSG_BL_BOOT:   return on_boot(bl, req, rsp);
    default:            return reply_status(rsp, req, ST_ERR_UNKNOWN);
    }
}
