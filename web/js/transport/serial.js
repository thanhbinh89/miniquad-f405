/**
 * Web Serial transport (Chrome / Edge / Opera, HTTPS or localhost).
 *
 * Board v2 talks USB CDC from the STM32 itself, so its serial port vanishes
 * on every MCU reset (reboot into the bootloader, firmware update, fault)
 * and when the cable is pulled, then comes back a moment later. When that
 * happens the transport reports onLinkDown(), keeps looking for a port with
 * the same USB ids among the ports this page may use (the permission
 * survives the re-enumeration: bootloader and application share VID, PID
 * and serial number), re-opens it with the same settings and reports
 * onLinkUp(). Only close() ends it for good.
 *
 * Board v1 goes through a USB-UART adapter whose port stays put; the same
 * logic simply covers an unplugged adapter. The port can also be re-opened
 * with other settings (v1 ROM bootloader recovery: 115200 8E1).
 */
export const serialSupported = () => typeof navigator !== 'undefined' && 'serial' in navigator;

/** MiniQuad-F405 v2 USB ids (ST "Virtual COM Port"). */
export const MQ_USB = { usbVendorId: 0x0483, usbProductId: 0x5740 };

const RECONNECT_POLL_MS = 250;
/* errors after which the port keeps working with a new reader */
const NONFATAL = new Set(['BreakError', 'FramingError', 'ParityError', 'BufferOverrunError']);
const hex4 = (v) => (v === undefined ? '????' : v.toString(16).padStart(4, '0'));

export class SerialTransport {
  constructor(port = null) {
    this.port = port;
    this.info = port?.getInfo?.() || {};
    this.reader = null;
    this.writer = null;
    this.onData = () => {};
    this.onClose = () => {};
    this.onLinkDown = () => {};
    this.onLinkUp = () => {};
    this.settings = null;
    this.reconnecting = false;
    this.lost = 0;
    this._closing = false;
    this._timer = null;
    this._onConnect = null;
    this._trying = false;
    this.kind = 'serial';
  }

  get isOpen() { return this.reader !== null; }
  get usb() { return this.info.usbVendorId === MQ_USB.usbVendorId && this.info.usbProductId === MQ_USB.usbProductId; }

  /**
   * Ask the user for a port (must run inside a user gesture).
   * @param board 2: only MiniQuad USB ports are listed; 1: any port (USB-UART adapter)
   */
  static async pick({ board = 2 } = {}) {
    const port = await navigator.serial.requestPort({ filters: board === 2 ? [MQ_USB] : [] });
    return new SerialTransport(port);
  }

  describe() {
    if (this.reconnecting) return 'Đang chờ board…';
    if (this.usb) return 'USB-C';
    return `USB ${hex4(this.info.usbVendorId)}:${hex4(this.info.usbProductId)}`;
  }

  details() {
    const ids = `VID:PID ${hex4(this.info.usbVendorId)}:${hex4(this.info.usbProductId)}`;
    return this.usb ? `${ids} – cổng USB CDC của MiniQuad-F405 v2` : `${ids}, ${this.settings?.baudRate ?? '?'} baud`;
  }

  async open({ baudRate = 460800, parity = 'none' } = {}) {
    if (this.isOpen) await this.close();
    this._stopReconnect();
    this.settings = { baudRate, parity };
    this._closing = false;
    await this._openPort(this.port);
  }

  async _openPort(port) {
    const s = this.settings;
    await port.open({ baudRate: s.baudRate, parity: s.parity, dataBits: 8, stopBits: 1, flowControl: 'none', bufferSize: 65536 });
    this.port = port;
    this.info = port.getInfo?.() || this.info;
    this.writer = port.writable.getWriter();
    this.reader = port.readable.getReader();
    this._readLoop(this.reader);
  }

  async _readLoop(reader) {
    let why = 'cổng đã đóng';
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.length) this.onData(value);
      }
    } catch (e) {
      if (!this._closing && reader === this.reader && NONFATAL.has(e.name) && this.port.readable) {
        try { reader.releaseLock(); } catch (_) { /* ignore */ }
        this.reader = this.port.readable.getReader();
        this._readLoop(this.reader);
        return;
      }
      why = e.message || 'lỗi đọc cổng';
    }
    if (this._closing || reader !== this.reader) return;
    await this._teardown();
    this._startReconnect(why);
  }

  async _teardown() {
    try { this.reader?.releaseLock(); } catch (_) { /* ignore */ }
    try { this.writer?.releaseLock(); } catch (_) { /* ignore */ }
    this.reader = null;
    this.writer = null;
    try { await this.port.close(); } catch (_) { /* device already gone */ }
  }

  /* ---------------- re-enumeration ---------------- */
  _startReconnect(why) {
    this.reconnecting = true;
    this.lost++;
    this.onLinkDown(why);
    if (!serialSupported()) return;
    this._onConnect = () => this._tryReconnect();
    navigator.serial.addEventListener?.('connect', this._onConnect);
    this._timer = setInterval(() => this._tryReconnect(), RECONNECT_POLL_MS);
    this._tryReconnect();
  }

  _stopReconnect() {
    this.reconnecting = false;
    if (this._timer) clearInterval(this._timer);
    this._timer = null;
    if (this._onConnect) navigator.serial?.removeEventListener?.('connect', this._onConnect);
    this._onConnect = null;
  }

  async _tryReconnect() {
    if (this._trying || !this.reconnecting) return;
    this._trying = true;
    try {
      const ports = await navigator.serial.getPorts();
      const sameIds = (p) => {
        const i = p.getInfo?.() || {};
        return i.usbVendorId === this.info.usbVendorId && i.usbProductId === this.info.usbProductId;
      };
      /* the old object first (Chrome may reuse it), then any port with the same ids */
      const candidates = [this.port, ...ports.filter((p) => p !== this.port && sameIds(p))];
      for (const p of candidates) {
        if (!this.reconnecting) return;
        try {
          await this._openPort(p);
        } catch (_) {
          continue;                         /* not back yet, or still being set up */
        }
        if (!this.reconnecting) {           /* closed by the user meanwhile */
          try { await this.reader?.cancel(); } catch (_) { /* ignore */ }
          await this._teardown();
          return;
        }
        this._stopReconnect();
        this.onLinkUp();
        return;
      }
    } catch (_) {
      /* getPorts() failed: try again on the next tick */
    } finally {
      this._trying = false;
    }
  }

  async close() {
    this._closing = true;
    this._stopReconnect();
    if (!this.reader) return;
    try { await this.reader.cancel(); } catch (_) { /* ignore */ }
    await this._teardown();
  }

  async write(bytes) {
    if (!this.writer) throw new Error(this.reconnecting ? 'Board đang kết nối lại' : 'Cổng chưa mở');
    await this.writer.write(bytes);
  }
}
