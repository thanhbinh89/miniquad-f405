/**
 * @file    link_port.h
 * @brief   Byte stream to the configurator, whatever the board revision:
 *            rev 1  USART1 on J2 through an external USB-UART adapter
 *            rev 2  USB CDC ACM on the USB-C connector (OTG_FS)
 *
 * The bootloader runs the link polled from its main loop; the application
 * runs it from interrupts (USART DMA / OTG interrupt) and calls
 * link_port_task() from the main loop for housekeeping (VBUS, connect).
 * write() is all-or-nothing and must only be called from thread context.
 */
#ifndef LINK_PORT_H
#define LINK_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LINK_PORT_APP = 0,          /* interrupt driven, product "MiniQuad-F405"            */
    LINK_PORT_BOOTLOADER = 1,   /* polled, product "MiniQuad-F405 Bootloader"           */
} link_port_role_t;

void     link_port_init(link_port_role_t role);
/** Every main loop pass. */
void     link_port_task(uint32_t now_ms);
size_t   link_port_read(uint8_t *dst, size_t max);
size_t   link_port_write(const void *src, size_t len);
size_t   link_port_tx_free(void);
/** Everything written so far has left (UART: last stop bit; USB: ACKed). */
bool     link_port_tx_idle(void);
/** rev 2: VBUS present on the USB-C port. rev 1: always false. */
bool     link_port_usb_power(void);
/** rev 2: enumerated by a host. rev 1: always true. */
bool     link_port_ready(void);
/** Before a system reset: rev 2 drops the D+ pull-up so the host sees the
 *  device leave, waits @p settle_ms; rev 1 does nothing. */
void     link_port_detach(uint32_t settle_ms);
uint32_t link_port_errors(void);

#endif /* LINK_PORT_H */
