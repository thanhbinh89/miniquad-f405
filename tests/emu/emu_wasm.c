/**
 * @file    emu_wasm.c
 * @brief   WebAssembly front-end of the device emulator (web "demo mode").
 *
 * The page copies host->device bytes into in_buf() and calls rx(n); after
 * run(ms) it reads tx() bytes from out_buf(). Everything else is optional
 * (virtual transmitter, ground truth for the 3D view, LED and buzzer).
 */
#include <stdint.h>

#include "emu_device.h"

#define EXPORT(name) __attribute__((export_name(name)))

static uint8_t s_in[8192];
static uint8_t s_out[16384];
static float   s_truth[16];

EXPORT("in_buf")   uint8_t *w_in_buf(void) { return s_in; }
EXPORT("out_buf")  uint8_t *w_out_buf(void) { return s_out; }
EXPORT("in_size")  int w_in_size(void) { return (int)sizeof(s_in); }
EXPORT("out_size") int w_out_size(void) { return (int)sizeof(s_out); }

EXPORT("init")     void w_init(int skip_window) { emu_init(skip_window != 0); }
EXPORT("rx")       void w_rx(int n)
{
    if (n > 0 && n <= (int)sizeof(s_in)) {
        emu_rx(s_in, (size_t)n);
    }
}
EXPORT("tx")       int w_tx(void) { return (int)emu_tx(s_out, sizeof(s_out)); }
EXPORT("run")      void w_run(int ms)
{
    if (ms > 0) {
        emu_run((uint32_t)ms);
    }
}
EXPORT("mode")     int w_mode(void) { return (int)emu_mode(); }
EXPORT("link_up")  int w_link_up(void) { return emu_link_up() ? 1 : 0; }
EXPORT("link_epoch") uint32_t w_link_epoch(void) { return emu_link_epoch(); }
EXPORT("usb_cable") void w_usb_cable(int plugged) { emu_usb_cable(plugged != 0); }
EXPORT("usb_cable_in") int w_usb_cable_in(void) { return emu_usb_cable_in() ? 1 : 0; }
EXPORT("board_rev") int w_board_rev(void) { return emu_board_rev(); }
EXPORT("uptime")   uint32_t w_uptime(void) { return emu_uptime_ms(); }

EXPORT("rc_set")   void w_rc_set(int ch, int us) { emu_rc_set(ch, (uint16_t)us); }
EXPORT("rc_get")   int w_rc_get(int ch) { return emu_rc_get(ch); }
EXPORT("rc_enable") void w_rc_enable(int on) { emu_rc_enable(on != 0); }
EXPORT("tof_enable") void w_tof_enable(int on) { emu_tof_enable(on != 0); }
EXPORT("boot0")    void w_boot0(int high) { emu_set_boot0(high != 0); }
EXPORT("power_cycle") void w_power_cycle(void) { emu_power_cycle(); }
EXPORT("reset_pin") void w_reset_pin(void) { emu_reset_pin(); }
EXPORT("vehicle_reset") void w_vehicle_reset(void) { emu_vehicle_reset(); }
EXPORT("battery_set") void w_battery_set(float soc) { emu_battery_set(soc); }
EXPORT("kick")     void w_kick(float r, float p, float y) { emu_kick(r, p, y); }
EXPORT("inject_fault") void w_inject_fault(void) { emu_inject_fault(); }

EXPORT("led")      int w_led(void) { return emu_led() ? 1 : 0; }
EXPORT("buzzer_hz") int w_buzzer_hz(void) { return (int)emu_buzzer_hz(); }
EXPORT("truth")    float *w_truth(void)
{
    emu_truth(s_truth, 14);
    return s_truth;
}
