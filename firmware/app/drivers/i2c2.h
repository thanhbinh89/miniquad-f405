/**
 * @file    i2c2.h
 * @brief   Non-blocking I2C2 master: interrupt driven protocol phases, DMA
 *          for the data phases (TX: DMA1 Stream7 ch7, RX: DMA1 Stream2 ch7).
 *
 * One transaction = optional write phase + optional read phase with a
 * repeated START in between (the usual "write register index, read data").
 * Completion is reported through i2c2_done()/i2c2_result(); a stuck bus is
 * detected by i2c2_poll() (timeout) and recovered with 9 clock pulses.
 */
#ifndef I2C2_H
#define I2C2_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    I2C_RES_NONE = 0,
    I2C_RES_OK,
    I2C_RES_NACK,
    I2C_RES_BUS_ERROR,
    I2C_RES_TIMEOUT,
} i2c_result_t;

void         i2c2_init(uint32_t hz);
/**
 * Start a transaction. Buffers must stay valid (and live in SRAM, not CCM)
 * until completion. @return false if the bus is busy.
 */
bool         i2c2_start(uint8_t addr7, const uint8_t *wbuf, uint16_t wlen, uint8_t *rbuf, uint16_t rlen);
bool         i2c2_busy(void);
/** Result of the last transaction (I2C_RES_NONE while running). */
i2c_result_t i2c2_result(void);
/** Call periodically from the main loop: timeout supervision. */
void         i2c2_poll(uint32_t now_ms);
uint32_t     i2c2_errors(void);

#endif /* I2C2_H */
