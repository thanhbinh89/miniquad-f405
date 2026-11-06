/**
 * @file    board.h
 * @brief   MiniQuad-F405 board definition - single source of truth for pins,
 *          peripheral instances, DMA streams and interrupt priorities.
 *
 * Derived from the EasyEDA project "MiniQuad-F405" netlist
 * (U3 = STM32F405RGT6, LQFP64). LQFP64 pin numbers are given in comments so
 * the mapping can be checked against the schematic by eye.
 *
 * Resource map (keep it conflict free - every DMA stream is used once):
 *
 *   Function        Peripheral  Pins                          DMA
 *   --------------  ----------  ----------------------------  ------------------
 *   IMU             SPI1        PA5/PA6/PA7, CS PA4, INT PA3  RX DMA2 S0 C3, TX DMA2 S3 C3
 *   Link (PC/web)   USART1      PA9 TX / PA10 RX  (J2)        RX DMA2 S2 C4, TX DMA2 S7 C4
 *   ToF             I2C2        PB10/PB11, XSHUT PB12,        RX DMA1 S2 C7, TX DMA1 S7 C7
 *                               GPIO1 PB13
 *   Battery/temp    ADC1        PC2 (IN12), VREFINT, TEMP     DMA2 S4 C0 (circular)
 *   Motors          TIM3 CH1-4  PB4/PB5/PB0/PB1               - (preload registers)
 *   Buzzer          TIM12 CH1   PB14                          -
 *   PPM input       EXTI11      PC11 (J3.4, "UART3_RX")       - (TIM2 timestamps)
 *   Time base       TIM2        32-bit free running 1 MHz     -
 *   Status LED      GPIO        PC12 (active low)             -
 *
 * Note: PC10/PC11 (J3) have no timer channel on the STM32F405, therefore PPM
 * is decoded with an EXTI edge interrupt time-stamped by TIM2 (1 us).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx.h"

#define BOARD_NAME              "MINIQUAD-F405"
#define BOARD_ID                0x01u
#define BOARD_HSE_HZ            8000000u    /* Y1 KDS DSX321G 8 MHz */

/* ------------------------------------------------------------------------ */
/* Clocks (fixed by clock_init(): SYSCLK 168 MHz, APB1 42 MHz, APB2 84 MHz)  */
/* ------------------------------------------------------------------------ */
#define SYSCLK_HZ               168000000u
#define HCLK_HZ                 168000000u
#define PCLK1_HZ                42000000u
#define PCLK2_HZ                84000000u
#define TIMCLK1_HZ              84000000u   /* TIM2-7, TIM12-14 (APB1 x2) */
#define TIMCLK2_HZ              168000000u  /* TIM1, TIM8-11    (APB2 x2) */

/* ------------------------------------------------------------------------ */
/* Status LED: D2 (green) cathode on PC12, anode via R5 100R to +3V3        */
/* ------------------------------------------------------------------------ */
#define LED_PORT                GPIOC
#define LED_PIN                 12u         /* LQFP64 pin 53 */
#define LED_ACTIVE_LOW          1

/* ------------------------------------------------------------------------ */
/* Link UART: USART1 on J2 (debug connector, also STM32 ROM bootloader)     */
/*   J2: 1 +3V3, 2 SWDIO, 3 SWCLK, 4 NRST, 5 UART1_TX, 6 UART1_RX, 7 GND     */
/* ------------------------------------------------------------------------ */
#define LINK_USART              USART1
#define LINK_USART_IRQn         USART1_IRQn
#define LINK_TX_PORT            GPIOA
#define LINK_TX_PIN             9u          /* pin 42, J2.5 */
#define LINK_RX_PORT            GPIOA
#define LINK_RX_PIN             10u         /* pin 43, J2.6 */
#define LINK_GPIO_AF            7u
#define LINK_BAUD               460800u
#define LINK_DMA                DMA2
#define LINK_DMA_RX_STREAM      DMA2_Stream2
#define LINK_DMA_RX_INDEX       2u
#define LINK_DMA_TX_STREAM      DMA2_Stream7
#define LINK_DMA_TX_INDEX       7u
#define LINK_DMA_TX_IRQn        DMA2_Stream7_IRQn
#define LINK_DMA_CHANNEL        4u

/* ------------------------------------------------------------------------ */
/* IMU: ICM-42688-P (U4) on SPI1, 33R series resistors R10..R13             */
/* ------------------------------------------------------------------------ */
#define IMU_SPI                 SPI1
#define IMU_SCK_PORT            GPIOA
#define IMU_SCK_PIN             5u          /* pin 21 */
#define IMU_MISO_PORT           GPIOA
#define IMU_MISO_PIN            6u          /* pin 22 */
#define IMU_MOSI_PORT           GPIOA
#define IMU_MOSI_PIN            7u          /* pin 23 */
#define IMU_SPI_AF              5u
#define IMU_CS_PORT             GPIOA
#define IMU_CS_PIN              4u          /* pin 20 */
#define IMU_INT_PORT            GPIOA
#define IMU_INT_PIN             3u          /* pin 17, EXTI3 */
#define IMU_INT_EXTI_IRQn       EXTI3_IRQn
#define IMU_DMA                 DMA2
#define IMU_DMA_RX_STREAM       DMA2_Stream0
#define IMU_DMA_RX_INDEX        0u
#define IMU_DMA_RX_IRQn         DMA2_Stream0_IRQn
#define IMU_DMA_TX_STREAM       DMA2_Stream3
#define IMU_DMA_TX_INDEX        3u
#define IMU_DMA_CHANNEL         3u

/* ------------------------------------------------------------------------ */
/* ToF: VL53L1CB / VL53L1X (U5) on I2C2 through 2N7002DW level shifter      */
/*   (3V3 <-> 2V8, 10k pull-ups each side). XSHUT and GPIO1 are in the 2V8  */
/*   domain (47k pull-ups to 2V8, 1k series to the MCU): XSHUT must be      */
/*   driven open-drain only.                                                */
/* ------------------------------------------------------------------------ */
#define TOF_I2C                 I2C2
#define TOF_I2C_EV_IRQn         I2C2_EV_IRQn
#define TOF_I2C_ER_IRQn         I2C2_ER_IRQn
#define TOF_SCL_PORT            GPIOB
#define TOF_SCL_PIN             10u         /* pin 29 */
#define TOF_SDA_PORT            GPIOB
#define TOF_SDA_PIN             11u         /* pin 30 */
#define TOF_I2C_AF              4u
#define TOF_I2C_HZ              400000u
#define TOF_XSHUT_PORT          GPIOB
#define TOF_XSHUT_PIN           12u         /* pin 33, open-drain */
#define TOF_GPIO1_PORT          GPIOB
#define TOF_GPIO1_PIN           13u         /* pin 34, EXTI13 */
#define TOF_I2C_ADDR7           0x29u       /* 0x52 8-bit */
#define TOF_DMA                 DMA1
#define TOF_DMA_RX_STREAM       DMA1_Stream2
#define TOF_DMA_RX_INDEX        2u
#define TOF_DMA_RX_IRQn         DMA1_Stream2_IRQn
#define TOF_DMA_TX_STREAM       DMA1_Stream7
#define TOF_DMA_TX_INDEX        7u
#define TOF_DMA_TX_IRQn         DMA1_Stream7_IRQn
#define TOF_DMA_CHANNEL         7u

/* ------------------------------------------------------------------------ */
/* Battery sense: R2/R3 100k/100k divider + C11 100n -> PC2 (ADC123_IN12)   */
/* ------------------------------------------------------------------------ */
#define VBAT_PORT               GPIOC
#define VBAT_PIN                2u          /* pin 10 */
#define VBAT_ADC_CHANNEL        12u
#define VBAT_DIVIDER            2.0f        /* (R2 + R3) / R3 */
#define ADC_VREFINT_CHANNEL     17u
#define ADC_TEMP_CHANNEL        16u
#define ADC_DMA                 DMA2
#define ADC_DMA_STREAM          DMA2_Stream4
#define ADC_DMA_INDEX           4u
#define ADC_DMA_CHANNEL         0u

/* ------------------------------------------------------------------------ */
/* Motors: IRLML2502 low-side switches, gate 100R + 10k pull-down           */
/*   PCB position (top view): M1 right-top, M2 left-top, M3 left-bottom,    */
/*   M4 right-bottom. Connector pin 1 (marked "+") = VBAT.                  */
/* ------------------------------------------------------------------------ */
#define MOTOR_COUNT             4u
#define MOTOR_TIM               TIM3
#define MOTOR_TIM_CLK_HZ        TIMCLK1_HZ
#define MOTOR_GPIO_AF           2u
#define MOTOR1_PORT             GPIOB
#define MOTOR1_PIN              4u          /* pin 56, TIM3_CH1 */
#define MOTOR2_PORT             GPIOB
#define MOTOR2_PIN              5u          /* pin 57, TIM3_CH2 */
#define MOTOR3_PORT             GPIOB
#define MOTOR3_PIN              0u          /* pin 26, TIM3_CH3 */
#define MOTOR4_PORT             GPIOB
#define MOTOR4_PIN              1u          /* pin 27, TIM3_CH4 */
#define MOTOR_PWM_HZ            32000u

/* ------------------------------------------------------------------------ */
/* Buzzer: MLT-5030 (4 kHz magnetic transducer) via MMBT3904, fed from VBAT */
/* ------------------------------------------------------------------------ */
#define BUZZER_TIM              TIM12
#define BUZZER_PORT             GPIOB
#define BUZZER_PIN              14u         /* pin 35, TIM12_CH1 */
#define BUZZER_GPIO_AF          9u
#define BUZZER_DEFAULT_HZ       4000u

/* ------------------------------------------------------------------------ */
/* RC input: PPM on J3 pin 4 (PC11). J3: 1 GND, 2 RX_VCC, 3 PC10, 4 PC11     */
/* ------------------------------------------------------------------------ */
#define PPM_PORT                GPIOC
#define PPM_PIN                 11u         /* pin 52, EXTI11 */
#define PPM_EXTI_IRQn           EXTI15_10_IRQn

/* ------------------------------------------------------------------------ */
/* Free-running 32-bit microsecond timer                                    */
/* ------------------------------------------------------------------------ */
#define TIMEBASE_TIM            TIM2

/* ------------------------------------------------------------------------ */
/* Interrupt priorities (NVIC priority group 4: 16 preemption levels).      */
/* Lower number = higher priority.                                          */
/* ------------------------------------------------------------------------ */
#define IRQ_PRIO_PPM            1u   /* EXTI15_10: PPM edge time stamp, ToF DRDY flag */
#define IRQ_PRIO_IMU            2u   /* EXTI3 data ready + SPI1 RX DMA complete     */
#define IRQ_PRIO_SYSTICK        3u   /* 1 kHz tick + flight-loop stall supervisor   */
#define IRQ_PRIO_I2C            5u   /* I2C2 event/error + its DMA streams          */
#define IRQ_PRIO_LINK           6u   /* USART1 TX DMA                               */
#define IRQ_PRIO_FLIGHT         8u   /* software interrupt running the flight loop  */

/* The flight loop runs inside this IRQ, pended by software after every IMU
 * sample. FSMC has no pins on LQFP64, so its vector is free. */
#define FLIGHT_SWI_IRQn         FSMC_IRQn
#define FLIGHT_SWI_IRQHandler   FSMC_IRQHandler

#endif /* BOARD_H */
