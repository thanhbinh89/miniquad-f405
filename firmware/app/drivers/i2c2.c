/**
 * @file    i2c2.c
 * @brief   STM32F4 (I2C v1) master state machine with DMA data phases.
 *
 * Sequence (RM0090 27.3.3 / AN2824):
 *   START -> SB: send address(W) -> ADDR: enable TX DMA, clear ADDR
 *   -> TX DMA TC -> BTF: repeated START or STOP
 *   START -> SB: send address(R) -> ADDR:
 *        n == 1 : ACK=0, clear ADDR, STOP, wait RXNE
 *        n >= 2 : ACK=1, LAST=1 (NACK after the last DMA byte), clear ADDR
 *   -> RX DMA TC: STOP.
 */
#include "i2c2.h"

#include "board.h"
#include "dma.h"
#include "gpio.h"
#include "systime.h"
#include "vectors.h"

#define I2C_TIMEOUT_MS  25u

typedef enum {
    ST_IDLE = 0,
    ST_START_W,
    ST_TX,
    ST_START_R,
    ST_RX_DMA,
    ST_RX_ONE,
} st_t;

static volatile st_t         s_state;
static volatile i2c_result_t s_result;
static uint8_t               s_addr;
static const uint8_t        *s_wbuf;
static uint16_t              s_wlen;
static uint8_t              *s_rbuf;
static uint16_t              s_rlen;
static uint32_t              s_hz;
static uint32_t              s_start_ms;
static volatile uint32_t     s_errors;

static void pins_af(void)
{
    gpio_alternate(TOF_SCL_PORT, TOF_SCL_PIN, TOF_I2C_AF, GPIO_OTYPE_OD, GPIO_SPEED_MEDIUM, GPIO_PULL_NONE);
    gpio_alternate(TOF_SDA_PORT, TOF_SDA_PIN, TOF_I2C_AF, GPIO_OTYPE_OD, GPIO_SPEED_MEDIUM, GPIO_PULL_NONE);
}

/* free a slave that holds SDA low: up to 9 SCL pulses, then a STOP */
static void bus_recover(void)
{
    gpio_output(TOF_SCL_PORT, TOF_SCL_PIN, GPIO_OTYPE_OD, GPIO_SPEED_MEDIUM, true);
    gpio_output(TOF_SDA_PORT, TOF_SDA_PIN, GPIO_OTYPE_OD, GPIO_SPEED_MEDIUM, true);
    delay_us(10u);
    for (int i = 0; i < 9 && !gpio_read(TOF_SDA_PORT, TOF_SDA_PIN); i++) {
        gpio_write(TOF_SCL_PORT, TOF_SCL_PIN, false);
        delay_us(5u);
        gpio_write(TOF_SCL_PORT, TOF_SCL_PIN, true);
        delay_us(5u);
    }
    gpio_write(TOF_SDA_PORT, TOF_SDA_PIN, false);
    delay_us(5u);
    gpio_write(TOF_SCL_PORT, TOF_SCL_PIN, true);
    delay_us(5u);
    gpio_write(TOF_SDA_PORT, TOF_SDA_PIN, true);
    delay_us(5u);
}

static void peripheral_setup(void)
{
    I2C_TypeDef *i2c = TOF_I2C;
    i2c->CR1 = I2C_CR1_SWRST;
    i2c->CR1 = 0u;
    uint32_t freq_mhz = PCLK1_HZ / 1000000u;
    i2c->CR2 = freq_mhz | I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;
    if (s_hz > 100000u) {
        uint32_t ccr = PCLK1_HZ / (3u * s_hz);              /* fast mode, Tlow/Thigh = 2 */
        if (ccr < 1u) {
            ccr = 1u;
        }
        i2c->CCR = I2C_CCR_FS | ccr;
        i2c->TRISE = (freq_mhz * 300u) / 1000u + 1u;        /* 300 ns */
    } else {
        i2c->CCR = PCLK1_HZ / (2u * s_hz);
        i2c->TRISE = freq_mhz + 1u;                          /* 1000 ns */
    }
    i2c->CR1 = I2C_CR1_PE;
}

void i2c2_init(uint32_t hz)
{
    s_hz = hz;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;
    gpio_clock_enable(TOF_SCL_PORT);
    gpio_clock_enable(TOF_SDA_PORT);
    bus_recover();
    pins_af();

    DMA_Stream_TypeDef *tx = TOF_DMA_TX_STREAM;
    DMA_Stream_TypeDef *rx = TOF_DMA_RX_STREAM;
    dma_stream_disable(tx);
    dma_stream_disable(rx);
    tx->PAR = (uint32_t)&TOF_I2C->DR;
    rx->PAR = (uint32_t)&TOF_I2C->DR;
    tx->FCR = 0u;
    rx->FCR = 0u;
    tx->CR = DMA_CR_CHSEL(TOF_DMA_CHANNEL) | DMA_SxCR_PL_0 | DMA_SxCR_MINC | DMA_SxCR_DIR_0 |
             DMA_SxCR_TCIE | DMA_SxCR_TEIE;
    rx->CR = DMA_CR_CHSEL(TOF_DMA_CHANNEL) | DMA_SxCR_PL_0 | DMA_SxCR_MINC |
             DMA_SxCR_TCIE | DMA_SxCR_TEIE;

    peripheral_setup();

    const IRQn_Type irqs[4] = { TOF_I2C_EV_IRQn, TOF_I2C_ER_IRQn, TOF_DMA_TX_IRQn, TOF_DMA_RX_IRQn };
    for (int i = 0; i < 4; i++) {
        NVIC_SetPriority(irqs[i], IRQ_PRIO_I2C);
        NVIC_EnableIRQ(irqs[i]);
    }
    s_state = ST_IDLE;
    s_result = I2C_RES_NONE;
}

static void finish(i2c_result_t r)
{
    I2C_TypeDef *i2c = TOF_I2C;
    i2c->CR2 &= ~(I2C_CR2_DMAEN | I2C_CR2_LAST | I2C_CR2_ITBUFEN);
    dma_stream_disable(TOF_DMA_TX_STREAM);
    dma_stream_disable(TOF_DMA_RX_STREAM);
    if (r != I2C_RES_OK) {
        s_errors++;
    }
    s_state = ST_IDLE;
    s_result = r;
}

bool i2c2_start(uint8_t addr7, const uint8_t *wbuf, uint16_t wlen, uint8_t *rbuf, uint16_t rlen)
{
    if (s_state != ST_IDLE || (wlen == 0u && rlen == 0u)) {
        return false;
    }
    s_addr = (uint8_t)(addr7 << 1);
    s_wbuf = wbuf;
    s_wlen = wlen;
    s_rbuf = rbuf;
    s_rlen = rlen;
    s_result = I2C_RES_NONE;
    s_start_ms = time_ms();
    s_state = (wlen > 0u) ? ST_START_W : ST_START_R;
    TOF_I2C->CR1 |= I2C_CR1_ACK | I2C_CR1_START;
    return true;
}

bool i2c2_busy(void)
{
    return s_state != ST_IDLE;
}

i2c_result_t i2c2_result(void)
{
    return s_result;
}

uint32_t i2c2_errors(void)
{
    return s_errors;
}

void i2c2_poll(uint32_t now_ms)
{
    if (s_state != ST_IDLE && (now_ms - s_start_ms) > I2C_TIMEOUT_MS) {
        NVIC_DisableIRQ(TOF_I2C_EV_IRQn);
        finish(I2C_RES_TIMEOUT);
        bus_recover();
        pins_af();
        peripheral_setup();
        NVIC_EnableIRQ(TOF_I2C_EV_IRQn);
    }
}

static void start_rx_phase(void)
{
    s_state = ST_START_R;
    TOF_I2C->CR1 |= I2C_CR1_ACK | I2C_CR1_START;
}

void I2C2_EV_IRQHandler(void)
{
    I2C_TypeDef *i2c = TOF_I2C;
    uint32_t sr1 = i2c->SR1;

    if ((sr1 & I2C_SR1_SB) != 0u) {
        i2c->DR = (s_state == ST_START_R) ? (uint32_t)(s_addr | 1u) : s_addr;
        return;
    }
    if ((sr1 & I2C_SR1_ADDR) != 0u) {
        if (s_state == ST_START_W) {
            DMA_Stream_TypeDef *tx = TOF_DMA_TX_STREAM;
            dma_clear_flags(TOF_DMA, TOF_DMA_TX_INDEX, DMA_FLAG_ALL);
            tx->M0AR = (uint32_t)s_wbuf;
            tx->NDTR = s_wlen;
            tx->CR |= DMA_SxCR_EN;
            i2c->CR2 |= I2C_CR2_DMAEN;
            s_state = ST_TX;
            (void)i2c->SR2;                     /* clears ADDR */
        } else if (s_rlen == 1u) {
            i2c->CR1 &= ~I2C_CR1_ACK;
            (void)i2c->SR2;
            i2c->CR1 |= I2C_CR1_STOP;
            i2c->CR2 |= I2C_CR2_ITBUFEN;        /* RXNE interrupt */
            s_state = ST_RX_ONE;
        } else {
            DMA_Stream_TypeDef *rx = TOF_DMA_RX_STREAM;
            dma_clear_flags(TOF_DMA, TOF_DMA_RX_INDEX, DMA_FLAG_ALL);
            rx->M0AR = (uint32_t)s_rbuf;
            rx->NDTR = s_rlen;
            rx->CR |= DMA_SxCR_EN;
            i2c->CR1 |= I2C_CR1_ACK;
            i2c->CR2 |= I2C_CR2_DMAEN | I2C_CR2_LAST;
            s_state = ST_RX_DMA;
            (void)i2c->SR2;
        }
        return;
    }
    if ((sr1 & I2C_SR1_BTF) != 0u && s_state == ST_TX && TOF_DMA_TX_STREAM->NDTR == 0u) {
        i2c->CR2 &= ~I2C_CR2_DMAEN;
        if (s_rlen > 0u) {
            start_rx_phase();
        } else {
            i2c->CR1 |= I2C_CR1_STOP;
            finish(I2C_RES_OK);
        }
        return;
    }
    if ((sr1 & I2C_SR1_RXNE) != 0u && s_state == ST_RX_ONE) {
        s_rbuf[0] = (uint8_t)i2c->DR;
        finish(I2C_RES_OK);
        return;
    }
}

void I2C2_ER_IRQHandler(void)
{
    I2C_TypeDef *i2c = TOF_I2C;
    uint32_t sr1 = i2c->SR1;
    i2c_result_t r = ((sr1 & I2C_SR1_AF) != 0u) ? I2C_RES_NACK : I2C_RES_BUS_ERROR;
    i2c->SR1 = ~(I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR | I2C_SR1_TIMEOUT);
    if ((sr1 & I2C_SR1_ARLO) == 0u) {
        i2c->CR1 |= I2C_CR1_STOP;
    }
    if (s_state != ST_IDLE) {
        finish(r);
    }
}

/* TX DMA complete: the last byte is in DR, BTF follows (event interrupt) */
void DMA1_Stream7_IRQHandler(void)
{
    uint32_t f = dma_flags(TOF_DMA, TOF_DMA_TX_INDEX);
    dma_clear_flags(TOF_DMA, TOF_DMA_TX_INDEX, f);
    if ((f & DMA_FLAG_TE) != 0u && s_state != ST_IDLE) {
        TOF_I2C->CR1 |= I2C_CR1_STOP;
        finish(I2C_RES_BUS_ERROR);
    }
}

/* RX DMA complete: NACK was sent automatically (LAST), generate STOP */
void DMA1_Stream2_IRQHandler(void)
{
    uint32_t f = dma_flags(TOF_DMA, TOF_DMA_RX_INDEX);
    dma_clear_flags(TOF_DMA, TOF_DMA_RX_INDEX, f);
    if (s_state != ST_RX_DMA) {
        return;
    }
    TOF_I2C->CR1 |= I2C_CR1_STOP;
    finish(((f & DMA_FLAG_TE) != 0u) ? I2C_RES_BUS_ERROR : I2C_RES_OK);
}
