/**
 * @file    test_comms.c
 * @brief   Application protocol handler (comms.c + telemetry.c) against
 *          stubbed services and an in-memory byte port.
 */
#include <string.h>

#include "app_services.h"
#include "comms.h"
#include "link.h"
#include "log.h"
#include "params.h"
#include "proto.h"
#include "telemetry.h"
#include "tlm_rings.h"
#include "tinytest.h"

/* ------------------------------------------------------------------------ */
/* stub services                                                             */
/* ------------------------------------------------------------------------ */
static uint32_t g_now;
static bool     g_armed;
static int      g_saves;
static bool     g_save_ok = true;
static uint16_t g_motor[4];
static int      g_motor_calls;
static int      g_motor_stops;
static uint8_t  g_cal;
static uint8_t  g_beep;

uint32_t svc_now_ms(void) { return g_now; }
bool svc_armed(void) { return g_armed; }
void svc_identity(msg_hello_rsp_t *h)
{
    h->board_id = 0x42u;
    h->fw_version = 0x00010203u;
    h->app_version = 0x00010203u;
    h->app_valid = 1u;
    h->app_max_size = 0xF4000u;
    strcpy(h->board_name, "TEST");
}
void svc_reboot(uint8_t target) { (void)target; }
bool svc_save_config(void) { g_saves++; return g_save_ok; }
void svc_motor_test(const uint16_t duty[4]) { memcpy(g_motor, duty, sizeof(g_motor)); g_motor_calls++; }
void svc_motor_test_stop(void) { g_motor_stops++; }
bool svc_calibrate(uint8_t what) { g_cal = what; return what >= CAL_GYRO && what <= CAL_ACCEL_RESET; }
void svc_beep(uint8_t pattern) { g_beep = pattern; }
void svc_status(tlm_status_t *st) { memset(st, 0, sizeof(*st)); st->time_ms = g_now; st->vbat_mv = 3900u; }
void svc_alt(tlm_alt_t *a) { memset(a, 0, sizeof(*a)); a->h_est = 0.5f; }
void svc_fault(msg_fault_info_rsp_t *f) { f->valid = 1u; f->pc = 0x0800C201u; }

/* ------------------------------------------------------------------------ */
/* in-memory port                                                            */
/* ------------------------------------------------------------------------ */
static uint8_t s_rx[4096];
static size_t  s_rx_n, s_rx_pos;
static uint8_t s_tx[65536];
static size_t  s_tx_n;

static size_t port_read(uint8_t *buf, size_t max)
{
    size_t n = s_rx_n - s_rx_pos;
    if (n > max) {
        n = max;
    }
    memcpy(buf, &s_rx[s_rx_pos], n);
    s_rx_pos += n;
    if (s_rx_pos == s_rx_n) {
        s_rx_pos = s_rx_n = 0u;
    }
    return n;
}

static size_t port_write(const void *data, size_t len)
{
    if (s_tx_n + len > sizeof(s_tx)) {
        return 0u;
    }
    memcpy(&s_tx[s_tx_n], data, len);
    s_tx_n += len;
    return len;
}

static size_t port_tx_free(void)
{
    return sizeof(s_tx) - s_tx_n;
}

static const comms_port_t PORT = { port_read, port_write, port_tx_free };

/* ------------------------------------------------------------------------ */
static link_packet_t s_out[256];
static int           s_out_n;

static void collect(void)
{
    link_parser_t p;
    link_parser_init(&p);
    s_out_n = 0;
    for (size_t i = 0; i < s_tx_n; i++) {
        if (link_parser_feed(&p, s_tx[i], &s_out[s_out_n]) && s_out_n < 255) {
            s_out_n++;
        }
    }
    s_tx_n = 0u;
}

static void send_raw(uint8_t id, uint8_t seq, const void *pl, size_t len)
{
    s_rx_n += link_encode(id, seq, pl, len, &s_rx[s_rx_n]);
}

/** Send one request, run the task and return the reply (NULL if none). */
static const link_packet_t *request(uint8_t id, const void *pl, size_t len)
{
    static uint8_t seq;
    seq++;
    send_raw(id, seq, pl, len);
    comms_task();
    collect();
    for (int i = 0; i < s_out_n; i++) {
        if (s_out[i].id == (uint8_t)(id | MSG_REPLY_FLAG)) {
            CHECK(s_out[i].seq == seq);
            return &s_out[i];
        }
    }
    return NULL;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static float u2f(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void setup(void)
{
    params_reset_defaults();
    g_now = 1000u;
    g_armed = false;
    g_saves = g_motor_calls = g_motor_stops = 0;
    s_rx_n = s_rx_pos = s_tx_n = 0u;
    comms_init(&PORT);
    log_msg_t m;
    while (log_pop(&m)) {
    }
}

/* ------------------------------------------------------------------------ */
static void test_hello(void)
{
    setup();
    CHECK(!comms_host_alive());
    const link_packet_t *r = request(MSG_HELLO, NULL, 0);
    CHECK(r != NULL);
    if (r == NULL) {
        return;
    }
    CHECK(r->len == sizeof(msg_hello_rsp_t));
    msg_hello_rsp_t h;
    memcpy(&h, r->payload, sizeof(h));
    CHECK(h.status == ST_OK);
    CHECK(h.mode == DEV_MODE_APP);
    CHECK(h.proto_version == PROTO_VERSION);
    CHECK(h.board_id == 0x42u);
    CHECK(strcmp(h.board_name, "TEST") == 0);
    CHECK(comms_host_alive());
}

static void test_params(void)
{
    setup();
    const link_packet_t *r = request(MSG_PARAM_INFO, NULL, 0);
    CHECK(r != NULL && r->len == sizeof(msg_param_info_rsp_t));
    msg_param_info_rsp_t info;
    memcpy(&info, r->payload, sizeof(info));
    CHECK(info.count == params_count());
    CHECK(info.table_hash == params_table_hash());

    /* every descriptor */
    for (uint16_t i = 0; i < params_count(); i++) {
        r = request(MSG_PARAM_DESC, &i, 2);
        CHECK(r != NULL && r->len == sizeof(msg_param_desc_rsp_t));
        msg_param_desc_rsp_t d;
        memcpy(&d, r->payload, sizeof(d));
        CHECK(d.status == ST_OK && d.index == i);
        CHECK(strcmp(d.name, params_def(i)->name) == 0);
        CHECK(d.value == params_get_raw(i));
        CHECK(d.def == params_raw_default(i));
    }
    uint16_t bad = params_count();
    r = request(MSG_PARAM_DESC, &bad, 2);
    CHECK(r != NULL && r->len == 1u && r->payload[0] == ST_ERR_ARG);
    r = request(MSG_PARAM_DESC, &bad, 1);
    CHECK(r != NULL && r->payload[0] == ST_ERR_LENGTH);

    /* bulk read, clipped at the end of the table */
    uint8_t q[3] = { 0, 0, PARAM_GET_MAX };
    r = request(MSG_PARAM_GET, q, 3);
    CHECK(r != NULL && r->len == 4u + 4u * PARAM_GET_MAX);
    CHECK(rd32(&r->payload[4 + 4 * 5]) == params_get_raw(5));
    uint16_t first = (uint16_t)(params_count() - 2u);
    q[0] = (uint8_t)first;
    q[1] = (uint8_t)(first >> 8);
    q[2] = 10;
    r = request(MSG_PARAM_GET, q, 3);
    CHECK(r != NULL && r->payload[0] == ST_OK && r->payload[3] == 2u && r->len == 12u);
    q[2] = PARAM_GET_MAX + 1u;
    r = request(MSG_PARAM_GET, q, 3);
    CHECK(r != NULL && r->payload[0] == ST_ERR_ARG);

    /* set with clamping */
    int vref = params_find("bat_vref");
    CHECK(vref >= 0);
    uint8_t set[6];
    uint32_t v = f2u(10.0f);
    set[0] = (uint8_t)vref;
    set[1] = (uint8_t)(vref >> 8);
    memcpy(&set[2], &v, 4);
    r = request(MSG_PARAM_SET, set, 6);
    CHECK(r != NULL && r->len == sizeof(msg_param_set_rsp_t) && r->payload[0] == ST_OK);
    CHECK_NEAR(u2f(rd32(&r->payload[3])), 4.4, 1e-6);
    CHECK_NEAR(g_cfg.bat_vref, 4.4, 1e-6);

    /* reset to defaults */
    r = request(MSG_PARAM_RESET, NULL, 0);
    CHECK(r != NULL && r->payload[0] == ST_OK);
    CHECK_NEAR(g_cfg.bat_vref, 3.9, 1e-6);
}

static void test_armed_rules(void)
{
    setup();
    g_armed = true;
    /* a LIVE parameter may be tuned in flight ... */
    int live = params_find("bat_vref");
    uint8_t set[6] = { (uint8_t)live, (uint8_t)(live >> 8) };
    uint32_t v = f2u(4.0f);
    memcpy(&set[2], &v, 4);
    const link_packet_t *r = request(MSG_PARAM_SET, set, 6);
    CHECK(r != NULL && r->payload[0] == ST_OK);
    CHECK_NEAR(g_cfg.bat_vref, 4.0, 1e-6);
    /* ... others not */
    int other = params_find("bat_warn_v");
    CHECK(other >= 0 && (params_def((uint16_t)other)->flags & PARAM_F_LIVE) == 0u);
    set[0] = (uint8_t)other;
    set[1] = (uint8_t)(other >> 8);
    r = request(MSG_PARAM_SET, set, 6);
    CHECK(r != NULL && r->payload[0] == ST_ERR_STATE);

    r = request(MSG_PARAM_SAVE, NULL, 0);
    CHECK(r != NULL && r->payload[0] == ST_ERR_STATE);
    CHECK(g_saves == 0);
    r = request(MSG_PARAM_RESET, NULL, 0);
    CHECK(r != NULL && r->payload[0] == ST_ERR_STATE);
    uint8_t cal = CAL_GYRO;
    r = request(MSG_CALIBRATE, &cal, 1);
    CHECK(r != NULL && r->payload[0] == ST_ERR_STATE);
    uint8_t mt[10] = { MOTOR_TEST_KEY, 0, 100, 0, 100, 0, 100, 0, 100, 0 };
    r = request(MSG_MOTOR_TEST, mt, 10);
    CHECK(r != NULL && r->payload[0] == ST_ERR_STATE);
    CHECK(g_motor_calls == 0);
    uint8_t rb = REBOOT_BOOTLOADER;
    r = request(MSG_REBOOT, &rb, 1);
    CHECK(r != NULL && r->payload[0] == ST_ERR_STATE);
    uint8_t target;
    CHECK(!comms_take_reboot(&target));

    /* disarmed: everything allowed */
    g_armed = false;
    r = request(MSG_PARAM_SAVE, NULL, 0);
    CHECK(r != NULL && r->payload[0] == ST_OK && g_saves == 1);
    g_save_ok = false;
    r = request(MSG_PARAM_SAVE, NULL, 0);
    CHECK(r != NULL && r->payload[0] == ST_ERR_FLASH);
    g_save_ok = true;
    r = request(MSG_CALIBRATE, &cal, 1);
    CHECK(r != NULL && r->payload[0] == ST_OK && g_cal == CAL_GYRO);
    cal = 9;
    r = request(MSG_CALIBRATE, &cal, 1);
    CHECK(r != NULL && r->payload[0] == ST_ERR_ARG);
    r = request(MSG_REBOOT, &rb, 1);
    CHECK(r != NULL && r->payload[0] == ST_OK);
    CHECK(comms_take_reboot(&target) && target == REBOOT_BOOTLOADER);
    CHECK(!comms_take_reboot(&target));
    rb = 7;
    r = request(MSG_REBOOT, &rb, 1);
    CHECK(r != NULL && r->payload[0] == ST_ERR_ARG);
}

static void test_motor_test(void)
{
    setup();
    uint8_t mt[10] = { 0x11, 0, 100, 0, 100, 0, 100, 0, 100, 0 };
    const link_packet_t *r = request(MSG_MOTOR_TEST, mt, 10);
    CHECK(r != NULL && r->payload[0] == ST_ERR_ARG);
    mt[0] = MOTOR_TEST_KEY;
    mt[2] = 0xE8;                   /* 1000 */
    mt[3] = 0x03;
    mt[4] = 0xFF;                   /* 65535 -> clamped */
    mt[5] = 0xFF;
    r = request(MSG_MOTOR_TEST, mt, 10);
    CHECK(r != NULL && r->payload[0] == ST_OK);
    CHECK(g_motor_calls == 1);
    CHECK(g_motor[0] == 1000u && g_motor[1] == 1000u && g_motor[2] == 100u);
    r = request(MSG_MOTOR_TEST, mt, 9);
    CHECK(r != NULL && r->payload[0] == ST_ERR_LENGTH);

    /* the host disappears: the motor test is stopped */
    g_now += COMMS_HOST_TIMEOUT_MS + 1u;
    comms_task();
    CHECK(g_motor_stops == 1);
    CHECK(!comms_host_alive());
}

static void test_streams(void)
{
    setup();
    uint8_t req[3] = { STREAM_RATE, 0x4D, 0x01 };     /* 333 Hz -> divider 3 */
    const link_packet_t *r = request(MSG_STREAM, req, 3);
    CHECK(r != NULL && r->len == sizeof(msg_stream_rsp_t) && r->payload[0] == ST_OK);
    CHECK((r->payload[2] | (r->payload[3] << 8)) == 333);
    CHECK(tlm_rate_divider() == 3u);
    req[1] = 0xE8;                                    /* 1000 Hz -> 500 */
    req[2] = 0x03;
    r = request(MSG_STREAM, req, 3);
    CHECK((r->payload[2] | (r->payload[3] << 8)) == 500);
    req[0] = STREAM_STATUS;
    req[1] = 60;
    req[2] = 0;
    r = request(MSG_STREAM, req, 3);
    CHECK((r->payload[2] | (r->payload[3] << 8)) == 50);
    req[0] = 9;
    r = request(MSG_STREAM, req, 3);
    CHECK(r != NULL && r->payload[0] == ST_ERR_ARG);

    /* 50 Hz status for 200 ms -> 10 frames; rate samples pushed by the loop */
    int status = 0, rate = 0;
    for (int ms = 0; ms < 200; ms++) {
        g_now++;
        tlm_rate_t s;
        memset(&s, 0, sizeof(s));
        s.time_us = g_now * 1000u;
        if ((ms % 2) == 0) {
            (void)tlm_push_rate(&s);    /* the ring itself is divider-agnostic */
        }
        comms_task();
        collect();
        for (int i = 0; i < s_out_n; i++) {
            status += (s_out[i].id == MSG_TLM_STATUS && s_out[i].len == sizeof(tlm_status_t));
            rate += (s_out[i].id == MSG_TLM_RATE && s_out[i].len == sizeof(tlm_rate_t));
        }
    }
    CHECK(status >= 9 && status <= 11);
    CHECK(rate == 100);

    /* host silent for 3 s: everything stops */
    g_now += COMMS_HOST_TIMEOUT_MS;
    comms_task();
    CHECK(telemetry_rate(STREAM_STATUS) == 0u);
    CHECK(telemetry_rate(STREAM_RATE) == 0u);
    CHECK(tlm_rate_divider() == 0u);
}

static void test_misc(void)
{
    setup();
    /* unknown request id -> error reply; ids >= 0x40 are ignored */
    const link_packet_t *r = request(0x3A, NULL, 0);
    CHECK(r != NULL && r->len == 1u && r->payload[0] == ST_ERR_UNKNOWN);
    r = request(0x50, NULL, 0);
    CHECK(r == NULL && s_out_n == 0);

    /* corrupted frame: no answer */
    send_raw(MSG_HELLO, 1, NULL, 0);
    s_rx[1] ^= 0x40u;
    comms_task();
    collect();
    CHECK(s_out_n == 0);

    uint8_t beep = 3;
    r = request(MSG_BEEP, &beep, 1);
    CHECK(r != NULL && r->payload[0] == ST_OK && g_beep == 3);

    r = request(MSG_FAULT_INFO, NULL, 0);
    CHECK(r != NULL && r->len == sizeof(msg_fault_info_rsp_t) && r->payload[1] == 1u);
    r = request(MSG_STATUS, NULL, 0);
    CHECK(r != NULL && r->len == 1u + sizeof(tlm_status_t));

    /* the log is forwarded while a host is connected */
    LOG_I("hello %d", 7);
    comms_task();
    collect();
    CHECK(s_out_n == 1);
    CHECK(s_out[0].id == MSG_LOG && s_out[0].payload[0] == LOG_LEVEL_INFO);
    CHECK(s_out[0].len == 8u && memcmp(&s_out[0].payload[1], "hello 7", 7) == 0);
}

int main(void)
{
    RUN(test_hello);
    RUN(test_params);
    RUN(test_armed_rules);
    RUN(test_motor_test);
    RUN(test_streams);
    RUN(test_misc);
    return TT_REPORT();
}
