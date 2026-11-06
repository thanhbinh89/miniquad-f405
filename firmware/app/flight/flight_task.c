/**
 * @file    flight_task.c
 * @brief   1 kHz flight loop (software interrupt) + stall supervisor.
 */
#include "flight_task.h"

#include <string.h>
#include "board.h"
#include "config.h"
#include "fc.h"
#include "icm42688.h"
#include "imu_frame.h"
#include "motor_pwm.h"
#include "ppm_input.h"
#include "proto.h"
#include "systime.h"
#include "vl53l1.h"
#include "watchdog.h"
#include "vectors.h"

#define STALL_MS            20u
#define NOMINAL_DT          (1.0f / (float)ICM42688_ODR_HZ)

static fc_input_t         s_in;
static fc_output_t        s_out;
static uint32_t           s_last_seq;
static uint32_t           s_last_t_us;

static volatile uint32_t  s_heartbeat;
static volatile bool      s_emergency;
static volatile bool      s_running;

static volatile uint16_t  s_mt_duty[4];
static volatile uint32_t  s_mt_until_ms;
static volatile uint8_t   s_cal_req;
static volatile bool      s_cfg_busy;

static volatile float     s_bat_comp;
static volatile float     s_bat_slow;
static volatile uint8_t   s_bat_state;
static volatile bool      s_bat_present;

static float              s_exec_avg;
static volatile uint32_t  s_exec_max;
static volatile uint32_t  s_loops;
static uint32_t           s_rate_t0;
static uint32_t           s_rate_n;
static volatile uint16_t  s_loop_hz;

void flight_task_init(void)
{
    memset(&s_in, 0, sizeof(s_in));
    s_bat_state = BAT_NONE;
    fc_init((float)ICM42688_ODR_HZ);
}

void flight_task_set_battery(const battery_t *b)
{
    s_bat_comp = b->v_comp;
    s_bat_slow = b->v_slow;
    s_bat_state = b->state;
    s_bat_present = b->present;
}

void flight_task_motor_test(const uint16_t duty[4], uint32_t until_ms)
{
    for (int i = 0; i < 4; i++) {
        s_mt_duty[i] = duty[i];
    }
    s_mt_until_ms = until_ms;
}

void flight_task_motor_test_stop(void)
{
    s_mt_until_ms = 0u;
    for (int i = 0; i < 4; i++) {
        s_mt_duty[i] = 0u;
    }
}

bool flight_task_motor_test_active(void)
{
    return s_mt_until_ms != 0u && (int32_t)(s_mt_until_ms - time_ms()) > 0;
}

void flight_task_request_cal(uint8_t what)
{
    s_cal_req = what;
}

void flight_task_set_config_busy(bool busy)
{
    s_cfg_busy = busy;
}

void flight_task_stats(flight_stats_t *out)
{
    out->loop_us_avg = (uint16_t)s_exec_avg;
    out->loop_us_max = (uint16_t)s_exec_max;
    out->loop_hz = s_loop_hz;
    out->loops = s_loops;
    out->running = s_running;
    out->emergency = s_emergency;
}

void flight_task_stats_reset_max(void)
{
    s_exec_max = 0u;
}

static void collect_range(fc_range_t *r, uint32_t now_ms)
{
    vl53l1_sample_t smp;
    r->healthy = vl53l1_healthy();
    if (vl53l1_get(&smp)) {
        r->seq = smp.seq;
        r->age_ms = now_ms - smp.t_ms;
        r->dist_m = (float)smp.dist_mm * 0.001f;
        r->status = smp.status;
    }
}

void FLIGHT_SWI_IRQHandler(void)
{
    uint32_t t0 = time_us();
    icm42688_raw_t raw;
    if (!icm42688_get(&raw) || raw.seq == s_last_seq) {
        return;
    }
    s_last_seq = raw.seq;

    float dt = NOMINAL_DT;
    if (s_last_t_us != 0u) {
        dt = (float)(raw.t_us - s_last_t_us) * 1e-6f;
    }
    s_last_t_us = raw.t_us;

    uint32_t now_ms = time_ms();
    fc_input_t *in = &s_in;
    in->now_ms = now_ms;

    vec3_t g = v3((float)raw.gyro[0] * ICM42688_GYRO_DPS_LSB, (float)raw.gyro[1] * ICM42688_GYRO_DPS_LSB,
                  (float)raw.gyro[2] * ICM42688_GYRO_DPS_LSB);
    vec3_t a = v3((float)raw.acc[0] * ICM42688_ACC_G_LSB, (float)raw.acc[1] * ICM42688_ACC_G_LSB,
                  (float)raw.acc[2] * ICM42688_ACC_G_LSB);
    in->imu.gyro_dps = imu_frame_to_body(g, g_cfg.board_yaw);
    in->imu.acc_g = imu_frame_to_body(a, g_cfg.board_yaw);
    in->imu.dt = dt;
    in->imu.t_us = raw.t_us;
    in->imu.temp_c = icm42688_temp_c(raw.temp);
    in->imu.healthy = true;

    ppm_input_get(&in->rc);

    in->bat.present = s_bat_present;
    in->bat.v_comp = s_bat_comp;
    in->bat.v_slow = s_bat_slow;
    in->bat.state = s_bat_state;

    collect_range(&in->range, now_ms);

    bool mt = s_mt_until_ms != 0u && (int32_t)(s_mt_until_ms - now_ms) > 0;
    in->motor_test_active = mt;
    for (int i = 0; i < 4; i++) {
        in->motor_test[i] = mt ? s_mt_duty[i] : 0u;
    }
    in->cal_request = s_cal_req;
    s_cal_req = 0u;
    in->config_busy = s_cfg_busy;
    in->force_disarm = s_emergency;

    fc_update(in, &s_out);

    if (s_emergency) {
        motor_pwm_stop();
        if (!s_out.armed) {
            s_emergency = false;            /* disarmed: back to normal */
        }
    } else {
        motor_pwm_write(s_out.duty);
    }

    /* while armed the flight loop keeps the watchdog alive by itself, so a
     * stuck main loop does not reset a flying vehicle */
    if (s_out.armed && (s_loops % 10u) == 0u) {
        watchdog_kick();
    }

    uint32_t exec = time_us() - t0;
    s_exec_avg += ((float)exec - s_exec_avg) * 0.01f;
    if (exec > s_exec_max) {
        s_exec_max = exec;
    }
    s_loops++;
    s_rate_n++;
    if ((now_ms - s_rate_t0) >= 1000u) {
        s_loop_hz = (uint16_t)s_rate_n;
        s_rate_n = 0u;
        s_rate_t0 = now_ms;
    }
    s_heartbeat++;
}

/* SysTick (1 kHz, higher priority than the flight loop) */
void time_tick_hook(void)
{
    static uint32_t last_hb;
    static uint32_t stale_ms;
    uint32_t hb = s_heartbeat;
    if (hb != last_hb) {
        last_hb = hb;
        stale_ms = 0u;
        s_running = true;
        return;
    }
    if (stale_ms < 0xFFFFu) {
        stale_ms++;
    }
    if (stale_ms > STALL_MS) {
        s_running = false;
        motor_pwm_stop();
        if (fc_is_armed()) {
            s_emergency = true;
        }
    }
}
