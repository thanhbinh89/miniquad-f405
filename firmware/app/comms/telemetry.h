/**
 * @file    telemetry.h
 * @brief   Telemetry streams towards the configurator.
 *
 *   STREAM_STATUS  <= 50 Hz   snapshot (svc_status)
 *   STREAM_RATE    <= 500 Hz  rate PID samples from the flight loop ring
 *   STREAM_IMU     <= 500 Hz  raw / filtered IMU samples from the flight loop ring
 *   STREAM_ALT     <= 100 Hz  altitude estimator / controller (svc_alt)
 *
 * The flight loop produces RATE/IMU samples every 'divider' loops, so they
 * are evenly spaced; the main loop only forwards them.
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

void     telemetry_init(void);
/** @return the rate actually applied (Hz). */
uint16_t telemetry_set_rate(uint8_t stream, uint16_t hz);
uint16_t telemetry_rate(uint8_t stream);
void     telemetry_stop_all(void);
void     telemetry_task(uint32_t now_ms);

#endif /* TELEMETRY_H */
