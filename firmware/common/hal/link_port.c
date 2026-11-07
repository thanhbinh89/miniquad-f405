/**
 * @file    link_port.c
 * @brief   link_port.h for both board revisions.
 */
#include "link_port.h"

#include "board.h"
#include "clock.h"

#if BOARD_LINK_UART
/* ======================================================================== */
/* rev 1: USART1 + DMA on J2                                                */
/* ======================================================================== */
#include "uart_link.h"

void link_port_init(link_port_role_t role)
{
    (void)role;
    uart_link_init(LINK_BAUD);
}

void link_port_task(uint32_t now_ms)
{
    (void)now_ms;
}

size_t link_port_read(uint8_t *dst, size_t max)
{
    return uart_link_read(dst, max);
}

size_t link_port_write(const void *src, size_t len)
{
    return uart_link_write(src, len);
}

size_t link_port_tx_free(void)
{
    return uart_link_tx_free();
}

bool link_port_tx_idle(void)
{
    return uart_link_tx_idle();
}

bool link_port_usb_power(void)
{
    return false;
}

bool link_port_ready(void)
{
    return true;
}

void link_port_detach(uint32_t settle_ms)
{
    (void)settle_ms;
}

uint32_t link_port_errors(void)
{
    return uart_link_rx_errors();
}

#elif BOARD_LINK_USB
/* ======================================================================== */
/* rev 2: USB CDC on the USB-C connector                                    */
/* ======================================================================== */
#include "memory_map.h"
#include "usb_cdc.h"
#include "vectors.h"

static char s_serial[25];       /* the 96-bit UID as 24 hex digits, like the configurator shows it */

static void make_serial(void)
{
    static const char HEX[] = "0123456789ABCDEF";
    const volatile uint32_t *uid = (const volatile uint32_t *)MM_UID_BASE;
    for (uint32_t w = 0u; w < 3u; w++) {
        uint32_t v = uid[w];
        for (uint32_t i = 0u; i < 8u; i++) {
            s_serial[w * 8u + i] = HEX[(v >> (28u - 4u * i)) & 0xFu];
        }
    }
    s_serial[24] = '\0';
}

void link_port_init(link_port_role_t role)
{
    make_serial();
    const usb_cdc_config_t cfg = {
        .vid = USB_VID,
        .pid = USB_PID,
        .bcd_device = USB_BCD_DEVICE,
        .manufacturer = USB_MANUFACTURER,
        .product = (role == LINK_PORT_BOOTLOADER) ? USB_PRODUCT_BL : USB_PRODUCT_APP,
        .serial = s_serial,             /* same in both: one COM port, one Web Serial permission */
        .use_irq = (role == LINK_PORT_APP),
    };
    usb_cdc_init(&cfg);
}

void OTG_FS_IRQHandler(void)
{
    usb_cdc_irq();
}

void link_port_task(uint32_t now_ms)
{
    usb_cdc_task(now_ms);
}

size_t link_port_read(uint8_t *dst, size_t max)
{
    return usb_cdc_read(dst, max);
}

size_t link_port_write(const void *src, size_t len)
{
    return usb_cdc_write(src, len);
}

size_t link_port_tx_free(void)
{
    return usb_cdc_tx_free();
}

bool link_port_tx_idle(void)
{
    return usb_cdc_tx_idle();
}

bool link_port_usb_power(void)
{
    return usb_cdc_vbus();
}

bool link_port_ready(void)
{
    return usb_cdc_configured();
}

void link_port_detach(uint32_t settle_ms)
{
    usb_cdc_detach();
    clock_delay_us(settle_ms * 1000u);
}

uint32_t link_port_errors(void)
{
    usb_cdc_state_t st;
    usb_cdc_state(&st);
    return st.resets;
}

#endif
