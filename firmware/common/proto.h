/**
 * @file    proto.h
 * @brief   MiniQuad link protocol (board <-> web configurator), carried by
 *          USB CDC on board rev 2 and by USART1 / J2 on rev 1.
 *
 * Wire format (see docs/protocol.md):
 *
 *     COBS( id | seq | payload[0..LINK_MAX_PAYLOAD] | crc16_lo | crc16_hi ) 0x00
 *
 *  - crc16 = CRC-16/CCITT-FALSE over id, seq and payload.
 *  - Host requests use ids 0x01..0x3F. The device answers every request with
 *    id | 0x80 and the same seq; the first payload byte is a status code.
 *  - Unsolicited device messages (telemetry, log) use ids 0xC0..0xFF, seq 0.
 *  - All multi-byte fields are little-endian. Structures are packed.
 *
 * This header is shared by the bootloader and the application and mirrored
 * by web/js/proto.js (checked by web/test/proto_sync.test.mjs).
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

#define PROTO_VERSION           1u
#define LINK_MAX_PAYLOAD        288u        /* bytes after id/seq, before CRC */

/* ---------------- message ids ---------------- */
#define MSG_HELLO               0x01u       /* both: identify device / mode   */
#define MSG_REBOOT              0x02u       /* both: reboot (target below)    */

#define MSG_BL_ERASE            0x10u       /* bootloader only                */
#define MSG_BL_WRITE            0x11u
#define MSG_BL_READ             0x12u
#define MSG_BL_VERIFY           0x13u
#define MSG_BL_BOOT             0x14u

#define MSG_PARAM_INFO          0x20u       /* application only               */
#define MSG_PARAM_DESC          0x21u
#define MSG_PARAM_GET           0x22u
#define MSG_PARAM_SET           0x23u
#define MSG_PARAM_SAVE          0x24u
#define MSG_PARAM_RESET         0x25u
#define MSG_STREAM              0x28u
#define MSG_MOTOR_TEST          0x30u
#define MSG_CALIBRATE           0x31u
#define MSG_BEEP                0x32u
#define MSG_FAULT_INFO          0x33u
#define MSG_STATUS              0x34u

#define MSG_REPLY_FLAG          0x80u

#define MSG_LOG                 0xC0u       /* async: text log                */
#define MSG_TLM_STATUS          0xC1u       /* async: status snapshot         */
#define MSG_TLM_RATE            0xC2u       /* async: rate loop (PID) samples */
#define MSG_TLM_IMU             0xC3u       /* async: raw/filtered IMU        */
#define MSG_TLM_ALT             0xC4u       /* async: altitude hold           */

/* ---------------- status codes (first byte of every reply) ---------------- */
#define ST_OK                   0u
#define ST_ERR_UNKNOWN          1u          /* unknown message id             */
#define ST_ERR_LENGTH           2u          /* malformed payload length       */
#define ST_ERR_ARG              3u          /* argument out of range          */
#define ST_ERR_STATE            4u          /* not allowed now (e.g. armed)   */
#define ST_ERR_FLASH            5u          /* flash erase/program failed     */
#define ST_ERR_VERIFY           6u          /* image verification failed      */
#define ST_ERR_BUSY             7u

/* ---------------- modes / targets ---------------- */
#define DEV_MODE_BOOTLOADER     0u
#define DEV_MODE_APP            1u

#define REBOOT_NORMAL           0u          /* plain reset -> app            */
#define REBOOT_BOOTLOADER       1u          /* stay in our bootloader        */
#define REBOOT_ROM              2u          /* STM32 system memory (AN3155 UART / USB DFU) */

/* HELLO.boot_reason (bootloader mode): why the bootloader did not start the
 * application. 0 in application mode. */
#define BLR_NONE                0u          /* boot window, about to start the app */
#define BLR_REQUEST             1u          /* the application asked (firmware update) */
#define BLR_NO_APP              2u          /* no valid application image */
#define BLR_DBL_RESET           3u          /* reset pressed twice within the boot window */
#define BLR_CRASH_LOOP          4u          /* app restarted 3x without confirming its boot */
#define BLR_HOST                5u          /* rev 1: the configurator caught the boot window */

#define PROTO_PACKED            __attribute__((packed))

/* ---------------- HELLO ---------------- */
typedef struct PROTO_PACKED {
    uint8_t  status;
    uint8_t  mode;              /* DEV_MODE_*                                */
    uint8_t  proto_version;
    uint8_t  board_id;
    uint32_t fw_version;        /* running image                             */
    uint32_t app_version;       /* version in the app header, 0 if invalid   */
    uint8_t  app_valid;         /* 1 if the application image verifies       */
    uint8_t  boot_reason;       /* bootloader: why it stays resident (BLR_*) */
    uint8_t  reserved[2];
    uint32_t app_max_size;      /* bytes available for header + image       */
    uint32_t uid[3];
    uint32_t build_time;
    char     git_hash[16];
    char     board_name[16];
} msg_hello_rsp_t;

/* ---------------- REBOOT ---------------- */
typedef struct PROTO_PACKED {
    uint8_t  target;            /* REBOOT_* */
} msg_reboot_req_t;

/* ---------------- bootloader ---------------- */
#define BL_WRITE_MAX            256u        /* data bytes per BL_WRITE, multiple of 4 */

typedef struct PROTO_PACKED {
    uint32_t image_size;        /* bytes that will be written from MM_APP_BASE */
} msg_bl_erase_req_t;

typedef struct PROTO_PACKED {
    uint32_t offset;            /* from MM_APP_BASE, multiple of 4             */
    uint8_t  data[BL_WRITE_MAX];/* actual length = payload length - 4          */
} msg_bl_write_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint32_t offset;
} msg_bl_write_rsp_t;

typedef struct PROTO_PACKED {
    uint32_t offset;
    uint16_t length;            /* <= BL_WRITE_MAX */
} msg_bl_read_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint32_t offset;
    uint8_t  data[BL_WRITE_MAX];
} msg_bl_read_rsp_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint8_t  valid;
    uint32_t image_size;
    uint32_t crc_expected;
    uint32_t crc_actual;
    uint32_t app_version;
} msg_bl_verify_rsp_t;

/* ---------------- parameters ---------------- */
#define PARAM_NAME_MAX          24u         /* including terminating NUL */

#define PARAM_T_FLOAT           0u
#define PARAM_T_INT             1u
#define PARAM_T_BOOL            2u

#define PARAM_F_REBOOT          0x01u       /* takes effect after reboot */
#define PARAM_F_READONLY        0x02u
#define PARAM_F_CAL             0x04u       /* written by a calibration routine */
#define PARAM_F_LIVE            0x08u       /* may be changed while armed (tuning) */

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint16_t count;
    uint32_t table_hash;        /* changes when names/types/order change */
} msg_param_info_rsp_t;

typedef struct PROTO_PACKED {
    uint16_t index;
} msg_param_desc_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint16_t index;
    uint8_t  type;              /* PARAM_T_* */
    uint8_t  flags;             /* PARAM_F_* */
    uint32_t value;             /* raw 32-bit (float bits or int32)  */
    uint32_t min;
    uint32_t max;
    uint32_t def;
    char     name[PARAM_NAME_MAX];
} msg_param_desc_rsp_t;

#define PARAM_GET_MAX           64u

typedef struct PROTO_PACKED {
    uint16_t first;
    uint8_t  count;             /* <= PARAM_GET_MAX */
} msg_param_get_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint16_t first;
    uint8_t  count;
    uint32_t values[PARAM_GET_MAX];
} msg_param_get_rsp_t;

typedef struct PROTO_PACKED {
    uint16_t index;
    uint32_t value;
} msg_param_set_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint16_t index;
    uint32_t value;             /* value actually applied (after clamping) */
} msg_param_set_rsp_t;

/* ---------------- telemetry streams ---------------- */
#define STREAM_STATUS           0u          /* MSG_TLM_STATUS, max 50 Hz  */
#define STREAM_RATE             1u          /* MSG_TLM_RATE,   max 500 Hz */
#define STREAM_IMU              2u          /* MSG_TLM_IMU,    max 500 Hz */
#define STREAM_ALT              3u          /* MSG_TLM_ALT,    max 100 Hz */
#define STREAM_COUNT            4u

typedef struct PROTO_PACKED {
    uint8_t  stream;
    uint16_t rate_hz;           /* 0 = off */
} msg_stream_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint8_t  stream;
    uint16_t rate_hz;           /* rate actually applied */
} msg_stream_rsp_t;

/* ---------------- tests / calibration ---------------- */
#define MOTOR_TEST_KEY          0xA5u

typedef struct PROTO_PACKED {
    uint8_t  key;               /* MOTOR_TEST_KEY: "propellers are removed" */
    uint8_t  reserved;
    uint16_t duty[4];           /* 0..1000 per motor, auto-stop after 500 ms */
} msg_motor_test_req_t;

#define CAL_GYRO                1u
#define CAL_ACCEL_LEVEL         2u
#define CAL_ACCEL_RESET         3u

typedef struct PROTO_PACKED {
    uint8_t  what;              /* CAL_* */
} msg_calibrate_req_t;

typedef struct PROTO_PACKED {
    uint8_t  pattern;           /* notify tone id, see notify.h */
} msg_beep_req_t;

typedef struct PROTO_PACKED {
    uint8_t  status;
    uint8_t  valid;             /* a fault was recorded before the last reset */
    uint8_t  type;              /* fault type */
    uint8_t  reserved;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t uptime_ms;
    uint32_t reset_csr;         /* RCC->CSR seen by the bootloader */
    uint32_t boot_count;
} msg_fault_info_rsp_t;

/* ---------------- async: log ---------------- */
#define LOG_LEVEL_DEBUG         0u
#define LOG_LEVEL_INFO          1u
#define LOG_LEVEL_WARN          2u
#define LOG_LEVEL_ERROR         3u

/* ---------------- async: status (also returned by MSG_STATUS) ---------------- */
/* flags */
#define STF_ARMED               (1u << 0)
#define STF_RX_OK               (1u << 1)
#define STF_FAILSAFE            (1u << 2)
#define STF_IMU_OK              (1u << 3)
#define STF_GYRO_CAL            (1u << 4)   /* gyro calibrated            */
#define STF_CALIBRATING         (1u << 5)
#define STF_TOF_OK              (1u << 6)   /* ToF healthy                */
#define STF_TOF_VALID           (1u << 7)   /* last range valid           */
#define STF_BAT_PRESENT         (1u << 8)
#define STF_ALT_VALID           (1u << 9)   /* altitude estimate usable   */
#define STF_MOTOR_TEST          (1u << 10)
#define STF_HSE_FAIL            (1u << 11)  /* running on HSI             */
#define STF_LANDED              (1u << 12)
#define STF_MIXER_SAT           (1u << 13)
#define STF_HOST_ALIVE          (1u << 14)
#define STF_CRASHED             (1u << 15)  /* crash cut-off triggered    */
#define STF_USB                 (1u << 16)  /* rev 2: VBUS on the USB-C port */

/* arming blockers (arm_block bitmask) */
#define ARMB_NO_RX              (1u << 0)
#define ARMB_FAILSAFE           (1u << 1)
#define ARMB_THROTTLE           (1u << 2)   /* throttle not low           */
#define ARMB_ANGLE              (1u << 3)   /* not level                  */
#define ARMB_CALIBRATING        (1u << 4)
#define ARMB_IMU                (1u << 5)
#define ARMB_BATTERY            (1u << 6)   /* critical battery           */
#define ARMB_SWITCH             (1u << 7)   /* arm switch must be cycled  */
#define ARMB_MOTOR_TEST         (1u << 8)
#define ARMB_BOOT_GRACE         (1u << 9)
#define ARMB_LOOP               (1u << 10)  /* flight loop not running    */
#define ARMB_CRASH              (1u << 11)  /* crash detected, cycle arm  */
#define ARMB_CONFIG             (1u << 12)  /* config being saved         */
#define ARMB_NO_BAT             (1u << 13)  /* no battery (e.g. USB power only) */

/* flight modes */
#define FMODE_ACRO              0u
#define FMODE_ANGLE             1u
#define FMODE_ALT               2u
#define FMODE_FAILSAFE          3u

/* battery state */
#define BAT_OK                  0u
#define BAT_WARN                1u
#define BAT_CRIT                2u
#define BAT_NONE                3u

typedef struct PROTO_PACKED {
    uint32_t time_ms;
    uint32_t flags;             /* STF_*  */
    uint32_t arm_block;         /* ARMB_* */
    uint8_t  mode;              /* FMODE_* */
    uint8_t  fs_phase;          /* 0 none, 1 landing, 2 dropped */
    uint8_t  bat_state;         /* BAT_* */
    uint8_t  cpu_load;          /* % of time the main loop was busy */
    int16_t  roll_cd;           /* centidegrees */
    int16_t  pitch_cd;
    int16_t  yaw_cd;
    uint16_t vbat_mv;           /* filtered for compensation */
    uint16_t vbat_slow_mv;      /* filtered for warnings */
    uint16_t bat_comp_x1000;    /* thrust compensation factor */
    uint16_t loop_us_avg;       /* flight loop execution time */
    uint16_t loop_us_max;
    uint16_t loop_hz;
    int8_t   mcu_temp_c;
    int8_t   imu_temp_c;
    uint8_t  rc_count;          /* channels per PPM frame */
    uint8_t  rc_rate_hz;
    uint16_t rc_us[8];
    uint16_t motor[4];          /* duty x1000 */
    uint16_t tof_mm;
    uint8_t  tof_status;        /* ULD range status (0 = valid) */
    uint8_t  tof_rate_hz;
    int16_t  alt_cm;
    int16_t  vz_cms;
    int16_t  alt_sp_cm;
    uint16_t throttle_x1000;    /* collective thrust command */
    uint16_t hover_x1000;       /* hover thrust estimate */
    uint16_t imu_errors;
    uint16_t tof_errors;
    uint16_t rx_errors;
    uint16_t link_drops;        /* telemetry frames dropped (TX full) */
} tlm_status_t;

/* ---------------- async: rate loop ---------------- */
typedef struct PROTO_PACKED {
    uint32_t time_us;
    int16_t  sp[3];             /* rate setpoint, dps x10  (roll, pitch, yaw) */
    int16_t  gyro[3];           /* filtered gyro, dps x10                     */
    int16_t  p[3];              /* PID terms, normalised thrust x10000        */
    int16_t  i[3];
    int16_t  d[3];
    int16_t  f[3];
    uint16_t throttle;          /* x1000 */
    uint16_t motor[4];          /* x1000 */
} tlm_rate_t;

/* ---------------- async: IMU ---------------- */
typedef struct PROTO_PACKED {
    uint32_t time_us;
    int16_t  gyro_raw[3];       /* body frame, before software filters, dps x10 */
    int16_t  gyro_filt[3];      /* dps x10 */
    int16_t  acc[3];            /* body frame, mg */
} tlm_imu_t;

/* ---------------- async: altitude ---------------- */
typedef struct PROTO_PACKED {
    uint32_t time_us;
    float    h_est;             /* m, above ground */
    float    vz_est;            /* m/s, up positive */
    float    h_sp;              /* m */
    float    vz_sp;             /* m/s */
    float    h_tof;             /* last tilt-compensated ToF height, m */
    float    acc_up;            /* m/s^2, earth frame, gravity removed */
    float    thrust;            /* collective command 0..1 */
    float    hover;             /* hover thrust estimate 0..1 */
    uint8_t  tof_status;
    uint8_t  flags;             /* bit0 measurement used, bit1 alt mode active, bit2 landed */
} tlm_alt_t;

_Static_assert(sizeof(msg_hello_rsp_t) == 68, "msg_hello_rsp_t size");
_Static_assert(sizeof(msg_param_desc_rsp_t) == 45, "msg_param_desc_rsp_t size");
_Static_assert(sizeof(msg_fault_info_rsp_t) == 44, "msg_fault_info_rsp_t size");
_Static_assert(sizeof(tlm_status_t) == 84, "tlm_status_t size");
_Static_assert(sizeof(tlm_rate_t) == 50, "tlm_rate_t size");
_Static_assert(sizeof(tlm_imu_t) == 22, "tlm_imu_t size");
_Static_assert(sizeof(tlm_alt_t) == 38, "tlm_alt_t size");
_Static_assert(sizeof(msg_param_get_rsp_t) <= LINK_MAX_PAYLOAD, "param get fits");
_Static_assert(sizeof(msg_bl_read_rsp_t) <= LINK_MAX_PAYLOAD, "bl read fits");
_Static_assert(sizeof(msg_bl_write_req_t) <= LINK_MAX_PAYLOAD, "bl write fits");

#endif /* PROTO_H */
