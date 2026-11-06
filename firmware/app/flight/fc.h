/**
 * @file    fc.h
 * @brief   Flight core: everything that happens once per IMU sample.
 *
 * Hardware independent. flight_task.c feeds it from the drivers inside the
 * flight software interrupt; tests/sim feeds it from a physics model.
 *
 *   IMU sample -> bias/offset -> notch/LPF -> Mahony attitude
 *              -> vertical KF (+ToF) -> RC -> arming/failsafe/mode
 *              -> angle loop -> rate PID -> mixer -> thrust linearisation
 *              -> battery compensation -> PWM duty
 */
#ifndef FC_H
#define FC_H

#include <stdbool.h>
#include <stdint.h>
#include "maths.h"
#include "rc.h"

typedef struct {
    float    dt;            /* s since the previous sample                 */
    uint32_t t_us;
    vec3_t   gyro_dps;      /* body FRD, raw (no bias correction)          */
    vec3_t   acc_g;         /* body FRD specific force, raw                */
    float    temp_c;
    bool     healthy;
} fc_imu_t;

typedef struct {
    bool    present;
    float   v_comp;         /* V, low-pass for thrust compensation */
    float   v_slow;         /* V, low-pass for warnings           */
    uint8_t state;          /* BAT_*                               */
} fc_bat_t;

typedef struct {
    uint32_t seq;           /* changes when a new measurement is available */
    uint32_t age_ms;        /* age of the measurement                      */
    float    dist_m;        /* raw range along the sensor axis              */
    uint8_t  status;        /* 0 = valid                                    */
    bool     healthy;       /* sensor initialised and responding            */
} fc_range_t;

typedef struct {
    uint32_t   now_ms;
    fc_imu_t   imu;
    rc_frame_t rc;
    fc_bat_t   bat;
    fc_range_t range;
    bool       motor_test_active;
    uint16_t   motor_test[4];   /* 0..1000 */
    uint8_t    cal_request;     /* CAL_* edge, 0 = none */
    bool       config_busy;
    bool       force_disarm;
} fc_input_t;

typedef struct {
    float duty[4];
    bool  armed;
} fc_output_t;

/** Snapshot for telemetry (published at 100 Hz, read with a seqlock). */
typedef struct {
    uint32_t time_ms;
    uint32_t flags;             /* STF_* */
    uint32_t arm_block;         /* ARMB_* */
    uint8_t  mode;
    uint8_t  fs_phase;
    uint8_t  bat_state;
    uint8_t  tof_status;
    float    roll, pitch, yaw;  /* deg */
    float    v_comp, v_slow, bat_factor;
    float    imu_temp;
    uint8_t  rc_count, rc_rate_hz;
    uint16_t rc_us[8];
    float    duty[4];
    float    collective;
    float    hover;
    float    h_est, vz_est, h_sp, vz_sp, h_tof, acc_up;
    float    alt_i;             /* altitude controller integrator */
    float    alt_bias;          /* KF accelerometer bias (m/s^2) */
    float    tof_dist;
    vec3_t   gyro_bias;
    uint32_t alt_flags;         /* bit0 measurement used recently, bit1 ALT active, bit2 landed */
} fc_status_t;

#define FS_NONE     0u
#define FS_LAND     1u
#define FS_DROP     2u

void fc_init(float loop_hz);
void fc_update(const fc_input_t *in, fc_output_t *out);
void fc_get_status(fc_status_t *st);
bool fc_is_armed(void);

/** Accelerometer calibration hand-off to the main loop (stores parameters). */
bool fc_take_acc_cal_result(vec3_t *offset, bool *ok);

#endif /* FC_H */
