/**
 * @file    params_list.h
 * @brief   THE parameter table (X-macro). Add a tunable here and it becomes
 *          available in flash storage, the link protocol and the web UI.
 *
 * X(name, type, field, default, min, max, flags)
 *   name   : identifier, max 23 chars, stable (it is hashed for storage)
 *   type   : FLOAT, INT or BOOL
 *   field  : member of config_t (see config.h)
 *   flags  : 0 or PARAM_F_* (proto.h); PARAM_F_LIVE = may change while armed
 *
 * Renaming a parameter loses its stored value (falls back to default).
 * The order defines the protocol index - append new entries at the end of
 * a group if possible; the web UI re-reads the table when its hash changes.
 */
#ifndef PARAMS_LIST_H
#define PARAMS_LIST_H

#define PARAMS_LIST(X) \
    /* ---- board / airframe ---- */ \
    X(board_yaw,       INT,   board_yaw,        0,      0,      3,      0) \
    X(props_out,       BOOL,  props_out,        0,      0,      1,      0) \
    /* ---- RC / PPM ---- */ \
    X(rc_map_roll,     INT,   rc_map[0],        1,      1,      12,     0) \
    X(rc_map_pitch,    INT,   rc_map[1],        2,      1,      12,     0) \
    X(rc_map_thr,      INT,   rc_map[2],        3,      1,      12,     0) \
    X(rc_map_yaw,      INT,   rc_map[3],        4,      1,      12,     0) \
    X(rc_ch_arm,       INT,   rc_ch_arm,        5,      1,      12,     0) \
    X(rc_ch_mode,      INT,   rc_ch_mode,       6,      0,      12,     0) \
    X(rc_ch_beeper,    INT,   rc_ch_beeper,     0,      0,      12,     0) \
    X(rc_min_us,       FLOAT, rc_min_us,        1000,   800,    1500,   0) \
    X(rc_mid_us,       FLOAT, rc_mid_us,        1500,   1300,   1700,   0) \
    X(rc_max_us,       FLOAT, rc_max_us,        2000,   1500,   2200,   0) \
    X(rc_deadband_us,  FLOAT, rc_deadband_us,   8,      0,      100,    0) \
    X(rc_arm_us,       FLOAT, rc_arm_us,        1700,   1100,   2100,   0) \
    X(rc_smooth_hz,    FLOAT, rc_smooth_hz,     25,     0,      100,    0) \
    X(mode_low,        INT,   mode_low,         1,      0,      2,      0) \
    X(mode_mid,        INT,   mode_mid,         2,      0,      2,      0) \
    X(mode_high,       INT,   mode_high,        0,      0,      2,      0) \
    X(rx_fs_ms,        INT,   rx_fs_ms,         250,    50,     2000,   0) \
    X(rx_fs_thr_us,    FLOAT, rx_fs_thr_us,     0,      0,      1000,   0) \
    X(ppm_falling,     BOOL,  ppm_falling,      0,      0,      1,      0) \
    /* ---- rates ---- */ \
    X(rate_roll,       FLOAT, rate_max[0],      600,    50,     1800,   PARAM_F_LIVE) \
    X(rate_pitch,      FLOAT, rate_max[1],      600,    50,     1800,   PARAM_F_LIVE) \
    X(rate_yaw,        FLOAT, rate_max[2],      400,    50,     1800,   PARAM_F_LIVE) \
    X(expo_rp,         FLOAT, expo_rp,          0.3,    0,      1,      PARAM_F_LIVE) \
    X(expo_yaw,        FLOAT, expo_yaw,         0.2,    0,      1,      PARAM_F_LIVE) \
    X(angle_max,       FLOAT, angle_max,        35,     10,     80,     PARAM_F_LIVE) \
    X(angle_p,         FLOAT, angle_p,          6,      0.5,    20,     PARAM_F_LIVE) \
    X(angle_rate_max,  FLOAT, angle_rate_max,   400,    50,     1000,   PARAM_F_LIVE) \
    X(thr_mid,         FLOAT, thr_mid,          0.5,    0.1,    0.9,    PARAM_F_LIVE) \
    X(thr_expo,        FLOAT, thr_expo,         0,      0,      1,      PARAM_F_LIVE) \
    /* ---- rate PID ---- */ \
    X(pid_roll_p,      FLOAT, pid_p[0],         55,     0,      250,    PARAM_F_LIVE) \
    X(pid_roll_i,      FLOAT, pid_i[0],         60,     0,      250,    PARAM_F_LIVE) \
    X(pid_roll_d,      FLOAT, pid_d[0],         28,     0,      150,    PARAM_F_LIVE) \
    X(pid_roll_f,      FLOAT, pid_f[0],         30,     0,      500,    PARAM_F_LIVE) \
    X(pid_pitch_p,     FLOAT, pid_p[1],         55,     0,      250,    PARAM_F_LIVE) \
    X(pid_pitch_i,     FLOAT, pid_i[1],         60,     0,      250,    PARAM_F_LIVE) \
    X(pid_pitch_d,     FLOAT, pid_d[1],         28,     0,      150,    PARAM_F_LIVE) \
    X(pid_pitch_f,     FLOAT, pid_f[1],         30,     0,      500,    PARAM_F_LIVE) \
    X(pid_yaw_p,       FLOAT, pid_p[2],         60,     0,      250,    PARAM_F_LIVE) \
    X(pid_yaw_i,       FLOAT, pid_i[2],         60,     0,      250,    PARAM_F_LIVE) \
    X(pid_yaw_d,       FLOAT, pid_d[2],         0,      0,      150,    PARAM_F_LIVE) \
    X(pid_yaw_f,       FLOAT, pid_f[2],         0,      0,      500,    PARAM_F_LIVE) \
    X(iterm_limit,     FLOAT, iterm_limit,      0.3,    0,      0.6,    PARAM_F_LIVE) \
    X(pid_sum_limit,   FLOAT, pid_sum_limit,    0.5,    0.1,    1,      PARAM_F_LIVE) \
    X(airmode,         BOOL,  airmode,          0,      0,      1,      0) \
    /* ---- filters ---- */ \
    X(gyro_lpf_type,   INT,   gyro_lpf_type,    1,      0,      1,      0) \
    X(gyro_lpf_hz,     FLOAT, gyro_lpf_hz,      120,    0,      450,    PARAM_F_LIVE) \
    X(notch1_hz,       FLOAT, notch1_hz,        0,      0,      480,    PARAM_F_LIVE) \
    X(notch1_cut,      FLOAT, notch1_cut,       0,      0,      470,    PARAM_F_LIVE) \
    X(notch2_hz,       FLOAT, notch2_hz,        0,      0,      480,    PARAM_F_LIVE) \
    X(notch2_cut,      FLOAT, notch2_cut,       0,      0,      470,    PARAM_F_LIVE) \
    X(dterm_lpf_hz,    FLOAT, dterm_lpf_hz,     90,     0,      450,    PARAM_F_LIVE) \
    X(dterm_lpf2_hz,   FLOAT, dterm_lpf2_hz,    150,    0,      450,    PARAM_F_LIVE) \
    X(acc_lpf_hz,      FLOAT, acc_lpf_hz,       20,     0,      200,    PARAM_F_LIVE) \
    /* ---- attitude estimator ---- */ \
    X(att_kp,          FLOAT, att_kp,           0.25,   0,      10,     PARAM_F_LIVE) \
    X(att_ki,          FLOAT, att_ki,           0.02,   0,      1,      PARAM_F_LIVE) \
    /* ---- motors ---- */ \
    X(motor_idle,      FLOAT, motor_idle,       0.04,   0,      0.2,    0) \
    X(motor_max,       FLOAT, motor_max,        1,      0.3,    1,      0) \
    X(thr_lin,         FLOAT, thr_lin,          0.5,    0,      1,      PARAM_F_LIVE) \
    /* ---- battery ---- */ \
    X(bat_vscale,      FLOAT, bat_vscale,       1,      0.8,    1.2,    0) \
    X(bat_warn_v,      FLOAT, bat_warn_v,       3.45,   2.8,    4.2,    0) \
    X(bat_crit_v,      FLOAT, bat_crit_v,       3.25,   2.7,    4.0,    0) \
    X(bat_comp,        BOOL,  bat_comp,         1,      0,      1,      0) \
    X(bat_vref,        FLOAT, bat_vref,         3.9,    3.0,    4.4,    PARAM_F_LIVE) \
    X(bat_comp_min,    FLOAT, bat_comp_min,     0.85,   0.5,    1,      PARAM_F_LIVE) \
    X(bat_comp_max,    FLOAT, bat_comp_max,     1.35,   1,      2,      PARAM_F_LIVE) \
    X(bat_lpf_hz,      FLOAT, bat_lpf_hz,       2,      0.1,    20,     PARAM_F_LIVE) \
    /* ---- altitude hold ---- */ \
    X(alt_hover,       FLOAT, alt_hover,        0.45,   0.1,    0.9,    0) \
    X(alt_p,           FLOAT, alt_p,            1.5,    0,      10,     PARAM_F_LIVE) \
    X(alt_vel_p,       FLOAT, alt_vel_p,        0.25,   0,      2,      PARAM_F_LIVE) \
    X(alt_vel_i,       FLOAT, alt_vel_i,        0.30,   0,      2,      PARAM_F_LIVE) \
    X(alt_vz_max,      FLOAT, alt_vz_max,       0.8,    0.1,    3,      PARAM_F_LIVE) \
    X(alt_deadband,    FLOAT, alt_deadband,     0.12,   0,      0.4,    PARAM_F_LIVE) \
    X(alt_max,         FLOAT, alt_max,          2.5,    0.3,    4,      0) \
    X(alt_land_vz,     FLOAT, alt_land_vz,      0.3,    0.05,   1.5,    0) \
    X(tof_mode,        INT,   tof_mode,         2,      1,      2,      PARAM_F_REBOOT) \
    X(tof_tb_ms,       INT,   tof_tb_ms,        33,     20,     500,    PARAM_F_REBOOT) \
    X(tof_offset_mm,   FLOAT, tof_offset_mm,    0,      -100,   100,    0) \
    X(tof_delay_ms,    FLOAT, tof_delay_ms,     20,     0,      100,    0) \
    X(kf_acc_noise,    FLOAT, kf_acc_noise,     0.6,    0.01,   10,     PARAM_F_LIVE) \
    X(kf_tof_noise,    FLOAT, kf_tof_noise,     0.03,   0.001,  1,      PARAM_F_LIVE) \
    X(kf_bias_noise,   FLOAT, kf_bias_noise,    0.02,   0,      1,      PARAM_F_LIVE) \
    /* ---- failsafe ---- */ \
    X(fs_mode,         INT,   fs_mode,          1,      0,      1,      0) \
    X(fs_thr,          FLOAT, fs_thr,           0,      0,      1,      0) \
    X(fs_land_s,       FLOAT, fs_land_s,        6,      0,      30,     0) \
    /* ---- arming / crash ---- */ \
    X(arm_angle,       FLOAT, arm_angle,        25,     5,      180,    0) \
    X(crash_angle,     FLOAT, crash_angle,      80,     0,      180,    0) \
    X(crash_ms,        INT,   crash_ms,         300,    50,     3000,   0) \
    /* ---- calibration ---- */ \
    X(acc_off_x,       FLOAT, acc_off[0],       0,      -0.5,   0.5,    PARAM_F_CAL) \
    X(acc_off_y,       FLOAT, acc_off[1],       0,      -0.5,   0.5,    PARAM_F_CAL) \
    X(acc_off_z,       FLOAT, acc_off[2],       0,      -0.5,   0.5,    PARAM_F_CAL) \
    X(trim_roll,       FLOAT, trim_roll,        0,      -10,    10,     PARAM_F_LIVE) \
    X(trim_pitch,      FLOAT, trim_pitch,       0,      -10,    10,     PARAM_F_LIVE) \
    /* ---- notification ---- */ \
    X(beep_mask,       INT,   beep_mask,        65535,  0,      65535,  PARAM_F_LIVE) \
    X(beep_hz,         INT,   beep_hz,          4000,   500,    8000,   0)

#endif /* PARAMS_LIST_H */
