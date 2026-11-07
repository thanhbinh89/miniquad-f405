/**
 * @file    comms.c
 * @brief   Link protocol handler (application).
 */
#include "comms.h"

#include <string.h>
#include "link.h"
#include "log.h"
#include "params.h"
#include "proto.h"
#include "telemetry.h"

static const comms_port_t *s_port;
static link_parser_t       s_parser;
static link_packet_t       s_req;
static uint8_t             s_frame[LINK_FRAME_MAX];
static uint32_t            s_last_rx_ms;
static bool                s_host_seen;
static uint32_t            s_drops;
static bool                s_reboot_pending;
static uint8_t             s_reboot_target;
static bool                s_motor_test;

/* ------------------------------------------------------------------------ */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static bool send_frame(uint8_t id, uint8_t seq, const void *payload, size_t len)
{
    size_t n = link_encode(id, seq, payload, len, s_frame);
    if (n == 0u) {
        return false;
    }
    return s_port->write(s_frame, n) == n;
}

bool comms_send(uint8_t id, const void *payload, size_t len)
{
    if (s_port == NULL || !comms_host_alive()) {
        return false;
    }
    if (!send_frame(id, 0u, payload, len)) {
        s_drops++;
        return false;
    }
    return true;
}

/* replies are important: wait for room (the TX DMA keeps draining) */
static void reply(const link_packet_t *req, const void *payload, size_t len)
{
    uint8_t id = (uint8_t)(req->id | MSG_REPLY_FLAG);
    size_t n = link_encode(id, req->seq, payload, len, s_frame);
    for (uint32_t tries = 0u; tries < 200000u; tries++) {
        if (s_port->write(s_frame, n) == n) {
            return;
        }
    }
    s_drops++;
}

static void reply_status(const link_packet_t *req, uint8_t st)
{
    reply(req, &st, 1u);
}

/* ------------------------------------------------------------------------ */
static void on_hello(const link_packet_t *req)
{
    msg_hello_rsp_t h;
    memset(&h, 0, sizeof(h));
    svc_identity(&h);
    h.status = ST_OK;
    h.mode = DEV_MODE_APP;
    h.proto_version = PROTO_VERSION;
    reply(req, &h, sizeof(h));
}

static void on_reboot(const link_packet_t *req)
{
    if (req->len < 1u || req->payload[0] > REBOOT_ROM) {
        reply_status(req, ST_ERR_ARG);
        return;
    }
    if (svc_armed()) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    s_reboot_target = req->payload[0];
    s_reboot_pending = true;
    reply_status(req, ST_OK);
}

static void on_param_info(const link_packet_t *req)
{
    msg_param_info_rsp_t r = { ST_OK, params_count(), params_table_hash() };
    reply(req, &r, sizeof(r));
}

static void on_param_desc(const link_packet_t *req)
{
    if (req->len != sizeof(msg_param_desc_req_t)) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    uint16_t idx = rd16(req->payload);
    const param_def_t *p = params_def(idx);
    if (p == NULL) {
        reply_status(req, ST_ERR_ARG);
        return;
    }
    msg_param_desc_rsp_t r;
    memset(&r, 0, sizeof(r));
    r.status = ST_OK;
    r.index = idx;
    r.type = p->type;
    r.flags = p->flags;
    r.value = params_get_raw(idx);
    r.min = params_raw_min(idx);
    r.max = params_raw_max(idx);
    r.def = params_raw_default(idx);
    strncpy(r.name, p->name, sizeof(r.name) - 1u);
    reply(req, &r, sizeof(r));
}

static void on_param_get(const link_packet_t *req)
{
    if (req->len != sizeof(msg_param_get_req_t)) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    uint16_t first = rd16(req->payload);
    uint8_t count = req->payload[2];
    uint16_t total = params_count();
    if (count > PARAM_GET_MAX || first >= total) {
        reply_status(req, ST_ERR_ARG);
        return;
    }
    if ((uint32_t)first + count > total) {
        count = (uint8_t)(total - first);
    }
    msg_param_get_rsp_t r;
    r.status = ST_OK;
    r.first = first;
    r.count = count;
    for (uint8_t i = 0u; i < count; i++) {
        r.values[i] = params_get_raw((uint16_t)(first + i));
    }
    reply(req, &r, 4u + 4u * (size_t)count);
}

static void on_param_set(const link_packet_t *req)
{
    if (req->len != sizeof(msg_param_set_req_t)) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    uint16_t idx = rd16(req->payload);
    uint32_t value = rd32(&req->payload[2]);
    const param_def_t *p = params_def(idx);
    if (p == NULL) {
        reply_status(req, ST_ERR_ARG);
        return;
    }
    if (svc_armed() && (p->flags & PARAM_F_LIVE) == 0u) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    msg_param_set_rsp_t r = { ST_OK, idx, params_set_raw(idx, value) };
    reply(req, &r, sizeof(r));
}

static void on_param_save(const link_packet_t *req)
{
    if (svc_armed()) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    reply_status(req, svc_save_config() ? ST_OK : ST_ERR_FLASH);
}

static void on_param_reset(const link_packet_t *req)
{
    if (svc_armed()) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    params_reset_defaults();
    reply_status(req, ST_OK);
}

static void on_stream(const link_packet_t *req)
{
    if (req->len != sizeof(msg_stream_req_t)) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    uint8_t stream = req->payload[0];
    if (stream >= STREAM_COUNT) {
        reply_status(req, ST_ERR_ARG);
        return;
    }
    msg_stream_rsp_t r = { ST_OK, stream, telemetry_set_rate(stream, rd16(&req->payload[1])) };
    reply(req, &r, sizeof(r));
}

static void on_motor_test(const link_packet_t *req)
{
    if (req->len != sizeof(msg_motor_test_req_t)) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    if (req->payload[0] != MOTOR_TEST_KEY) {
        reply_status(req, ST_ERR_ARG);
        return;
    }
    if (svc_armed()) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    uint16_t duty[4];
    for (int i = 0; i < 4; i++) {
        uint16_t d = rd16(&req->payload[2 + 2 * i]);
        duty[i] = (d > 1000u) ? 1000u : d;
    }
    if (!svc_motor_test(duty)) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    s_motor_test = true;
    reply_status(req, ST_OK);
}

static void on_calibrate(const link_packet_t *req)
{
    if (req->len != 1u) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    if (svc_armed()) {
        reply_status(req, ST_ERR_STATE);
        return;
    }
    reply_status(req, svc_calibrate(req->payload[0]) ? ST_OK : ST_ERR_ARG);
}

static void on_beep(const link_packet_t *req)
{
    if (req->len != 1u) {
        reply_status(req, ST_ERR_LENGTH);
        return;
    }
    svc_beep(req->payload[0]);
    reply_status(req, ST_OK);
}

static void on_fault_info(const link_packet_t *req)
{
    msg_fault_info_rsp_t f;
    memset(&f, 0, sizeof(f));
    svc_fault(&f);
    f.status = ST_OK;
    reply(req, &f, sizeof(f));
}

static void on_status(const link_packet_t *req)
{
    uint8_t buf[1u + sizeof(tlm_status_t)];
    tlm_status_t st;
    svc_status(&st);
    buf[0] = ST_OK;
    memcpy(&buf[1], &st, sizeof(st));
    reply(req, buf, sizeof(buf));
}

static void dispatch(const link_packet_t *req)
{
    switch (req->id) {
    case MSG_HELLO:         on_hello(req); break;
    case MSG_REBOOT:        on_reboot(req); break;
    case MSG_PARAM_INFO:    on_param_info(req); break;
    case MSG_PARAM_DESC:    on_param_desc(req); break;
    case MSG_PARAM_GET:     on_param_get(req); break;
    case MSG_PARAM_SET:     on_param_set(req); break;
    case MSG_PARAM_SAVE:    on_param_save(req); break;
    case MSG_PARAM_RESET:   on_param_reset(req); break;
    case MSG_STREAM:        on_stream(req); break;
    case MSG_MOTOR_TEST:    on_motor_test(req); break;
    case MSG_CALIBRATE:     on_calibrate(req); break;
    case MSG_BEEP:          on_beep(req); break;
    case MSG_FAULT_INFO:    on_fault_info(req); break;
    case MSG_STATUS:        on_status(req); break;
    default:
        if (req->id < 0x40u) {
            reply_status(req, ST_ERR_UNKNOWN);
        }
        break;
    }
}

/* ------------------------------------------------------------------------ */
void comms_init(const comms_port_t *port)
{
    s_port = port;
    link_parser_init(&s_parser);
    s_host_seen = false;
    s_drops = 0u;
    s_reboot_pending = false;
    telemetry_init();
}

bool comms_host_alive(void)
{
    return s_host_seen && (svc_now_ms() - s_last_rx_ms) < COMMS_HOST_TIMEOUT_MS;
}

uint32_t comms_drops(void)
{
    return s_drops;
}

bool comms_take_reboot(uint8_t *target)
{
    if (!s_reboot_pending) {
        return false;
    }
    s_reboot_pending = false;
    *target = s_reboot_target;
    return true;
}

void comms_task(void)
{
    if (s_port == NULL) {
        return;
    }
    uint8_t buf[128];
    size_t n;
    int budget = 8;                         /* bounded work per call */
    while (budget-- > 0 && (n = s_port->read(buf, sizeof(buf))) > 0u) {
        for (size_t i = 0u; i < n; i++) {
            if (link_parser_feed(&s_parser, buf[i], &s_req)) {
                s_last_rx_ms = svc_now_ms();
                s_host_seen = true;
                dispatch(&s_req);
            }
        }
    }

    if (!comms_host_alive()) {
        if (s_host_seen) {
            /* host went away: stop streaming and any motor test */
            s_host_seen = false;
            telemetry_stop_all();
        }
        if (s_motor_test) {
            svc_motor_test_stop();
            s_motor_test = false;
        }
        return;
    }

    telemetry_task(svc_now_ms());

    log_msg_t m;
    while (s_port->tx_free() > LINK_FRAME_MAX * 2u && log_pop(&m)) {
        uint8_t pl[1u + LOG_MSG_MAX];
        pl[0] = m.level;
        memcpy(&pl[1], m.text, m.len);
        (void)comms_send(MSG_LOG, pl, 1u + (size_t)m.len);
    }
}
