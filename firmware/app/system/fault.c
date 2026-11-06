/**
 * @file    fault.c
 * @brief   Fault handlers, fault record, stack painting and MPU stack guard.
 */
#include "fault.h"

#include <string.h>
#include "board.h"
#include "clock.h"
#include "systime.h"
#include "vectors.h"

#define FAULT_MAGIC         0xFA017ED5u
#define STACK_PAINT         0xC0FFEE11u
#define STACK_GUARD_SIZE    32u

extern uint32_t _sstack;
extern uint32_t _estack;

__attribute__((section(".noinit"))) static fault_record_t s_fault;

/* Motors off without relying on any driver state: force the four timer
 * outputs to plain GPIO outputs driven low. */
static void motors_kill(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    GPIOB->BSRR = (1u << (MOTOR1_PIN + 16u)) | (1u << (MOTOR2_PIN + 16u)) |
                  (1u << (MOTOR3_PIN + 16u)) | (1u << (MOTOR4_PIN + 16u));
    const uint32_t pins[4] = { MOTOR1_PIN, MOTOR2_PIN, MOTOR3_PIN, MOTOR4_PIN };
    for (int i = 0; i < 4; i++) {
        uint32_t p = pins[i];
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (p * 2u))) | (1u << (p * 2u));
    }
}

void fault_fatal(fault_type_t type, const uint32_t *frame)
{
    motors_kill();
    s_fault.magic = FAULT_MAGIC;
    s_fault.type = (uint32_t)type;
    s_fault.pc = (frame != NULL) ? frame[6] : 0u;
    s_fault.lr = (frame != NULL) ? frame[5] : 0u;
    s_fault.psr = (frame != NULL) ? frame[7] : 0u;
    s_fault.cfsr = SCB->CFSR;
    s_fault.hfsr = SCB->HFSR;
    s_fault.mmfar = SCB->MMFAR;
    s_fault.bfar = SCB->BFAR;
    s_fault.uptime_ms = time_ms();
    __DSB();
    NVIC_SystemReset();
    for (;;) {
    }
}

/* C part of the handlers: frame = stacked r0..r3, r12, lr, pc, xpsr */
void fault_handler_c(const uint32_t *frame, uint32_t type);
void fault_handler_c(const uint32_t *frame, uint32_t type)
{
    /* MPU stack guard hit -> report as stack overflow */
    if (type == FAULT_MEMMANAGE && (SCB->CFSR & SCB_CFSR_MMARVALID_Msk) != 0u &&
        SCB->MMFAR >= (uint32_t)&_sstack && SCB->MMFAR < (uint32_t)&_sstack + STACK_GUARD_SIZE) {
        type = FAULT_STACK;
    }
    fault_fatal((fault_type_t)type, frame);
}

#define FAULT_TRAMPOLINE(name, code)                \
    __attribute__((naked)) void name(void);         \
    __attribute__((naked)) void name(void)          \
    {                                               \
        __asm volatile(                             \
            "tst lr, #4      \n"                    \
            "ite eq          \n"                    \
            "mrseq r0, msp   \n"                    \
            "mrsne r0, psp   \n"                    \
            "mov r1, %0      \n"                    \
            "b fault_handler_c \n"                  \
            :: "i"(code));                          \
    }

FAULT_TRAMPOLINE(HardFault_Handler, FAULT_HARD)
FAULT_TRAMPOLINE(MemManage_Handler, FAULT_MEMMANAGE)
FAULT_TRAMPOLINE(BusFault_Handler, FAULT_BUS)
FAULT_TRAMPOLINE(UsageFault_Handler, FAULT_USAGE)

void NMI_Handler(void)
{
    if ((RCC->CIR & RCC_CIR_CSSF) != 0u) {
        clock_on_css();             /* crystal failed: back to 168 MHz from HSI */
        return;
    }
    fault_fatal(FAULT_NMI, NULL);
}

static void stack_paint(void)
{
    uint32_t *p = &_sstack;
    uint32_t sp = __get_MSP();
    while ((uint32_t)p + 64u < sp) {
        *p++ = STACK_PAINT;
    }
}

uint32_t fault_stack_free(void)
{
    const uint32_t *p = &_sstack + STACK_GUARD_SIZE / 4u;
    uint32_t n = 0u;
    while (p < &_estack && *p == STACK_PAINT) {
        p++;
        n += 4u;
    }
    return n;
}

static void mpu_stack_guard(void)
{
    /* region 0: no access to the lowest 32 bytes of the (CCM) stack */
    MPU->RNR = 0u;
    MPU->RBAR = (uint32_t)&_sstack & MPU_RBAR_ADDR_Msk;
    MPU->RASR = (0u << MPU_RASR_AP_Pos) | MPU_RASR_XN_Msk |
                ((5u - 1u) << MPU_RASR_SIZE_Pos) |          /* 2^(4+1) = 32 bytes */
                MPU_RASR_ENABLE_Msk;
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
}

void fault_init(void)
{
    stack_paint();
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    mpu_stack_guard();
}

bool fault_last(fault_record_t *out)
{
    if (s_fault.magic != FAULT_MAGIC) {
        return false;
    }
    if (out != NULL) {
        *out = s_fault;
    }
    return true;
}

void fault_clear(void)
{
    memset(&s_fault, 0, sizeof(s_fault));
}
