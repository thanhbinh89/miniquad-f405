/**
 * @file    app_services.h
 * @brief   What the protocol handler needs from the rest of the application.
 *
 * The protocol code (comms.c, telemetry.c) is hardware independent; these
 * functions are implemented by app/app_services.c on the target and by
 * tests/emu/emu_device.c on the host (used to test the web configurator
 * against the real protocol implementation).
 */
#ifndef APP_SERVICES_H
#define APP_SERVICES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "proto.h"

/** Byte stream to the configurator. write() is all-or-nothing. */
typedef struct {
    size_t (*read)(uint8_t *buf, size_t max);
    size_t (*write)(const void *data, size_t len);
    size_t (*tx_free)(void);
} comms_port_t;

uint32_t svc_now_ms(void);
bool     svc_armed(void);
/** Fill identity fields of the HELLO reply (status/mode set by the caller). */
void     svc_identity(msg_hello_rsp_t *h);
/** Executed by the application once the reply has left the UART. */
void     svc_reboot(uint8_t target);
/** Persist g_cfg (blocking flash erase/program). */
bool     svc_save_config(void);
/** False when the motors cannot run (no battery). */
bool     svc_motor_test(const uint16_t duty[4]);
void     svc_motor_test_stop(void);
bool     svc_calibrate(uint8_t what);
void     svc_beep(uint8_t pattern);
void     svc_status(tlm_status_t *st);
void     svc_alt(tlm_alt_t *a);
void     svc_fault(msg_fault_info_rsp_t *f);

#endif /* APP_SERVICES_H */
