/**
 * @file    startup_stm32f405.c
 * @brief   Vector table and reset handler for STM32F405xx (GCC, C only).
 *
 * The IRQ list was generated from IRQn_Type in the vendored CMSIS header
 * stm32f405xx.h (82 device vectors, positions 61/62/78/79 reserved).
 * Every handler is a weak alias of Default_Handler and can be overridden by
 * defining a function with the same name.
 *
 * Shared by the bootloader and the application; the linker script decides
 * where .isr_vector lands and provides the section boundary symbols.
 */
#include <stdint.h>
#include "stm32f4xx.h"

#define WEAK_ALIAS  __attribute__((weak, alias("Default_Handler")))

/* Linker script symbols */
extern uint32_t _sidata;    /* .data load address in flash */
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _sccm_bss;  /* CCM zero-initialised data (may be empty) */
extern uint32_t _eccm_bss;
extern uint32_t _estack;

int  main(void);
void Reset_Handler(void);
void Default_Handler(void);

/**
 * Hook called first thing after reset, before .data/.bss initialisation.
 * Must not rely on initialised globals. The bootloader uses it to jump into
 * the application on a pristine, freshly reset MCU.
 */
void startup_early_hook(void) __attribute__((weak));
void startup_early_hook(void) { }

/* Cortex-M4 core exceptions */
void NMI_Handler(void)          WEAK_ALIAS;
void HardFault_Handler(void)    WEAK_ALIAS;
void MemManage_Handler(void)    WEAK_ALIAS;
void BusFault_Handler(void)     WEAK_ALIAS;
void UsageFault_Handler(void)   WEAK_ALIAS;
void SVC_Handler(void)          WEAK_ALIAS;
void DebugMon_Handler(void)     WEAK_ALIAS;
void PendSV_Handler(void)       WEAK_ALIAS;
void SysTick_Handler(void)      WEAK_ALIAS;

/* STM32F405 peripheral interrupts */
void WWDG_IRQHandler(void)        WEAK_ALIAS;
void PVD_IRQHandler(void)        WEAK_ALIAS;
void TAMP_STAMP_IRQHandler(void)        WEAK_ALIAS;
void RTC_WKUP_IRQHandler(void)        WEAK_ALIAS;
void FLASH_IRQHandler(void)        WEAK_ALIAS;
void RCC_IRQHandler(void)        WEAK_ALIAS;
void EXTI0_IRQHandler(void)        WEAK_ALIAS;
void EXTI1_IRQHandler(void)        WEAK_ALIAS;
void EXTI2_IRQHandler(void)        WEAK_ALIAS;
void EXTI3_IRQHandler(void)        WEAK_ALIAS;
void EXTI4_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream0_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream1_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream2_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream3_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream4_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream5_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream6_IRQHandler(void)        WEAK_ALIAS;
void ADC_IRQHandler(void)        WEAK_ALIAS;
void CAN1_TX_IRQHandler(void)        WEAK_ALIAS;
void CAN1_RX0_IRQHandler(void)        WEAK_ALIAS;
void CAN1_RX1_IRQHandler(void)        WEAK_ALIAS;
void CAN1_SCE_IRQHandler(void)        WEAK_ALIAS;
void EXTI9_5_IRQHandler(void)        WEAK_ALIAS;
void TIM1_BRK_TIM9_IRQHandler(void)        WEAK_ALIAS;
void TIM1_UP_TIM10_IRQHandler(void)        WEAK_ALIAS;
void TIM1_TRG_COM_TIM11_IRQHandler(void)        WEAK_ALIAS;
void TIM1_CC_IRQHandler(void)        WEAK_ALIAS;
void TIM2_IRQHandler(void)        WEAK_ALIAS;
void TIM3_IRQHandler(void)        WEAK_ALIAS;
void TIM4_IRQHandler(void)        WEAK_ALIAS;
void I2C1_EV_IRQHandler(void)        WEAK_ALIAS;
void I2C1_ER_IRQHandler(void)        WEAK_ALIAS;
void I2C2_EV_IRQHandler(void)        WEAK_ALIAS;
void I2C2_ER_IRQHandler(void)        WEAK_ALIAS;
void SPI1_IRQHandler(void)        WEAK_ALIAS;
void SPI2_IRQHandler(void)        WEAK_ALIAS;
void USART1_IRQHandler(void)        WEAK_ALIAS;
void USART2_IRQHandler(void)        WEAK_ALIAS;
void USART3_IRQHandler(void)        WEAK_ALIAS;
void EXTI15_10_IRQHandler(void)        WEAK_ALIAS;
void RTC_Alarm_IRQHandler(void)        WEAK_ALIAS;
void OTG_FS_WKUP_IRQHandler(void)        WEAK_ALIAS;
void TIM8_BRK_TIM12_IRQHandler(void)        WEAK_ALIAS;
void TIM8_UP_TIM13_IRQHandler(void)        WEAK_ALIAS;
void TIM8_TRG_COM_TIM14_IRQHandler(void)        WEAK_ALIAS;
void TIM8_CC_IRQHandler(void)        WEAK_ALIAS;
void DMA1_Stream7_IRQHandler(void)        WEAK_ALIAS;
void FSMC_IRQHandler(void)        WEAK_ALIAS;
void SDIO_IRQHandler(void)        WEAK_ALIAS;
void TIM5_IRQHandler(void)        WEAK_ALIAS;
void SPI3_IRQHandler(void)        WEAK_ALIAS;
void UART4_IRQHandler(void)        WEAK_ALIAS;
void UART5_IRQHandler(void)        WEAK_ALIAS;
void TIM6_DAC_IRQHandler(void)        WEAK_ALIAS;
void TIM7_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream0_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream1_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream2_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream3_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream4_IRQHandler(void)        WEAK_ALIAS;
void CAN2_TX_IRQHandler(void)        WEAK_ALIAS;
void CAN2_RX0_IRQHandler(void)        WEAK_ALIAS;
void CAN2_RX1_IRQHandler(void)        WEAK_ALIAS;
void CAN2_SCE_IRQHandler(void)        WEAK_ALIAS;
void OTG_FS_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream5_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream6_IRQHandler(void)        WEAK_ALIAS;
void DMA2_Stream7_IRQHandler(void)        WEAK_ALIAS;
void USART6_IRQHandler(void)        WEAK_ALIAS;
void I2C3_EV_IRQHandler(void)        WEAK_ALIAS;
void I2C3_ER_IRQHandler(void)        WEAK_ALIAS;
void OTG_HS_EP1_OUT_IRQHandler(void)        WEAK_ALIAS;
void OTG_HS_EP1_IN_IRQHandler(void)        WEAK_ALIAS;
void OTG_HS_WKUP_IRQHandler(void)        WEAK_ALIAS;
void OTG_HS_IRQHandler(void)        WEAK_ALIAS;
void RNG_IRQHandler(void)        WEAK_ALIAS;
void FPU_IRQHandler(void)        WEAK_ALIAS;

typedef void (*vector_fn_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_fn_t g_vector_table[16 + 82] = {
    (vector_fn_t)(uintptr_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    WWDG_IRQHandler,                        /*  0 */
    PVD_IRQHandler,                         /*  1 */
    TAMP_STAMP_IRQHandler,                  /*  2 */
    RTC_WKUP_IRQHandler,                    /*  3 */
    FLASH_IRQHandler,                       /*  4 */
    RCC_IRQHandler,                         /*  5 */
    EXTI0_IRQHandler,                       /*  6 */
    EXTI1_IRQHandler,                       /*  7 */
    EXTI2_IRQHandler,                       /*  8 */
    EXTI3_IRQHandler,                       /*  9 */
    EXTI4_IRQHandler,                       /* 10 */
    DMA1_Stream0_IRQHandler,                /* 11 */
    DMA1_Stream1_IRQHandler,                /* 12 */
    DMA1_Stream2_IRQHandler,                /* 13 */
    DMA1_Stream3_IRQHandler,                /* 14 */
    DMA1_Stream4_IRQHandler,                /* 15 */
    DMA1_Stream5_IRQHandler,                /* 16 */
    DMA1_Stream6_IRQHandler,                /* 17 */
    ADC_IRQHandler,                         /* 18 */
    CAN1_TX_IRQHandler,                     /* 19 */
    CAN1_RX0_IRQHandler,                    /* 20 */
    CAN1_RX1_IRQHandler,                    /* 21 */
    CAN1_SCE_IRQHandler,                    /* 22 */
    EXTI9_5_IRQHandler,                     /* 23 */
    TIM1_BRK_TIM9_IRQHandler,               /* 24 */
    TIM1_UP_TIM10_IRQHandler,               /* 25 */
    TIM1_TRG_COM_TIM11_IRQHandler,          /* 26 */
    TIM1_CC_IRQHandler,                     /* 27 */
    TIM2_IRQHandler,                        /* 28 */
    TIM3_IRQHandler,                        /* 29 */
    TIM4_IRQHandler,                        /* 30 */
    I2C1_EV_IRQHandler,                     /* 31 */
    I2C1_ER_IRQHandler,                     /* 32 */
    I2C2_EV_IRQHandler,                     /* 33 */
    I2C2_ER_IRQHandler,                     /* 34 */
    SPI1_IRQHandler,                        /* 35 */
    SPI2_IRQHandler,                        /* 36 */
    USART1_IRQHandler,                      /* 37 */
    USART2_IRQHandler,                      /* 38 */
    USART3_IRQHandler,                      /* 39 */
    EXTI15_10_IRQHandler,                   /* 40 */
    RTC_Alarm_IRQHandler,                   /* 41 */
    OTG_FS_WKUP_IRQHandler,                 /* 42 */
    TIM8_BRK_TIM12_IRQHandler,              /* 43 */
    TIM8_UP_TIM13_IRQHandler,               /* 44 */
    TIM8_TRG_COM_TIM14_IRQHandler,          /* 45 */
    TIM8_CC_IRQHandler,                     /* 46 */
    DMA1_Stream7_IRQHandler,                /* 47 */
    FSMC_IRQHandler,                        /* 48 */
    SDIO_IRQHandler,                        /* 49 */
    TIM5_IRQHandler,                        /* 50 */
    SPI3_IRQHandler,                        /* 51 */
    UART4_IRQHandler,                       /* 52 */
    UART5_IRQHandler,                       /* 53 */
    TIM6_DAC_IRQHandler,                    /* 54 */
    TIM7_IRQHandler,                        /* 55 */
    DMA2_Stream0_IRQHandler,                /* 56 */
    DMA2_Stream1_IRQHandler,                /* 57 */
    DMA2_Stream2_IRQHandler,                /* 58 */
    DMA2_Stream3_IRQHandler,                /* 59 */
    DMA2_Stream4_IRQHandler,                /* 60 */
    0,                                       /* 61 reserved */
    0,                                       /* 62 reserved */
    CAN2_TX_IRQHandler,                     /* 63 */
    CAN2_RX0_IRQHandler,                    /* 64 */
    CAN2_RX1_IRQHandler,                    /* 65 */
    CAN2_SCE_IRQHandler,                    /* 66 */
    OTG_FS_IRQHandler,                      /* 67 */
    DMA2_Stream5_IRQHandler,                /* 68 */
    DMA2_Stream6_IRQHandler,                /* 69 */
    DMA2_Stream7_IRQHandler,                /* 70 */
    USART6_IRQHandler,                      /* 71 */
    I2C3_EV_IRQHandler,                     /* 72 */
    I2C3_ER_IRQHandler,                     /* 73 */
    OTG_HS_EP1_OUT_IRQHandler,              /* 74 */
    OTG_HS_EP1_IN_IRQHandler,               /* 75 */
    OTG_HS_WKUP_IRQHandler,                 /* 76 */
    OTG_HS_IRQHandler,                      /* 77 */
    0,                                       /* 78 reserved */
    0,                                       /* 79 reserved */
    RNG_IRQHandler,                         /* 80 */
    FPU_IRQHandler,                         /* 81 */
};

__attribute__((noreturn, optimize("no-tree-loop-distribute-patterns")))
void Reset_Handler(void)
{
    /* FPU full access (CP10/CP11) before any compiler generated VFP code. */
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    __DSB();
    __ISB();

    startup_early_hook();

    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0u;
    }
    for (uint32_t *dst = &_sccm_bss; dst < &_eccm_bss;) {
        *dst++ = 0u;
    }

    SCB->VTOR = (uint32_t)(uintptr_t)g_vector_table;
    __DSB();

    (void)main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    /* Unexpected interrupt: stop here so a debugger shows the culprit
     * (SCB->ICSR VECTACTIVE). The watchdog resets the MCU in the field. */
    for (;;) {
    }
}
