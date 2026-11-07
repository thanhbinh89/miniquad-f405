/**
 * @file    test_usb.c
 * @brief   usb_cdc.c (chapter 9 + CDC ACM) against a scripted USB host.
 *
 * The controller layer (usb_hw.h) is replaced by a mock that keeps one
 * queued IN packet per endpoint, the OUT "armed" state and STALL bits, and
 * lets the test play the host: control transfers on EP0, bulk OUT with
 * NAK flow control, bulk IN with transfer termination (short packet / ZLP).
 */
#include <stdio.h>
#include <string.h>

#include "tinytest.h"
#include "usb_cdc.h"
#include "usb_hw.h"

/* ------------------------------------------------------------------------ */
/* mock controller                                                          */
/* ------------------------------------------------------------------------ */
typedef struct {
    bool     busy;
    uint8_t  data[64];
    uint16_t len;
} mock_in_t;

static struct {
    const usbhw_events_t *ev;
    bool      use_irq;
    bool      vbus;
    bool      connected;
    uint8_t   address;
    bool      open_in[USB_HW_EP_COUNT];
    bool      open_out[USB_HW_EP_COUNT];
    uint8_t   type_in[USB_HW_EP_COUNT];
    uint16_t  mps_in[USB_HW_EP_COUNT];
    bool      armed[USB_HW_EP_COUNT];
    bool      stall_in[USB_HW_EP_COUNT];
    bool      stall_out[USB_HW_EP_COUNT];
    bool      ep0_stall;
    mock_in_t in[USB_HW_EP_COUNT];
    int       kicks;
    int       locks;
} m;

void usbhw_init(const usbhw_events_t *ev, bool use_irq)
{
    memset(&m, 0, sizeof(m));
    m.ev = ev;
    m.use_irq = use_irq;
}
void usbhw_deinit(void) {}
void usbhw_connect(bool on) { m.connected = on; }
bool usbhw_vbus(void) { return m.vbus; }
void usbhw_poll(void) {}
void usbhw_kick(void) { m.kicks++; }
void usbhw_lock(void) { m.locks++; }
void usbhw_unlock(void) { m.locks--; }
void usbhw_set_address(uint8_t addr) { m.address = addr; }

void usbhw_ep_open(uint8_t ep, uint8_t type, uint16_t mps)
{
    uint8_t n = ep & 0x7Fu;
    if ((ep & USB_EP_DIR_IN) != 0u) {
        m.open_in[n] = true;
        m.type_in[n] = type;
        m.mps_in[n] = mps;
        m.in[n].busy = false;
    } else {
        m.open_out[n] = true;
        m.armed[n] = false;
    }
}

void usbhw_ep_close_all(void)
{
    for (unsigned n = 1u; n < USB_HW_EP_COUNT; n++) {
        m.open_in[n] = m.open_out[n] = m.armed[n] = false;
        m.stall_in[n] = m.stall_out[n] = false;
        m.in[n].busy = false;
    }
}

bool usbhw_ep_write(uint8_t ep, const void *data, uint16_t len)
{
    uint8_t n = ep & 0x7Fu;
    CHECK(len <= 64u);
    if (m.in[n].busy) {
        return false;
    }
    if (n != 0u) {
        CHECK(m.open_in[n]);
    }
    m.in[n].busy = true;
    m.in[n].len = len;
    if (len > 0u) {
        memcpy(m.in[n].data, data, len);
    }
    return true;
}

void usbhw_ep_out_arm(uint8_t ep)
{
    CHECK(ep != 0u && m.open_out[ep]);
    m.armed[ep] = true;
}

void usbhw_ep_stall(uint8_t ep, bool stall)
{
    uint8_t n = ep & 0x7Fu;
    if (n == 0u) {
        m.ep0_stall = stall;
        return;
    }
    if ((ep & USB_EP_DIR_IN) != 0u) {
        m.stall_in[n] = stall;
    } else {
        m.stall_out[n] = stall;
    }
}

bool usbhw_ep_stalled(uint8_t ep)
{
    uint8_t n = ep & 0x7Fu;
    if (n == 0u) {
        return false;
    }
    return ((ep & USB_EP_DIR_IN) != 0u) ? m.stall_in[n] : m.stall_out[n];
}

/* ------------------------------------------------------------------------ */
/* scripted host                                                            */
/* ------------------------------------------------------------------------ */
#define XFER_STALL  (-1)

/* the "interrupt": what the OTG ISR does after a kick */
static void isr(void)
{
    usb_cdc_irq();
}

static void bus_reset(void)
{
    m.ep0_stall = false;
    m.in[0].busy = false;
    m.address = 0u;
    usbhw_ep_close_all();
    m.ev->reset();
}

/* One control transfer. IN: returns bytes received into @p buf. OUT/no data:
 * returns 0. XFER_STALL when the device stalled a stage. */
static int control(uint8_t bm, uint8_t req, uint16_t value, uint16_t index, uint16_t length,
                   const uint8_t *out, uint8_t *buf, int *npackets)
{
    uint8_t setup[8] = { bm, req, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index,
                         (uint8_t)(index >> 8), (uint8_t)length, (uint8_t)(length >> 8) };
    m.ep0_stall = false;                    /* a SETUP clears the control STALL */
    m.in[0].busy = false;                   /* ...and flushes what EP0 had queued */
    m.ev->setup(setup);
    if (npackets != NULL) {
        *npackets = 0;
    }
    if ((bm & 0x80u) != 0u) {
        int got = 0;
        for (int guard = 0; guard < 64; guard++) {
            if (m.ep0_stall) {
                return XFER_STALL;
            }
            if (!m.in[0].busy) {
                CHECK(false);               /* device must answer the IN token */
                return XFER_STALL;
            }
            uint16_t n = m.in[0].len;
            CHECK(got + n <= length);
            memcpy(&buf[got], m.in[0].data, n);
            got += n;
            if (npackets != NULL) {
                (*npackets)++;
            }
            m.in[0].busy = false;
            m.ev->in_done(0u);
            if (n < 64u || got == length) {
                break;                      /* short packet or all we asked for */
            }
        }
        CHECK(!m.in[0].busy);               /* nothing more queued after the end */
        m.ev->out(0u, NULL, 0u);            /* status stage: zero-length OUT */
        return got;
    }
    if (length > 0u) {
        uint16_t sent = 0u;
        while (sent < length) {
            if (m.ep0_stall) {
                return XFER_STALL;
            }
            uint16_t left = (uint16_t)(length - sent);
            uint16_t n = (left > 64u) ? 64u : left;
            m.ev->out(0u, &out[sent], n);
            sent = (uint16_t)(sent + n);
        }
    }
    if (m.ep0_stall) {
        return XFER_STALL;
    }
    CHECK(m.in[0].busy && m.in[0].len == 0u);   /* status stage: zero-length IN */
    if (!m.in[0].busy) {
        return XFER_STALL;
    }
    m.in[0].busy = false;
    m.ev->in_done(0u);
    return 0;
}

static int get_desc(uint8_t type, uint8_t index, uint16_t length, uint8_t *buf, int *npk)
{
    uint16_t langid = (uint16_t)((type == 3u && index != 0u) ? 0x0409u : 0u);
    return control(0x80u, 6u, (uint16_t)((type << 8) | index), langid, length, NULL, buf, npk);
}

/* host sends up to @p len bytes on bulk OUT; returns bytes the device took */
static size_t bulk_out(const uint8_t *data, size_t len)
{
    size_t sent = 0u;
    while (sent < len) {
        if (!m.armed[1]) {
            break;                          /* NAK */
        }
        m.armed[1] = false;
        uint16_t n = (uint16_t)((len - sent) > 64u ? 64u : (len - sent));
        m.ev->out(1u, &data[sent], n);
        sent += n;
    }
    return sent;
}

/* host reads bulk IN until the device has nothing queued; records packet sizes */
static size_t bulk_in(uint8_t *buf, size_t max, int *sizes, int *count)
{
    size_t got = 0u;
    *count = 0;
    for (int guard = 0; guard < 1000; guard++) {
        isr();                              /* device may start a packet after a kick */
        if (!m.in[1].busy) {
            break;
        }
        uint16_t n = m.in[1].len;
        CHECK(got + n <= max);
        memcpy(&buf[got], m.in[1].data, n);
        got += n;
        sizes[(*count)++] = n;
        m.in[1].busy = false;
        m.ev->in_done(1u);
    }
    return got;
}

static uint32_t s_now;

static void tick(uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; i++) {
        s_now++;
        usb_cdc_task(s_now);
    }
}

static const usb_cdc_config_t CFG = {
    .vid = 0x0483u, .pid = 0x5740u, .bcd_device = 0x0200u,
    .manufacturer = "MiniQuad", .product = "MiniQuad-F405", .serial = "0123456789ABCDEF01234567",
    .use_irq = true,
};

/* plug in, enumerate like Windows does, open the port like Web Serial */
static void enumerate(void)
{
    uint8_t b[256];
    m.vbus = true;
    tick(20u);
    CHECK(m.connected);
    bus_reset();
    CHECK_EQ(get_desc(1u, 0u, 64u, b, NULL), 18);
    bus_reset();
    CHECK_EQ(control(0x00u, 5u, 7u, 0u, 0u, NULL, NULL, NULL), 0);         /* SET_ADDRESS 7 */
    CHECK_EQ(get_desc(1u, 0u, 18u, b, NULL), 18);
    CHECK_EQ(get_desc(2u, 0u, 9u, b, NULL), 9);
    CHECK_EQ(get_desc(2u, 0u, 255u, b, NULL), 67);
    CHECK_EQ(control(0x00u, 9u, 1u, 0u, 0u, NULL, NULL, NULL), 0);         /* SET_CONFIGURATION 1 */
    const uint8_t lc[7] = { 0x00, 0x08, 0x07, 0x00, 0, 0, 8 };             /* 460800 8N1 */
    CHECK_EQ(control(0x21u, 0x20u, 0u, 0u, 7u, lc, NULL, NULL), 0);         /* SET_LINE_CODING */
    CHECK_EQ(control(0x21u, 0x22u, 3u, 0u, 0u, NULL, NULL, NULL), 0);       /* DTR | RTS */
}

/* ------------------------------------------------------------------------ */
static void test_descriptors(void)
{
    usb_cdc_init(&CFG);
    uint8_t b[256];
    int npk;

    /* VBUS debounce: the pull-up follows VBUS after 10 ms */
    tick(50u);
    CHECK(!m.connected);
    m.vbus = true;
    tick(5u);
    CHECK(!m.connected);
    tick(10u);
    CHECK(m.connected);

    bus_reset();
    CHECK_EQ(get_desc(1u, 0u, 64u, b, &npk), 18);
    CHECK_EQ(npk, 1);
    CHECK_EQ(b[0], 18);
    CHECK_EQ(b[1], 1);
    CHECK_EQ(b[2] | (b[3] << 8), 0x0200);                   /* USB 2.0 */
    CHECK_EQ(b[4], 0x02);                                   /* CDC */
    CHECK_EQ(b[7], 64);
    CHECK_EQ(b[8] | (b[9] << 8), 0x0483);
    CHECK_EQ(b[10] | (b[11] << 8), 0x5740);
    CHECK_EQ(b[12] | (b[13] << 8), 0x0200);
    CHECK_EQ(b[16], 3);                                     /* iSerialNumber */
    CHECK_EQ(b[17], 1);

    CHECK_EQ(control(0x00u, 5u, 42u, 0u, 0u, NULL, NULL, NULL), 0);
    CHECK_EQ(m.address, 42);                                /* set before the status stage (DWC2) */

    /* configuration: 9 bytes first, then all of it in 64 + 3 */
    CHECK_EQ(get_desc(2u, 0u, 9u, b, NULL), 9);
    CHECK_EQ(b[2] | (b[3] << 8), 67);
    CHECK_EQ(get_desc(2u, 0u, 255u, b, &npk), 67);
    CHECK_EQ(npk, 2);
    /* exactly 64 asked: 64 bytes and no ZLP (host got all it asked for) */
    CHECK_EQ(get_desc(2u, 0u, 64u, b, &npk), 64);
    CHECK_EQ(npk, 1);

    /* walk the configuration */
    CHECK_EQ(get_desc(2u, 0u, 255u, b, NULL), 67);
    int interfaces = 0, endpoints = 0, cs = 0, total = 0;
    uint8_t ep_addr[4] = { 0 };
    for (int i = 0; i < 67; i += b[i]) {
        CHECK(b[i] > 0);
        total += b[i];
        if (b[i + 1] == 4u) {
            interfaces++;
            if (interfaces == 1) {
                CHECK_EQ(b[i + 5], 0x02);                   /* communication class */
                CHECK_EQ(b[i + 6], 0x02);                   /* ACM */
                CHECK_EQ(b[i + 7], 0x00);                   /* no AT protocol (ModemManager) */
            } else {
                CHECK_EQ(b[i + 5], 0x0A);                   /* CDC data */
            }
        } else if (b[i + 1] == 5u) {
            ep_addr[endpoints++] = b[i + 2];
            CHECK((b[i + 4] | (b[i + 5] << 8)) <= 64);
        } else if (b[i + 1] == 0x24u) {
            cs++;
        }
    }
    CHECK_EQ(total, 67);
    CHECK_EQ(interfaces, 2);
    CHECK_EQ(endpoints, 3);
    CHECK_EQ(cs, 4);
    CHECK_EQ(ep_addr[0], 0x82);
    CHECK_EQ(ep_addr[1], 0x01);
    CHECK_EQ(ep_addr[2], 0x81);

    /* strings */
    CHECK_EQ(get_desc(3u, 0u, 255u, b, NULL), 4);
    CHECK_EQ(b[2] | (b[3] << 8), 0x0409);
    int n = get_desc(3u, 3u, 255u, b, NULL);
    CHECK_EQ(n, 2 + 2 * 24);
    char serial[32] = { 0 };
    for (int i = 0; i < (n - 2) / 2; i++) {
        serial[i] = (char)b[2 + 2 * i];
        CHECK_EQ(b[3 + 2 * i], 0);
    }
    CHECK_STR(serial, CFG.serial);
    n = get_desc(3u, 2u, 255u, b, NULL);
    CHECK_EQ(n, 2 + 2 * 13);
    CHECK_EQ(get_desc(3u, 2u, 2u, b, NULL), 2);             /* length probe */
    CHECK_EQ(b[0], 28);

    /* things a full-speed CDC device does not have */
    CHECK_EQ(get_desc(3u, 0xEEu, 255u, b, NULL), XFER_STALL);   /* MS OS string */
    CHECK_EQ(get_desc(6u, 0u, 10u, b, NULL), XFER_STALL);       /* device qualifier */
    CHECK_EQ(get_desc(15u, 0u, 5u, b, NULL), XFER_STALL);       /* BOS */
    CHECK_EQ(control(0xC0u, 0x01u, 0u, 0u, 16u, NULL, b, NULL), XFER_STALL); /* vendor */
    CHECK_EQ(control(0x00u, 9u, 2u, 0u, 0u, NULL, NULL, NULL), XFER_STALL);  /* config 2 */

    /* a stall does not wedge EP0: the next request works */
    CHECK_EQ(get_desc(1u, 0u, 18u, b, NULL), 18);

    /* configure */
    CHECK(!usb_cdc_configured());
    CHECK_EQ(control(0x80u, 8u, 0u, 0u, 1u, NULL, b, NULL), 1);
    CHECK_EQ(b[0], 0);
    CHECK_EQ(control(0x00u, 9u, 1u, 0u, 0u, NULL, NULL, NULL), 0);
    CHECK(usb_cdc_configured());
    CHECK(m.open_in[1] && m.open_out[1] && m.open_in[2]);
    CHECK_EQ(m.type_in[1], USB_EPTYPE_BULK);
    CHECK_EQ(m.type_in[2], USB_EPTYPE_INTERRUPT);
    CHECK(m.armed[1]);                                      /* ready for the first OUT packet */
    CHECK_EQ(control(0x80u, 8u, 0u, 0u, 1u, NULL, b, NULL), 1);
    CHECK_EQ(b[0], 1);
    CHECK_EQ(control(0x81u, 10u, 0u, 1u, 1u, NULL, b, NULL), 1);    /* GET_INTERFACE */
    CHECK_EQ(control(0x01u, 11u, 0u, 1u, 0u, NULL, NULL, NULL), 0); /* SET_INTERFACE alt 0 */
    CHECK_EQ(control(0x01u, 11u, 1u, 1u, 0u, NULL, NULL, NULL), XFER_STALL);
    CHECK_EQ(control(0x80u, 0u, 0u, 0u, 2u, NULL, b, NULL), 2);     /* GET_STATUS device */

    /* endpoint halt */
    CHECK_EQ(control(0x02u, 3u, 0u, 0x81u, 0u, NULL, NULL, NULL), 0);   /* SET_FEATURE HALT */
    CHECK(m.stall_in[1]);
    CHECK_EQ(control(0x82u, 0u, 0u, 0x81u, 2u, NULL, b, NULL), 2);
    CHECK_EQ(b[0], 1);
    CHECK_EQ(control(0x02u, 1u, 0u, 0x81u, 0u, NULL, NULL, NULL), 0);   /* CLEAR_FEATURE HALT */
    CHECK(!m.stall_in[1]);
    CHECK_EQ(control(0x02u, 1u, 0u, 0x85u, 0u, NULL, NULL, NULL), XFER_STALL);
}

static void test_cdc_requests(void)
{
    usb_cdc_init(&CFG);
    enumerate();
    uint8_t b[16];
    usb_cdc_state_t st;
    usb_cdc_state(&st);
    CHECK(st.configured && st.vbus && st.connected && st.dtr);
    CHECK_EQ(st.baud, 460800u);
    CHECK_EQ(control(0xA1u, 0x21u, 0u, 0u, 7u, NULL, b, NULL), 7);      /* GET_LINE_CODING */
    CHECK_EQ(b[0] | (b[1] << 8) | (b[2] << 16), 460800);
    CHECK_EQ(b[6], 8);
    CHECK_EQ(control(0x21u, 0x23u, 100u, 0u, 0u, NULL, NULL, NULL), 0); /* SEND_BREAK */
    /* wrong interface / wrong length */
    CHECK_EQ(control(0x21u, 0x22u, 1u, 1u, 0u, NULL, NULL, NULL), XFER_STALL);
    const uint8_t lc[8] = { 0 };
    CHECK_EQ(control(0x21u, 0x20u, 0u, 0u, 8u, lc, NULL, NULL), XFER_STALL);
    usb_cdc_state(&st);
    CHECK_EQ(st.baud, 460800u);
}

static void test_bulk_out_flow_control(void)
{
    usb_cdc_init(&CFG);
    enumerate();
    static uint8_t data[3000];
    for (size_t i = 0u; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i * 7u + 3u);
    }
    uint8_t rx[3000];

    CHECK_EQ(bulk_out(data, 100u), 100u);
    CHECK_EQ(usb_cdc_read(rx, sizeof(rx)), 100u);
    CHECK(memcmp(rx, data, 100u) == 0);

    /* nobody reads: the ring fills, then the endpoint stays NAKed */
    size_t took = bulk_out(data, sizeof(data));
    CHECK(took >= USB_CDC_RX_SIZE - 64u && took < USB_CDC_RX_SIZE);
    CHECK(!m.armed[1]);
    CHECK_EQ(bulk_out(data + took, 64u), 0u);              /* NAK */

    /* the thread reads: the ISR re-arms after the kick, data stays in order */
    int kicks = m.kicks;
    size_t got = usb_cdc_read(rx, 200u);
    CHECK_EQ(got, 200u);
    CHECK(m.kicks > kicks);
    isr();
    CHECK(m.armed[1]);
    size_t more = bulk_out(data + took, sizeof(data) - took);
    CHECK(more > 0u);
    size_t total = got;
    for (int i = 0; i < 100 && total < took + more; i++) {
        size_t n = usb_cdc_read(rx + total, sizeof(rx) - total);
        total += n;
        isr();
        size_t k = bulk_out(data + took + more, sizeof(data) - took - more);
        more += k;
    }
    CHECK_EQ(total, took + more);
    CHECK(memcmp(rx, data, total) == 0);
}

static void test_bulk_in_packets(void)
{
    usb_cdc_init(&CFG);
    enumerate();
    static uint8_t data[5000];
    for (size_t i = 0u; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i ^ 0x5Au);
    }
    uint8_t rx[5000];
    int sizes[200], count;

    CHECK(usb_cdc_tx_idle());
    CHECK_EQ(usb_cdc_write(data, 10u), 10u);
    CHECK(!usb_cdc_tx_idle());
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), 10u);
    CHECK_EQ(count, 1);
    CHECK_EQ(sizes[0], 10);
    CHECK(usb_cdc_tx_idle());

    /* a transfer ending on a full packet is terminated by a ZLP */
    CHECK_EQ(usb_cdc_write(data, 64u), 64u);
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), 64u);
    CHECK_EQ(count, 2);
    CHECK_EQ(sizes[0], 64);
    CHECK_EQ(sizes[1], 0);
    CHECK(usb_cdc_tx_idle());

    CHECK_EQ(usb_cdc_write(data, 200u), 200u);
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), 200u);
    CHECK_EQ(count, 4);
    CHECK_EQ(sizes[3], 8);
    CHECK(memcmp(rx, data, 200u) == 0);

    CHECK_EQ(usb_cdc_write(data, 128u), 128u);
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), 128u);
    CHECK_EQ(count, 3);
    CHECK_EQ(sizes[2], 0);

    /* all-or-nothing when the ring is full; wrap-around keeps the order */
    size_t queued = 0u;
    while (usb_cdc_write(data + queued, 1000u) == 1000u) {
        queued += 1000u;
    }
    CHECK(queued >= USB_CDC_TX_SIZE - 1000u - 64u);
    CHECK(usb_cdc_tx_free() < 1000u);
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), queued);
    CHECK(memcmp(rx, data, queued) == 0);
    CHECK_EQ(usb_cdc_write(data, 3000u), 3000u);           /* wraps */
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), 3000u);
    CHECK(memcmp(rx, data, 3000u) == 0);
    CHECK(usb_cdc_tx_idle());
}

static void test_sessions(void)
{
    usb_cdc_init(&CFG);
    uint8_t data[300], rx[300];
    int sizes[20], count;
    memset(data, 0xA5, sizeof(data));

    /* not configured: writes vanish, the link counts as idle */
    CHECK_EQ(usb_cdc_write(data, 100u), 100u);
    CHECK(usb_cdc_tx_idle());
    usb_cdc_state_t st;
    usb_cdc_state(&st);
    CHECK_EQ(st.dropped, 100u);

    enumerate();
    /* port closed (DTR low): queued data is discarded */
    CHECK_EQ(usb_cdc_write(data, 300u), 300u);
    isr();                                                  /* first packet goes out */
    CHECK_EQ(control(0x21u, 0x22u, 0u, 0u, 0u, NULL, NULL, NULL), 0);
    CHECK_EQ(bulk_in(rx, sizeof(rx), sizes, &count), 64u);  /* only the packet in flight */
    CHECK(usb_cdc_tx_idle());

    /* bus reset: unconfigured, DTR gone, OUT endpoint closed */
    CHECK_EQ(control(0x21u, 0x22u, 1u, 0u, 0u, NULL, NULL, NULL), 0);
    CHECK_EQ(bulk_out(data, 50u), 50u);
    CHECK_EQ(usb_cdc_read(rx, sizeof(rx)), 50u);
    bus_reset();
    CHECK(!usb_cdc_configured());
    CHECK(!m.armed[1]);
    usb_cdc_state(&st);
    CHECK(!st.dtr);
    CHECK_EQ(st.resets, 3u);

    /* cable pulled: pull-up off after the debounce, session over */
    CHECK_EQ(control(0x00u, 9u, 1u, 0u, 0u, NULL, NULL, NULL), 0);
    CHECK(usb_cdc_configured());
    m.vbus = false;
    tick(5u);
    CHECK(m.connected);
    tick(10u);
    CHECK(!m.connected);
    CHECK(!usb_cdc_configured());
    CHECK(!usb_cdc_vbus());
    CHECK_EQ(m.locks, 0);

    /* plugged again: enumerates again */
    enumerate();
    CHECK(usb_cdc_configured());

    /* detach before a reset: off for good */
    usb_cdc_detach();
    CHECK(!m.connected);
    CHECK(!usb_cdc_configured());
    tick(100u);
    CHECK(!m.connected);
    CHECK_EQ(m.locks, 0);
}

static void test_polled_mode(void)
{
    usb_cdc_config_t c = CFG;
    c.use_irq = false;
    c.product = "MiniQuad-F405 Bootloader";
    usb_cdc_init(&c);
    enumerate();
    uint8_t b[64];
    int n = get_desc(3u, 2u, 255u, b, NULL);
    CHECK_EQ(n, 2 + 2 * 24);
    /* in polled mode usb_cdc_task() services the endpoints */
    uint8_t data[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    CHECK_EQ(usb_cdc_write(data, 10u), 10u);
    CHECK(!m.in[1].busy);
    tick(1u);
    CHECK(m.in[1].busy);
    CHECK_EQ(m.in[1].len, 10u);
}

int main(void)
{
    RUN(test_descriptors);
    RUN(test_cdc_requests);
    RUN(test_bulk_out_flow_control);
    RUN(test_bulk_in_packets);
    RUN(test_sessions);
    RUN(test_polled_mode);
    return TT_REPORT();
}
