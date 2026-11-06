/**
 * Web Serial transport (Chrome / Edge / Opera, HTTPS or localhost).
 * The same port object can be re-opened with other settings, which the ROM
 * bootloader recovery needs (115200 8E1).
 */
export const serialSupported = () => typeof navigator !== 'undefined' && 'serial' in navigator;

export class SerialTransport {
  constructor(port = null) {
    this.port = port;
    this.reader = null;
    this.writer = null;
    this.onData = () => {};
    this.onClose = () => {};
    this.settings = null;
    this._closing = false;
    this.kind = 'serial';
  }

  get isOpen() { return this.reader !== null; }

  /** Ask the user for a port (must run inside a user gesture). */
  static async pick() {
    const port = await navigator.serial.requestPort({});
    return new SerialTransport(port);
  }

  describe() {
    const i = this.port?.getInfo?.() || {};
    const hex = (v) => (v === undefined ? '?' : v.toString(16).padStart(4, '0'));
    return `USB ${hex(i.usbVendorId)}:${hex(i.usbProductId)}`;
  }

  async open({ baudRate = 460800, parity = 'none' } = {}) {
    if (this.isOpen) await this.close();
    this.settings = { baudRate, parity };
    await this.port.open({ baudRate, parity, dataBits: 8, stopBits: 1, flowControl: 'none', bufferSize: 65536 });
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this._closing = false;
    this._readLoop();
  }

  async _readLoop() {
    const reader = this.reader;
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.length) this.onData(value);
      }
    } catch (e) {
      if (!this._closing) {
        await this._teardown();
        this.onClose(e.message || 'lỗi đọc cổng');
        return;
      }
    }
    if (!this._closing) {
      await this._teardown();
      this.onClose('cổng đã đóng');
    }
  }

  async _teardown() {
    try { this.reader?.releaseLock(); } catch (_) { /* ignore */ }
    try { this.writer?.releaseLock(); } catch (_) { /* ignore */ }
    this.reader = null;
    this.writer = null;
    try { await this.port.close(); } catch (_) { /* ignore */ }
  }

  async close() {
    if (!this.reader) return;
    this._closing = true;
    try { await this.reader.cancel(); } catch (_) { /* ignore */ }
    await this._teardown();
  }

  async write(bytes) {
    if (!this.writer) throw new Error('Cổng chưa mở');
    await this.writer.write(bytes);
  }
}
