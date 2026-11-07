/**
 * @file    app_services.c
 * @brief   Target implementation of the services used by the protocol.
 */
#include "app_services.h"

#include <string.h>
#include "app_header.h"
#include "battery.h"
#include "board.h"
#include "clock.h"
#include "comms.h"
#include "config.h"
#include "config_store.h"
#include "fault.h"
#include "fc.h"
#include "flight_task.h"
#include "fw_version.h"
#include "icm42688.h"
#include "memory_map.h"
#include "notify.h"
#include "params.h"
#include "ppm_input.h"
#include "sched.h"
#include "shared_ram.h"
#include "systime.h"
#include "tlm_format.h"
#include "tlm_rings.h"
#include "link_port.h"
#include "vl53l1.h"
#include "watchdog.h"
#include "i2c2.h"

#define MOTOR_TEST_HOLD_MS  500u
#define REBOOT_DETACH_MS    20u     /* USB: let the host see the device leave */

extern battery_t g_battery;         /* owned by main.c */

uint32_t svc_now_ms(void)
{
    return time_ms();
}

bool svc_armed(void)
{
    return fc_is_armed();
}

void svc_identity(msg_hello_rsp_t *h)
{
    h->board_id = BOARD_ID;
    h->fw_version = FW_VERSION;
    h->app_version = APP_HEADER->fw_version;
    h->app_valid = 1u;
    h->app_max_size = MM_APP_MAX_SIZE;
    const volatile uint32_t *uid = (const volatile uint32_t *)MM_UID_BASE;
    h->uid[0] = uid[0];
    h->uid[1] = uid[1];
    h->uid[2] = uid[2];
    h->build_time = FW_BUILD_TIME;
    strncpy(h->git_hash, FW_GIT_HASH, sizeof(h->git_hash) - 1u);
    _Static_assert(sizeof(BOARD_NAME) <= sizeof(h->board_name), "BOARD_NAME too long");
    memcpy(h->board_name, BOARD_NAME, sizeof(BOARD_NAME));
}

void svc_reboot(uint8_t target)
{
    volatile shared_ram_t *sh = SHARED_RAM;
    if (sh->magic != SHARED_MAGIC) {
        memset((void *)sh, 0, sizeof(*sh));
        sh->magic = SHARED_MAGIC;
    }
    sh->request = (target == REBOOT_BOOTLOADER) ? BL_REQ_STAY
                : (target == REBOOT_ROM) ? BL_REQ_ROM : BL_REQ_NONE;
    sh->app_starts = 0u;                    /* deliberate: not a crash */
    __DSB();
    link_port_detach(REBOOT_DETACH_MS);
    NVIC_SystemReset();
}

bool svc_save_config(void)
{
    if (fc_is_armed()) {
        return false;
    }
    flight_task_motor_test_stop();
    flight_task_set_config_busy(true);
    watchdog_set_timeout(WATCHDOG_LONG_TIMEOUT_MS);     /* sector erase ~0.3-0.5 s */
    bool ok = config_store_save();
    watchdog_set_timeout(WATCHDOG_TIMEOUT_MS);
    flight_task_set_config_busy(false);
    if (ok) {
        notify_play(TONE_SAVE_OK);
    }
    return ok;
}

bool svc_motor_test(const uint16_t duty[4])
{
    if (!g_battery.present) {
        return false;                       /* USB power only: nothing to drive the motors */
    }
    flight_task_motor_test(duty, time_ms() + MOTOR_TEST_HOLD_MS);
    return true;
}

void svc_motor_test_stop(void)
{
    flight_task_motor_test_stop();
}

bool svc_calibrate(uint8_t what)
{
    switch (what) {
    case CAL_GYRO:
    case CAL_ACCEL_LEVEL:
        flight_task_request_cal(what);
        return true;
    case CAL_ACCEL_RESET:
        for (int i = 0; i < 3; i++) {
            int idx = params_find((i == 0) ? "acc_off_x" : ((i == 1) ? "acc_off_y" : "acc_off_z"));
            if (idx >= 0) {
                params_set((uint16_t)idx, 0.0f);
            }
        }
        return true;
    default:
        return false;
    }
}

void svc_beep(uint8_t pattern)
{
    notify_play((notify_tone_t)pattern);
}

void svc_status(tlm_status_t *t)
{
    fc_status_t st;
    fc_get_status(&st);
    flight_stats_t fs;
    flight_task_stats(&fs);
    rc_frame_t rc;
    ppm_input_get(&rc);
    vl53l1_sample_t smp;
    memset(&smp, 0, sizeof(smp));
    bool have_tof = vl53l1_get(&smp);

    /* live driver state wins over the (possibly stale) flight snapshot */
    tlm_extra_t x;
    memset(&x, 0, sizeof(x));
    x.time_ms = time_ms();
    x.flags_clear = STF_IMU_OK | STF_BAT_PRESENT | STF_MOTOR_TEST |
                    (vl53l1_healthy() ? 0u : (STF_TOF_OK | STF_TOF_VALID));
    x.flags_set = (icm42688_healthy() ? STF_IMU_OK : 0u) |
                  (vl53l1_healthy() ? STF_TOF_OK : 0u) |
                  (g_battery.present ? STF_BAT_PRESENT : 0u) |
                  ((clock_source() == CLOCK_SRC_HSI) ? STF_HSE_FAIL : 0u) |
                  (comms_host_alive() ? STF_HOST_ALIVE : 0u) |
                  (link_port_usb_power() ? STF_USB : 0u) |
                  (flight_task_motor_test_active() ? STF_MOTOR_TEST : 0u);
    x.arm_block_extra = (fs.running ? 0u : ARMB_LOOP) | (icm42688_healthy() ? 0u : ARMB_IMU);
    x.bat_state = g_battery.state;
    x.v_comp = g_battery.v_comp;
    x.v_slow = g_battery.v_slow;
    x.cpu_load = sched_cpu_load();
    x.loop_us_avg = fs.loop_us_avg;
    x.loop_us_max = fs.loop_us_max;
    x.loop_hz = fs.loop_hz;
    x.mcu_temp = g_battery.mcu_temp;
    x.rc = &rc;
    x.tof_have = have_tof;
    x.tof_mm = smp.dist_mm;
    x.tof_status = smp.status;
    x.tof_rate_hz = vl53l1_rate_hz();
    x.imu_errors = icm42688_errors();
    x.tof_errors = vl53l1_errors() + i2c2_errors();
    x.rx_errors = ppm_input_glitches();
    x.link_drops = comms_drops() + tlm_rings_dropped();
    tlm_format_status(&st, &x, t);
}

void svc_alt(tlm_alt_t *a)
{
    fc_status_t st;
    fc_get_status(&st);
    tlm_format_alt(&st, time_us(), a);
}

void svc_fault(msg_fault_info_rsp_t *f)
{
    fault_record_t r;
    if (fault_last(&r)) {
        f->valid = 1u;
        f->type = (uint8_t)r.type;
        f->pc = r.pc;
        f->lr = r.lr;
        f->psr = r.psr;
        f->cfsr = r.cfsr;
        f->hfsr = r.hfsr;
        f->mmfar = r.mmfar;
        f->bfar = r.bfar;
        f->uptime_ms = r.uptime_ms;
    }
    volatile shared_ram_t *sh = SHARED_RAM;
    if (sh->magic == SHARED_MAGIC) {
        f->reset_csr = sh->reset_csr;
        f->boot_count = sh->boot_count;
    }
}
