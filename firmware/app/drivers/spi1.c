/**
 * @file    spi1.c
 * @brief   SPI1 + DMA2 driver.
 */
#include "spi1.h"

#include "board.h"
#include "dma.h"
#include "gpio.h"
#include "vectors.h"

static volatile bool s_busy;
static void (*volatile s_done)(void);

void spi1_init(uint32_t div)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    gpio_clock_enable(IMU_SCK_PORT);
    gpio_alternate(IMU_SCK_PORT, IMU_SCK_PIN, IMU_SPI_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_alternate(IMU_MISO_PORT, IMU_MISO_PIN, IMU_SPI_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_UP);
    gpio_alternate(IMU_MOSI_PORT, IMU_MOSI_PIN, IMU_SPI_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_NONE);

    SPI_TypeDef *spi = IMU_SPI;
    spi->CR1 = 0u;
    spi->CR2 = 0u;
    /* master, mode 3 (CPOL=1, CPHA=1), MSB first, software NSS */
    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_SSM | SPI_CR1_SSI |
               ((div & 7u) << SPI_CR1_BR_Pos);
    spi->CR1 |= SPI_CR1_SPE;

    DMA_Stream_TypeDef *rx = IMU_DMA_RX_STREAM;
    DMA_Stream_TypeDef *tx = IMU_DMA_TX_STREAM;
    dma_stream_disable(rx);
    dma_stream_disable(tx);
    rx->PAR = (uint32_t)&spi->DR;
    tx->PAR = (uint32_t)&spi->DR;
    rx->FCR = 0u;
    tx->FCR = 0u;
    rx->CR = DMA_CR_CHSEL(IMU_DMA_CHANNEL) | DMA_SxCR_PL_1 | DMA_SxCR_PL_0 | DMA_SxCR_MINC |
             DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    tx->CR = DMA_CR_CHSEL(IMU_DMA_CHANNEL) | DMA_SxCR_PL_1 | DMA_SxCR_MINC | DMA_SxCR_DIR_0;

    NVIC_SetPriority(IMU_DMA_RX_IRQn, IRQ_PRIO_IMU);
    NVIC_EnableIRQ(IMU_DMA_RX_IRQn);
    s_busy = false;
}

void spi1_set_div(uint32_t div)
{
    SPI_TypeDef *spi = IMU_SPI;
    while ((spi->SR & SPI_SR_BSY) != 0u) {
    }
    spi->CR1 &= ~SPI_CR1_SPE;
    spi->CR1 = (spi->CR1 & ~SPI_CR1_BR) | ((div & 7u) << SPI_CR1_BR_Pos);
    spi->CR1 |= SPI_CR1_SPE;
}

void spi1_transfer(const uint8_t *tx, uint8_t *rx, size_t n)
{
    SPI_TypeDef *spi = IMU_SPI;
    for (size_t i = 0u; i < n; i++) {
        while ((spi->SR & SPI_SR_TXE) == 0u) {
        }
        *(volatile uint8_t *)&spi->DR = (tx != NULL) ? tx[i] : 0xFFu;
        while ((spi->SR & SPI_SR_RXNE) == 0u) {
        }
        uint8_t b = *(volatile uint8_t *)&spi->DR;
        if (rx != NULL) {
            rx[i] = b;
        }
    }
    while ((spi->SR & SPI_SR_BSY) != 0u) {
    }
}

bool spi1_transfer_dma(const uint8_t *tx, uint8_t *rx, uint16_t n, void (*done)(void))
{
    if (s_busy) {
        return false;
    }
    s_busy = true;
    s_done = done;

    DMA_Stream_TypeDef *srx = IMU_DMA_RX_STREAM;
    DMA_Stream_TypeDef *stx = IMU_DMA_TX_STREAM;
    dma_clear_flags(IMU_DMA, IMU_DMA_RX_INDEX, DMA_FLAG_ALL);
    dma_clear_flags(IMU_DMA, IMU_DMA_TX_INDEX, DMA_FLAG_ALL);
    srx->M0AR = (uint32_t)rx;
    srx->NDTR = n;
    stx->M0AR = (uint32_t)tx;
    stx->NDTR = n;
    srx->CR |= DMA_SxCR_EN;
    stx->CR |= DMA_SxCR_EN;
    /* RX request first, then TX (RM0090 28.3.9) */
    IMU_SPI->CR2 |= SPI_CR2_RXDMAEN;
    IMU_SPI->CR2 |= SPI_CR2_TXDMAEN;
    return true;
}

bool spi1_busy(void)
{
    return s_busy;
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t f = dma_flags(IMU_DMA, IMU_DMA_RX_INDEX);
    dma_clear_flags(IMU_DMA, IMU_DMA_RX_INDEX, f);
    if ((f & (DMA_FLAG_TC | DMA_FLAG_TE)) == 0u) {
        return;
    }
    IMU_SPI->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    dma_stream_disable(IMU_DMA_TX_STREAM);
    dma_stream_disable(IMU_DMA_RX_STREAM);
    while ((IMU_SPI->SR & SPI_SR_BSY) != 0u) {
    }
    s_busy = false;
    void (*cb)(void) = s_done;
    if (cb != NULL) {
        cb();
    }
}
