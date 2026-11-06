/**
 * @file    spi1.h
 * @brief   SPI1 master (mode 3, 8 bit) with full-duplex DMA transfers.
 *
 * RX: DMA2 Stream0 ch3 (transfer-complete interrupt), TX: DMA2 Stream3 ch3.
 * Chip select is handled by the device driver.
 */
#ifndef SPI1_H
#define SPI1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SPI1 runs from APB2 = 84 MHz */
#define SPI1_DIV_8      2u      /* 10.5 MHz - data reads   */
#define SPI1_DIV_128    6u      /* 656 kHz  - configuration */

void spi1_init(uint32_t div);
void spi1_set_div(uint32_t div);
/** Blocking full-duplex transfer (tx or rx may be NULL). */
void spi1_transfer(const uint8_t *tx, uint8_t *rx, size_t n);
/**
 * Start a DMA transfer; @p done is called from the DMA interrupt.
 * Buffers must be in SRAM (not CCM) and stay valid until completion.
 * @return false if a transfer is already running.
 */
bool spi1_transfer_dma(const uint8_t *tx, uint8_t *rx, uint16_t n, void (*done)(void));
bool spi1_busy(void);

#endif /* SPI1_H */
