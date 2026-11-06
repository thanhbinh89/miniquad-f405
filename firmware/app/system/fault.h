/**
 * @file    fault.h
 * @brief   Fault handling: HardFault / MemManage / BusFault / UsageFault.
 *
 * On a fault the motors are forced off at register level, the faulting
 * context (PC, LR, xPSR, CFSR, HFSR, MMFAR, BFAR) is stored in a no-init RAM
 * record and the MCU resets. After the reboot the record is reported to the
 * configurator (MSG_FAULT_INFO) and in the log.
 */
#ifndef FAULT_H
#define FAULT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FAULT_NONE = 0,
    FAULT_HARD,
    FAULT_MEMMANAGE,
    FAULT_BUS,
    FAULT_USAGE,
    FAULT_NMI,
    FAULT_STACK,        /* stack guard (MPU) hit */
} fault_type_t;

typedef struct {
    uint32_t magic;
    uint32_t type;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t uptime_ms;
} fault_record_t;

/** Enable the configurable fault handlers, divide-by-zero trap, stack guard. */
void fault_init(void);
/** Last recorded fault (survives the reset). */
bool fault_last(fault_record_t *out);
void fault_clear(void);
/** Free bytes never touched on the stack (stack painting). */
uint32_t fault_stack_free(void);

/** Called by the handlers; also usable for fatal software errors. */
__attribute__((noreturn)) void fault_fatal(fault_type_t type, const uint32_t *frame);

#endif /* FAULT_H */
