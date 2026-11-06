/**
 * MiniQuad link protocol - JavaScript mirror of firmware/common/proto.h.
 *
 * `C` holds every #define of proto.h under its C name; test/proto_sync.test.js
 * parses proto.h and fails when the two drift apart. Decoders follow the
 * packed little-endian structures field by field.
 */

export const C = {
  PROTO_VERSION: 1,
  LINK_MAX_PAYLOAD: 288,

  MSG_HELLO: 0x01, MSG_REBOOT: 0x02,
  MSG_BL_ERASE: 0x10, MSG_BL_WRITE: 0x11, MSG_BL_READ: 0x12, MSG_BL_VERIFY: 0x13, MSG_BL_BOOT: 0x14,
  MSG_PARAM_INFO: 0x20, MSG_PARAM_DESC: 0x21, MSG_PARAM_GET: 0x22, MSG_PARAM_SET: 0x23,
  MSG_PARAM_SAVE: 0x24, MSG_PARAM_RESET: 0x25, MSG_STREAM: 0x28,
  MSG_MOTOR_TEST: 0x30, MSG_CALIBRATE: 0x31, MSG_BEEP: 0x32, MSG_FAULT_INFO: 0x33, MSG_STATUS: 0x34,
  MSG_REPLY_FLAG: 0x80,
  MSG_LOG: 0xC0, MSG_TLM_STATUS: 0xC1, MSG_TLM_RATE: 0xC2, MSG_TLM_IMU: 0xC3, MSG_TLM_ALT: 0xC4,

  ST_OK: 0, ST_ERR_UNKNOWN: 1, ST_ERR_LENGTH: 2, ST_ERR_ARG: 3, ST_ERR_STATE: 4,
  ST_ERR_FLASH: 5, ST_ERR_VERIFY: 6, ST_ERR_BUSY: 7,

  DEV_MODE_BOOTLOADER: 0, DEV_MODE_APP: 1,
  REBOOT_NORMAL: 0, REBOOT_BOOTLOADER: 1, REBOOT_ROM: 2,

  BL_WRITE_MAX: 256,

  PARAM_NAME_MAX: 24,
  PARAM_T_FLOAT: 0, PARAM_T_INT: 1, PARAM_T_BOOL: 2,
  PARAM_F_REBOOT: 0x01, PARAM_F_READONLY: 0x02, PARAM_F_CAL: 0x04, PARAM_F_LIVE: 0x08,
  PARAM_GET_MAX: 64,

  STREAM_STATUS: 0, STREAM_RATE: 1, STREAM_IMU: 2, STREAM_ALT: 3, STREAM_COUNT: 4,

  MOTOR_TEST_KEY: 0xA5,
  CAL_GYRO: 1, CAL_ACCEL_LEVEL: 2, CAL_ACCEL_RESET: 3,

  LOG_LEVEL_DEBUG: 0, LOG_LEVEL_INFO: 1, LOG_LEVEL_WARN: 2, LOG_LEVEL_ERROR: 3,

  STF_ARMED: 1 << 0, STF_RX_OK: 1 << 1, STF_FAILSAFE: 1 << 2, STF_IMU_OK: 1 << 3,
  STF_GYRO_CAL: 1 << 4, STF_CALIBRATING: 1 << 5, STF_TOF_OK: 1 << 6, STF_TOF_VALID: 1 << 7,
  STF_BAT_PRESENT: 1 << 8, STF_ALT_VALID: 1 << 9, STF_MOTOR_TEST: 1 << 10, STF_HSE_FAIL: 1 << 11,
  STF_LANDED: 1 << 12, STF_MIXER_SAT: 1 << 13, STF_HOST_ALIVE: 1 << 14, STF_CRASHED: 1 << 15,

  ARMB_NO_RX: 1 << 0, ARMB_FAILSAFE: 1 << 1, ARMB_THROTTLE: 1 << 2, ARMB_ANGLE: 1 << 3,
  ARMB_CALIBRATING: 1 << 4, ARMB_IMU: 1 << 5, ARMB_BATTERY: 1 << 6, ARMB_SWITCH: 1 << 7,
  ARMB_MOTOR_TEST: 1 << 8, ARMB_BOOT_GRACE: 1 << 9, ARMB_LOOP: 1 << 10, ARMB_CRASH: 1 << 11,
  ARMB_CONFIG: 1 << 12,

  FMODE_ACRO: 0, FMODE_ANGLE: 1, FMODE_ALT: 2, FMODE_FAILSAFE: 3,
  BAT_OK: 0, BAT_WARN: 1, BAT_CRIT: 2, BAT_NONE: 3,
};

/** sizeof() of the packed structures (checked against the _Static_asserts). */
export const SIZES = {
  msg_hello_rsp_t: 68,
  msg_param_desc_rsp_t: 45,
  msg_fault_info_rsp_t: 44,
  tlm_status_t: 84,
  tlm_rate_t: 50,
  tlm_imu_t: 22,
  tlm_alt_t: 38,
};

export const STATUS_TEXT = {
  [C.ST_OK]: 'OK',
  [C.ST_ERR_UNKNOWN]: 'Lệnh không được hỗ trợ',
  [C.ST_ERR_LENGTH]: 'Sai độ dài gói',
  [C.ST_ERR_ARG]: 'Tham số không hợp lệ',
  [C.ST_ERR_STATE]: 'Không cho phép lúc này (đang ARM?)',
  [C.ST_ERR_FLASH]: 'Lỗi ghi flash',
  [C.ST_ERR_VERIFY]: 'Kiểm tra image thất bại',
  [C.ST_ERR_BUSY]: 'Thiết bị bận',
};

export const STATUS_FLAGS = [
  ['STF_ARMED', 'ARMED'], ['STF_RX_OK', 'RX'], ['STF_FAILSAFE', 'FAILSAFE'], ['STF_IMU_OK', 'IMU'],
  ['STF_GYRO_CAL', 'Gyro cal'], ['STF_CALIBRATING', 'Đang hiệu chuẩn'], ['STF_TOF_OK', 'ToF'],
  ['STF_TOF_VALID', 'ToF đo được'], ['STF_BAT_PRESENT', 'Pin'], ['STF_ALT_VALID', 'Độ cao OK'],
  ['STF_MOTOR_TEST', 'Test motor'], ['STF_HSE_FAIL', 'Lỗi thạch anh'], ['STF_LANDED', 'Đã đáp'],
  ['STF_MIXER_SAT', 'Mixer bão hoà'], ['STF_HOST_ALIVE', 'Host'], ['STF_CRASHED', 'Va chạm'],
].map(([k, label]) => ({ key: k, bit: C[k], label }));

export const ARM_BLOCKERS = [
  ['ARMB_NO_RX', 'Không có tín hiệu PPM'],
  ['ARMB_FAILSAFE', 'Đang failsafe'],
  ['ARMB_THROTTLE', 'Ga chưa về thấp nhất'],
  ['ARMB_ANGLE', 'Quad không nằm ngang'],
  ['ARMB_CALIBRATING', 'Đang hiệu chuẩn gyro/accel'],
  ['ARMB_IMU', 'IMU lỗi'],
  ['ARMB_BATTERY', 'Pin quá yếu'],
  ['ARMB_SWITCH', 'Gạt công tắc ARM về OFF rồi bật lại'],
  ['ARMB_MOTOR_TEST', 'Đang test motor'],
  ['ARMB_BOOT_GRACE', 'Vừa khởi động (chờ vài giây)'],
  ['ARMB_LOOP', 'Vòng điều khiển không chạy'],
  ['ARMB_CRASH', 'Phát hiện va chạm - gạt ARM về OFF'],
  ['ARMB_CONFIG', 'Đang lưu cấu hình'],
].map(([k, text]) => ({ key: k, bit: C[k], text }));

export const FLIGHT_MODES = { 0: 'ACRO', 1: 'ANGLE', 2: 'ALT HOLD', 3: 'FAILSAFE' };
export const BAT_STATES = { 0: 'OK', 1: 'Yếu', 2: 'Rất yếu', 3: 'Không có pin' };
export const FS_PHASES = { 0: '-', 1: 'Đang hạ cánh', 2: 'Tắt motor' };
export const ALT_PHASES = { 0: 'Trên đất', 1: 'Cất cánh', 2: 'Bay', 3: 'Hạ cánh' };

/** MSG_BEEP patterns = notify_tone_t (firmware/app/ui/notify.h). */
export const TONES = [
  [1, 'TONE_STARTUP', 'Khởi động'], [2, 'TONE_ARM', 'ARM'], [3, 'TONE_DISARM', 'DISARM'],
  [4, 'TONE_CAL_OK', 'Hiệu chuẩn OK'], [5, 'TONE_CAL_FAIL', 'Hiệu chuẩn lỗi'], [6, 'TONE_ERROR', 'Lỗi'],
  [7, 'TONE_MODE', 'Đổi chế độ'], [8, 'TONE_SAVE_OK', 'Đã lưu'], [9, 'TONE_TEST', 'Test'],
].map(([id, key, label]) => ({ id, key, label }));

/** beep_mask bits = NOTIFY_MASK_* (notify.h). */
export const BEEP_MASK = [
  [0, 'Khởi động'], [1, 'ARM / DISARM'], [2, 'Failsafe'], [3, 'Pin yếu'], [4, 'Hiệu chuẩn'],
  [5, 'Beeper (công tắc)'], [6, 'Lỗi'], [7, 'Đổi chế độ'], [8, 'Định vị sau va chạm'],
].map(([bit, label]) => ({ bit, label }));

export const FAULT_TYPES = { 0: '-', 1: 'HardFault', 2: 'MemManage', 3: 'BusFault', 4: 'UsageFault', 5: 'NMI', 6: 'Tràn stack' };

/* ------------------------------------------------------------------------ */
/* binary helpers                                                           */
/* ------------------------------------------------------------------------ */
export class Reader {
  constructor(bytes, offset = 0) {
    this.b = bytes;
    this.dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.o = offset;
  }
  u8() { return this.dv.getUint8(this.o++); }
  i8() { return this.dv.getInt8(this.o++); }
  u16() { const v = this.dv.getUint16(this.o, true); this.o += 2; return v; }
  i16() { const v = this.dv.getInt16(this.o, true); this.o += 2; return v; }
  u32() { const v = this.dv.getUint32(this.o, true); this.o += 4; return v; }
  i32() { const v = this.dv.getInt32(this.o, true); this.o += 4; return v; }
  f32() { const v = this.dv.getFloat32(this.o, true); this.o += 4; return v; }
  bytes(n) {
    if (this.o + n > this.b.length) throw new RangeError('payload too short');
    const v = this.b.slice(this.o, this.o + n);
    this.o += n;
    return v;
  }
  str(n) {
    const raw = this.bytes(n);
    let end = raw.indexOf(0);
    if (end < 0) end = raw.length;
    return new TextDecoder().decode(raw.subarray(0, end));
  }
  get left() { return this.b.length - this.o; }
}

export class Writer {
  constructor(size) {
    this.b = new Uint8Array(size);
    this.dv = new DataView(this.b.buffer);
    this.o = 0;
  }
  u8(v) { this.dv.setUint8(this.o++, v & 0xFF); return this; }
  u16(v) { this.dv.setUint16(this.o, v & 0xFFFF, true); this.o += 2; return this; }
  u32(v) { this.dv.setUint32(this.o, v >>> 0, true); this.o += 4; return this; }
  bytes(a) { this.b.set(a, this.o); this.o += a.length; return this; }
  done() { return this.b.subarray(0, this.o); }
}

export const f32ToU32 = (f) => { const dv = new DataView(new ArrayBuffer(4)); dv.setFloat32(0, f, true); return dv.getUint32(0, true); };
export const u32ToF32 = (u) => { const dv = new DataView(new ArrayBuffer(4)); dv.setUint32(0, u >>> 0, true); return dv.getFloat32(0, true); };
export const u32ToI32 = (u) => u | 0;

export const versionString = (v) => `${(v >>> 16) & 0xFF}.${(v >>> 8) & 0xFF}.${v & 0xFF}`;
export const parseVersion = (s) => {
  const m = /^v?(\d+)\.(\d+)\.(\d+)/.exec(String(s).trim());
  return m ? ((+m[1] << 16) | (+m[2] << 8) | +m[3]) >>> 0 : 0;
};

/* ------------------------------------------------------------------------ */
/* decoders (payload = reply payload incl. the status byte)                 */
/* ------------------------------------------------------------------------ */
export function decodeHello(p) {
  const r = new Reader(p);
  const h = {
    status: r.u8(), mode: r.u8(), protoVersion: r.u8(), boardId: r.u8(),
    fwVersion: r.u32(), appVersion: r.u32(), appValid: r.u8() === 1,
  };
  r.bytes(3);
  h.appMaxSize = r.u32();
  h.uid = [r.u32(), r.u32(), r.u32()];
  h.buildTime = r.u32();
  h.gitHash = r.str(16);
  h.boardName = r.str(16);
  h.uidHex = h.uid.map((x) => x.toString(16).padStart(8, '0')).join('').toUpperCase();
  return h;
}

export function decodeParamInfo(p) {
  const r = new Reader(p);
  return { status: r.u8(), count: r.u16(), tableHash: r.u32() };
}

export function decodeParamDesc(p) {
  const r = new Reader(p);
  return {
    status: r.u8(), index: r.u16(), type: r.u8(), flags: r.u8(),
    value: r.u32(), min: r.u32(), max: r.u32(), def: r.u32(), name: r.str(C.PARAM_NAME_MAX),
  };
}

export function decodeParamGet(p) {
  const r = new Reader(p);
  const out = { status: r.u8(), first: r.u16(), count: r.u8(), values: [] };
  for (let i = 0; i < out.count; i++) out.values.push(r.u32());
  return out;
}

export function decodeParamSet(p) {
  const r = new Reader(p);
  return { status: r.u8(), index: r.u16(), value: r.u32() };
}

export function decodeStream(p) {
  const r = new Reader(p);
  return { status: r.u8(), stream: r.u8(), rateHz: r.u16() };
}

export function decodeBlWrite(p) {
  const r = new Reader(p);
  return { status: r.u8(), offset: r.u32() };
}

export function decodeBlRead(p) {
  const r = new Reader(p);
  return { status: r.u8(), offset: r.u32(), data: r.bytes(r.left) };
}

export function decodeBlVerify(p) {
  const r = new Reader(p);
  return {
    status: r.u8(), valid: r.u8() === 1, imageSize: r.u32(),
    crcExpected: r.u32(), crcActual: r.u32(), appVersion: r.u32(),
  };
}

export function decodeFault(p) {
  const r = new Reader(p);
  const f = { status: r.u8(), valid: r.u8() === 1, type: r.u8() };
  r.u8();
  Object.assign(f, {
    pc: r.u32(), lr: r.u32(), psr: r.u32(), cfsr: r.u32(), hfsr: r.u32(), mmfar: r.u32(),
    bfar: r.u32(), uptimeMs: r.u32(), resetCsr: r.u32(), bootCount: r.u32(),
  });
  return f;
}

/** tlm_status_t (also inside the MSG_STATUS reply after the status byte). */
export function decodeStatus(p, offset = 0) {
  const r = new Reader(p, offset);
  const s = {
    timeMs: r.u32(), flags: r.u32(), armBlock: r.u32(),
    mode: r.u8(), fsPhase: r.u8(), batState: r.u8(), cpuLoad: r.u8(),
    roll: r.i16() / 100, pitch: r.i16() / 100, yaw: r.i16() / 100,
    vbat: r.u16() / 1000, vbatSlow: r.u16() / 1000, batComp: r.u16() / 1000,
    loopUsAvg: r.u16(), loopUsMax: r.u16(), loopHz: r.u16(),
    mcuTemp: r.i8(), imuTemp: r.i8(), rcCount: r.u8(), rcRate: r.u8(),
  };
  s.rc = [];
  for (let i = 0; i < 8; i++) s.rc.push(r.u16());
  s.motor = [];
  for (let i = 0; i < 4; i++) s.motor.push(r.u16() / 1000);
  s.tofMm = r.u16();
  s.tofStatus = r.u8();
  s.tofRate = r.u8();
  s.alt = r.i16() / 100;
  s.vz = r.i16() / 100;
  s.altSp = r.i16() / 100;
  s.throttle = r.u16() / 1000;
  s.hover = r.u16() / 1000;
  s.imuErrors = r.u16();
  s.tofErrors = r.u16();
  s.rxErrors = r.u16();
  s.linkDrops = r.u16();
  s.armed = (s.flags & C.STF_ARMED) !== 0;
  return s;
}

export function decodeRate(p) {
  const r = new Reader(p);
  const v3 = (scale) => [r.i16() / scale, r.i16() / scale, r.i16() / scale];
  return {
    timeUs: r.u32(), sp: v3(10), gyro: v3(10), p: v3(10000), i: v3(10000), d: v3(10000), f: v3(10000),
    throttle: r.u16() / 1000, motor: [r.u16() / 1000, r.u16() / 1000, r.u16() / 1000, r.u16() / 1000],
  };
}

export function decodeImu(p) {
  const r = new Reader(p);
  const v3 = (scale) => [r.i16() / scale, r.i16() / scale, r.i16() / scale];
  return { timeUs: r.u32(), gyroRaw: v3(10), gyroFilt: v3(10), acc: v3(1000) };
}

export function decodeAlt(p) {
  const r = new Reader(p);
  const a = {
    timeUs: r.u32(), h: r.f32(), vz: r.f32(), hSp: r.f32(), vzSp: r.f32(), hTof: r.f32(),
    accUp: r.f32(), thrust: r.f32(), hover: r.f32(), tofStatus: r.u8(), flags: r.u8(),
  };
  a.measUsed = (a.flags & 1) !== 0;
  a.altActive = (a.flags & 2) !== 0;
  a.landed = (a.flags & 4) !== 0;
  a.phase = (a.flags >> 4) & 0x0F;
  return a;
}

export function decodeLog(p) {
  return { level: p[0], text: new TextDecoder().decode(p.subarray(1)) };
}
