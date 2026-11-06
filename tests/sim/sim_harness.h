/**
 * @file    sim_harness.h
 * @brief   Couples the real flight core (fc.c) with the quad model:
 *          simulated IMU (bias, noise, vibration), PPM frames at 50 Hz,
 *          ToF at ~30 Hz with latency, battery voltage with sag.
 */
#ifndef SIM_HARNESS_H
#define SIM_HARNESS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "fc.h"
#include "quad_model.h"

#define SIM_CH_ROLL     0
#define SIM_CH_PITCH    1
#define SIM_CH_THR      2
#define SIM_CH_YAW      3
#define SIM_CH_ARM      4
#define SIM_CH_MODE     5

typedef struct {
    quadsim_t      quad;
    fc_input_t  in;
    fc_output_t out;
    double      t;
    uint32_t    ms;
    uint16_t    ch[8];
    bool        tx_on;
    double      gyro_bias[3];
    double      gyro_noise;     /* deg/s rms   */
    double      acc_noise;      /* g rms       */
    double      vib_gyro;       /* deg/s amplitude at full thrust */
    double      vib_acc;        /* g amplitude at full thrust     */
    double      vib_hz;
    bool        tof_on;
    double      tof_noise;      /* m rms */
    double      tof_period_ms;
    double      tof_latency_ms;
    double      tof_next_ms;
    double      h_hist[256];
    unsigned    h_idx;
    double      v_comp, v_slow;
    double      rc_next_ms;
    uint32_t    rc_seq;
    uint32_t    rc_last_ms;
    uint32_t    rng;
    double      drain_v_per_s;  /* open circuit voltage drain */
    bool        imu_override;   /* report override_acc and zero rates */
    double      override_acc[3];
    bool        props_removed;  /* motors spin, no thrust (motor test)       */
    bool        bat_external;   /* in.bat is filled by the caller (emulator) */
    FILE       *log;
} sim_t;

/** Parameters to defaults, flight core reset, vehicle on the ground. */
void   sim_init(sim_t *s);
/** Vehicle and sensor models only (parameters and flight core untouched). */
void   sim_init_env(sim_t *s);
/** One 1 ms step: physics, sensors, fc_update(). */
void   sim_step(sim_t *s);
/** Sticks: roll/pitch/yaw -1..1, throttle 0..1 (converted to 1000..2000 us). */
void   sim_sticks(sim_t *s, double roll, double pitch, double thr, double yaw);
void   sim_channel(sim_t *s, int ch, uint16_t us);
void   sim_run(sim_t *s, double seconds);
/** Run until fc reports armed (or timeout). */
bool   sim_arm(sim_t *s, double timeout_s);
double sim_height(const sim_t *s);
void   sim_euler(const sim_t *s, double *roll, double *pitch, double *yaw);

#endif /* SIM_HARNESS_H */
