/**
 * @file    usb_cdc.h
 * @brief   USB full-speed device with a single CDC ACM function: the
 *          "virtual COM port" the configurator (Web Serial) and any serial
 *          terminal open. Hardware independent; talks to the controller
 *          through usb_hw.h.
 *
 *   EP0        control: standard requests, CDC line coding / control lines
 *   EP 0x82    interrupt IN, CDC notifications (declared, never used)
 *   EP 0x01    bulk OUT  -> RX ring (1 KB); NAKed while the ring is full
 *   EP 0x81    bulk IN   <- TX ring (4 KB); ZLP after a full last packet
 *
 * The line coding (baud rate...) is stored and echoed but has no effect:
 * the link runs at USB speed whatever the host asks for.
 *
 * Two execution models, the same code:
 *  - interrupt (application): the OTG interrupt calls usb_cdc_irq();
 *    the thread side only touches the rings and pends the interrupt;
 *  - polled (bootloader): usb_cdc_task() runs everything in the main loop.
 *
 * Connection: the D+ pull-up is enabled only while VBUS is present
 * (debounced 10 ms), so a battery-powered board never back-powers an
 * unplugged host and a cable plug-in is seen as a new connection.
 */
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_CDC_RX_SIZE     1024u   /* power of two */
#define USB_CDC_TX_SIZE     4096u   /* power of two */

typedef struct {
    uint16_t    vid;
    uint16_t    pid;
    uint16_t    bcd_device;
    const char *manufacturer;
    const char *product;
    const char *serial;             /* ASCII, kept by reference */
    bool        use_irq;
} usb_cdc_config_t;

typedef struct {
    bool     vbus;                  /* debounced VBUS                       */
    bool     connected;             /* D+ pull-up on                        */
    bool     configured;            /* SET_CONFIGURATION(1) received        */
    bool     suspended;
    bool     dtr;                   /* host has the port open (DTR)         */
    uint32_t baud;                  /* last SET_LINE_CODING (informative)   */
    uint32_t resets;                /* bus resets seen                      */
    uint32_t dropped;               /* bytes written while not configured   */
} usb_cdc_state_t;

void   usb_cdc_init(const usb_cdc_config_t *cfg);
/** Thread context, every main loop pass: VBUS / soft connect; in polled
 *  mode also the controller events. */
void   usb_cdc_task(uint32_t now_ms);
/** OTG interrupt handler body (interrupt mode). */
void   usb_cdc_irq(void);

/** Copy up to @p max received bytes. Non-blocking. */
size_t usb_cdc_read(uint8_t *dst, size_t max);
/** All-or-nothing: @p len when queued, 0 when the TX ring lacks room.
 *  Without a configured host the bytes are discarded (returns @p len). */
size_t usb_cdc_write(const void *src, size_t len);
size_t usb_cdc_tx_free(void);
/** Nothing left to send (ring empty, last packet ACKed by the host). */
bool   usb_cdc_tx_idle(void);

/** Remove the pull-up now and stay disconnected (before a system reset,
 *  so the host sees the device leave even if the reset is quick). */
void   usb_cdc_detach(void);

void   usb_cdc_state(usb_cdc_state_t *st);
bool   usb_cdc_vbus(void);
bool   usb_cdc_configured(void);

#endif /* USB_CDC_H */
