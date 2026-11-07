/**
 * Protocol client: request/reply matching (seq), retries, timeouts,
 * telemetry dispatch and the host heartbeat.
 *
 * Works with any transport implementing:
 *   open(opts), close(), write(Uint8Array), onData = (bytes) => {},
 *   onClose = (reason) => {}, isOpen
 * and optionally onLinkDown = (why) => {} / onLinkUp = () => {} for links
 * that vanish and come back by themselves (USB CDC re-enumeration on board
 * v2: every reset of the MCU). Pending requests then fail at once with a
 * LinkLostError; 'linkdown' / 'linkup' events tell the application.
 * (transport/serial.js for Web Serial, transport/demo.js for the WASM
 * emulator, test/helpers.js for a child process in Node).
 */
import { encodeFrame, FrameParser } from './link.js';
import {
  C, STATUS_TEXT, Writer, decodeAlt, decodeBlRead, decodeBlVerify, decodeBlWrite, decodeFault,
  decodeHello, decodeImu, decodeLog, decodeParamDesc, decodeParamGet, decodeParamInfo,
  decodeParamSet, decodeRate, decodeStatus, decodeStream,
} from './proto.js';
import { Emitter } from '../util/emitter.js';

export class ProtoError extends Error {
  constructor(msg, status) {
    super(msg);
    this.status = status;
  }
}

export class TimeoutError extends Error {}

/** The serial port disappeared (USB re-enumeration, cable pulled). */
export class LinkLostError extends Error {}

const HEARTBEAT_MS = 1000;

export class Device extends Emitter {
  constructor(transport) {
    super();
    this.transport = transport;
    this.parser = new FrameParser((pkt) => this._onPacket(pkt));
    this.seq = 0;
    this.pending = new Map();      // seq -> {id, resolve, reject, timer}
    this.queue = [];               // requests waiting for a free slot
    this.inflight = 0;
    this.maxInflight = 4;
    this.lastTx = 0;
    this.hb = null;
    this.stats = { tx: 0, rx: 0, timeouts: 0, retries: 0, rxBytes: 0, txBytes: 0 };
    this.info = null;              // last HELLO
    this.rawListeners = new Set(); // ROM bootloader takes the byte stream
    transport.onData = (bytes) => this._onBytes(bytes);
    transport.onClose = (reason) => this._onClose(reason);
    transport.onLinkDown = (why) => this._onLinkDown(why);
    transport.onLinkUp = () => this._onLinkUp();
  }

  /** The link is gone for now; the transport is looking for the board. */
  get reconnecting() { return !!this.transport.reconnecting; }

  get isOpen() { return this.transport.isOpen; }

  async open(opts) {
    await this.transport.open(opts);
    this.parser.reset();
    this._startHeartbeat();
  }

  async close() {
    this._stopHeartbeat();
    this._failAll(new Error('Đã ngắt kết nối'));
    await this.transport.close();
  }

  /* ---------------- raw access (ROM bootloader) ---------------- */
  addRawListener(fn) { this.rawListeners.add(fn); return () => this.rawListeners.delete(fn); }
  async writeRaw(bytes) { this.stats.txBytes += bytes.length; await this.transport.write(bytes); }
  pauseHeartbeat(paused) { if (paused) this._stopHeartbeat(); else this._startHeartbeat(); }

  /* ---------------- requests ---------------- */
  /**
   * Send a request and resolve with the reply payload (status byte first).
   * Rejects with ProtoError when the status is not ST_OK (unless allowError),
   * or TimeoutError after the retries.
   */
  request(id, payload = new Uint8Array(0), { timeout = 400, retries = 2, allowError = false } = {}) {
    return new Promise((resolve, reject) => {
      const job = { id, payload, timeout, retriesLeft: retries, allowError, resolve, reject };
      this.queue.push(job);
      this._pump();
    });
  }

  _pump() {
    while (this.inflight < this.maxInflight && this.queue.length > 0) {
      this._send(this.queue.shift());
    }
  }

  _send(job) {
    if (!this.transport.isOpen) {
      job.reject(new Error('Chưa kết nối'));
      return;
    }
    this.seq = (this.seq % 255) + 1;
    const seq = this.seq;
    job.seq = seq;
    this.inflight++;
    this.pending.set(seq, job);
    job.timer = setTimeout(() => this._onTimeout(seq), job.timeout);
    const frame = encodeFrame(job.id, seq, job.payload);
    this.stats.tx++;
    this.stats.txBytes += frame.length;
    this.lastTx = performance.now();
    this.transport.write(frame).catch((e) => this._finish(seq, null, e));
  }

  _finish(seq, payload, err) {
    const job = this.pending.get(seq);
    if (!job) return;
    clearTimeout(job.timer);
    this.pending.delete(seq);
    this.inflight--;
    if (err) job.reject(err);
    else if (payload.length >= 1 && payload[0] !== C.ST_OK && !job.allowError) {
      job.reject(new ProtoError(`${STATUS_TEXT[payload[0]] || 'Lỗi'} (0x${job.id.toString(16)})`, payload[0]));
    } else job.resolve(payload);
    this._pump();
  }

  _onTimeout(seq) {
    const job = this.pending.get(seq);
    if (!job) return;
    this.pending.delete(seq);
    this.inflight--;
    if (job.retriesLeft > 0 && this.transport.isOpen) {
      job.retriesLeft--;
      this.stats.retries++;
      this.queue.unshift(job);
    } else {
      this.stats.timeouts++;
      job.reject(new TimeoutError(`Không có phản hồi (lệnh 0x${job.id.toString(16).padStart(2, '0')})`));
    }
    this._pump();
  }

  _failAll(err) {
    for (const [seq, job] of this.pending) {
      clearTimeout(job.timer);
      job.reject(err);
      this.pending.delete(seq);
    }
    for (const job of this.queue) job.reject(err);
    this.queue = [];
    this.inflight = 0;
  }

  /* ---------------- receive ---------------- */
  _onBytes(bytes) {
    this.stats.rxBytes += bytes.length;
    for (const fn of this.rawListeners) fn(bytes);
    if (this.rawListeners.size === 0) this.parser.push(bytes);
    this.emit('bytes', bytes);
  }

  _onPacket(pkt) {
    this.stats.rx++;
    if (pkt.id & C.MSG_REPLY_FLAG && pkt.id < C.MSG_LOG) {
      const job = this.pending.get(pkt.seq);
      if (job && job.id === (pkt.id & 0x7F)) this._finish(pkt.seq, pkt.payload, null);
      return;
    }
    try {
      switch (pkt.id) {
        case C.MSG_LOG: this.emit('log', decodeLog(pkt.payload)); break;
        case C.MSG_TLM_STATUS: this.emit('status', decodeStatus(pkt.payload)); break;
        case C.MSG_TLM_RATE: this.emit('rate', decodeRate(pkt.payload)); break;
        case C.MSG_TLM_IMU: this.emit('imu', decodeImu(pkt.payload)); break;
        case C.MSG_TLM_ALT: this.emit('alt', decodeAlt(pkt.payload)); break;
        default: this.emit('unknown', pkt);
      }
    } catch (e) {
      this.emit('error', e);
    }
  }

  _onClose(reason) {
    this._stopHeartbeat();
    this._failAll(new Error('Cổng serial đã đóng'));
    this.info = null;
    this.emit('close', reason);
  }

  _onLinkDown(why) {
    this._failAll(new LinkLostError(`Mất kết nối với board (${why})`));
    this.parser.reset();
    this.emit('linkdown', why);
  }

  _onLinkUp() {
    this.parser.reset();
    this.emit('linkup');
  }

  /* ---------------- heartbeat ---------------- */
  _startHeartbeat() {
    this._stopHeartbeat();
    this.hb = setInterval(() => {
      if (!this.transport.isOpen || this.rawListeners.size > 0) return;
      if (performance.now() - this.lastTx < HEARTBEAT_MS) return;
      if (this.info && this.info.mode === C.DEV_MODE_APP) {
        this.status().then((s) => this.emit('status', s)).catch(() => {});
      } else if (this.info) {
        this.hello().catch(() => {});
      }
    }, 250);
  }

  _stopHeartbeat() {
    if (this.hb) clearInterval(this.hb);
    this.hb = null;
  }

  /* ---------------- API ---------------- */
  async hello(opts = {}) {
    const p = await this.request(C.MSG_HELLO, undefined, { timeout: 300, retries: 2, ...opts });
    this.info = decodeHello(p);
    this.emit('hello', this.info);
    return this.info;
  }

  async reboot(target = C.REBOOT_NORMAL) {
    try {
      await this.request(C.MSG_REBOOT, new Uint8Array([target]), { retries: 0 });
    } catch (e) {
      if (!(e instanceof LinkLostError)) throw e;     /* USB left before the reply: it rebooted */
    }
    this.info = null;
  }

  async paramInfo() { return decodeParamInfo(await this.request(C.MSG_PARAM_INFO)); }

  async paramDesc(index) {
    return decodeParamDesc(await this.request(C.MSG_PARAM_DESC, new Writer(2).u16(index).done()));
  }

  async paramGet(first, count) {
    return decodeParamGet(await this.request(C.MSG_PARAM_GET, new Writer(3).u16(first).u8(count).done()));
  }

  async paramSet(index, raw) {
    return decodeParamSet(await this.request(C.MSG_PARAM_SET, new Writer(6).u16(index).u32(raw).done()));
  }

  async paramSave() { await this.request(C.MSG_PARAM_SAVE, undefined, { timeout: 2500, retries: 0 }); }
  async paramReset() { await this.request(C.MSG_PARAM_RESET, undefined, { retries: 0 }); }

  async stream(stream, rateHz) {
    return decodeStream(await this.request(C.MSG_STREAM, new Writer(3).u8(stream).u16(rateHz).done()));
  }

  async motorTest(duty) {
    const w = new Writer(10).u8(C.MOTOR_TEST_KEY).u8(0);
    for (let i = 0; i < 4; i++) w.u16(Math.max(0, Math.min(1000, Math.round(duty[i] || 0))));
    await this.request(C.MSG_MOTOR_TEST, w.done(), { timeout: 300, retries: 1 });
  }

  async calibrate(what) { await this.request(C.MSG_CALIBRATE, new Uint8Array([what]), { retries: 0 }); }
  async beep(tone) { await this.request(C.MSG_BEEP, new Uint8Array([tone])); }
  async faultInfo() { return decodeFault(await this.request(C.MSG_FAULT_INFO)); }
  async status() { return decodeStatus(await this.request(C.MSG_STATUS), 1); }

  /* bootloader */
  async blErase(size, timeout) {
    await this.request(C.MSG_BL_ERASE, new Writer(4).u32(size).done(), { timeout, retries: 0 });
  }

  async blWrite(offset, data) {
    const w = new Writer(4 + data.length).u32(offset).bytes(data);
    return decodeBlWrite(await this.request(C.MSG_BL_WRITE, w.done(), { timeout: 500, retries: 3 }));
  }

  async blRead(offset, length) {
    return decodeBlRead(await this.request(C.MSG_BL_READ, new Writer(6).u32(offset).u16(length).done()));
  }

  async blVerify() { return decodeBlVerify(await this.request(C.MSG_BL_VERIFY, undefined, { timeout: 2000, allowError: true })); }
  async blBoot() {
    try {
      await this.request(C.MSG_BL_BOOT, undefined, { retries: 0 });
    } catch (e) {
      if (!(e instanceof LinkLostError)) throw e;
    }
    this.info = null;
  }
}
