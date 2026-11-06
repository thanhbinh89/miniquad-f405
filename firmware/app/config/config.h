/**
 * @file    config.h
 * @brief   Persistent configuration (all tunables), grouped per subsystem.
 *
 * Every field is exposed as a parameter through config/params_list.h.
 * Integers and booleans are stored as int32_t so that every parameter is
 * exactly 32 bits on the wire and in flash.
 *
 * Concurrency: fields are written by the main loop (configurator) and read
 * by the flight loop. 32-bit aligned accesses are atomic on Cortex-M4;
 * modules re-derive cached coefficients when g_cfg_revision changes.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

typedef struct {
    /* board / airframe */
    int32_t board_yaw;          /* nose direction: 0 top edge (J1), 1 right (J2), 2 bottom, 3 left */
    int32_t props_out;          /* 0 props-in, 1 props-out                  */

    /* RC input (PPM) */
    int32_t rc_map[4];          /* channel (1-based) for roll, pitch, throttle, yaw */
    int32_t rc_ch_arm;
    int32_t rc_ch_mode;
    int32_t rc_ch_beeper;       /* 0 = none */
    float   rc_min_us;
    float   rc_mid_us;
    float   rc_max_us;
    float   rc_deadband_us;
    float   rc_arm_us;          /* arm switch threshold */
    float   rc_smooth_hz;       /* stick smoothing low-pass */
    int32_t mode_low;           /* flight mode for each position of the mode switch */
    int32_t mode_mid;
    int32_t mode_high;
    int32_t rx_fs_ms;           /* frame timeout -> signal loss */
    float   rx_fs_thr_us;       /* throttle below this = receiver failsafe (0 = off) */
    int32_t ppm_falling;        /* 0 rising edge, 1 falling edge */

    /* rates */
    float   rate_max[3];        /* deg/s at full stick: roll, pitch, yaw */
    float   expo_rp;
    float   expo_yaw;
    float   angle_max;          /* deg, self-level modes */
    float   angle_p;            /* deg/s per deg of angle error */
    float   angle_rate_max;     /* deg/s limit of the angle loop output */
    float   thr_mid;            /* throttle curve mid point (0..1) */
    float   thr_expo;           /* throttle curve expo (0..1) */

    /* rate PID (Betaflight-like scaling) */
    float   pid_p[3];
    float   pid_i[3];
    float   pid_d[3];
    float   pid_f[3];
    float   iterm_limit;        /* normalised thrust */
    float   pid_sum_limit;      /* normalised thrust, roll/pitch */
    int32_t airmode;

    /* filters */
    int32_t gyro_lpf_type;      /* 0 PT1, 1 biquad */
    float   gyro_lpf_hz;
    float   notch1_hz;
    float   notch1_cut;
    float   notch2_hz;
    float   notch2_cut;
    float   dterm_lpf_hz;       /* biquad */
    float   dterm_lpf2_hz;      /* PT1, second stage */
    float   acc_lpf_hz;

    /* attitude estimator (Mahony) */
    float   att_kp;
    float   att_ki;

    /* motor output */
    float   motor_idle;         /* duty when armed at zero thrust */
    float   motor_max;
    float   thr_lin;            /* quadratic share of the thrust curve (0 = linear) */

    /* battery */
    float   bat_vscale;
    float   bat_warn_v;
    float   bat_crit_v;
    int32_t bat_comp;           /* thrust compensation on/off */
    float   bat_vref;
    float   bat_comp_min;
    float   bat_comp_max;
    float   bat_lpf_hz;

    /* altitude hold (ToF) */
    float   alt_hover;          /* initial hover thrust estimate */
    float   alt_p;              /* (m/s) per m */
    float   alt_vel_p;          /* thrust per (m/s) */
    float   alt_vel_i;
    float   alt_vz_max;         /* m/s at full stick deflection */
    float   alt_deadband;       /* stick fraction around centre */
    float   alt_max;            /* m, ceiling */
    float   alt_land_vz;        /* m/s, failsafe landing speed */
    int32_t tof_mode;           /* 1 short, 2 long */
    int32_t tof_tb_ms;          /* timing budget */
    float   tof_offset_mm;
    float   tof_delay_ms;       /* measurement latency compensation */
    float   kf_acc_noise;       /* m/s^2 */
    float   kf_tof_noise;       /* m */
    float   kf_bias_noise;      /* m/s^2/sqrt(s) */

    /* failsafe */
    int32_t fs_mode;            /* 0 drop (disarm), 1 land */
    float   fs_thr;             /* landing thrust without ToF (0 = 0.85 x hover) */
    float   fs_land_s;          /* max landing time before disarm */

    /* arming / crash */
    float   arm_angle;
    float   crash_angle;        /* 0 = off */
    int32_t crash_ms;

    /* calibration (written by the calibration routines) */
    float   acc_off[3];         /* g, body frame */
    float   trim_roll;          /* deg */
    float   trim_pitch;

    /* notification */
    int32_t beep_mask;
    int32_t beep_hz;
} config_t;

extern config_t          g_cfg;
extern volatile uint32_t g_cfg_revision;   /* incremented on every change */

#endif /* CONFIG_H */
