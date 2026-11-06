/**
 * @file    icm42688.h
 * @brief   TDK ICM-42688-P 6-axis IMU on SPI1, data-ready driven DMA reads.
 *
 * Configuration: gyro +/-2000 dps, accel +/-16 g, ODR 1 kHz, low-noise mode,
 * anti-alias filters 258 Hz, INT1 push-pull active high pulsed on data ready,
 * pin 9 forced to FSYNC input (it is grounded through R14 on this board).
 *
 * Data path (no CPU polling):
 *   INT1 rising edge (EXTI3) -> 15-byte SPI DMA read (temp + accel + gyro)
 *   -> DMA complete -> raw sample published -> flight software IRQ pended.
 */
#ifndef ICM42688_H
#define ICM42688_H

#include <stdbool.h>
#include <stdint.h>

#define ICM42688_WHOAMI         0x47u
#define ICM42688_ODR_HZ         1000u
#define ICM42688_GYRO_DPS_LSB   (1.0f / 16.4f)
#define ICM42688_ACC_G_LSB      (1.0f / 2048.0f)

typedef struct {
    int16_t  temp;
    int16_t  acc[3];        /* sensor frame (X right, Y top edge, Z up) */
    int16_t  gyro[3];
    uint32_t t_us;          /* time stamp of the data-ready edge */
    uint32_t seq;
} icm42688_raw_t;

/** Blocking probe + configuration. Returns false if WHO_AM_I mismatches. */
bool     icm42688_init(void);
/** Enable the data-ready interrupt (starts the 1 kHz flight loop). */
void     icm42688_start(void);
void     icm42688_stop(void);
/** Copy the newest sample (consistent snapshot). */
bool     icm42688_get(icm42688_raw_t *out);
bool     icm42688_healthy(void);
uint8_t  icm42688_whoami(void);
uint32_t icm42688_errors(void);
float    icm42688_temp_c(int16_t raw);

/* interrupt entry points */
void     icm42688_drdy_isr(void);

#endif /* ICM42688_H */
