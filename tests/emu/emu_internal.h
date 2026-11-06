/**
 * @file    emu_internal.h
 * @brief   Shared helpers of the emulator modules (flash, UART, boot).
 */
#ifndef EMU_INTERNAL_H
#define EMU_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- flash (STM32F405: 4x16 KB, 64 KB, 7x128 KB) ---- */
#define EMU_SECTORS     12
int      emu_flash_sector(uint32_t addr);       /* -1 if not in flash */
uint32_t emu_flash_sector_base(int sector);
uint32_t emu_flash_sector_size(int sector);
/** Erase one sector; returns the time it takes (ms). */
uint32_t emu_flash_erase(int sector);
/** Program (NOR semantics: bits can only be cleared); false on verify error. */
bool     emu_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
uint8_t *emu_flash_mem(void);

/* ---- UART: RX FIFO + 4 KB TX ring drained at the baud rate ---- */
size_t   emu_uart_read(uint8_t *buf, size_t max);
size_t   emu_uart_write(const void *data, size_t len);     /* all or nothing */
size_t   emu_uart_tx_free(void);
bool     emu_uart_tx_idle(void);
/** The CPU is blocked (flash erase): nothing new is processed or sent. */
void     emu_cpu_block(uint32_t ms);
bool     emu_cpu_blocked(void);

/* ---- boot ---- */
/** ROM bootloader GO to flash: start the code in sector 0 (our bootloader). */
void     emu_start_from_flash(void);

/* ---- ROM bootloader (emu_rom.c) ---- */
void     rom_reset(void);
void     rom_tick(void);

#endif /* EMU_INTERNAL_H */
