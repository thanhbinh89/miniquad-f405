/**
 * @file    emu_device.h
 * @brief   MiniQuad-F405 device emulator: the real protocol, bootloader,
 *          parameter, flight-core and notification code running on a
 *          simulated board (flash, UART at 460800 baud, quad physics).
 *
 * Modes follow the hardware exactly:
 *
 *   reset -> BOOT0 high            -> ROM bootloader (AN3155, emu_rom.c)
 *         -> sector 0 erased       -> dead (nothing answers)
 *         -> our bootloader (bl_core.c): stays on request / invalid app /
 *            a packet within 300 ms, otherwise starts the application
 *   application: comms.c + params + config_store (in emulated flash) +
 *                fc.c fed by tests/sim (IMU, PPM, ToF, battery)
 *
 * Two front-ends: emu_stdio.c (native, byte stream on stdin/stdout, used by
 * the Node integration tests) and emu_wasm.c (WebAssembly "demo mode" of the
 * web configurator).
 */
#ifndef EMU_DEVICE_H
#define EMU_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    EMU_MODE_DEAD = 0,          /* no bootloader in flash               */
    EMU_MODE_BOOTLOADER = 1,
    EMU_MODE_APP = 2,
    EMU_MODE_ROM = 3,           /* STM32 system memory bootloader        */
} emu_mode_t;

#define EMU_FLASH_BASE      0x08000000u
#define EMU_FLASH_SIZE      0x00100000u
#define EMU_UART_BYTES_MS   46.08           /* 460800 baud, 8N1 */

/** Power on with factory content (bootloader + a valid application).
 *  @param skip_window start the application directly (no 300 ms window) */
void       emu_init(bool skip_window);
/** Power cycle (RAM lost) / reset pin (shared RAM kept). */
void       emu_power_cycle(void);
void       emu_reset_pin(void);
void       emu_set_boot0(bool high);

/** Bytes from the host (wire). */
void       emu_rx(const uint8_t *data, size_t len);
/** Bytes towards the host; returns the count copied. */
size_t     emu_tx(uint8_t *out, size_t max);
/** Advance the emulation by @p ms milliseconds (1 ms steps). */
void       emu_run(uint32_t ms);

emu_mode_t emu_mode(void);
uint32_t   emu_uptime_ms(void);         /* since the last reset */

/* ---- virtual transmitter and environment ---- */
void       emu_rc_set(int ch, uint16_t us);     /* ch 0-based, 8 channels */
uint16_t   emu_rc_get(int ch);
void       emu_rc_enable(bool on);
void       emu_battery_set(float soc);          /* 0..1, new battery = 1   */
void       emu_vehicle_reset(void);             /* back on the ground      */
void       emu_kick(float roll_dps, float pitch_dps, float yaw_dps);
void       emu_tof_enable(bool on);
void       emu_inject_fault(void);              /* HardFault record + reset */

/* ---- outputs for the UI ---- */
bool       emu_led(void);
uint32_t   emu_buzzer_hz(void);
/** Ground truth: x, y, height (m), roll, pitch, yaw (deg), vbat (V), current (A),
 *  armed, motor duty x4, soc. Returns the number of values written (<= 14). */
int        emu_truth(float *out, int max);
const uint8_t *emu_flash(void);

#endif /* EMU_DEVICE_H */
