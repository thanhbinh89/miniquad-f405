/**
 * @file    uart_link.h
 * @brief   USART1 driver for the configurator link, fully DMA driven.
 *
 *  - RX: DMA2 Stream2 in circular mode into a 1 KB buffer; the CPU only
 *    reads the DMA write index (NDTR), no per-byte interrupt.
 *  - TX: 4 KB ring buffer drained by DMA2 Stream7; the transfer-complete
 *    interrupt chains the next contiguous chunk.
 *
 * Single producer: uart_link_write() must only be called from thread
 * (main loop) context.
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_LINK_RX_SIZE   1024u   /* power of two */
#define UART_LINK_TX_SIZE   4096u   /* power of two */

void     uart_link_init(uint32_t baud);

/** Copy up to @p max received bytes into @p dst. Non-blocking. */
size_t   uart_link_read(uint8_t *dst, size_t max);

/**
 * Queue @p len bytes for transmission. All-or-nothing: returns @p len when
 * queued, 0 when the ring does not have enough room (nothing queued).
 */
size_t   uart_link_write(const void *src, size_t len);

size_t   uart_link_tx_free(void);

/** True when the ring is empty, the DMA is idle and the last stop bit left. */
bool     uart_link_tx_idle(void);

/** Number of RX overrun / framing errors seen. */
uint32_t uart_link_rx_errors(void);

/** Stop DMA and the USART (used before jumping to another image). */
void     uart_link_deinit(void);

#endif /* UART_LINK_H */
