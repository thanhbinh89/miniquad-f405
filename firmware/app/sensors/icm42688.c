/**
 * @file    icm42688.c
 * @brief   ICM-42688-P driver.
 */
#include "icm42688.h"

#include <string.h>
#include "board.h"
#include "exti.h"
#include "gpio.h"
#include "spi1.h"
#include "systime.h"

/* bank 0 */
#define REG_DEVICE_CONFIG       0x11u
#define REG_INT_CONFIG          0x14u
#define REG_TEMP_DATA1          0x1Du
#define REG_INT_STATUS          0x2Du
#define REG_PWR_MGMT0           0x4Eu
#define REG_GYRO_CONFIG0        0x4Fu
#define REG_ACCEL_CONFIG0       0x50u
#define REG_GYRO_CONFIG1        0x51u
#define REG_GYRO_ACCEL_CONFIG0  0x52u
#define REG_INT_CONFIG1         0x64u
#define REG_INT_SOURCE0         0x65u
#define REG_WHO_AM_I            0x75u
#define REG_BANK_SEL            0x76u
/* bank 1 */
#define REG_GYRO_CONFIG_STATIC3 0x0Cu
#define REG_GYRO_CONFIG_STATIC4 0x0Du
#define REG_GYRO_CONFIG_STATIC5 0x0Eu
#define REG_INTF_CONFIG5        0x7Bu
/* bank 2 */
#define REG_ACCEL_CONFIG_STATIC2 0x03u
#define REG_ACCEL_CONFIG_STATIC3 0x04u
#define REG_ACCEL_CONFIG_STATIC4 0x05u

#define READ_FLAG               0x80u
#define BURST_LEN               14u         /* TEMP_DATA1 .. GYRO_DATA_Z0 */

/* AAF 258 Hz (datasheet section 5.3): delt, deltsqr, bitshift */
#define AAF_DELT                6u
#define AAF_DELTSQR             36u
#define AAF_BITSHIFT            10u

#define STALE_US                5000u

static uint8_t            s_tx[BURST_LEN + 1u];
static uint8_t            s_rx[BURST_LEN + 1u];
static icm42688_raw_t     s_sample;
static volatile uint32_t  s_seq;            /* seqlock for s_sample */
static volatile uint32_t  s_drdy_us;
static volatile uint32_t  s_errors;
static uint8_t            s_whoami;
static bool               s_ok;

static void cs(bool active)
{
    gpio_write(IMU_CS_PORT, IMU_CS_PIN, !active);
}

static void reg_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg, val };
    cs(true);
    spi1_transfer(tx, NULL, 2u);
    cs(false);
    delay_us(10u);
}

static uint8_t reg_read(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg | READ_FLAG), 0u };
    uint8_t rx[2];
    cs(true);
    spi1_transfer(tx, rx, 2u);
    cs(false);
    delay_us(2u);
    return rx[1];
}

static void bank(uint8_t b)
{
    reg_write(REG_BANK_SEL, b);
}

bool icm42688_init(void)
{
    gpio_clock_enable(IMU_CS_PORT);
    gpio_output(IMU_CS_PORT, IMU_CS_PIN, GPIO_OTYPE_PP, GPIO_SPEED_HIGH, true);
    gpio_clock_enable(IMU_INT_PORT);
    gpio_input(IMU_INT_PORT, IMU_INT_PIN, GPIO_PULL_DOWN);

    spi1_init(SPI1_DIV_128);
    delay_ms(2u);

    bank(0u);
    reg_write(REG_DEVICE_CONFIG, 0x01u);       /* soft reset */
    delay_ms(2u);

    s_whoami = reg_read(REG_WHO_AM_I);
    if (s_whoami != ICM42688_WHOAMI) {
        s_ok = false;
        return false;
    }

    /* pin 9 = FSYNC input (grounded through R14): never drive it as INT2 */
    bank(1u);
    uint8_t intf5 = reg_read(REG_INTF_CONFIG5);
    reg_write(REG_INTF_CONFIG5, (uint8_t)((intf5 & ~0x06u) | 0x02u));
    /* gyro anti-alias filter 258 Hz */
    reg_write(REG_GYRO_CONFIG_STATIC3, AAF_DELT);
    reg_write(REG_GYRO_CONFIG_STATIC4, (uint8_t)(AAF_DELTSQR & 0xFFu));
    reg_write(REG_GYRO_CONFIG_STATIC5, (uint8_t)((AAF_DELTSQR >> 8) | (AAF_BITSHIFT << 4)));
    /* accel anti-alias filter 258 Hz */
    bank(2u);
    reg_write(REG_ACCEL_CONFIG_STATIC2, (uint8_t)(AAF_DELT << 1));
    reg_write(REG_ACCEL_CONFIG_STATIC3, (uint8_t)(AAF_DELTSQR & 0xFFu));
    reg_write(REG_ACCEL_CONFIG_STATIC4, (uint8_t)((AAF_DELTSQR >> 8) | (AAF_BITSHIFT << 4)));
    bank(0u);

    /* INT1: push-pull, active high, pulsed; data ready routed to INT1 */
    reg_write(REG_INT_CONFIG, 0x03u);
    reg_write(REG_INT_CONFIG1, 0x60u);          /* 8 us pulse, no de-assert wait, ASYNC_RESET = 0 */
    reg_write(REG_INT_SOURCE0, 0x08u);          /* UI_DRDY_INT1_EN */

    /* +/-2000 dps, 1 kHz | +/-16 g, 1 kHz; UI filters: 2nd order, ODR/4 */
    reg_write(REG_GYRO_CONFIG0, 0x06u);
    reg_write(REG_ACCEL_CONFIG0, 0x06u);
    reg_write(REG_GYRO_CONFIG1, 0x16u);
    reg_write(REG_GYRO_ACCEL_CONFIG0, 0x11u);

    /* gyro + accel low-noise mode; wait > 200 us before any further write,
     * gyro needs ~45 ms to start */
    reg_write(REG_PWR_MGMT0, 0x0Fu);
    delay_ms(50u);

    bool ok = reg_read(REG_PWR_MGMT0) == 0x0Fu && reg_read(REG_GYRO_CONFIG0) == 0x06u;

    spi1_set_div(SPI1_DIV_8);
    memset(s_tx, 0, sizeof(s_tx));
    s_tx[0] = REG_TEMP_DATA1 | READ_FLAG;

    exti_config(IMU_INT_PORT, IMU_INT_PIN, EXTI_EDGE_RISING);
    NVIC_SetPriority(IMU_INT_EXTI_IRQn, IRQ_PRIO_IMU);
    NVIC_EnableIRQ(IMU_INT_EXTI_IRQn);

    s_ok = ok;
    return ok;
}

void icm42688_start(void)
{
    NVIC_SetPriority(FLIGHT_SWI_IRQn, IRQ_PRIO_FLIGHT);
    NVIC_EnableIRQ(FLIGHT_SWI_IRQn);
    exti_enable(IMU_INT_PIN, true);
}

void icm42688_stop(void)
{
    exti_enable(IMU_INT_PIN, false);
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void dma_done(void)
{
    cs(false);
    const uint8_t *d = &s_rx[1];
    s_seq++;
    __DMB();
    s_sample.temp = be16(&d[0]);
    s_sample.acc[0] = be16(&d[2]);
    s_sample.acc[1] = be16(&d[4]);
    s_sample.acc[2] = be16(&d[6]);
    s_sample.gyro[0] = be16(&d[8]);
    s_sample.gyro[1] = be16(&d[10]);
    s_sample.gyro[2] = be16(&d[12]);
    s_sample.t_us = s_drdy_us;
    s_sample.seq++;
    __DMB();
    s_seq++;
    NVIC_SetPendingIRQ(FLIGHT_SWI_IRQn);
}

void icm42688_drdy_isr(void)
{
    if (spi1_busy()) {
        s_errors++;             /* previous read still running: overrun */
        return;
    }
    s_drdy_us = time_us();
    cs(true);
    if (!spi1_transfer_dma(s_tx, s_rx, (uint16_t)(BURST_LEN + 1u), dma_done)) {
        cs(false);
        s_errors++;
    }
}

bool icm42688_get(icm42688_raw_t *out)
{
    uint32_t a, b;
    int tries = 0;
    do {
        a = s_seq;
        __DMB();
        *out = s_sample;
        __DMB();
        b = s_seq;
    } while ((a != b || (a & 1u) != 0u) && ++tries < 4);
    return a == b && (a & 1u) == 0u && out->seq != 0u;
}

bool icm42688_healthy(void)
{
    return s_ok && (time_us() - s_sample.t_us) < STALE_US;
}

uint8_t icm42688_whoami(void)
{
    return s_whoami;
}

uint32_t icm42688_errors(void)
{
    return s_errors;
}

float icm42688_temp_c(int16_t raw)
{
    return (float)raw / 132.48f + 25.0f;
}
