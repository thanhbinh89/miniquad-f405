/**
 * @file    usb_hw.h
 * @brief   USB device controller interface used by usb_cdc.c.
 *
 * Implemented by usb_dwc2.c for the STM32F4 OTG_FS core (Synopsys DWC2 in
 * slave mode) and by the scripted host of tests/unit/test_usb.c. The model
 * is "one packet per transfer", which keeps the controller layer small:
 *
 *  - OUT: every received packet is copied out of the core's receive FIFO and
 *    reported with events.out() once its transfer completed. EP0 OUT is
 *    re-armed by the controller layer itself (SETUP, data and status stages
 *    are always accepted). The other OUT endpoints are re-armed only by
 *    usbhw_ep_out_arm(): that is the flow control - the host gets NAKs
 *    until the receiver has room for a full packet.
 *  - IN : usbhw_ep_write() queues one packet (<= max packet size, may be
 *    empty); events.in_done() reports the host's ACK.
 *
 * Context: every function except usbhw_vbus() and usbhw_kick() runs in the
 * USB event context - inside usbhw_poll() (the OTG interrupt, or the main
 * loop of the polled bootloader) or between usbhw_lock() / usbhw_unlock().
 */
#ifndef USB_HW_H
#define USB_HW_H

#include <stdbool.h>
#include <stdint.h>

#define USB_EPTYPE_CONTROL      0u
#define USB_EPTYPE_ISO          1u
#define USB_EPTYPE_BULK         2u
#define USB_EPTYPE_INTERRUPT    3u

#define USB_EP_DIR_IN           0x80u
#define USB_EP0_MPS             64u
#define USB_HW_EP_COUNT         4u      /* OTG_FS: EP0..EP3 in each direction */

typedef struct {
    void (*reset)(void);                                        /* bus reset, address 0 */
    void (*setup)(const uint8_t setup[8]);                      /* SETUP packet on EP0 */
    void (*out)(uint8_t ep, const uint8_t *data, uint16_t len); /* OUT packet (EP0 or bulk) */
    void (*in_done)(uint8_t ep);                                /* IN packet ACKed */
    void (*suspend)(bool suspended);
} usbhw_events_t;

/** Power the core and PHY up, soft-disconnected. @p use_irq: events from
 *  the OTG_FS interrupt (application) instead of usbhw_poll() calls. */
void usbhw_init(const usbhw_events_t *ev, bool use_irq);
/** Disconnect and power the core down. */
void usbhw_deinit(void);
/** D+ pull-up on / off (soft connect). */
void usbhw_connect(bool on);
/** VBUS present on the connector (raw pin level, any context). */
bool usbhw_vbus(void);
/** Process pending controller events (the interrupt handler body). */
void usbhw_poll(void);
/** Ask for a pass of the event context: pends the interrupt in IRQ mode,
 *  nothing in polled mode (the loop polls anyway). Any context. */
void usbhw_kick(void);
void usbhw_lock(void);
void usbhw_unlock(void);

void usbhw_set_address(uint8_t addr);
/** Activate a non-control endpoint (@p ep with USB_EP_DIR_IN for IN). */
void usbhw_ep_open(uint8_t ep, uint8_t type, uint16_t mps);
/** Deactivate every non-control endpoint (configuration 0, bus reset). */
void usbhw_ep_close_all(void);
/** Queue one IN packet on endpoint number @p ep. False if one is pending. */
bool usbhw_ep_write(uint8_t ep, const void *data, uint16_t len);
/** Accept the next OUT packet on endpoint number @p ep (>= 1). */
void usbhw_ep_out_arm(uint8_t ep);
/** STALL / un-STALL (data toggle back to DATA0). @p ep 0 = both directions
 *  of the control endpoint until the next SETUP. */
void usbhw_ep_stall(uint8_t ep, bool stall);
bool usbhw_ep_stalled(uint8_t ep);

#endif /* USB_HW_H */
