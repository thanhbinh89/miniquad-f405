/**
 * @file    adc.c
 * @brief   ADC1 + DMA2 Stream4 circular scan.
 */
#include "adc.h"

#include "board.h"
#include "dma.h"
#include "gpio.h"

#define ADC_CHANNELS    3u
#define ADC_SCANS       32u

static volatile uint16_t s_buf[ADC_CHANNELS * ADC_SCANS];

void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    gpio_clock_enable(VBAT_PORT);
    gpio_set_pull(VBAT_PORT, VBAT_PIN, GPIO_PULL_NONE);
    gpio_set_mode(VBAT_PORT, VBAT_PIN, GPIO_MODE_ANALOG);

    /* ADCCLK = PCLK2 / 4 = 21 MHz, enable temperature sensor + VREFINT */
    ADC->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;

    ADC_TypeDef *adc = ADC1;
    adc->CR2 = 0u;
    adc->CR1 = ADC_CR1_SCAN;                            /* 12 bit */
    /* 480 cycles for channels 12, 16, 17 (SMPR1 covers channels 10..18) */
    adc->SMPR1 = (7u << ((VBAT_ADC_CHANNEL - 10u) * 3u)) |
                 (7u << ((ADC_TEMP_CHANNEL - 10u) * 3u)) |
                 (7u << ((ADC_VREFINT_CHANNEL - 10u) * 3u));
    adc->SQR1 = (ADC_CHANNELS - 1u) << ADC_SQR1_L_Pos;
    adc->SQR3 = (VBAT_ADC_CHANNEL << 0) | (ADC_VREFINT_CHANNEL << 5) | (ADC_TEMP_CHANNEL << 10);

    DMA_Stream_TypeDef *s = ADC_DMA_STREAM;
    dma_stream_disable(s);
    dma_clear_flags(ADC_DMA, ADC_DMA_INDEX, DMA_FLAG_ALL);
    s->PAR = (uint32_t)&adc->DR;
    s->M0AR = (uint32_t)s_buf;
    s->NDTR = ADC_CHANNELS * ADC_SCANS;
    s->FCR = 0u;
    s->CR = DMA_CR_CHSEL(ADC_DMA_CHANNEL) | DMA_SxCR_PL_0 | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 |
            DMA_SxCR_MINC | DMA_SxCR_CIRC;
    s->CR |= DMA_SxCR_EN;

    adc->CR2 = ADC_CR2_ADON | ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_DDS;
    for (volatile int i = 0; i < 1000; i++) {           /* tSTAB ~3 us */
    }
    adc->CR2 |= ADC_CR2_SWSTART;
}

void adc_read(adc_raw_t *out)
{
    uint32_t sum[ADC_CHANNELS] = { 0u, 0u, 0u };
    for (uint32_t i = 0u; i < ADC_SCANS; i++) {
        for (uint32_t c = 0u; c < ADC_CHANNELS; c++) {
            sum[c] += s_buf[i * ADC_CHANNELS + c];
        }
    }
    out->vbat = (uint16_t)(sum[0] / ADC_SCANS);
    out->vref = (uint16_t)(sum[1] / ADC_SCANS);
    out->temp = (uint16_t)(sum[2] / ADC_SCANS);
}
