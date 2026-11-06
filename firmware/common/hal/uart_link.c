/**
 * @file    uart_link.c
 * @brief   USART1 + DMA2 link driver.
 */
#include "uart_link.h"

#include <string.h>
#include "board.h"
#include "dma.h"
#include "gpio.h"
#include "vectors.h"

_Static_assert((UART_LINK_RX_SIZE & (UART_LINK_RX_SIZE - 1u)) == 0u, "RX size power of two");
_Static_assert((UART_LINK_TX_SIZE & (UART_LINK_TX_SIZE - 1u)) == 0u, "TX size power of two");

static uint8_t           s_rx_buf[UART_LINK_RX_SIZE];
static uint32_t          s_rx_tail;
static uint32_t          s_rx_errors;

static uint8_t           s_tx_buf[UART_LINK_TX_SIZE];
static volatile uint32_t s_tx_head;     /* written by thread  */
static volatile uint32_t s_tx_tail;     /* written by DMA ISR */
static volatile uint32_t s_tx_len;      /* chunk in flight, 0 = idle */

void uart_link_init(uint32_t baud)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    gpio_clock_enable(LINK_TX_PORT);
    gpio_clock_enable(LINK_RX_PORT);

    gpio_alternate(LINK_TX_PORT, LINK_TX_PIN, LINK_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_UP);
    gpio_alternate(LINK_RX_PORT, LINK_RX_PIN, LINK_GPIO_AF, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, GPIO_PULL_UP);

    USART_TypeDef *u = LINK_USART;
    u->CR1 = 0u;
    u->CR2 = 0u;
    u->CR3 = 0u;
    u->BRR = (PCLK2_HZ + baud / 2u) / baud;     /* OVER8 = 0 */

    /* RX: circular DMA, peripheral -> memory */
    DMA_Stream_TypeDef *rx = LINK_DMA_RX_STREAM;
    dma_stream_disable(rx);
    dma_clear_flags(LINK_DMA, LINK_DMA_RX_INDEX, DMA_FLAG_ALL);
    rx->PAR  = (uint32_t)&u->DR;
    rx->M0AR = (uint32_t)s_rx_buf;
    rx->NDTR = UART_LINK_RX_SIZE;
    rx->FCR  = 0u;                              /* direct mode */
    rx->CR   = DMA_CR_CHSEL(LINK_DMA_CHANNEL) | DMA_SxCR_PL_1 | DMA_SxCR_MINC | DMA_SxCR_CIRC;
    s_rx_tail = 0u;

    /* TX: memory -> peripheral, chunks started by software */
    DMA_Stream_TypeDef *tx = LINK_DMA_TX_STREAM;
    dma_stream_disable(tx);
    dma_clear_flags(LINK_DMA, LINK_DMA_TX_INDEX, DMA_FLAG_ALL);
    tx->PAR = (uint32_t)&u->DR;
    tx->FCR = 0u;
    tx->CR  = DMA_CR_CHSEL(LINK_DMA_CHANNEL) | DMA_SxCR_PL_0 | DMA_SxCR_MINC
            | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_tx_len = 0u;

    u->CR3 = USART_CR3_DMAR | USART_CR3_DMAT;
    u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    rx->CR |= DMA_SxCR_EN;

    NVIC_SetPriority(LINK_DMA_TX_IRQn, IRQ_PRIO_LINK);
    NVIC_ClearPendingIRQ(LINK_DMA_TX_IRQn);
    NVIC_EnableIRQ(LINK_DMA_TX_IRQn);
}

void uart_link_deinit(void)
{
    NVIC_DisableIRQ(LINK_DMA_TX_IRQn);
    dma_stream_disable(LINK_DMA_RX_STREAM);
    dma_stream_disable(LINK_DMA_TX_STREAM);
    dma_clear_flags(LINK_DMA, LINK_DMA_RX_INDEX, DMA_FLAG_ALL);
    dma_clear_flags(LINK_DMA, LINK_DMA_TX_INDEX, DMA_FLAG_ALL);
    LINK_USART->CR1 = 0u;
    LINK_USART->CR3 = 0u;
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
}

size_t uart_link_read(uint8_t *dst, size_t max)
{
    USART_TypeDef *u = LINK_USART;
    uint32_t sr = u->SR;
    if ((sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0u) {
        (void)u->DR;                            /* SR then DR read clears the flags */
        s_rx_errors++;
    }

    uint32_t head = (UART_LINK_RX_SIZE - LINK_DMA_RX_STREAM->NDTR) & (UART_LINK_RX_SIZE - 1u);
    size_t n = 0u;
    while (s_rx_tail != head && n < max) {
        dst[n++] = s_rx_buf[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1u) & (UART_LINK_RX_SIZE - 1u);
    }
    return n;
}

/* Must be called with the TX DMA interrupt unable to preempt. */
static void tx_start_locked(void)
{
    if (s_tx_len != 0u) {
        return;
    }
    uint32_t head = s_tx_head;
    uint32_t tail = s_tx_tail;
    if (head == tail) {
        return;
    }
    uint32_t len = (head > tail) ? (head - tail) : (UART_LINK_TX_SIZE - tail);
    s_tx_len = len;

    DMA_Stream_TypeDef *tx = LINK_DMA_TX_STREAM;
    dma_clear_flags(LINK_DMA, LINK_DMA_TX_INDEX, DMA_FLAG_ALL);
    tx->M0AR = (uint32_t)&s_tx_buf[tail];
    tx->NDTR = len;
    LINK_USART->SR = ~USART_SR_TC;              /* clear TC (rc_w0) */
    tx->CR |= DMA_SxCR_EN;
}

size_t uart_link_tx_free(void)
{
    return (size_t)((s_tx_tail - s_tx_head - 1u) & (UART_LINK_TX_SIZE - 1u));
}

size_t uart_link_write(const void *src, size_t len)
{
    if (len == 0u || len > uart_link_tx_free()) {
        return 0u;
    }
    const uint8_t *p = (const uint8_t *)src;
    uint32_t head = s_tx_head;
    size_t first = UART_LINK_TX_SIZE - head;
    if (first > len) {
        first = len;
    }
    memcpy(&s_tx_buf[head], p, first);
    if (len > first) {
        memcpy(&s_tx_buf[0], p + first, len - first);
    }
    __DMB();
    s_tx_head = (head + (uint32_t)len) & (UART_LINK_TX_SIZE - 1u);

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    tx_start_locked();
    __set_PRIMASK(primask);
    return len;
}

bool uart_link_tx_idle(void)
{
    return (s_tx_head == s_tx_tail) && (s_tx_len == 0u) && ((LINK_USART->SR & USART_SR_TC) != 0u);
}

uint32_t uart_link_rx_errors(void)
{
    return s_rx_errors;
}

void DMA2_Stream7_IRQHandler(void)
{
    uint32_t f = dma_flags(LINK_DMA, LINK_DMA_TX_INDEX);
    dma_clear_flags(LINK_DMA, LINK_DMA_TX_INDEX, f);
    if ((f & (DMA_FLAG_TC | DMA_FLAG_TE)) != 0u) {
        /* stream is disabled by hardware at TC / on error */
        dma_stream_disable(LINK_DMA_TX_STREAM);
        s_tx_tail = (s_tx_tail + s_tx_len) & (UART_LINK_TX_SIZE - 1u);
        s_tx_len = 0u;
        tx_start_locked();
    }
}
