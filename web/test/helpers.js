/** Test helpers: the native device emulator as a transport (child process). */
import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
export const EMU = process.env.MQ_EMU || resolve(ROOT, 'build/host-tests/tests/miniquad-emu');
export const EMU_V1 = process.env.MQ_EMU_V1 || resolve(ROOT, 'build/host-tests/tests/miniquad-emu-v1');
export const WASM = resolve(ROOT, 'web/demo/miniquad-emu.wasm');
export const FW_OUT = resolve(ROOT, process.env.MQ_FW_OUT || 'build/release/out');
export const FW_OUT_V1 = resolve(ROOT, process.env.MQ_FW_OUT_V1 || 'build/release-v1/out');

export const haveEmu = (path = EMU) => existsSync(path);
export const read = (p) => readFileSync(resolve(ROOT, p));

/**
 * Transport over the emulator's stdin/stdout; fd 3 carries control
 * commands (virtual transmitter, power, BOOT0, USB cable ...), stderr the
 * answers. "LINK 0/1" lines play the USB re-enumeration of board v2 the way
 * SerialTransport reports it (onLinkDown / onLinkUp, writes refused while
 * the port is gone).
 */
export class ProcessTransport {
  constructor(args = [], { exe = EMU } = {}) {
    this.args = args;
    this.exe = exe;
    this.child = null;
    this.onData = () => {};
    this.onClose = () => {};
    this.onLinkDown = () => {};
    this.onLinkUp = () => {};
    this.linkUp = false;
    this.reconnecting = false;
    this.boardRev = 0;
    this.lost = 0;
    this.stderr = '';
    this.lineWaiters = [];
    this.lastData = 0;
    this.downTimer = null;
    this.opened = false;
    this.kind = 'process';
  }

  get isOpen() { return this.child !== null && this.linkUp; }

  async open() {
    if (this.child) return;
    this.opened = false;
    this.child = spawn(this.exe, this.args, { stdio: ['pipe', 'pipe', 'pipe', 'pipe'] });
    this.child.stdout.on('data', (b) => { this.lastData = Date.now(); this.onData(new Uint8Array(b)); });
    this.child.stderr.on('data', (b) => {
      this.stderr += b.toString();
      let i;
      while ((i = this.stderr.indexOf('\n')) >= 0) {
        const line = this.stderr.slice(0, i);
        this.stderr = this.stderr.slice(i + 1);
        this._line(line);
        for (const w of [...this.lineWaiters]) w(line);
      }
    });
    this.child.on('exit', () => { this.child = null; this.onClose('exit'); });
    await this.waitLine((l) => l.startsWith('LINK '), 2000);
    /* a serial port can only be opened once it exists */
    if (!this.linkUp) await this.waitLine((l) => l.startsWith('LINK 1'), 5000);
    this.opened = true;
  }

  _line(line) {
    if (line.startsWith('BOARD ')) this.boardRev = Number(line.slice(6));
    if (!line.startsWith('LINK ')) return;
    const up = line[5] === '1';
    if (!this.opened) {                      /* still in open(): just track it */
      this.linkUp = up;
      return;
    }
    if (!up) {
      if (this.linkUp && !this.downTimer) this._scheduleDown();
      return;
    }
    if (this.downTimer) this._fireDown();
    if (!this.linkUp) {
      this.linkUp = true;
      this.reconnecting = false;
      this.onLinkUp();
    }
  }

  /* bytes written before the reset may still be in the stdout pipe */
  _scheduleDown() {
    this.downTimer = setTimeout(() => {
      if (Date.now() - this.lastData < 20) this._scheduleDown();
      else this._fireDown();
    }, 20);
  }

  _fireDown() {
    clearTimeout(this.downTimer);
    this.downTimer = null;
    if (!this.linkUp) return;
    this.linkUp = false;
    this.reconnecting = true;
    this.lost++;
    this.onLinkDown('thiết bị USB đã ngắt');
  }

  async close() {
    clearTimeout(this.downTimer);
    this.downTimer = null;
    if (!this.child) return;
    const c = this.child;
    this.ctrl('quit');
    c.stdin.end();
    await new Promise((r) => { c.once('exit', r); setTimeout(() => { c.kill(); r(); }, 1000); });
    this.child = null;
  }

  async write(bytes) {
    if (!this.child) throw new Error('closed');
    if (!this.linkUp) throw new Error('Board đang kết nối lại');
    this.child.stdin.write(Buffer.from(bytes));
  }

  ctrl(cmd) { this.child?.stdio[3].write(cmd + '\n'); }

  waitLine(pred, timeout = 2000) {
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => { done(); reject(new Error('timeout waiting for emulator output')); }, timeout);
      const w = (line) => { if (pred(line)) { done(); resolve(line); } };
      const done = () => { clearTimeout(t); this.lineWaiters = this.lineWaiters.filter((x) => x !== w); };
      this.lineWaiters.push(w);
    });
  }

  async state() {
    const p = this.waitLine((l) => l.startsWith('STATE '));
    this.ctrl('state');
    return JSON.parse((await p).slice(6));
  }
}

/** Build a valid application image like tools/fwpack.py (for tests). */
export async function makeTestImage(version, size = 6000, boardId = 2) {
  const { crc32 } = await import('../js/proto/crc.js');
  const img = new Uint8Array(0x200 + size);
  const dv = new DataView(img.buffer);
  for (let i = 0x200 + 8; i < img.length; i++) img[i] = (i * 13 + 7) & 0xFF;
  dv.setUint32(0x200, 0x10004000, true);           // SP in CCM
  dv.setUint32(0x204, 0x0800C200 + 0x189, true);   // reset vector (thumb)
  dv.setUint32(0x00, 0x5041514D, true);
  dv.setUint32(0x04, 1, true);
  dv.setUint32(0x08, 0x200, true);
  dv.setUint32(0x0C, 0x200, true);
  dv.setUint32(0x10, size, true);
  dv.setUint32(0x14, crc32(img.subarray(0x200)), true);
  dv.setUint32(0x18, version, true);
  dv.setUint32(0x20, boardId, true);
  img.set(new TextEncoder().encode('test'), 0x28);
  img.set(new TextEncoder().encode(boardId === 2 ? 'MINIQUAD-F405V2' : 'MINIQUAD-F405'), 0x38);
  dv.setUint32(0x1FC, crc32(img.subarray(0, 0x1FC)), true);
  return img;
}
