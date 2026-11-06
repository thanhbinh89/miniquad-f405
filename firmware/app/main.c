/**
 * @file    main.c
 * @brief   MiniQuad-F405 flight controller application.
 *
 * Execution model (bare metal, no RTOS):
 *
 *   interrupts (priority)        work
 *   ---------------------------  -----------------------------------------
 *   EXTI15_10 (1)                PPM edge time stamp, ToF data-ready flag
 *   EXTI3 / DMA2 S0 (2)          IMU data ready -> SPI DMA read -> pend loop
 *   SysTick (3)                  1 ms tick, flight loop stall supervisor
 *   I2C2 + DMA1 (5)              ToF transactions
 *   DMA2 S7 (6)                  link TX
 *   FLIGHT_SWI (8)               flight loop, 1 kHz (fc_update)
 *   main loop (thread)           cooperative tasks below
 */
#include <stdbool.h>
#include <string.h>

#include "adc.h"
#include "app_services.h"
#include "battery.h"
#include "board.h"
#include "buzzer.h"
#include "clock.h"
#include "comms.h"
#include "config.h"
#include "config_store.h"
#include "fault.h"
#include "fc.h"
#include "fc_events.h"
#include "flight_task.h"
#include "fw_version.h"
#include "icm42688.h"
#include "led.h"
#include "log.h"
#include "memory_map.h"
#include "motor_pwm.h"
#include "notify.h"
#include "params.h"
#include "ppm_input.h"
#include "proto.h"
#include "sched.h"
#include "shared_ram.h"
#include "systime.h"
#include "uart_link.h"
#include "vl53l1.h"
#include "watchdog.h"

battery_t g_battery;

static battery_cal_t s_bat_cal;
static bool          s_imu_ok;
static uint32_t      s_imu_retry_ms;

/* ---------------- link port ---------------- */
static const comms_port_t s_port = {
    .read = uart_link_read,
    .write = uart_link_write,
    .tx_free = uart_link_tx_free,
};

/* ---------------- tasks ---------------- */
static void task_comms(void)
{
    comms_task();
    uint8_t target;
    if (comms_take_reboot(&target)) {
        uint32_t t0 = time_ms();
        while (!uart_link_tx_idle() && (time_ms() - t0) < 100u) {
        }
        motor_pwm_stop();
        svc_reboot(target);
    }
}

static void task_tof(void)
{
    vl53l1_task(time_ms());
}

static void task_battery(void)
{
    adc_raw_t raw;
    adc_read(&raw);
    battery_update(&g_battery, &s_bat_cal, raw.vbat, raw.vref, raw.temp, 0.005f);
    flight_task_set_battery(&g_battery);
}

static void task_notify(void)
{
    fc_event_t ev;
    while (fc_event_pop(&ev)) {
        notify_event(ev);
        log_printf((ev == FCEV_FAILSAFE || ev == FCEV_CRASH || ev == FCEV_EMERGENCY) ? LOG_LEVEL_WARN
                                                                                     : LOG_LEVEL_INFO,
                   "%s", fc_event_name(ev));
    }
    fc_status_t st;
    fc_get_status(&st);
    if (!icm42688_healthy()) {
        st.flags &= ~STF_IMU_OK;
    }
    st.bat_state = g_battery.state;

    /* beeper switch: decoded from the raw frame (works disarmed too) */
    bool beeper = false;
    if (g_cfg.rc_ch_beeper > 0) {
        rc_frame_t rc;
        ppm_input_get(&rc);
        int ch = g_cfg.rc_ch_beeper - 1;
        beeper = rc.age_ms < 500u && ch < (int)rc.count && (float)rc.ch[ch] >= g_cfg.rc_arm_us;
    }
    notify_update(&st, time_ms(), beeper);
}

static void task_housekeeping(void)
{
    uint32_t now = time_ms();
    ppm_input_task(now, g_cfg.ppm_falling != 0);

    /* accelerometer calibration finished in the flight loop: store it */
    vec3_t off;
    bool ok;
    if (fc_take_acc_cal_result(&off, &ok)) {
        if (ok) {
            params_set((uint16_t)params_find("acc_off_x"), off.x);
            params_set((uint16_t)params_find("acc_off_y"), off.y);
            params_set((uint16_t)params_find("acc_off_z"), off.z);
            bool saved = svc_save_config();
            log_printf(LOG_LEVEL_INFO, "acc offsets %.3f %.3f %.3f g (%s)", (double)off.x, (double)off.y,
                       (double)off.z, saved ? "saved" : "NOT saved");
        } else {
            log_printf(LOG_LEVEL_WARN, "accelerometer calibration failed");
        }
    }

    /* IMU missing at boot: retry while disarmed (e.g. loose connection) */
    if (!s_imu_ok && !fc_is_armed() && (now - s_imu_retry_ms) > 2000u) {
        s_imu_retry_ms = now;
        s_imu_ok = icm42688_init();
        if (s_imu_ok) {
            icm42688_start();
            log_printf(LOG_LEVEL_INFO, "IMU recovered");
        }
    }

    watchdog_kick();
}

static void task_stats(void)
{
    static uint32_t n;
    if ((++n % 10u) == 0u) {
        flight_task_stats_reset_max();      /* max over the last 10 s */
    }
}

static sched_task_t s_tasks[] = {
    { "comms",   task_comms,        0u,      0u, 0u, 0u, 0u },
    { "tof",     task_tof,          0u,      0u, 0u, 0u, 0u },
    { "battery", task_battery,      5000u,   0u, 0u, 0u, 0u },
    { "notify",  task_notify,       10000u,  0u, 0u, 0u, 0u },
    { "house",   task_housekeeping, 10000u,  0u, 0u, 0u, 0u },
    { "stats",   task_stats,        1000000u, 0u, 0u, 0u, 0u },
};

/* ---------------- boot ---------------- */
static void log_boot_info(void)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    log_printf(LOG_LEVEL_INFO, "MiniQuad-F405 %s (%s), clock %s", FW_VERSION_STR, FW_GIT_HASH,
               (clock_source() == CLOCK_SRC_HSE) ? "HSE" : "HSI (crystal failed!)");
    if (sh->magic == SHARED_MAGIC) {
        uint32_t csr = sh->reset_csr;
        log_printf(LOG_LEVEL_INFO, "reset cause 0x%08X%s%s%s boot #%u", (unsigned)csr,
                   (csr & RCC_CSR_IWDGRSTF) ? " watchdog" : "", (csr & RCC_CSR_SFTRSTF) ? " software" : "",
                   (csr & RCC_CSR_BORRSTF) ? " brown-out" : "", (unsigned)sh->boot_count);
    }
    fault_record_t f;
    if (fault_last(&f)) {
        log_printf(LOG_LEVEL_ERROR, "previous FAULT type %u pc 0x%08X lr 0x%08X cfsr 0x%08X",
                   (unsigned)f.type, (unsigned)f.pc, (unsigned)f.lr, (unsigned)f.cfsr);
    }
    const cfg_store_info_t *ci = config_store_info();
    log_printf(LOG_LEVEL_INFO, "config: %s (seq %u, %u params, %u unknown)",
               ci->loaded ? "loaded" : "defaults", (unsigned)ci->seq, (unsigned)ci->entries_loaded,
               (unsigned)ci->entries_unknown);
    log_printf(LOG_LEVEL_INFO, "IMU %s (WHO_AM_I 0x%02X)", s_imu_ok ? "ok" : "NOT FOUND",
               (unsigned)icm42688_whoami());
}

int main(void)
{
    /* safe outputs first: motors stay low until the timer drives them */
    (void)clock_init(true);
    NVIC_SetPriorityGrouping(3u);           /* 4 bits preemption, no sub-priority */
    time_init();
    fault_init();

    motor_pwm_init();
    led_init();
    buzzer_init();

    config_store_init(config_store_target_ops());
    (void)config_store_load();

    uart_link_init(LINK_BAUD);
    comms_init(&s_port);

    s_bat_cal.vrefint_cal = *(const volatile uint16_t *)MM_VREFINT_CAL;
    s_bat_cal.ts_cal1 = *(const volatile uint16_t *)MM_TS_CAL1;
    s_bat_cal.ts_cal2 = *(const volatile uint16_t *)MM_TS_CAL2;
    battery_init(&g_battery);
    adc_init();

    ppm_input_init(g_cfg.ppm_falling != 0);
    vl53l1_init((g_cfg.tof_mode == 1) ? VL53L1_DIST_SHORT : VL53L1_DIST_LONG, (uint16_t)g_cfg.tof_tb_ms,
                (int16_t)0);
    flight_task_init();
    notify_init();

    s_imu_ok = icm42688_init();
    if (s_imu_ok) {
        icm42688_start();                   /* the 1 kHz flight loop starts here */
        notify_play(TONE_STARTUP);
    } else {
        notify_play(TONE_ERROR);
    }
    log_boot_info();
    fault_clear();

    watchdog_init(WATCHDOG_TIMEOUT_MS);
    sched_init(s_tasks, sizeof(s_tasks) / sizeof(s_tasks[0]));

    for (;;) {
        sched_run();
    }
}
