import assert from 'node:assert/strict';
import { test } from 'node:test';

import { cobsDecode, cobsEncode } from '../js/proto/cobs.js';
import { crc16, crc32 } from '../js/proto/crc.js';
import { encodeFrame, FrameParser } from '../js/proto/link.js';

const hex = (u8) => Buffer.from(u8).toString('hex');
const ascii = (s) => new TextEncoder().encode(s);

test('CRC check values', () => {
  assert.equal(crc16(ascii('123456789')), 0x29B1);
  assert.equal(crc32(ascii('123456789')), 0xCBF43926);
  assert.equal(crc32(ascii('6789'), crc32(ascii('12345'))), 0xCBF43926);
});

test('reference frame identical to the C implementation', () => {
  // same vector as tests/unit/test_link.c
  const f = encodeFrame(0x23, 5, Uint8Array.from([0x00, 0x01, 0x02, 0x00]));
  assert.equal(hex(f), '0323050301020' + '3fd8700');
});

test('COBS edge cases round-trip', () => {
  const cases = [
    [], [0], [0, 0], [1], [1, 0, 1],
    Array.from({ length: 254 }, (_, i) => (i % 255) + 1),
    Array.from({ length: 255 }, (_, i) => (i % 255) + 1),
    Array.from({ length: 600 }, (_, i) => (i * 7) & 0xFF),
  ];
  for (const c of cases) {
    const enc = cobsEncode(Uint8Array.from(c));
    assert.ok(!enc.includes(0), 'no zero inside an encoded block');
    assert.deepEqual(Array.from(cobsDecode(enc)), c);
  }
  assert.equal(cobsDecode(Uint8Array.from([5, 1, 2])), null, 'truncated');
});

test('parser: split input, garbage, bad CRC', () => {
  const got = [];
  const p = new FrameParser((pkt) => got.push(pkt));
  const a = encodeFrame(0x81, 7, Uint8Array.from([0, 1, 2, 3]));
  const b = encodeFrame(0xC1, 0, new Uint8Array(84).fill(0x55));
  const bad = encodeFrame(0x81, 8, Uint8Array.from([9]));
  bad[2] ^= 0x10;
  const stream = Uint8Array.from([0x13, 0x37, 0x00, ...a, ...bad, ...b]);
  for (let i = 0; i < stream.length; i += 3) p.push(stream.subarray(i, i + 3));
  assert.equal(got.length, 2);
  assert.equal(got[0].id, 0x81);
  assert.equal(got[0].seq, 7);
  assert.deepEqual(Array.from(got[0].payload), [0, 1, 2, 3]);
  assert.equal(got[1].payload.length, 84);
  assert.equal(p.stats.crc + p.stats.cobs, 2);
});

test('maximum payload', () => {
  const pl = new Uint8Array(288).map((_, i) => i & 0xFF);
  const got = [];
  new FrameParser((x) => got.push(x)).push(encodeFrame(0x11, 1, pl));
  assert.deepEqual(got[0].payload, pl);
  assert.throws(() => encodeFrame(1, 1, new Uint8Array(289)));
});
