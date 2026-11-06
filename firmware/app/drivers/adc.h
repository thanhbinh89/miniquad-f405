/**
 * @file    adc.h
 * @brief   ADC1 continuous scan with circular DMA (DMA2 Stream4 ch0):
 *          VBAT divider (PC2/IN12), VREFINT (IN17), temperature (IN16).
 *
 * 480-cycle sampling (divider source impedance 50 kOhm, temperature sensor
 * >= 10 us), ADCCLK = 21 MHz -> ~14 k scans/s. The CPU never waits for a
 * conversion: adc_read() just averages the DMA buffer.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

typedef struct {
    uint16_t vbat;      /* averaged raw counts */
    uint16_t vref;
    uint16_t temp;
} adc_raw_t;

void adc_init(void);
void adc_read(adc_raw_t *out);

#endif /* ADC_H */
