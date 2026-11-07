/**
 * Demo transport: the firmware itself (protocol, bootloader, parameters,
 * flight core) compiled to WebAssembly from tests/emu, running on a
 * simulated quad. Bytes go through the same link protocol as over USB.
 *
 * Besides the byte stream it exposes the "physical" side of the emulated
 * board: virtual transmitter, BOOT0 jumper, power, USB cable, battery,
 * ground truth.
 *
 * The emulator is built for board v2: like the real USB-C board its serial
 * port disappears on every MCU reset and comes back once the firmware has
 * enumerated again. The transport reports that like SerialTransport does
 * (onLinkDown / onLinkUp) and refuses writes while the port is gone.
 */
const MODES = ['dead', 'bootloader', 'app', 'rom'];

function wasiStubs(getMemory) {
  const dec = new TextDecoder();
  return {
    fd_write(fd, iovs, iovsLen, nwritten) {
      const mem = new DataView(getMemory().buffer);
      let total = 0;
      let text = '';
      for (let i = 0; i < iovsLen; i++) {
        const ptr = mem.getUint32(iovs + i * 8, true);
        const len = mem.getUint32(iovs + i * 8 + 4, true);
        text += dec.decode(new Uint8Array(getMemory().buffer, ptr, len));
        total += len;
      }
      if (text.trim()) console.debug('[emu]', text.trim());
      mem.setUint32(nwritten, total, true);
      return 0;
    },
    fd_close: () => 0,
    fd_seek: () => 70,          /* ESPIPE */
    proc_exit: () => 0,
  };
}

export class DemoTransport {
  constructor({ wasmUrl = 'demo/miniquad-emu.wasm', wasmBytes = null, autoTick = true } = {}) {
    this.wasmUrl = wasmUrl;
    this.wasmBytes = wasmBytes;
    this.autoTick = autoTick;
    this.e = null;
    this.open_ = false;
    this.onData = () => {};
    this.onClose = () => {};
    this.onLinkDown = () => {};
    this.onLinkUp = () => {};
    this.link = { up: true, epoch: 0 };
    this.reconnecting = false;
    this.timer = null;
    this.last = 0;
    this.kind = 'demo';
    this.speed = 1;
  }

  get isOpen() { return this.open_ && this.link.up; }
  describe() { return this.open_ && !this.link.up ? 'Mô phỏng: đang chờ USB…' : 'Mô phỏng'; }
  details() { return 'Firmware MiniQuad biên dịch sang WebAssembly, chạy trên quad ảo'; }

  async load() {
    if (this.e) return;
    let bytes = this.wasmBytes;
    if (!bytes) {
      const res = await fetch(this.wasmUrl);
      if (!res.ok) throw new Error(`Không tải được ${this.wasmUrl} (${res.status})`);
      bytes = await res.arrayBuffer();
    }
    let memory = null;
    const { instance } = await WebAssembly.instantiate(bytes, {
      wasi_snapshot_preview1: wasiStubs(() => memory),
    });
    this.e = instance.exports;
    memory = this.e.memory;
    if (this.e._initialize) this.e._initialize();
    this.e.init(1);                 /* board already running the application */
    this.e.run(600);                /* ...and enumerated on USB */
    this.e.tx();                    /* nobody listened before connecting */
    this.link = this._linkState();
    if (this.autoTick) this._startClock();
  }

  _startClock() {
    this.last = performance.now();
    this.timer = setInterval(() => this.tick(), 4);
  }

  /** Advance the emulation by real elapsed time (or @p ms when given). */
  tick(ms) {
    if (!this.e) return;
    const now = performance.now();
    let step = ms ?? Math.round((now - this.last) * this.speed);
    if (ms === undefined) this.last = now;
    if (step <= 0) return;
    while (step > 0) {
      const n = Math.min(step, 50);
      this.e.run(n);
      step -= n;
      this._drain();
      this._checkLink();
    }
  }

  _linkState() {
    if (!this.e.link_up) return { up: true, epoch: 0 };     /* older wasm build */
    return { up: this.e.link_up() === 1, epoch: this.e.link_epoch() };
  }

  /* USB re-enumeration, reported like Web Serial would */
  _checkLink() {
    const now = this._linkState();
    const was = this.link;
    this.link = now;
    if (!this.open_) return;
    if (was.up && (!now.up || now.epoch !== was.epoch)) {
      this.reconnecting = true;
      this.onLinkDown('thiết bị USB đã ngắt');
    }
    if (now.up && (!was.up || now.epoch !== was.epoch)) {
      this.reconnecting = false;
      this.onLinkUp();
    }
  }

  _drain() {
    for (;;) {
      const n = this.e.tx();
      if (n <= 0) break;
      if (this.open_) {
        const out = new Uint8Array(this.e.memory.buffer, this.e.out_buf(), n).slice();
        this.onData(out);
      }
    }
  }

  async open() {
    await this.load();
    this.open_ = true;
    this.reconnecting = false;
    /* like a real port: it can only be opened while the board is enumerated */
    for (let i = 0; i < 300 && !this.link.up; i++) {
      if (!this.autoTick) this.tick(10);
      else await new Promise((r) => setTimeout(r, 10));
    }
    if (!this.link.up) {
      this.open_ = false;
      throw new Error('Board mô phỏng chưa kết nối USB');
    }
  }

  async close() {
    this.open_ = false;
  }

  /** Stop the clock (page unload / tests). */
  dispose() {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
    this.open_ = false;
  }

  async write(bytes) {
    if (!this.open_) throw new Error('Demo chưa mở');
    if (!this.link.up) throw new Error('Board đang kết nối lại');
    const size = this.e.in_size();
    for (let o = 0; o < bytes.length; o += size) {
      const chunk = bytes.subarray(o, Math.min(bytes.length, o + size));
      new Uint8Array(this.e.memory.buffer, this.e.in_buf(), chunk.length).set(chunk);
      this.e.rx(chunk.length);
    }
  }

  /* ---------------- the emulated board ---------------- */
  get mode() { return this.e ? MODES[this.e.mode()] : 'off'; }
  rc(ch, us) { this.e?.rc_set(ch, us); }
  rcGet(ch) { return this.e ? this.e.rc_get(ch) : 0; }
  rcEnable(on) { this.e?.rc_enable(on ? 1 : 0); }
  tofEnable(on) { this.e?.tof_enable(on ? 1 : 0); }
  boot0(high) { this.e?.boot0(high ? 1 : 0); }
  powerCycle() { this.e?.power_cycle(); }
  resetPin() { this.e?.reset_pin(); }
  vehicleReset() { this.e?.vehicle_reset(); }
  battery(soc) { this.e?.battery_set(soc); }
  kick(r, p, y) { this.e?.kick(r, p, y); }
  fault() { this.e?.inject_fault(); }
  usbCable(plugged) { this.e?.usb_cable?.(plugged ? 1 : 0); }
  get usbCableIn() { return this.e?.usb_cable_in ? this.e.usb_cable_in() === 1 : true; }
  get boardRev() { return this.e?.board_rev ? this.e.board_rev() : 1; }
  get led() { return this.e ? this.e.led() === 1 : false; }
  get buzzerHz() { return this.e ? this.e.buzzer_hz() : 0; }

  truth() {
    if (!this.e) return null;
    const v = new Float32Array(this.e.memory.buffer, this.e.truth(), 14);
    return {
      x: v[0], y: v[1], h: v[2], roll: v[3], pitch: v[4], yaw: v[5], vbat: v[6], current: v[7],
      armed: v[8] > 0.5, motors: [v[9], v[10], v[11], v[12]], soc: v[13],
    };
  }
}
