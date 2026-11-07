/**
 * @file    usb_cdc.c
 * @brief   USB device (chapter 9) + CDC ACM function on top of usb_hw.h.
 */
#include "usb_cdc.h"

#include <string.h>
#include "usb_hw.h"

#if defined(__ARM_ARCH)
#define MEM_BARRIER()   __asm volatile("dmb" ::: "memory")
#else
#define MEM_BARRIER()   __asm volatile("" ::: "memory")
#endif

_Static_assert((USB_CDC_RX_SIZE & (USB_CDC_RX_SIZE - 1u)) == 0u, "RX ring size power of two");
_Static_assert((USB_CDC_TX_SIZE & (USB_CDC_TX_SIZE - 1u)) == 0u, "TX ring size power of two");

/* ---------------- endpoints ---------------- */
#define EP_DATA_OUT         0x01u
#define EP_DATA_IN          0x81u
#define EP_NOTIFY           0x82u
#define DATA_EP             1u
#define DATA_MPS            64u
#define NOTIFY_MPS          16u
#define VBUS_DEBOUNCE_MS    10u

/* ---------------- chapter 9 ---------------- */
#define REQ_DIR_IN          0x80u
#define REQ_TYPE_MASK       0x60u
#define REQ_TYPE_STANDARD   0x00u
#define REQ_TYPE_CLASS      0x20u
#define REQ_RCPT_MASK       0x1Fu
#define REQ_RCPT_DEVICE     0u
#define REQ_RCPT_INTERFACE  1u
#define REQ_RCPT_ENDPOINT   2u

#define GET_STATUS          0u
#define CLEAR_FEATURE       1u
#define SET_FEATURE         3u
#define SET_ADDRESS         5u
#define GET_DESCRIPTOR      6u
#define GET_CONFIGURATION   8u
#define SET_CONFIGURATION   9u
#define GET_INTERFACE       10u
#define SET_INTERFACE       11u

#define FEAT_ENDPOINT_HALT  0u
#define FEAT_REMOTE_WAKEUP  1u

#define DESC_DEVICE         1u
#define DESC_CONFIGURATION  2u
#define DESC_STRING         3u

/* ---------------- CDC 1.2 / PSTN ---------------- */
#define CDC_SET_LINE_CODING         0x20u
#define CDC_GET_LINE_CODING         0x21u
#define CDC_SET_CONTROL_LINE_STATE  0x22u
#define CDC_SEND_BREAK              0x23u
#define CDC_COMM_INTERFACE          0u
#define CDC_LINE_DTR                0x01u

/* Configuration: CDC ACM, interface 0 = communication (+ notification
 * endpoint), interface 1 = data. Interface protocol 0 ("no specific
 * protocol"): the port is not an AT modem, so Linux ModemManager leaves it
 * alone; Windows (usbser), Linux (cdc_acm) and macOS bind on class 2/2. */
#define CONFIG_DESC_LEN     67u
static const uint8_t s_config_desc[CONFIG_DESC_LEN] = {
    9, 2, CONFIG_DESC_LEN, 0, 2, 1, 0, 0x80, 50,        /* 2 interfaces, bus powered 100 mA */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,                 /* interface 0: CDC comm, ACM      */
    5, 0x24, 0x00, 0x10, 0x01,                          /* header, CDC 1.10                */
    5, 0x24, 0x01, 0x00, 1,                             /* call management: data if 1      */
    4, 0x24, 0x02, 0x02,                                /* ACM: line coding + ctrl lines   */
    5, 0x24, 0x06, 0, 1,                                /* union: master 0, slave 1        */
    7, 5, EP_NOTIFY, 0x03, NOTIFY_MPS, 0, 16,           /* interrupt IN, 16 ms             */
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,                 /* interface 1: CDC data           */
    7, 5, EP_DATA_OUT, 0x02, DATA_MPS, 0, 0,            /* bulk OUT                        */
    7, 5, EP_DATA_IN, 0x02, DATA_MPS, 0, 0,             /* bulk IN                         */
};

typedef enum {
    EP0_IDLE = 0,
    EP0_DATA_IN,            /* sending the reply of a device-to-host request */
    EP0_STATUS_OUT,         /* waiting for the host's zero-length OUT        */
    EP0_DATA_OUT,           /* receiving host-to-device data                 */
    EP0_STATUS_IN,          /* our zero-length IN is queued                  */
} ep0_state_t;

static struct {
    ep0_state_t    state;
    uint8_t        bm;
    uint8_t        request;
    uint16_t       value;
    uint16_t       index;
    uint16_t       length;
    const uint8_t *data;
    uint16_t       len;
    uint16_t       sent;
    bool           last_full;
    uint16_t       got;
    uint8_t        buf[96];         /* generated replies, received OUT data */
} s_ep0;

static usb_cdc_config_t s_cfg;
static uint8_t          s_dev_desc[18];
static uint8_t          s_line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0u, 0u, 8u };  /* 115200 8N1 */
static uint8_t          s_config_value;
static volatile bool    s_configured;
static volatile bool    s_suspended;
static volatile uint8_t s_line_state;
static volatile uint32_t s_resets;
static volatile uint32_t s_dropped;

/* rings: RX produced by the event context, TX consumed by it */
static uint8_t           s_rx[USB_CDC_RX_SIZE];
static volatile uint32_t s_rx_head;         /* event context */
static volatile uint32_t s_rx_tail;         /* thread        */
static volatile bool     s_rx_paused;       /* OUT endpoint left NAKing (ring full) */

static uint8_t           s_tx[USB_CDC_TX_SIZE];
static volatile uint32_t s_tx_head;         /* thread        */
static volatile uint32_t s_tx_tail;         /* event context */
static volatile bool     s_tx_busy;         /* a packet is queued on EP 0x81 */
static volatile bool     s_tx_last_full;    /* last packet was 64 bytes: ZLP owed */

/* connection (thread) */
static bool     s_vbus_raw;
static bool     s_vbus;
static uint32_t s_vbus_t;
static bool     s_connected;
static bool     s_detached;

/* ------------------------------------------------------------------------ */
/* rings                                                                    */
/* ------------------------------------------------------------------------ */
static uint32_t rx_free(void)
{
    return (s_rx_tail - s_rx_head - 1u) & (USB_CDC_RX_SIZE - 1u);
}

size_t usb_cdc_tx_free(void)
{
    return (size_t)((s_tx_tail - s_tx_head - 1u) & (USB_CDC_TX_SIZE - 1u));
}

/* event context (or locked): drop everything queued for the host */
static void tx_discard(void)
{
    s_tx_tail = s_tx_head;
    s_tx_last_full = false;
}

/* event context: start the next IN packet when the endpoint is free */
static void tx_service(void)
{
    if (!s_configured || s_tx_busy) {
        return;
    }
    uint32_t tail = s_tx_tail;
    uint32_t avail = (s_tx_head - tail) & (USB_CDC_TX_SIZE - 1u);
    MEM_BARRIER();
    if (avail == 0u) {
        /* a transfer that ended on a full packet needs a ZLP, otherwise the
         * host keeps waiting for the rest of its read buffer */
        if (s_tx_last_full && usbhw_ep_write(DATA_EP, NULL, 0u)) {
            s_tx_busy = true;
            s_tx_last_full = false;
        }
        return;
    }
    uint8_t pkt[DATA_MPS];
    uint32_t n = (avail > DATA_MPS) ? DATA_MPS : avail;
    uint32_t first = USB_CDC_TX_SIZE - tail;
    if (first > n) {
        first = n;
    }
    memcpy(pkt, &s_tx[tail], first);
    if (n > first) {
        memcpy(&pkt[first], &s_tx[0], n - first);
    }
    if (usbhw_ep_write(DATA_EP, pkt, (uint16_t)n)) {
        s_tx_tail = (tail + n) & (USB_CDC_TX_SIZE - 1u);
        s_tx_busy = true;
        s_tx_last_full = (n == DATA_MPS);
    }
}

/* event context: accept the next OUT packet only with room for a full one */
static void rx_arm(void)
{
    if (!s_configured) {
        return;
    }
    if (rx_free() >= DATA_MPS) {
        s_rx_paused = false;
        usbhw_ep_out_arm(DATA_EP);
    } else {
        s_rx_paused = true;
    }
}

static void service(void)
{
    tx_service();
    if (s_rx_paused) {
        rx_arm();
    }
}

/* event context (or locked): forget the host */
static void session_end(void)
{
    s_configured = false;
    s_config_value = 0u;
    s_line_state = 0u;
    s_tx_busy = false;
    s_rx_paused = false;
    tx_discard();                           /* RX keeps its bytes: the framing resyncs */
    s_ep0.state = EP0_IDLE;
}

/* ------------------------------------------------------------------------ */
/* descriptors                                                              */
/* ------------------------------------------------------------------------ */
static void build_device_desc(void)
{
    const uint8_t d[18] = {
        18, DESC_DEVICE, 0x00, 0x02,            /* USB 2.0 (full speed)          */
        0x02, 0x02, 0x00,                       /* CDC at device level (ACM)     */
        USB_EP0_MPS,
        (uint8_t)s_cfg.vid, (uint8_t)(s_cfg.vid >> 8),
        (uint8_t)s_cfg.pid, (uint8_t)(s_cfg.pid >> 8),
        (uint8_t)s_cfg.bcd_device, (uint8_t)(s_cfg.bcd_device >> 8),
        1, 2, 3,                                /* manufacturer, product, serial */
        1,
    };
    memcpy(s_dev_desc, d, sizeof(d));
}

/* UTF-16LE string descriptor into s_ep0.buf; 0 if there is none */
static uint16_t string_desc(uint8_t index)
{
    uint8_t *out = s_ep0.buf;
    if (index == 0u) {
        out[0] = 4u;
        out[1] = DESC_STRING;
        out[2] = 0x09u;                         /* LANGID en-US */
        out[3] = 0x04u;
        return 4u;
    }
    const char *s = (index == 1u) ? s_cfg.manufacturer
                  : (index == 2u) ? s_cfg.product
                  : (index == 3u) ? s_cfg.serial : NULL;
    if (s == NULL) {
        return 0u;
    }
    uint16_t n = 0u;
    while (s[n] != '\0' && 2u + 2u * (n + 1u) <= sizeof(s_ep0.buf)) {
        out[2u + 2u * n] = (uint8_t)s[n];
        out[3u + 2u * n] = 0u;
        n++;
    }
    out[0] = (uint8_t)(2u + 2u * n);
    out[1] = DESC_STRING;
    return (uint16_t)(2u + 2u * n);
}

/* ------------------------------------------------------------------------ */
/* control endpoint                                                         */
/* ------------------------------------------------------------------------ */
static void ep0_stall(void)
{
    usbhw_ep_stall(0u, true);
    s_ep0.state = EP0_IDLE;
}

static void ep0_status_in(void)
{
    (void)usbhw_ep_write(0u, NULL, 0u);
    s_ep0.state = EP0_STATUS_IN;
}

static void ep0_in_next(void)
{
    uint16_t left = (uint16_t)(s_ep0.len - s_ep0.sent);
    uint16_t n = (left > USB_EP0_MPS) ? USB_EP0_MPS : left;
    (void)usbhw_ep_write(0u, s_ep0.data + s_ep0.sent, n);
    s_ep0.sent = (uint16_t)(s_ep0.sent + n);
    s_ep0.last_full = (n == USB_EP0_MPS);
}

static void ep0_reply(const uint8_t *data, uint16_t len)
{
    s_ep0.data = data;
    s_ep0.len = (len > s_ep0.length) ? s_ep0.length : len;
    s_ep0.sent = 0u;
    s_ep0.state = EP0_DATA_IN;
    ep0_in_next();
}

static void set_configuration(uint8_t value)
{
    usbhw_ep_close_all();
    session_end();
    s_line_state = 0u;
    if (value == 0u) {
        return;
    }
    s_config_value = value;
    usbhw_ep_open(EP_NOTIFY, USB_EPTYPE_INTERRUPT, NOTIFY_MPS);
    usbhw_ep_open(EP_DATA_IN, USB_EPTYPE_BULK, DATA_MPS);
    usbhw_ep_open(EP_DATA_OUT, USB_EPTYPE_BULK, DATA_MPS);
    s_configured = true;
    rx_arm();
}

static bool ep_valid(uint8_t ep)
{
    return ep == EP_DATA_IN || ep == EP_DATA_OUT || ep == EP_NOTIFY;
}

/* standard requests; returns false to STALL */
static bool standard_request(void)
{
    uint8_t rcpt = s_ep0.bm & REQ_RCPT_MASK;
    bool in = (s_ep0.bm & REQ_DIR_IN) != 0u;
    uint8_t *b = s_ep0.buf;

    switch (s_ep0.request) {
    case GET_STATUS:
        if (!in) {
            return false;
        }
        b[0] = 0u;
        b[1] = 0u;
        if (rcpt == REQ_RCPT_ENDPOINT) {
            uint8_t ep = (uint8_t)s_ep0.index;
            if (ep != 0u && ep != 0x80u && !ep_valid(ep)) {
                return false;
            }
            b[0] = usbhw_ep_stalled(ep) ? 1u : 0u;
        }
        ep0_reply(b, 2u);
        return true;

    case CLEAR_FEATURE:
    case SET_FEATURE:
        if (rcpt == REQ_RCPT_DEVICE && s_ep0.value == FEAT_REMOTE_WAKEUP) {
            ep0_status_in();                    /* accepted, never used */
            return true;
        }
        if (rcpt == REQ_RCPT_ENDPOINT && s_ep0.value == FEAT_ENDPOINT_HALT) {
            uint8_t ep = (uint8_t)s_ep0.index;
            if ((ep & 0x7Fu) == 0u) {
                ep0_status_in();
                return true;
            }
            if (!ep_valid(ep) || !s_configured) {
                return false;
            }
            usbhw_ep_stall(ep, s_ep0.request == SET_FEATURE);
            ep0_status_in();
            return true;
        }
        return false;

    case SET_ADDRESS:
        if (rcpt != REQ_RCPT_DEVICE || s_ep0.value > 127u) {
            return false;
        }
        usbhw_set_address((uint8_t)s_ep0.value);
        ep0_status_in();
        return true;

    case GET_DESCRIPTOR: {
        if (!in) {
            return false;
        }
        uint8_t type = (uint8_t)(s_ep0.value >> 8);
        uint8_t idx = (uint8_t)s_ep0.value;
        if (type == DESC_DEVICE) {
            ep0_reply(s_dev_desc, sizeof(s_dev_desc));
            return true;
        }
        if (type == DESC_CONFIGURATION && idx == 0u) {
            ep0_reply(s_config_desc, sizeof(s_config_desc));
            return true;
        }
        if (type == DESC_STRING) {
            uint16_t n = string_desc(idx);
            if (n == 0u) {
                return false;
            }
            ep0_reply(s_ep0.buf, n);
            return true;
        }
        return false;                           /* device qualifier etc.: FS only */
    }

    case GET_CONFIGURATION:
        if (!in) {
            return false;
        }
        b[0] = s_config_value;
        ep0_reply(b, 1u);
        return true;

    case SET_CONFIGURATION:
        if (rcpt != REQ_RCPT_DEVICE || s_ep0.value > 1u) {
            return false;
        }
        set_configuration((uint8_t)s_ep0.value);
        ep0_status_in();
        return true;

    case GET_INTERFACE:
        if (!in || !s_configured || s_ep0.index > 1u) {
            return false;
        }
        b[0] = 0u;
        ep0_reply(b, 1u);
        return true;

    case SET_INTERFACE:
        if (!s_configured || s_ep0.index > 1u || s_ep0.value != 0u) {
            return false;
        }
        ep0_status_in();
        return true;

    default:
        return false;
    }
}

static void line_state_changed(uint8_t state)
{
    if ((s_line_state & CDC_LINE_DTR) != 0u && (state & CDC_LINE_DTR) == 0u) {
        tx_discard();                           /* port closed: nothing stale next time */
    }
    s_line_state = state;
}

/* CDC requests to the communication interface; returns false to STALL */
static bool class_request(void)
{
    if ((s_ep0.bm & REQ_RCPT_MASK) != REQ_RCPT_INTERFACE || s_ep0.index != CDC_COMM_INTERFACE) {
        return false;
    }
    switch (s_ep0.request) {
    case CDC_SET_LINE_CODING:
        if (s_ep0.length != sizeof(s_line_coding)) {
            return false;
        }
        s_ep0.got = 0u;
        s_ep0.state = EP0_DATA_OUT;             /* data stage follows */
        return true;
    case CDC_GET_LINE_CODING:
        ep0_reply(s_line_coding, sizeof(s_line_coding));
        return true;
    case CDC_SET_CONTROL_LINE_STATE:
        line_state_changed((uint8_t)(s_ep0.value & 0x03u));
        ep0_status_in();
        return true;
    case CDC_SEND_BREAK:
        ep0_status_in();
        return true;
    default:
        return false;
    }
}

static void ev_setup(const uint8_t p[8])
{
    s_ep0.bm = p[0];
    s_ep0.request = p[1];
    s_ep0.value = (uint16_t)(p[2] | (p[3] << 8));
    s_ep0.index = (uint16_t)(p[4] | (p[5] << 8));
    s_ep0.length = (uint16_t)(p[6] | (p[7] << 8));
    s_ep0.state = EP0_IDLE;

    bool in = (s_ep0.bm & REQ_DIR_IN) != 0u;
    bool ok;
    switch (s_ep0.bm & REQ_TYPE_MASK) {
    case REQ_TYPE_STANDARD: ok = standard_request(); break;
    case REQ_TYPE_CLASS:    ok = class_request(); break;
    default:                ok = false; break;
    }
    if (!ok) {
        ep0_stall();
        return;
    }
    /* a host-to-device request with data must have been accepted as such */
    if (!in && s_ep0.length > 0u && s_ep0.state != EP0_DATA_OUT) {
        ep0_stall();
    }
}

static void ep0_out(const uint8_t *data, uint16_t len)
{
    if (s_ep0.state == EP0_DATA_OUT) {
        uint16_t room = (uint16_t)(sizeof(s_ep0.buf) - s_ep0.got);
        uint16_t n = (len > room) ? room : len;
        memcpy(&s_ep0.buf[s_ep0.got], data, n);
        s_ep0.got = (uint16_t)(s_ep0.got + n);
        if (s_ep0.got < s_ep0.length && len == USB_EP0_MPS) {
            return;                             /* more packets follow */
        }
        if (s_ep0.request == CDC_SET_LINE_CODING && s_ep0.got >= sizeof(s_line_coding)) {
            memcpy(s_line_coding, s_ep0.buf, sizeof(s_line_coding));
        }
        ep0_status_in();
        return;
    }
    /* status stage of a control read (or a stray packet): transfer over */
    s_ep0.state = EP0_IDLE;
}

static void ep0_in_done(void)
{
    if (s_ep0.state == EP0_DATA_IN) {
        if (s_ep0.sent < s_ep0.len) {
            ep0_in_next();
        } else if (s_ep0.last_full && s_ep0.len < s_ep0.length) {
            ep0_in_next();                      /* short reply ending on 64: ZLP */
        } else {
            s_ep0.state = EP0_STATUS_OUT;
        }
    } else if (s_ep0.state == EP0_STATUS_IN) {
        s_ep0.state = EP0_IDLE;
    }
}

/* ------------------------------------------------------------------------ */
/* controller events                                                        */
/* ------------------------------------------------------------------------ */
static void ev_reset(void)
{
    session_end();
    s_suspended = false;
    s_resets++;
}

static void ev_out(uint8_t ep, const uint8_t *data, uint16_t len)
{
    if (ep == 0u) {
        ep0_out(data, len);
        return;
    }
    if (ep != DATA_EP || !s_configured) {
        return;
    }
    /* the endpoint was armed with room for a full packet */
    uint32_t head = s_rx_head;
    uint32_t room = rx_free();
    uint32_t n = (len > room) ? room : len;
    uint32_t first = USB_CDC_RX_SIZE - head;
    if (first > n) {
        first = n;
    }
    memcpy(&s_rx[head], data, first);
    if (n > first) {
        memcpy(&s_rx[0], data + first, n - first);
    }
    MEM_BARRIER();
    s_rx_head = (head + n) & (USB_CDC_RX_SIZE - 1u);
    rx_arm();
}

static void ev_in_done(uint8_t ep)
{
    if (ep == 0u) {
        ep0_in_done();
    } else if (ep == DATA_EP) {
        s_tx_busy = false;
        tx_service();
    }
}

static void ev_suspend(bool suspended)
{
    s_suspended = suspended;
}

static const usbhw_events_t s_events = {
    .reset = ev_reset,
    .setup = ev_setup,
    .out = ev_out,
    .in_done = ev_in_done,
    .suspend = ev_suspend,
};

/* ------------------------------------------------------------------------ */
/* public API                                                               */
/* ------------------------------------------------------------------------ */
void usb_cdc_init(const usb_cdc_config_t *cfg)
{
    s_cfg = *cfg;
    memset(&s_ep0, 0, sizeof(s_ep0));
    build_device_desc();
    s_rx_head = s_rx_tail = 0u;
    s_tx_head = s_tx_tail = 0u;
    s_configured = false;
    s_suspended = false;
    s_line_state = 0u;
    s_config_value = 0u;
    s_tx_busy = false;
    s_tx_last_full = false;
    s_rx_paused = false;
    s_resets = 0u;
    s_dropped = 0u;
    s_vbus_raw = false;
    s_vbus = false;
    s_vbus_t = 0u;
    s_connected = false;
    s_detached = false;
    usbhw_init(&s_events, cfg->use_irq);
}

void usb_cdc_irq(void)
{
    usbhw_poll();
    service();
}

void usb_cdc_task(uint32_t now_ms)
{
    bool v = usbhw_vbus();
    if (v != s_vbus_raw) {
        s_vbus_raw = v;
        s_vbus_t = now_ms;
    } else if (v != s_vbus && (now_ms - s_vbus_t) >= VBUS_DEBOUNCE_MS) {
        s_vbus = v;
        bool want = v && !s_detached;
        if (want != s_connected) {
            usbhw_lock();
            if (!want) {
                session_end();                  /* cable pulled: host gone */
            }
            usbhw_connect(want);
            s_connected = want;
            usbhw_unlock();
        }
    }
    if (!s_cfg.use_irq) {
        usb_cdc_irq();
    }
}

size_t usb_cdc_read(uint8_t *dst, size_t max)
{
    uint32_t head = s_rx_head;
    MEM_BARRIER();
    uint32_t tail = s_rx_tail;
    size_t n = 0u;
    while (tail != head && n < max) {
        dst[n++] = s_rx[tail];
        tail = (tail + 1u) & (USB_CDC_RX_SIZE - 1u);
    }
    MEM_BARRIER();
    s_rx_tail = tail;
    if (n > 0u && s_rx_paused) {
        usbhw_kick();                           /* room again: re-arm the OUT endpoint */
    }
    return n;
}

size_t usb_cdc_write(const void *src, size_t len)
{
    if (len == 0u) {
        return 0u;
    }
    if (!s_configured) {
        s_dropped += (uint32_t)len;             /* nobody listening */
        return len;
    }
    if (len > usb_cdc_tx_free()) {
        return 0u;
    }
    const uint8_t *p = (const uint8_t *)src;
    uint32_t head = s_tx_head;
    size_t first = USB_CDC_TX_SIZE - head;
    if (first > len) {
        first = len;
    }
    memcpy(&s_tx[head], p, first);
    if (len > first) {
        memcpy(&s_tx[0], p + first, len - first);
    }
    MEM_BARRIER();
    s_tx_head = (head + (uint32_t)len) & (USB_CDC_TX_SIZE - 1u);
    usbhw_kick();
    return len;
}

bool usb_cdc_tx_idle(void)
{
    if (!s_configured) {
        return true;
    }
    return s_tx_head == s_tx_tail && !s_tx_busy && !s_tx_last_full;
}

void usb_cdc_detach(void)
{
    usbhw_lock();
    s_detached = true;
    session_end();
    usbhw_connect(false);
    s_connected = false;
    usbhw_unlock();
}

void usb_cdc_state(usb_cdc_state_t *st)
{
    st->vbus = s_vbus;
    st->connected = s_connected;
    st->configured = s_configured;
    st->suspended = s_suspended;
    st->dtr = (s_line_state & CDC_LINE_DTR) != 0u;
    st->baud = (uint32_t)s_line_coding[0] | ((uint32_t)s_line_coding[1] << 8) |
               ((uint32_t)s_line_coding[2] << 16) | ((uint32_t)s_line_coding[3] << 24);
    st->resets = s_resets;
    st->dropped = s_dropped;
}

bool usb_cdc_vbus(void)
{
    return s_vbus;
}

bool usb_cdc_configured(void)
{
    return s_configured;
}
