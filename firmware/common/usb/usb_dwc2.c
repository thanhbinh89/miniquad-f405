/**
 * @file    usb_dwc2.c
 * @brief   USB device controller layer for the STM32F405 OTG_FS core
 *          (Synopsys DWC2, "slave" mode - the FS core has no DMA).
 *
 *  - Receive: one shared RX FIFO. Every SETUP / OUT packet is announced by
 *    a status word popped from GRXSTSP and read word by word from FIFO 0;
 *    "SETUP done" and "OUT transfer completed" are status words too.
 *  - Transmit: one TX FIFO per IN endpoint. A packet is queued by
 *    programming DIEPTSIZ (1 packet, n bytes), enabling the endpoint and
 *    writing the words into the endpoint's FIFO window; DIEPINT.XFRC tells
 *    when the host ACKed it.
 *  - VBUS: the core's comparator is disabled (NOVBUSSENS), the board feeds
 *    PA9 with VBUS/2 and reads it as a GPIO; soft connect with DCTL.SDIS.
 *
 * Reference: RM0090, "USB on-the-go full-speed (OTG_FS)", device-mode
 * programming model (core initialisation, endpoint initialisation on USB
 * reset / enumeration done, OUT and IN data transfers in slave mode).
 */
#include "usb_hw.h"

#include <string.h>
#include "board.h"
#include "clock.h"
#include "gpio.h"
#include "stm32f4xx.h"

#if !BOARD_LINK_USB
#error "usb_dwc2.c is only built for board revisions with the USB-C port"
#endif

#define OTG             USB_OTG_FS
#define OTG_DEV         ((USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))
#define OTG_IN(n)       ((USB_OTG_INEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_IN_ENDPOINT_BASE + \
                                                       (uint32_t)(n) * USB_OTG_EP_REG_SIZE))
#define OTG_OUT(n)      ((USB_OTG_OUTEndpointTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_OUT_ENDPOINT_BASE + \
                                                        (uint32_t)(n) * USB_OTG_EP_REG_SIZE))
#define OTG_PCGCCTL     (*(volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_PCGCCTL_BASE))
#define OTG_FIFO(n)     (*(volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_FIFO_BASE + \
                                                (uint32_t)(n) * USB_OTG_FIFO_SIZE))

/* FIFO RAM of the FS core: 1.25 KB = 320 words */
#define FIFO_WORDS          320u
#define RX_FIFO_WORDS       128u    /* SETUPs + several 64-byte OUT packets      */
#define TX0_FIFO_WORDS      32u     /* EP0 IN, one 64-byte packet at a time      */
#define TX1_FIFO_WORDS      128u    /* CDC data IN                               */
#define TX2_FIFO_WORDS      16u     /* CDC notification IN                       */
#define TX3_FIFO_WORDS      16u     /* unused, keeps the map well defined        */
_Static_assert(RX_FIFO_WORDS + TX0_FIFO_WORDS + TX1_FIFO_WORDS + TX2_FIFO_WORDS + TX3_FIFO_WORDS <= FIFO_WORDS,
               "OTG_FS FIFO RAM is 320 words");

#define GRXSTS_EPNUM(s)     ((s) & 0xFu)
#define GRXSTS_BCNT(s)      (((s) >> 4) & 0x7FFu)
#define GRXSTS_PKTSTS(s)    (((s) >> 17) & 0xFu)
#define PKTSTS_OUT_DATA     2u      /* OUT data packet, BCNT bytes follow        */
#define PKTSTS_OUT_DONE     3u      /* OUT transfer completed                    */
#define PKTSTS_SETUP_DONE   4u      /* SETUP stage done, data/status stage began */
#define PKTSTS_SETUP_DATA   6u      /* SETUP packet, 8 bytes follow              */

#define TSIZ_PKTCNT_1       (1u << 19)
#define TSIZ0_STUPCNT_3     (3u << 29)
#define TRDT_AHB_GE_32MHZ   6u      /* RM0090 "TRDT values": AHB >= 32 MHz       */
#define DSPD_FULL_SPEED     3u      /* internal full-speed PHY                   */
#define TXFNUM_ALL          0x10u
/* bounded busy waits: these run in the OTG interrupt too (priority above the
 * flight loop), so a misbehaving core must not stall the CPU for long */
#define WAIT_LOOPS          20000u  /* core reset / FIFO flush: < 1 ms           */
#define EP_WAIT_LOOPS       2000u   /* endpoint NAK / disable effective: < 0.1 ms */
#define MAX_PACKET          64u

static const usbhw_events_t *s_ev;
static bool     s_irq;
static uint32_t s_setup[2];                                 /* last SETUP packet  */
static uint32_t s_out_buf[USB_HW_EP_COUNT][MAX_PACKET / 4u];
static uint16_t s_out_len[USB_HW_EP_COUNT];
static uint16_t s_out_mps[USB_HW_EP_COUNT];

/* ------------------------------------------------------------------------ */
static bool wait_clear(volatile uint32_t *reg, uint32_t mask)
{
    for (uint32_t i = 0u; i < WAIT_LOOPS; i++) {
        if ((*reg & mask) == 0u) {
            return true;
        }
    }
    return false;
}

static bool wait_set_n(volatile uint32_t *reg, uint32_t mask, uint32_t loops)
{
    for (uint32_t i = 0u; i < loops; i++) {
        if ((*reg & mask) != 0u) {
            return true;
        }
    }
    return false;
}

static bool wait_set(volatile uint32_t *reg, uint32_t mask)
{
    return wait_set_n(reg, mask, WAIT_LOOPS);
}

static void flush_tx(uint32_t fifo)
{
    (void)wait_set(&OTG->GRSTCTL, USB_OTG_GRSTCTL_AHBIDL);
    OTG->GRSTCTL = USB_OTG_GRSTCTL_TXFFLSH | (fifo << USB_OTG_GRSTCTL_TXFNUM_Pos);
    (void)wait_clear(&OTG->GRSTCTL, USB_OTG_GRSTCTL_TXFFLSH);
}

static void flush_rx(void)
{
    (void)wait_set(&OTG->GRSTCTL, USB_OTG_GRSTCTL_AHBIDL);
    OTG->GRSTCTL = USB_OTG_GRSTCTL_RXFFLSH;
    (void)wait_clear(&OTG->GRSTCTL, USB_OTG_GRSTCTL_RXFFLSH);
}

/* Pop @p bytes from the RX FIFO, keeping at most @p cap_words words. */
static void fifo_read(uint32_t *dst, uint32_t bytes, uint32_t cap_words)
{
    uint32_t words = (bytes + 3u) / 4u;
    for (uint32_t i = 0u; i < words; i++) {
        uint32_t w = OTG_FIFO(0);
        if (dst != NULL && i < cap_words) {
            dst[i] = w;
        }
    }
}

/* Drop whatever IN endpoint @p n still has queued (RM0090 "IN endpoint
 * disable": NAK, wait NAK effective, disable, wait disabled, flush). Used
 * when the host abandons a control transfer or the endpoint is closed. */
static void ep_in_abort(uint32_t n)
{
    USB_OTG_INEndpointTypeDef *in = OTG_IN(n);
    if ((in->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0u) {
        in->DIEPCTL |= USB_OTG_DIEPCTL_SNAK;
        (void)wait_set_n(&in->DIEPINT, USB_OTG_DIEPINT_INEPNE, EP_WAIT_LOOPS);
        in->DIEPCTL |= USB_OTG_DIEPCTL_EPDIS | USB_OTG_DIEPCTL_SNAK;
        (void)wait_set_n(&in->DIEPINT, USB_OTG_DIEPINT_EPDISD, EP_WAIT_LOOPS);
    }
    flush_tx(n);
    in->DIEPTSIZ = 0u;
    in->DIEPINT = 0xFFu;
}

/* EP0 OUT always accepts the next SETUP / data / status packet. */
static void ep0_out_arm(void)
{
    OTG_OUT(0)->DOEPTSIZ = TSIZ0_STUPCNT_3 | TSIZ_PKTCNT_1 | USB_EP0_MPS;
    OTG_OUT(0)->DOEPCTL |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
}

/* ------------------------------------------------------------------------ */
void usbhw_init(const usbhw_events_t *ev, bool use_irq)
{
    s_ev = ev;
    s_irq = use_irq;
    memset(s_out_len, 0, sizeof(s_out_len));
    memset(s_out_mps, 0, sizeof(s_out_mps));

    /* pins: D-/D+ AF10, VBUS/2 as input (external divider), PA10 unused */
    gpio_clock_enable(GPIOA);
    gpio_alternate(USB_DM_PORT, USB_DM_PIN, USB_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_alternate(USB_DP_PORT, USB_DP_PIN, USB_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_input(USB_VBUS_PORT, USB_VBUS_PIN, GPIO_PULL_NONE);
    gpio_set_mode(USB_NC_PORT, USB_NC_PIN, GPIO_MODE_ANALOG);

    /* clock (PLL Q = 48 MHz) + peripheral reset: same state after a jump */
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;

    /* core soft reset once the AHB master is idle */
    (void)wait_set(&OTG->GRSTCTL, USB_OTG_GRSTCTL_AHBIDL);
    OTG->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    (void)wait_clear(&OTG->GRSTCTL, USB_OTG_GRSTCTL_CSRST);
    clock_delay_us(3u);

    /* full-speed serial transceiver, forced device mode (no ID pin) */
    OTG->GUSBCFG = (OTG->GUSBCFG & ~(USB_OTG_GUSBCFG_TRDT | USB_OTG_GUSBCFG_FHMOD))
                 | USB_OTG_GUSBCFG_PHYSEL | USB_OTG_GUSBCFG_FDMOD
                 | (TRDT_AHB_GE_32MHZ << USB_OTG_GUSBCFG_TRDT_Pos);
    clock_delay_us(25000u);                         /* forced mode takes effect */

    /* stay disconnected until VBUS is seen, then usbhw_connect() */
    OTG_DEV->DCTL |= USB_OTG_DCTL_SDIS;
    OTG_DEV->DCFG = (OTG_DEV->DCFG & ~(USB_OTG_DCFG_DSPD | USB_OTG_DCFG_DAD | USB_OTG_DCFG_PFIVL))
                  | (DSPD_FULL_SPEED << USB_OTG_DCFG_DSPD_Pos);
    /* PHY on, VBUS comparator off: "VBUS always valid" */
    OTG->GCCFG = USB_OTG_GCCFG_PWRDWN | USB_OTG_GCCFG_NOVBUSSENS;
    OTG_PCGCCTL = 0u;

    /* FIFO RAM map (words): RX | TX0 | TX1 | TX2 | TX3 */
    OTG->GRXFSIZ = RX_FIFO_WORDS;
    OTG->DIEPTXF0_HNPTXFSIZ = (TX0_FIFO_WORDS << 16) | RX_FIFO_WORDS;
    OTG->DIEPTXF[0] = (TX1_FIFO_WORDS << 16) | (RX_FIFO_WORDS + TX0_FIFO_WORDS);
    OTG->DIEPTXF[1] = (TX2_FIFO_WORDS << 16) | (RX_FIFO_WORDS + TX0_FIFO_WORDS + TX1_FIFO_WORDS);
    OTG->DIEPTXF[2] = (TX3_FIFO_WORDS << 16) |
                      (RX_FIFO_WORDS + TX0_FIFO_WORDS + TX1_FIFO_WORDS + TX2_FIFO_WORDS);
    flush_tx(TXFNUM_ALL);
    flush_rx();

    /* endpoints idle; IN transfer-complete is the only endpoint interrupt */
    for (uint32_t ep = 0u; ep < USB_HW_EP_COUNT; ep++) {
        OTG_IN(ep)->DIEPCTL = 0u;
        OTG_IN(ep)->DIEPTSIZ = 0u;
        OTG_IN(ep)->DIEPINT = 0xFFu;
        OTG_OUT(ep)->DOEPCTL = 0u;
        OTG_OUT(ep)->DOEPTSIZ = 0u;
        OTG_OUT(ep)->DOEPINT = 0xFFu;
    }
    OTG_DEV->DIEPMSK = USB_OTG_DIEPMSK_XFRCM;
    OTG_DEV->DOEPMSK = 0u;
    OTG_DEV->DAINTMSK = 0x0Fu;                      /* IN EP0..3 */

    OTG->GINTSTS = 0xBFFFFFFFu;
    OTG->GINTMSK = USB_OTG_GINTMSK_USBRST | USB_OTG_GINTMSK_ENUMDNEM | USB_OTG_GINTMSK_RXFLVLM |
                   USB_OTG_GINTMSK_IEPINT | USB_OTG_GINTMSK_USBSUSPM | USB_OTG_GINTMSK_WUIM;
    if (use_irq) {
        OTG->GAHBCFG = USB_OTG_GAHBCFG_GINT;
        NVIC_SetPriority(USB_IRQn, IRQ_PRIO_LINK);
        NVIC_ClearPendingIRQ(USB_IRQn);
        NVIC_EnableIRQ(USB_IRQn);
    } else {
        OTG->GAHBCFG = 0u;                          /* status bits are polled */
    }
}

void usbhw_deinit(void)
{
    if (s_irq) {
        NVIC_DisableIRQ(USB_IRQn);
    }
    OTG_DEV->DCTL |= USB_OTG_DCTL_SDIS;
    OTG->GAHBCFG = 0u;
    OTG->GCCFG = 0u;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
    RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
    RCC->AHB2ENR &= ~RCC_AHB2ENR_OTGFSEN;
}

void usbhw_connect(bool on)
{
    if (on) {
        OTG_DEV->DCTL &= ~USB_OTG_DCTL_SDIS;
    } else {
        OTG_DEV->DCTL |= USB_OTG_DCTL_SDIS;
    }
}

bool usbhw_vbus(void)
{
    return gpio_read(USB_VBUS_PORT, USB_VBUS_PIN);
}

void usbhw_kick(void)
{
    if (s_irq) {
        NVIC_SetPendingIRQ(USB_IRQn);
    }
}

void usbhw_lock(void)
{
    if (s_irq) {
        NVIC_DisableIRQ(USB_IRQn);
        __DSB();
        __ISB();
    }
}

void usbhw_unlock(void)
{
    if (s_irq) {
        NVIC_EnableIRQ(USB_IRQn);
    }
}

void usbhw_set_address(uint8_t addr)
{
    /* DWC2 takes the new address before the status stage of SET_ADDRESS */
    OTG_DEV->DCFG = (OTG_DEV->DCFG & ~USB_OTG_DCFG_DAD) | ((uint32_t)(addr & 0x7Fu) << 4);
}

/* ------------------------------------------------------------------------ */
/* endpoints                                                                */
/* ------------------------------------------------------------------------ */
void usbhw_ep_open(uint8_t ep, uint8_t type, uint16_t mps)
{
    uint32_t n = ep & 0x7Fu;
    if (n == 0u || n >= USB_HW_EP_COUNT || mps > MAX_PACKET) {
        return;
    }
    if ((ep & USB_EP_DIR_IN) != 0u) {
        OTG_IN(n)->DIEPINT = 0xFFu;
        OTG_IN(n)->DIEPCTL = USB_OTG_DIEPCTL_USBAEP | ((uint32_t)type << USB_OTG_DIEPCTL_EPTYP_Pos) |
                             (n << USB_OTG_DIEPCTL_TXFNUM_Pos) | USB_OTG_DIEPCTL_SD0PID_SEVNFRM |
                             USB_OTG_DIEPCTL_SNAK | mps;
    } else {
        s_out_mps[n] = mps;
        s_out_len[n] = 0u;
        OTG_OUT(n)->DOEPINT = 0xFFu;
        OTG_OUT(n)->DOEPCTL = USB_OTG_DOEPCTL_USBAEP | ((uint32_t)type << USB_OTG_DOEPCTL_EPTYP_Pos) |
                              USB_OTG_DOEPCTL_SD0PID_SEVNFRM | USB_OTG_DOEPCTL_SNAK | mps;
    }
}

void usbhw_ep_close_all(void)
{
    for (uint32_t n = 1u; n < USB_HW_EP_COUNT; n++) {
        ep_in_abort(n);
        OTG_IN(n)->DIEPCTL &= ~(USB_OTG_DIEPCTL_USBAEP | USB_OTG_DIEPCTL_STALL);
        /* OUT: best-effort disable as ST's HAL does (a clean disable needs the
         * global OUT NAK handshake through the RX FIFO we are popping from);
         * usbhw_ep_out_arm() copes with an endpoint that stayed enabled */
        if ((OTG_OUT(n)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) != 0u) {
            OTG_OUT(n)->DOEPCTL |= USB_OTG_DOEPCTL_SNAK | USB_OTG_DOEPCTL_EPDIS;
        }
        OTG_OUT(n)->DOEPCTL &= ~(USB_OTG_DOEPCTL_USBAEP | USB_OTG_DOEPCTL_STALL);
        OTG_OUT(n)->DOEPINT = 0xFFu;
        s_out_mps[n] = 0u;
        s_out_len[n] = 0u;
    }
}

bool usbhw_ep_write(uint8_t ep, const void *data, uint16_t len)
{
    uint32_t n = ep & 0x7Fu;
    if (n >= USB_HW_EP_COUNT || len > MAX_PACKET) {
        return false;
    }
    USB_OTG_INEndpointTypeDef *in = OTG_IN(n);
    if ((in->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0u || (in->DIEPTSIZ & USB_OTG_DIEPTSIZ_PKTCNT) != 0u) {
        return false;                               /* previous packet still queued */
    }
    uint32_t words = ((uint32_t)len + 3u) / 4u;
    uint32_t buf[MAX_PACKET / 4u];
    if (len > 0u) {
        buf[words - 1u] = 0u;
        memcpy(buf, data, len);
    }
    in->DIEPTSIZ = TSIZ_PKTCNT_1 | len;
    in->DIEPCTL |= USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;
    for (uint32_t i = 0u; i < words; i++) {
        OTG_FIFO(n) = buf[i];
    }
    return true;
}

void usbhw_ep_out_arm(uint8_t ep)
{
    uint32_t n = ep & 0x7Fu;
    if (n == 0u || n >= USB_HW_EP_COUNT || s_out_mps[n] == 0u) {
        return;
    }
    USB_OTG_OUTEndpointTypeDef *out = OTG_OUT(n);
    if ((out->DOEPCTL & USB_OTG_DOEPCTL_EPENA) != 0u) {
        out->DOEPCTL |= USB_OTG_DOEPCTL_CNAK;       /* still armed: just stop NAKing */
        return;
    }
    out->DOEPTSIZ = TSIZ_PKTCNT_1 | s_out_mps[n];
    out->DOEPCTL |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
}

void usbhw_ep_stall(uint8_t ep, bool stall)
{
    uint32_t n = ep & 0x7Fu;
    if (n == 0u) {
        if (stall) {                                /* cleared by the core on the next SETUP */
            OTG_IN(0)->DIEPCTL |= USB_OTG_DIEPCTL_STALL;
            OTG_OUT(0)->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
        }
        return;
    }
    if (n >= USB_HW_EP_COUNT) {
        return;
    }
    if ((ep & USB_EP_DIR_IN) != 0u) {
        if (stall) {
            OTG_IN(n)->DIEPCTL |= USB_OTG_DIEPCTL_STALL;
        } else {
            OTG_IN(n)->DIEPCTL = (OTG_IN(n)->DIEPCTL & ~USB_OTG_DIEPCTL_STALL) | USB_OTG_DIEPCTL_SD0PID_SEVNFRM;
        }
    } else {
        if (stall) {
            OTG_OUT(n)->DOEPCTL |= USB_OTG_DOEPCTL_STALL;
        } else {
            OTG_OUT(n)->DOEPCTL = (OTG_OUT(n)->DOEPCTL & ~USB_OTG_DOEPCTL_STALL) | USB_OTG_DOEPCTL_SD0PID_SEVNFRM;
        }
    }
}

bool usbhw_ep_stalled(uint8_t ep)
{
    uint32_t n = ep & 0x7Fu;
    if (n >= USB_HW_EP_COUNT) {
        return false;
    }
    return ((ep & USB_EP_DIR_IN) != 0u) ? (OTG_IN(n)->DIEPCTL & USB_OTG_DIEPCTL_STALL) != 0u
                                        : (OTG_OUT(n)->DOEPCTL & USB_OTG_DOEPCTL_STALL) != 0u;
}

/* ------------------------------------------------------------------------ */
/* events                                                                   */
/* ------------------------------------------------------------------------ */
static void bus_reset(void)
{
    OTG_DEV->DCTL &= ~USB_OTG_DCTL_RWUSIG;
    flush_tx(TXFNUM_ALL);
    for (uint32_t n = 0u; n < USB_HW_EP_COUNT; n++) {
        OTG_IN(n)->DIEPINT = 0xFFu;
        OTG_OUT(n)->DOEPINT = 0xFFu;
        s_out_len[n] = 0u;
    }
    OTG_IN(0)->DIEPCTL &= ~USB_OTG_DIEPCTL_STALL;
    OTG_OUT(0)->DOEPCTL = (OTG_OUT(0)->DOEPCTL & ~USB_OTG_DOEPCTL_STALL) | USB_OTG_DOEPCTL_SNAK;
    usbhw_ep_close_all();
    OTG_DEV->DCFG &= ~USB_OTG_DCFG_DAD;
    s_ev->reset();
}

static void enum_done(void)
{
    /* full speed: EP0 max packet 64 bytes (MPSIZ = 0) */
    OTG_IN(0)->DIEPCTL &= ~USB_OTG_DIEPCTL_MPSIZ;
    OTG_DEV->DCTL |= USB_OTG_DCTL_CGINAK;
    ep0_out_arm();
}

static void rx_pop(void)
{
    uint32_t st = OTG->GRXSTSP;
    uint32_t ep = GRXSTS_EPNUM(st);
    uint32_t bcnt = GRXSTS_BCNT(st);

    switch (GRXSTS_PKTSTS(st)) {
    case PKTSTS_SETUP_DATA:
        /* a new SETUP aborts whatever EP0 still had queued (host gave up) */
        if ((OTG_IN(0)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0u ||
            (OTG_IN(0)->DIEPTSIZ & USB_OTG_DIEPTSIZ_PKTCNT) != 0u) {
            ep_in_abort(0u);
        }
        fifo_read(s_setup, bcnt, 2u);
        break;

    case PKTSTS_SETUP_DONE:
        OTG_OUT(0)->DOEPINT = USB_OTG_DOEPINT_STUP;
        s_ev->setup((const uint8_t *)s_setup);
        ep0_out_arm();
        break;

    case PKTSTS_OUT_DATA:
        if (ep < USB_HW_EP_COUNT) {
            fifo_read(s_out_buf[ep], bcnt, MAX_PACKET / 4u);
            s_out_len[ep] = (uint16_t)((bcnt > MAX_PACKET) ? MAX_PACKET : bcnt);
        } else {
            fifo_read(NULL, bcnt, 0u);
        }
        break;

    case PKTSTS_OUT_DONE:
        if (ep < USB_HW_EP_COUNT) {
            uint16_t len = s_out_len[ep];
            s_out_len[ep] = 0u;
            OTG_OUT(ep)->DOEPINT = USB_OTG_DOEPINT_XFRC;
            s_ev->out((uint8_t)ep, (const uint8_t *)s_out_buf[ep], len);
            if (ep == 0u) {
                ep0_out_arm();
            }
        }
        break;

    default:                                        /* global OUT NAK: unused */
        if (bcnt > 0u) {
            fifo_read(NULL, bcnt, 0u);
        }
        break;
    }
}

void usbhw_poll(void)
{
    uint32_t sts = OTG->GINTSTS;

    if ((sts & USB_OTG_GINTSTS_USBRST) != 0u) {
        OTG->GINTSTS = USB_OTG_GINTSTS_USBRST;
        bus_reset();
    }
    if ((sts & USB_OTG_GINTSTS_ENUMDNE) != 0u) {
        OTG->GINTSTS = USB_OTG_GINTSTS_ENUMDNE;
        enum_done();
    }
    /* IN completions first: they happened before anything still in the RX FIFO */
    for (uint32_t n = 0u; n < USB_HW_EP_COUNT; n++) {
        if ((OTG_IN(n)->DIEPINT & USB_OTG_DIEPINT_XFRC) != 0u) {
            OTG_IN(n)->DIEPINT = USB_OTG_DIEPINT_XFRC;
            s_ev->in_done((uint8_t)n);
        }
    }
    for (uint32_t guard = 0u; guard < 32u && (OTG->GINTSTS & USB_OTG_GINTSTS_RXFLVL) != 0u; guard++) {
        rx_pop();
    }
    if ((sts & USB_OTG_GINTSTS_USBSUSP) != 0u) {
        OTG->GINTSTS = USB_OTG_GINTSTS_USBSUSP;
        s_ev->suspend(true);
    }
    if ((sts & USB_OTG_GINTSTS_WKUINT) != 0u) {
        OTG->GINTSTS = USB_OTG_GINTSTS_WKUINT;
        s_ev->suspend(false);
    }
    OTG->GINTSTS = sts & (USB_OTG_GINTSTS_ESUSP | USB_OTG_GINTSTS_SOF | USB_OTG_GINTSTS_MMIS);
}
