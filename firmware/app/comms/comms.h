/**
 * @file    comms.h
 * @brief   Application side of the link protocol (see proto.h,
 *          docs/protocol.md): requests, replies, log forwarding.
 *
 * Safety rules enforced here:
 *  - parameters without PARAM_F_LIVE, save/reset, calibration and motor
 *    test are refused while armed (ST_ERR_STATE);
 *  - streams and the motor test stop when the host has been silent for 3 s.
 */
#ifndef COMMS_H
#define COMMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app_services.h"

#define COMMS_HOST_TIMEOUT_MS   3000u

void     comms_init(const comms_port_t *port);
/** Main-loop task: receive and answer requests, forward telemetry + log. */
void     comms_task(void);
/** Queue an unsolicited frame; returns false (dropped) if the TX ring is full. */
bool     comms_send(uint8_t id, const void *payload, size_t len);
bool     comms_host_alive(void);
uint32_t comms_drops(void);
/** A reboot was requested and acknowledged; returns the target once. */
bool     comms_take_reboot(uint8_t *target);

#endif /* COMMS_H */
