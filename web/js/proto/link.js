/**
 * Link framing: COBS(id | seq | payload | crc16 LE) 0x00 - see proto.h and
 * firmware/common/util/link.c.
 */
import { cobsDecode, cobsEncode } from './cobs.js';
import { crc16 } from './crc.js';

export const LINK_MAX_PAYLOAD = 288;
const RAW_MAX = 2 + LINK_MAX_PAYLOAD + 2;
const FRAME_MAX = RAW_MAX + Math.ceil(RAW_MAX / 254) + 2;

/** Build one complete frame including the trailing 0x00. */
export function encodeFrame(id, seq, payload = new Uint8Array(0)) {
  if (payload.length > LINK_MAX_PAYLOAD) throw new RangeError(`payload too long (${payload.length})`);
  const raw = new Uint8Array(payload.length + 4);
  raw[0] = id & 0xFF;
  raw[1] = seq & 0xFF;
  raw.set(payload, 2);
  const c = crc16(raw.subarray(0, payload.length + 2));
  raw[payload.length + 2] = c & 0xFF;
  raw[payload.length + 3] = c >> 8;
  const enc = cobsEncode(raw);
  const out = new Uint8Array(enc.length + 1);
  out.set(enc, 0);
  out[enc.length] = 0;
  return out;
}

/**
 * Streaming decoder. push() bytes as they arrive; onPacket({id, seq, payload})
 * is called for every CRC-valid frame. Bytes that do not form frames
 * (e.g. a ROM bootloader answer) are counted in stats.
 */
export class FrameParser {
  constructor(onPacket) {
    this.onPacket = onPacket;
    this.buf = new Uint8Array(FRAME_MAX);
    this.n = 0;
    this.overflow = false;
    this.stats = { frames: 0, crc: 0, cobs: 0, overflow: 0 };
  }

  reset() {
    this.n = 0;
    this.overflow = false;
  }

  push(bytes) {
    for (let i = 0; i < bytes.length; i++) {
      const b = bytes[i];
      if (b !== 0) {
        if (this.n < this.buf.length) this.buf[this.n++] = b;
        else this.overflow = true;
        continue;
      }
      if (this.overflow) {
        this.stats.overflow++;
      } else if (this.n > 0) {
        this._frame(this.buf.subarray(0, this.n));
      }
      this.n = 0;
      this.overflow = false;
    }
  }

  _frame(enc) {
    const raw = cobsDecode(enc);
    if (raw === null || raw.length < 4) {
      this.stats.cobs++;
      return;
    }
    const len = raw.length - 4;
    const c = crc16(raw.subarray(0, len + 2));
    if ((c & 0xFF) !== raw[len + 2] || (c >> 8) !== raw[len + 3]) {
      this.stats.crc++;
      return;
    }
    this.stats.frames++;
    this.onPacket({ id: raw[0], seq: raw[1], payload: raw.slice(2, 2 + len) });
  }
}
