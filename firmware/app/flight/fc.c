/**
 * @file    fc.c
 * @brief   Flight core (runs once per IMU sample, 1 kHz).
 */
#include "fc.h"

#include <string.h>
#include "alt_ctrl.h"
#include "altitude.h"
#include "angle_ctrl.h"
#include "attitude.h"
#include "config.h"
#include "fc_events.h"
#include "filters.h"
#include "imu_cal.h"
#include "mixer.h"
#include "motor_out.h"
#include "pid.h"
#include "proto.h"
#include "tlm_rings.h"

#define BOOT_GRACE_MS       2000u
#define RC_RECOVER_MS       500u
#define ALT_VALID_MS        500u
#define THROTTLE_LOW        0.05f
#define TOF_MAX_TILT_COS    0.85f       /* ~32 deg */
#define TOF_MAX_RANGE_M     4.0f
#define STATUS_DIV          10u         /* publish status every 10 loops */
#define YAW_SUM_LIMIT       0.35f
#define COS_TILT_MIN        0.6f
#define GROUND_NEAR_M       0.20f       /* ToF lost below this height -> final descent */
#define TAKEOFF_DONE_M      0.10f
#define TAKEOFF_TIMEOUT_S   3.0f

/* ALT hold sub-states */
#define ALT_PH_LANDED       0u          /* on the ground, motors idle            */
#define ALT_PH_TAKEOFF      1u          /* open-loop ramp until ToF height valid */
#define ALT_PH_FLY          2u          /* closed-loop height / climb-rate hold  */
#define ALT_PH_FINAL        3u          /* ToF below min range: ramp down        */

typedef struct {
    float    loop_hz;
    float    dt_nom;
    uint32_t cfg_rev;
    uint32_t loops;
    uint32_t now_ms;

    /* filters */
    pt1_t    gyro_pt1[3];
    biquad_t gyro_bq[3];
    biquad_t notch1[3];
    biquad_t notch2[3];
    biquad_t acc_lpf[3];

    /* estimation */
    imu_cal_t  gcal;
    imu_cal_t  acal;
    bool       gyro_cal_ok;
    vec3_t     gyro_bias;
    vec3_t     gyro_raw;        /* bias corrected, unfiltered */
    vec3_t     gyro_f;          /* filtered                   */
    vec3_t     acc_f;
    attitude_t att;
    alt_est_t  alt;
    float      acc_up;
    float      h_tof;
    float      tof_dist;
    uint8_t    tof_status;
    uint32_t   range_seq;
    uint32_t   alt_meas_ms;
    bool       alt_valid;
    bool       acc_cal_ready;
    bool       acc_cal_ok;
    vec3_t     acc_cal_offset;

    /* control */
    rc_state_t   rc;
    pid_axis_t   pid[3];
    mixer_geom_t geom;
    mixer_out_t  mix;
    alt_ctrl_t   altc;
    vec3_t       rate_sp;
    float        collective;
    float        thrust[4];
    float        duty[4];
    float        bat_factor;
    float        cos_tilt;

    /* supervision */
    bool     armed;
    uint8_t  mode;
    uint8_t  fs_phase;
    uint32_t fs_start_ms;
    uint32_t arm_block;
    bool     arm_sw_prev;
    bool     arm_need_cycle;
    bool     crashed;
    float    crash_time;
    bool     landed;
    uint8_t  alt_phase;         /* ALT_PH_* */
    bool     alt_inhibit;       /* take-off without valid height failed */
    float    alt_ramp;          /* open-loop collective (take-off / final) */
    float    alt_ph_time;
    float    gc_time;           /* ground contact timer */
    float    last_valid_h;
    bool     liftoff;
    pt1_t    hover_coll_lpf;
    float    hover_learn_s;     /* airborne time the hover estimator has run */
    bool     rx_was_valid;
    bool     motor_test;
    uint8_t  bat_state;
    bool     bat_present;
    float    v_comp;
    float    v_slow;
    float    imu_temp;
    rc_frame_t rc_in;
} fc_ctx_t;

static fc_ctx_t s;

static volatile uint32_t s_status_seq;
static fc_status_t       s_status;

static void alt_engage(float collective_level);

/* ------------------------------------------------------------------------ */
/* configuration                                                            */
/* ------------------------------------------------------------------------ */
static void apply_config(bool reset_state)
{
    s.cfg_rev = g_cfg_revision;
    mixer_init_geometry(&s.geom, g_cfg.board_yaw, g_cfg.props_out != 0);

    float q1 = notch_q_from_cutoff(g_cfg.notch1_hz, g_cfg.notch1_cut);
    float q2 = notch_q_from_cutoff(g_cfg.notch2_hz, g_cfg.notch2_cut);
    for (int i = 0; i < 3; i++) {
        if (reset_state) {
            pt1_init(&s.gyro_pt1[i], g_cfg.gyro_lpf_hz, s.dt_nom);
            biquad_init_lpf(&s.gyro_bq[i], g_cfg.gyro_lpf_hz, s.loop_hz);
            biquad_init_notch(&s.notch1[i], (q1 > 0.0f) ? g_cfg.notch1_hz : 0.0f, q1, s.loop_hz);
            biquad_init_notch(&s.notch2[i], (q2 > 0.0f) ? g_cfg.notch2_hz : 0.0f, q2, s.loop_hz);
            biquad_init_lpf(&s.acc_lpf[i], g_cfg.acc_lpf_hz, s.loop_hz);
        } else {
            pt1_set_cutoff(&s.gyro_pt1[i], g_cfg.gyro_lpf_hz, s.dt_nom);
            biquad_set_lpf(&s.gyro_bq[i], g_cfg.gyro_lpf_hz, s.loop_hz);
            biquad_set_notch(&s.notch1[i], (q1 > 0.0f) ? g_cfg.notch1_hz : 0.0f, q1, s.loop_hz);
            biquad_set_notch(&s.notch2[i], (q2 > 0.0f) ? g_cfg.notch2_hz : 0.0f, q2, s.loop_hz);
            biquad_set_lpf(&s.acc_lpf[i], g_cfg.acc_lpf_hz, s.loop_hz);
        }
        pid_set_gains(&s.pid[i], g_cfg.pid_p[i], g_cfg.pid_i[i], g_cfg.pid_d[i], g_cfg.pid_f[i],
                      g_cfg.iterm_limit);
        pid_set_dterm_filters(&s.pid[i], g_cfg.dterm_lpf_hz, g_cfg.dterm_lpf2_hz, s.loop_hz,
                              reset_state);
    }
}

void fc_init(float loop_hz)
{
    memset(&s, 0, sizeof(s));
    s.loop_hz = loop_hz;
    s.dt_nom = 1.0f / loop_hz;
    for (int i = 0; i < 3; i++) {
        pid_init(&s.pid[i]);
    }
    attitude_reset(&s.att);
    alt_est_reset(&s.alt);
    alt_ctrl_init(&s.altc, g_cfg.alt_hover);
    rc_init(&s.rc, loop_hz);
    imu_cal_start_gyro(&s.gcal);
    s.mode = FMODE_ANGLE;
    s.arm_need_cycle = true;
    s.bat_factor = 1.0f;
    s.cos_tilt = 1.0f;
    pt1_init(&s.hover_coll_lpf, 4.0f, s.dt_nom);
    apply_config(true);
}

bool fc_is_armed(void)
{
    return s.armed;
}

bool fc_take_acc_cal_result(vec3_t *offset, bool *ok)
{
    if (!s.acc_cal_ready) {
        return false;
    }
    *offset = s.acc_cal_offset;
    *ok = s.acc_cal_ok;
    __sync_synchronize();
    s.acc_cal_ready = false;
    return true;
}

/* ------------------------------------------------------------------------ */
/* sensors                                                                  */
/* ------------------------------------------------------------------------ */
static float gyro_filter(int axis, float x)
{
    x = biquad_apply(&s.notch1[axis], x);
    x = biquad_apply(&s.notch2[axis], x);
    if (g_cfg.gyro_lpf_type == 0) {
        return pt1_apply(&s.gyro_pt1[axis], x);
    }
    return biquad_apply(&s.gyro_bq[axis], x);
}

static void process_imu(const fc_input_t *in, float dt)
{
    vec3_t g = in->imu.gyro_dps;
    vec3_t a = in->imu.acc_g;

    if (in->cal_request == CAL_GYRO && !s.armed) {
        imu_cal_start_gyro(&s.gcal);
    }
    if (in->cal_request == CAL_ACCEL_LEVEL && !s.armed && s.acal.state != CAL_RUNNING) {
        imu_cal_start_accel(&s.acal);
    }

    if (s.gcal.state == CAL_RUNNING) {
        if (imu_cal_update(&s.gcal, g, dt) == CAL_DONE) {
            s.gyro_bias = s.gcal.result;
            s.gyro_cal_ok = true;
            fc_event_post(FCEV_GYRO_CAL_DONE);
        }
    }
    if (s.acal.state == CAL_RUNNING) {
        cal_state_t st = imu_cal_update(&s.acal, a, dt);
        if (st == CAL_DONE || st == CAL_FAILED) {
            s.acc_cal_ok = (st == CAL_DONE);
            /* offset = mean - expected level reading (0, 0, -1 g) */
            s.acc_cal_offset = v3(s.acal.result.x, s.acal.result.y, s.acal.result.z + 1.0f);
            __sync_synchronize();
            s.acc_cal_ready = true;
            fc_event_post(s.acc_cal_ok ? FCEV_ACC_CAL_DONE : FCEV_ACC_CAL_FAILED);
            s.acal.state = CAL_IDLE;
        }
    }

    s.gyro_raw = v3_sub(g, s.gyro_bias);
    s.gyro_f.x = gyro_filter(0, s.gyro_raw.x);
    s.gyro_f.y = gyro_filter(1, s.gyro_raw.y);
    s.gyro_f.z = gyro_filter(2, s.gyro_raw.z);

    vec3_t ac = v3_sub(a, v3(g_cfg.acc_off[0], g_cfg.acc_off[1], g_cfg.acc_off[2]));
    s.acc_f.x = biquad_apply(&s.acc_lpf[0], ac.x);
    s.acc_f.y = biquad_apply(&s.acc_lpf[1], ac.y);
    s.acc_f.z = biquad_apply(&s.acc_lpf[2], ac.z);
    s.imu_temp = in->imu.temp_c;

    if (!s.att.aligned) {
        if (s.gyro_cal_ok) {
            attitude_align(&s.att, s.acc_f);
        }
    } else {
        attitude_update(&s.att, v3_scale(s.gyro_f, DEG2RAD), s.acc_f, g_cfg.att_kp, g_cfg.att_ki, dt);
    }
    s.cos_tilt = attitude_cos_tilt(&s.att);
}

static void process_altitude(const fc_input_t *in, float dt)
{
    if (!s.att.aligned) {
        return;
    }
    vec3_t fn = attitude_body_to_earth(&s.att, s.acc_f);
    s.acc_up = -(fn.z + 1.0f) * GRAVITY_MSS;
    alt_est_predict(&s.alt, s.acc_up, dt, g_cfg.kf_acc_noise, g_cfg.kf_bias_noise);

    if (in->range.seq != s.range_seq) {
        s.range_seq = in->range.seq;
        s.tof_dist = in->range.dist_m;
        s.tof_status = in->range.status;
        if (in->range.healthy && in->range.status == 0u && s.cos_tilt > TOF_MAX_TILT_COS &&
            in->range.dist_m >= 0.0f && in->range.dist_m < TOF_MAX_RANGE_M) {
            float h = in->range.dist_m * s.cos_tilt - g_cfg.tof_offset_mm * 0.001f;
            if (h < 0.0f) {
                h = 0.0f;
            }
            s.h_tof = h;
            float delay_ms = g_cfg.tof_delay_ms + (float)in->range.age_ms;
            uint32_t delay = (uint32_t)(delay_ms * s.loop_hz * 0.001f);
            float sigma = g_cfg.kf_tof_noise * (1.0f + in->range.dist_m);
            if (alt_est_update(&s.alt, h, sigma, delay)) {
                s.alt_meas_ms = s.now_ms;
            }
        }
    }
    s.alt_valid = s.alt.initialized && (s.now_ms - s.alt_meas_ms) < ALT_VALID_MS;
}

/* ------------------------------------------------------------------------ */
/* supervision                                                              */
/* ------------------------------------------------------------------------ */
static void controllers_reset(void)
{
    for (int i = 0; i < 3; i++) {
        pid_reset(&s.pid[i]);
    }
    s.mix.saturated = false;
}

static void do_arm(void)
{
    s.armed = true;
    s.crash_time = 0.0f;
    s.landed = true;
    s.alt_phase = ALT_PH_LANDED;
    controllers_reset();
    fc_event_post(FCEV_ARMED);
}

static void do_disarm(fc_event_t why)
{
    if (!s.armed) {
        return;
    }
    s.armed = false;
    controllers_reset();
    fc_event_post(why);
}

static void update_arming(const fc_input_t *in)
{
    uint32_t b = 0u;
    float roll = s.att.roll * RAD2DEG;
    float pitch = s.att.pitch * RAD2DEG;

    if (!s.rc.valid) {
        b |= ARMB_NO_RX;
    }
    if (s.fs_phase != FS_NONE) {
        b |= ARMB_FAILSAFE;
    }
    if (s.rc.throttle_raw > THROTTLE_LOW) {
        b |= ARMB_THROTTLE;
    }
    if (fabsf(roll) > g_cfg.arm_angle || fabsf(pitch) > g_cfg.arm_angle) {
        b |= ARMB_ANGLE;
    }
    if (s.gcal.state == CAL_RUNNING || s.acal.state == CAL_RUNNING || !s.gyro_cal_ok || !s.att.aligned) {
        b |= ARMB_CALIBRATING;
    }
    if (!in->imu.healthy) {
        b |= ARMB_IMU;
    }
    if (in->bat.present && in->bat.state == BAT_CRIT) {
        b |= ARMB_BATTERY;
    }
    if (!in->bat.present) {
        b |= ARMB_NO_BAT;                   /* e.g. USB power only: motors have no supply */
    }
    if (in->motor_test_active) {
        b |= ARMB_MOTOR_TEST;
    }
    if (in->now_ms < BOOT_GRACE_MS) {
        b |= ARMB_BOOT_GRACE;
    }
    if (s.crashed) {
        b |= ARMB_CRASH;
    }
    if (in->config_busy) {
        b |= ARMB_CONFIG;
    }

    bool sw = s.rc.valid && s.rc.arm;

    if (!s.armed) {
        if (!s.rc.valid) {
            s.arm_need_cycle = true;            /* never arm on (re)connection */
        } else if (!s.rc.arm) {
            s.arm_need_cycle = false;
            s.crashed = false;
        }
        if (s.arm_need_cycle) {
            b |= ARMB_SWITCH;
        }
        if (sw && !s.arm_sw_prev) {
            if (b == 0u) {
                do_arm();
            } else {
                s.arm_need_cycle = true;
                fc_event_post(FCEV_ARM_BLOCKED);
            }
        }
    } else {
        if (in->force_disarm) {
            do_disarm(FCEV_EMERGENCY);
            s.arm_need_cycle = true;
        } else if (s.rc.valid && !s.rc.arm) {
            do_disarm(FCEV_DISARMED);
        }
    }
    s.arm_sw_prev = sw;
    s.arm_block = b;

    if (s.rc.valid != s.rx_was_valid) {
        fc_event_post(s.rc.valid ? FCEV_RX_OK : FCEV_RX_LOST);
        s.rx_was_valid = s.rc.valid;
    }
}

static void update_failsafe(void)
{
    if (!s.armed) {
        s.fs_phase = FS_NONE;
        return;
    }
    if (!s.rc.valid) {
        if (s.fs_phase == FS_NONE) {
            s.fs_start_ms = s.now_ms;
            fc_event_post(FCEV_FAILSAFE);
            if (g_cfg.fs_mode == 0) {
                s.fs_phase = FS_DROP;
                do_disarm(FCEV_FS_DISARM);
                s.arm_need_cycle = true;
                return;
            }
            s.fs_phase = FS_LAND;
            if (s.landed) {
                s.alt_phase = ALT_PH_LANDED;
            } else {
                s.alt_phase = ALT_PH_FLY;   /* airborne until proven otherwise */
                alt_engage(s.collective * s.cos_tilt);
            }
        }
    } else if (s.fs_phase == FS_LAND && s.rc.valid_ms >= RC_RECOVER_MS) {
        /* update_mode() re-engages the mode selected by the switch */
        s.fs_phase = FS_NONE;
        fc_event_post(FCEV_FS_RECOVERED);
    }

    if (s.fs_phase == FS_LAND) {
        bool timeout = (s.now_ms - s.fs_start_ms) > (uint32_t)(g_cfg.fs_land_s * 1000.0f);
        if (timeout || s.alt_phase == ALT_PH_LANDED) {
            do_disarm(FCEV_FS_DISARM);
            s.arm_need_cycle = true;
        }
    }
}

static uint8_t mode_for_position(uint8_t pos)
{
    int32_t m = (pos == 0u) ? g_cfg.mode_low : ((pos == 1u) ? g_cfg.mode_mid : g_cfg.mode_high);
    return (uint8_t)constraini(m, FMODE_ACRO, FMODE_ALT);
}

/* Engage the height controller. With a trusted hover estimate the
 * integrator starts from zero (the current collective may contain a climb or
 * descent the pilot was doing); otherwise the transfer is bumpless. */
static void alt_engage(float collective_level)
{
    bool trusted = s.liftoff || s.hover_learn_s > 2.0f;
    alt_ctrl_engage(&s.altc, s.alt.h, trusted ? s.altc.hover : collective_level);
}

static void alt_engage_in_air(void)
{
    s.alt_phase = ALT_PH_FLY;
    alt_engage(s.collective * s.cos_tilt);
    /* stop where the current vertical speed would carry us */
    s.altc.h_sp = s.alt.h + constrainf(s.alt.v * 0.3f, -0.3f, 0.3f);
}

static void update_mode(void)
{
    uint8_t want = mode_for_position(s.rc.mode_pos);
    if (want != FMODE_ALT) {
        s.alt_inhibit = false;          /* switch left ALT: allow it again */
    }
    if (s.fs_phase == FS_LAND) {
        want = FMODE_FAILSAFE;
    } else if (want == FMODE_ALT && s.mode != FMODE_ALT) {
        bool on_ground = !s.armed || s.landed;
        if (s.alt_inhibit || (!on_ground && !s.alt_valid)) {
            if (s.mode != FMODE_ANGLE) {
                fc_event_post(FCEV_ALT_UNAVAILABLE);
            }
            want = FMODE_ANGLE;
        }
    } else if (want == FMODE_ALT && s.alt_inhibit) {
        want = FMODE_ANGLE;
    }
    if (want == s.mode) {
        return;
    }
    if (want == FMODE_ALT) {
        if (!s.armed || s.landed) {
            s.alt_phase = ALT_PH_LANDED;
        } else {
            alt_engage_in_air();
        }
        fc_event_post(FCEV_MODE_ALT);
    } else if (want == FMODE_ANGLE) {
        fc_event_post(FCEV_MODE_ANGLE);
    } else if (want == FMODE_ACRO) {
        fc_event_post(FCEV_MODE_ACRO);
    }
    s.mode = want;
}

static void update_crash(float dt)
{
    if (!s.armed || g_cfg.crash_angle <= 0.0f || s.mode == FMODE_ACRO) {
        s.crash_time = 0.0f;
        return;
    }
    float roll = fabsf(s.att.roll * RAD2DEG);
    float pitch = fabsf(s.att.pitch * RAD2DEG);
    bool tilted = roll > g_cfg.crash_angle || pitch > g_cfg.crash_angle;

    /* lying still on its side / back: the accelerometer alone tells, without
     * waiting for the attitude filter to converge */
    float acc_n = v3_norm(s.acc_f);
    if (!tilted && acc_n > 0.5f && fabsf(acc_n - 1.0f) < 0.25f && v3_norm(s.gyro_f) < 50.0f) {
        float c = constrainf(-s.acc_f.z / acc_n, -1.0f, 1.0f);
        tilted = acosf(c) * RAD2DEG > g_cfg.crash_angle;
    }
    if (tilted) {
        s.crash_time += dt;
        if (s.crash_time * 1000.0f >= (float)g_cfg.crash_ms) {
            s.crashed = true;
            s.arm_need_cycle = true;
            do_disarm(FCEV_CRASH);
        }
    } else {
        s.crash_time = 0.0f;
    }
}

/* ------------------------------------------------------------------------ */
/* control                                                                  */
/* ------------------------------------------------------------------------ */
static alt_ctrl_cfg_t alt_cfg(void)
{
    alt_ctrl_cfg_t c = {
        .alt_p = g_cfg.alt_p,
        .vel_p = g_cfg.alt_vel_p,
        .vel_i = g_cfg.alt_vel_i,
        .vz_max = g_cfg.alt_vz_max,
        .alt_max = g_cfg.alt_max,
    };
    return c;
}

/* throttle stick -> climb rate command for ALT mode */
static float stick_climb_rate(bool *hold)
{
    float t = s.rc.throttle;
    float db = g_cfg.alt_deadband;
    float span = 0.5f - db;
    if (span < 0.05f) {
        span = 0.05f;
    }
    *hold = fabsf(t - 0.5f) <= db;
    if (t > 0.5f + db) {
        return (t - 0.5f - db) / span * g_cfg.alt_vz_max;
    }
    if (t < 0.5f - db) {
        return (t - 0.5f + db) / span * g_cfg.alt_vz_max;
    }
    return 0.0f;
}

/* Supported by the ground: thrust well below hover, yet no downward
 * acceleration and no rotation. Independent of the ToF (which is below its
 * minimum range when landed). */
static bool ground_contact(float collective_level, float dt)
{
    float acc_up = s.acc_up - s.alt.b;
    bool cond = collective_level < 0.8f * s.altc.hover && acc_up > -0.8f &&
                fabsf(s.gyro_f.x) < 60.0f && fabsf(s.gyro_f.y) < 60.0f;
    s.gc_time = cond ? s.gc_time + dt : 0.0f;
    return s.gc_time > 0.25f;
}

/* ALT hold state machine; returns the level collective. */
static float collective_alt_phases(float vz_cmd, bool hold, float dt)
{
    alt_ctrl_cfg_t cfg = alt_cfg();
    float hover = s.altc.hover;

    if (s.alt_valid) {
        s.last_valid_h = s.alt.h;
    }

    switch (s.alt_phase) {
    case ALT_PH_LANDED:
        controllers_reset();
        if (vz_cmd > 0.05f) {
            s.alt_phase = ALT_PH_TAKEOFF;
            s.alt_ramp = 0.5f * hover;
            s.alt_ph_time = 0.0f;
            s.liftoff = false;
        }
        return 0.0f;

    case ALT_PH_TAKEOFF: {
        s.alt_ph_time += dt;
        s.alt_ramp = fminf(s.alt_ramp + 0.35f * hover * dt, 1.3f * hover);
        if (vz_cmd <= 0.0f && s.alt_ph_time > 0.3f && s.last_valid_h < TAKEOFF_DONE_M) {
            s.alt_phase = ALT_PH_LANDED;        /* take-off aborted */
            return 0.0f;
        }
        /* lift-off: the vehicle starts to accelerate upwards - the ramp value
         * at this moment is a direct measurement of the hover thrust */
        float a = s.acc_up - s.alt.b;
        if (!s.liftoff && a > 0.4f) {
            s.liftoff = true;
            s.altc.hover = constrainf(s.alt_ramp / (1.0f + a / GRAVITY_MSS), 0.1f, 0.9f);
        }
        if (s.alt_valid && s.alt.h > TAKEOFF_DONE_M) {
            s.alt_phase = ALT_PH_FLY;
            alt_engage(s.alt_ramp);
            s.altc.holding = false;             /* climb-rate control first */
        } else if (s.alt_ph_time > TAKEOFF_TIMEOUT_S) {
            s.alt_inhibit = true;               /* no height: fall back to ANGLE */
            fc_event_post(FCEV_ALT_UNAVAILABLE);
        }
        return s.alt_ramp;
    }

    case ALT_PH_FINAL:
        s.alt_ramp = fmaxf(s.alt_ramp - 0.25f * hover * dt, 0.4f * hover);
        if (ground_contact(s.alt_ramp, dt) || s.alt_ramp <= 0.4f * hover + 1e-4f) {
            s.alt_phase = ALT_PH_LANDED;
            return 0.0f;
        }
        if (s.alt_valid && s.alt.h > GROUND_NEAR_M) {
            alt_engage_in_air();                /* height is back */
        } else if (vz_cmd > 0.05f) {
            s.alt_phase = ALT_PH_TAKEOFF;       /* pilot wants up again */
            s.alt_ph_time = 0.0f;
        }
        return s.alt_ramp;

    case ALT_PH_FLY:
    default: {
        if (!s.alt_valid && s.last_valid_h < GROUND_NEAR_M && vz_cmd <= 0.0f) {
            s.alt_phase = ALT_PH_FINAL;         /* ToF below its minimum range */
            s.alt_ramp = s.altc.out;
            s.gc_time = 0.0f;
            return s.alt_ramp;
        }
        float c = alt_ctrl_update(&s.altc, &cfg, s.alt.h, s.alt.v, vz_cmd, hold && s.alt_valid, dt);
        if (vz_cmd < 0.0f && s.alt.h < GROUND_NEAR_M && ground_contact(c, dt)) {
            s.alt_phase = ALT_PH_LANDED;        /* touch-down with valid ToF */
            return 0.0f;
        }
        return c;
    }
    }
}

static float collective_alt(float dt)
{
    bool hold;
    float vz_cmd = stick_climb_rate(&hold);
    return collective_alt_phases(vz_cmd, hold, dt);
}

static float collective_failsafe(float dt)
{
    if (s.alt_valid || s.alt_phase != ALT_PH_FLY || s.last_valid_h < GROUND_NEAR_M) {
        return collective_alt_phases(-g_cfg.alt_land_vz, false, dt);
    }
    /* no usable height: gentle open-loop descent, fs_land_s timeout disarms */
    return (g_cfg.fs_thr > 0.0f) ? g_cfg.fs_thr : 0.85f * s.altc.hover;
}

static void run_control(float dt)
{
    float roll_deg = s.att.roll * RAD2DEG;
    float pitch_deg = s.att.pitch * RAD2DEG;
    float cos_t = constrainf(s.cos_tilt, COS_TILT_MIN, 1.0f);

    /* ---- attitude / rate set points ---- */
    if (s.mode == FMODE_ACRO) {
        s.rate_sp.x = rc_stick_to_rate(s.rc.roll, g_cfg.rate_max[0], g_cfg.expo_rp);
        s.rate_sp.y = -rc_stick_to_rate(s.rc.pitch, g_cfg.rate_max[1], g_cfg.expo_rp);
    } else {
        bool fs = (s.mode == FMODE_FAILSAFE);
        float ar = (fs ? 0.0f : s.rc.roll * g_cfg.angle_max) + g_cfg.trim_roll;
        float ap = (fs ? 0.0f : -s.rc.pitch * g_cfg.angle_max) + g_cfg.trim_pitch;
        s.rate_sp.x = angle_ctrl_rate(ar, roll_deg, g_cfg.angle_p, g_cfg.angle_rate_max);
        s.rate_sp.y = angle_ctrl_rate(ap, pitch_deg, g_cfg.angle_p, g_cfg.angle_rate_max);
    }
    s.rate_sp.z = (s.mode == FMODE_FAILSAFE) ? 0.0f
                : rc_stick_to_rate(s.rc.yaw, g_cfg.rate_max[2], g_cfg.expo_yaw);

    /* ---- collective ---- */
    float coll;
    switch (s.mode) {
    case FMODE_ALT:
        coll = collective_alt(dt) / cos_t;
        s.landed = (s.alt_phase == ALT_PH_LANDED);
        break;
    case FMODE_FAILSAFE:
        coll = collective_failsafe(dt) / cos_t;
        s.landed = (s.alt_phase == ALT_PH_LANDED);
        break;
    default:
        coll = rc_throttle_curve(s.rc.throttle, g_cfg.thr_mid, g_cfg.thr_expo);
        if (s.rc.throttle < THROTTLE_LOW) {
            s.landed = true;
        } else if (s.landed && (s.acc_up - s.alt.b) > 0.4f) {
            s.landed = false;                   /* lift-off in manual modes */
        }
        break;
    }

    /* hover thrust estimator (all modes, airborne only) */
    bool airborne = !s.landed && (!s.alt_valid || s.alt.h > 0.08f) &&
                    (s.mode != FMODE_ALT || s.alt_phase == ALT_PH_FLY);
    /* the measured acceleration lags the command (motor time constant +
     * accelerometer filter): delay-match the collective before comparing */
    float coll_now = s.mix.collective * s.cos_tilt;
    float coll_lagged = pt1_apply(&s.hover_coll_lpf, coll_now);
    /* quasi-steady only: no strong vertical acceleration, collective not moving fast */
    bool steady = fabsf(s.acc_up - s.alt.b) < 1.0f && fabsf(coll_now - coll_lagged) < 0.02f;
    if (airborne && steady && s.cos_tilt > 0.9f) {
        /* converge quickly from the configured guess, then average slowly */
        float tau = (s.hover_learn_s < 2.0f) ? 0.5f : 3.0f;
        alt_ctrl_learn_hover(&s.altc, coll_lagged, s.acc_up - s.alt.b, tau, dt);
        s.hover_learn_s += dt;
    } else if (!airborne) {
        pt1_reset(&s.hover_coll_lpf, s.altc.hover);
    }
    s.collective = constrainf(coll, 0.0f, 1.0f);

    /* ---- rate PID ---- */
    bool airmode = g_cfg.airmode != 0;
    bool integrate = !s.landed && (airmode || s.collective > THROTTLE_LOW);
    float out[3];
    const float sp[3] = { s.rate_sp.x, s.rate_sp.y, s.rate_sp.z };
    const float meas[3] = { s.gyro_f.x, s.gyro_f.y, s.gyro_f.z };
    for (int i = 0; i < 3; i++) {
        out[i] = pid_update(&s.pid[i], sp[i], meas[i], dt, integrate, s.mix.saturated);
        if (!integrate && !airmode && s.collective <= THROTTLE_LOW) {
            pid_reset_integrator(&s.pid[i]);
        }
    }
    float lim = g_cfg.pid_sum_limit;
    float r = constrainf(out[0], -lim, lim);
    float p = constrainf(out[1], -lim, lim);
    float y = constrainf(out[2], -YAW_SUM_LIMIT, YAW_SUM_LIMIT);

    mixer_mix(&s.geom, s.collective, r, p, y, airmode, &s.mix);
    for (int i = 0; i < 4; i++) {
        s.thrust[i] = s.mix.motor[i];
    }
}

/* ------------------------------------------------------------------------ */
/* telemetry                                                                */
/* ------------------------------------------------------------------------ */
static int16_t sat16(float v)
{
    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return (int16_t)v;
}

static void push_telemetry(const fc_input_t *in)
{
    uint16_t rdiv = tlm_rate_divider();
    if (rdiv != 0u && (s.loops % rdiv) == 0u) {
        tlm_rate_t t;
        t.time_us = in->imu.t_us;
        const float sp[3] = { s.rate_sp.x, s.rate_sp.y, s.rate_sp.z };
        const float gy[3] = { s.gyro_f.x, s.gyro_f.y, s.gyro_f.z };
        for (int i = 0; i < 3; i++) {
            t.sp[i] = sat16(sp[i] * 10.0f);
            t.gyro[i] = sat16(gy[i] * 10.0f);
            t.p[i] = sat16(s.pid[i].p_out * 10000.0f);
            t.i[i] = sat16(s.pid[i].i_out * 10000.0f);
            t.d[i] = sat16(s.pid[i].d_out * 10000.0f);
            t.f[i] = sat16(s.pid[i].f_out * 10000.0f);
        }
        t.throttle = (uint16_t)(s.collective * 1000.0f);
        for (int i = 0; i < 4; i++) {
            t.motor[i] = (uint16_t)(s.duty[i] * 1000.0f);
        }
        (void)tlm_push_rate(&t);
    }

    uint16_t idiv = tlm_imu_divider();
    if (idiv != 0u && (s.loops % idiv) == 0u) {
        tlm_imu_t t;
        t.time_us = in->imu.t_us;
        t.gyro_raw[0] = sat16(s.gyro_raw.x * 10.0f);
        t.gyro_raw[1] = sat16(s.gyro_raw.y * 10.0f);
        t.gyro_raw[2] = sat16(s.gyro_raw.z * 10.0f);
        t.gyro_filt[0] = sat16(s.gyro_f.x * 10.0f);
        t.gyro_filt[1] = sat16(s.gyro_f.y * 10.0f);
        t.gyro_filt[2] = sat16(s.gyro_f.z * 10.0f);
        t.acc[0] = sat16(in->imu.acc_g.x * 1000.0f);
        t.acc[1] = sat16(in->imu.acc_g.y * 1000.0f);
        t.acc[2] = sat16(in->imu.acc_g.z * 1000.0f);
        (void)tlm_push_imu(&t);
    }
}

static void publish_status(const fc_input_t *in)
{
    fc_status_t *st = &s_status;
    s_status_seq++;
    __sync_synchronize();

    uint32_t f = 0u;
    f |= s.armed ? STF_ARMED : 0u;
    f |= s.rc.valid ? STF_RX_OK : 0u;
    f |= (s.fs_phase != FS_NONE) ? STF_FAILSAFE : 0u;
    f |= in->imu.healthy ? STF_IMU_OK : 0u;
    f |= s.gyro_cal_ok ? STF_GYRO_CAL : 0u;
    f |= (s.gcal.state == CAL_RUNNING || s.acal.state == CAL_RUNNING) ? STF_CALIBRATING : 0u;
    f |= in->range.healthy ? STF_TOF_OK : 0u;
    f |= (s.tof_status == 0u && in->range.healthy) ? STF_TOF_VALID : 0u;
    f |= in->bat.present ? STF_BAT_PRESENT : 0u;
    f |= s.alt_valid ? STF_ALT_VALID : 0u;
    f |= in->motor_test_active ? STF_MOTOR_TEST : 0u;
    f |= s.landed ? STF_LANDED : 0u;
    f |= s.mix.saturated ? STF_MIXER_SAT : 0u;
    f |= s.crashed ? STF_CRASHED : 0u;

    st->time_ms = s.now_ms;
    st->flags = f;
    st->arm_block = s.arm_block;
    st->mode = s.mode;
    st->fs_phase = s.fs_phase;
    st->bat_state = in->bat.state;
    st->tof_status = s.tof_status;
    st->roll = s.att.roll * RAD2DEG;
    st->pitch = s.att.pitch * RAD2DEG;
    st->yaw = s.att.yaw * RAD2DEG;
    st->v_comp = in->bat.v_comp;
    st->v_slow = in->bat.v_slow;
    st->bat_factor = s.bat_factor;
    st->imu_temp = s.imu_temp;
    st->rc_count = in->rc.count;
    st->rc_rate_hz = in->rc.rate_hz;
    for (int i = 0; i < 8; i++) {
        st->rc_us[i] = (i < (int)in->rc.count) ? in->rc.ch[i] : 0u;
    }
    for (int i = 0; i < 4; i++) {
        st->duty[i] = s.duty[i];
    }
    st->collective = s.collective;
    st->hover = s.altc.hover;
    st->h_est = s.alt.h;
    st->vz_est = s.alt.v;
    st->h_sp = s.altc.h_sp;
    st->vz_sp = s.altc.vz_sp;
    st->h_tof = s.h_tof;
    st->acc_up = s.acc_up;
    st->alt_i = s.altc.i_term;
    st->alt_bias = s.alt.b;
    st->tof_dist = s.tof_dist;
    st->gyro_bias = s.gyro_bias;
    st->alt_flags = (s.alt_valid ? 1u : 0u) | ((s.mode == FMODE_ALT) ? 2u : 0u) |
                    (s.landed ? 4u : 0u) | ((uint32_t)s.alt_phase << 4);

    __sync_synchronize();
    s_status_seq++;
}

void fc_get_status(fc_status_t *out)
{
    uint32_t a, b;
    do {
        a = s_status_seq;
        __sync_synchronize();
        memcpy(out, (const void *)&s_status, sizeof(*out));
        __sync_synchronize();
        b = s_status_seq;
    } while (a != b || (a & 1u) != 0u);
}

/* ------------------------------------------------------------------------ */
void fc_update(const fc_input_t *in, fc_output_t *out)
{
    float dt = in->imu.dt;
    if (!(dt > 0.0f) || dt > 5.0f * s.dt_nom) {
        dt = s.dt_nom;
    }
    if (g_cfg_revision != s.cfg_rev) {
        apply_config(false);
    }
    s.now_ms = in->now_ms;
    s.loops++;

    process_imu(in, dt);
    process_altitude(in, dt);
    rc_update(&s.rc, &in->rc, in->now_ms);

    update_arming(in);
    update_failsafe();
    update_mode();
    update_crash(dt);

    if (s.armed) {
        run_control(dt);
    } else {
        controllers_reset();
        s.collective = 0.0f;
        s.rate_sp = v3(0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 4; i++) {
            s.thrust[i] = 0.0f;
        }
    }

    s.bat_factor = (g_cfg.bat_comp != 0)
        ? motor_battery_factor(in->bat.v_comp, in->bat.present, g_cfg.bat_vref,
                               g_cfg.bat_comp_min, g_cfg.bat_comp_max)
        : 1.0f;

    if (s.armed) {
        motor_output(s.thrust, s.bat_factor, g_cfg.motor_idle, g_cfg.motor_max, g_cfg.thr_lin, s.duty);
    } else if (in->motor_test_active) {
        for (int i = 0; i < 4; i++) {
            s.duty[i] = constrainf((float)in->motor_test[i] * 0.001f, 0.0f, g_cfg.motor_max);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            s.duty[i] = 0.0f;
        }
    }

    for (int i = 0; i < 4; i++) {
        out->duty[i] = s.duty[i];
    }
    out->armed = s.armed;

    push_telemetry(in);
    if ((s.loops % STATUS_DIV) == 0u) {
        publish_status(in);
    }
}
