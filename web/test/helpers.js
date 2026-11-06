/** Test helpers: the native device emulator as a transport (child process). */
import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
export const EMU = process.env.MQ_EMU || resolve(ROOT, 'build/host-tests/tests/miniquad-emu');
export const WASM = resolve(ROOT, 'web/demo/miniquad-emu.wasm');
export const FW_OUT = resolve(ROOT, process.env.MQ_FW_OUT || 'build/release/out');

export const haveEmu = () => existsSync(EMU);
export const read = (p) => readFileSync(resolve(ROOT, p));

/**
 * Transport over the emulator's stdin/stdout; fd 3 carries control
 * commands (virtual transmitter, power, BOOT0 ...), stderr the answers.
 */
export class ProcessTransport {
  constructor(args = []) {
    this.args = args;
    this.child = null;
    this.onData = () => {};
    this.onClose = () => {};
    this.stderr = '';
    this.lineWaiters = [];
    this.kind = 'process';
  }

  get isOpen() { return this.child !== null; }

  async open() {
    if (this.child) return;
    this.child = spawn(EMU, this.args, { stdio: ['pipe', 'pipe', 'pipe', 'pipe'] });
    this.child.stdout.on('data', (b) => this.onData(new Uint8Array(b)));
    this.child.stderr.on('data', (b) => {
      this.stderr += b.toString();
      let i;
      while ((i = this.stderr.indexOf('\n')) >= 0) {
        const line = this.stderr.slice(0, i);
        this.stderr = this.stderr.slice(i + 1);
        for (const w of [...this.lineWaiters]) w(line);
      }
    });
    this.child.on('exit', () => { this.child = null; this.onClose('exit'); });
    await this.waitLine((l) => l.startsWith('MODE '), 2000);
  }

  async close() {
    if (!this.child) return;
    const c = this.child;
    this.ctrl('quit');
    c.stdin.end();
    await new Promise((r) => { c.once('exit', r); setTimeout(() => { c.kill(); r(); }, 1000); });
    this.child = null;
  }

  async write(bytes) {
    if (!this.child) throw new Error('closed');
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
export async function makeTestImage(version, size = 6000) {
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
  dv.setUint32(0x20, 1, true);
  img.set(new TextEncoder().encode('test'), 0x28);
  img.set(new TextEncoder().encode('MINIQUAD-F405'), 0x38);
  dv.setUint32(0x1FC, crc32(img.subarray(0, 0x1FC)), true);
  return img;
}
