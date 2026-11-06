/**
 * STM32 system-memory bootloader over USART (ST AN3155) - recovery path
 * when our bootloader is damaged, or for the very first flashing.
 *
 * Entering it: REBOOT(2) from the application/bootloader, or BOOT0 jumper
 * (JP1) closed + reset. The ROM loader talks 8E1 and auto-detects the baud
 * rate from the first 0x7F byte; we use 115200.
 */
import { MEM, SECTORS, eraseTimeMs, sectorsFor } from './firmware.js';
import { sleep } from '../util/emitter.js';

const ACK = 0x79;
const NACK = 0x1F;

export const ROM_BAUD = 115200;

export class RomError extends Error {}

export class Stm32Rom {
  /** @param device Device (proto/device.js) whose transport is already open at 8E1 */
  constructor(device) {
    this.device = device;
    this.buf = [];
    this.waiter = null;
    this.unlisten = null;
  }

  attach() {
    this.device.pauseHeartbeat(true);
    this.unlisten = this.device.addRawListener((bytes) => {
      for (const b of bytes) this.buf.push(b);
      if (this.waiter) this.waiter();
    });
  }

  detach() {
    this.unlisten?.();
    this.unlisten = null;
    this.device.pauseHeartbeat(false);
  }

  async _read(n, timeout) {
    const t0 = Date.now();
    while (this.buf.length < n) {
      const left = timeout - (Date.now() - t0);
      if (left <= 0) throw new RomError(`ROM bootloader không trả lời (${this.buf.length}/${n} byte)`);
      await new Promise((resolve) => {
        const t = setTimeout(() => { this.waiter = null; resolve(); }, left);
        this.waiter = () => { clearTimeout(t); this.waiter = null; resolve(); };
      });
    }
    return Uint8Array.from(this.buf.splice(0, n));
  }

  async _write(bytes) { await this.device.writeRaw(Uint8Array.from(bytes)); }

  async _ack(timeout = 1000, what = '') {
    const [b] = await this._read(1, timeout);
    if (b === ACK) return;
    if (b === NACK) throw new RomError(`ROM bootloader từ chối ${what} (NACK)`);
    throw new RomError(`Byte lạ 0x${b.toString(16)} khi chờ ACK ${what}`);
  }

  async _cmd(code, what) {
    await this._write([code, code ^ 0xFF]);
    await this._ack(1000, what);
  }

  async _addr(a) {
    const b = [(a >>> 24) & 0xFF, (a >>> 16) & 0xFF, (a >>> 8) & 0xFF, a & 0xFF];
    await this._write([...b, b[0] ^ b[1] ^ b[2] ^ b[3]]);
  }

  /** Auto-baud synchronisation. */
  async sync(tries = 8) {
    for (let i = 0; i < tries; i++) {
      this.buf = [];
      await this._write([0x7F]);
      try {
        const [b] = await this._read(1, 300);
        if (b === ACK || b === NACK) return true;   // NACK: already synchronised
      } catch (_) { /* retry */ }
      await sleep(50);
    }
    throw new RomError('Không đồng bộ được với ROM bootloader (BOOT0? đúng cổng?)');
  }

  async get() {
    await this._cmd(0x00, 'GET');
    const [n] = await this._read(1, 500);
    const data = await this._read(n + 1, 500);
    await this._ack(500, 'GET');
    return { version: data[0], commands: Array.from(data.subarray(1)) };
  }

  async getId() {
    await this._cmd(0x02, 'GET_ID');
    const [n] = await this._read(1, 500);
    const id = await this._read(n + 1, 500);
    await this._ack(500, 'GET_ID');
    return (id[0] << 8) | id[1];
  }

  async read(address, length) {
    if (length < 1 || length > 256) throw new RangeError('1..256 byte');
    await this._cmd(0x11, 'READ');
    await this._addr(address);
    await this._ack(500, 'địa chỉ đọc');
    await this._write([length - 1, (length - 1) ^ 0xFF]);
    await this._ack(500, 'độ dài đọc');
    return this._read(length, 2000);
  }

  async write(address, data) {
    if (data.length < 1 || data.length > 256) throw new RangeError('1..256 byte');
    await this._cmd(0x31, 'WRITE');
    await this._addr(address);
    await this._ack(500, 'địa chỉ ghi');
    const frame = new Uint8Array(data.length + 2);
    frame[0] = data.length - 1;
    frame.set(data, 1);
    let x = frame[0];
    for (const b of data) x ^= b;
    frame[data.length + 1] = x;
    await this._write(frame);
    await this._ack(1000, 'ghi');
  }

  /** Extended erase (0x44) of the given sector numbers. */
  async eraseSectors(list) {
    await this._cmd(0x44, 'ERASE');
    const n = list.length - 1;
    const bytes = [(n >> 8) & 0xFF, n & 0xFF];
    for (const s of list) bytes.push((s >> 8) & 0xFF, s & 0xFF);
    let x = 0;
    for (const b of bytes) x ^= b;
    await this._write([...bytes, x]);
    await this._ack(eraseTimeMs(list) + 3000, 'xoá');
  }

  async massErase() {
    await this._cmd(0x44, 'ERASE');
    await this._write([0xFF, 0xFF, 0x00]);
    await this._ack(eraseTimeMs(SECTORS.map((s) => s.index)) + 5000, 'xoá toàn bộ');
  }

  async go(address) {
    await this._cmd(0x21, 'GO');
    await this._addr(address);
    await this._ack(500, 'GO');
  }
}

/**
 * Flash raw images through the ROM loader.
 * @param images [{address, bytes, name}]
 */
export async function romFlash(rom, images, { eraseConfig = false, verify = true, log = () => {}, progress = () => {} } = {}) {
  const sectorSet = new Set();
  for (const im of images) for (const s of sectorsFor(im.address, im.bytes.length)) sectorSet.add(s);
  if (eraseConfig) { sectorSet.add(1); sectorSet.add(2); }
  const sectors = [...sectorSet].sort((a, b) => a - b);

  log(`Xoá sector ${sectors.join(', ')}…`);
  progress(0, 'erase');
  await rom.eraseSectors(sectors);

  const total = images.reduce((t, im) => t + im.bytes.length, 0) * (verify ? 2 : 1);
  let done = 0;
  for (const im of images) {
    log(`Ghi ${im.name} (${im.bytes.length} byte) tại 0x${im.address.toString(16).toUpperCase().padStart(8, "0")}…`);
    for (let off = 0; off < im.bytes.length; off += 256) {
      const n = Math.min(256, im.bytes.length - off);
      const block = new Uint8Array((n + 3) & ~3).fill(0xFF);
      block.set(im.bytes.subarray(off, off + n));
      await rom.write(im.address + off, block);
      done += n;
      progress(done / total, 'write');
    }
  }
  if (verify) {
    for (const im of images) {
      log(`Đọc lại kiểm tra ${im.name}…`);
      for (let off = 0; off < im.bytes.length; off += 256) {
        const n = Math.min(256, im.bytes.length - off);
        const got = await rom.read(im.address + off, n);
        for (let i = 0; i < n; i++) {
          if (got[i] !== im.bytes[off + i]) {
            throw new RomError(`Sai dữ liệu tại 0x${(im.address + off + i).toString(16)}`);
          }
        }
        done += n;
        progress(done / total, 'verify');
      }
    }
  }
  log('Chạy firmware (GO 0x08000000)…');
  await rom.go(MEM.FLASH_BASE);
}
