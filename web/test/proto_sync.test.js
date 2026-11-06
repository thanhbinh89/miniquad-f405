/** proto.js / meta.js must match the C headers they mirror. */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { META } from '../js/params/meta.js';
import {
  BEEP_MASK, C, SIZES, TONES, decodeAlt, decodeFault, decodeHello, decodeImu, decodeParamDesc,
  decodeRate, decodeStatus,
} from '../js/proto/proto.js';
import { read } from './helpers.js';

function cDefines(text) {
  const out = {};
  for (const m of text.matchAll(/^#define\s+([A-Z0-9_]+)\s+([^\n/]+?)\s*(?:\/\*.*)?$/gm)) {
    const [, name, expr] = m;
    if (/^__attribute__|^"/.test(expr)) continue;
    const js = expr.replace(/\b(0x[0-9a-fA-F]+|\d+)[uUlL]+\b/g, '$1');
    if (!/^[\s\d()xa-fA-F<|+\-]+$/.test(js)) continue;
    out[name] = Function(`return (${js});`)();
  }
  return out;
}

test('every #define of proto.h is mirrored in proto.js', () => {
  const defs = cDefines(read('firmware/common/proto.h').toString());
  delete defs.PROTO_H;
  assert.ok(Object.keys(defs).length > 80);
  for (const [name, value] of Object.entries(defs)) {
    assert.equal(C[name], value, `${name} (proto.h ${value}, proto.js ${C[name]})`);
  }
  for (const name of Object.keys(C)) assert.ok(name in defs, `${name} is not in proto.h any more`);
});

test('structure sizes match the _Static_asserts', () => {
  const text = read('firmware/common/proto.h').toString();
  const sizes = {};
  for (const m of text.matchAll(/_Static_assert\(sizeof\((\w+)\) == (\d+)/g)) sizes[m[1]] = Number(m[2]);
  assert.deepEqual(sizes, SIZES);
  // decoders consume exactly sizeof(): N bytes decode, N-1 throw
  const cases = [
    [decodeHello, 'msg_hello_rsp_t'], [decodeFault, 'msg_fault_info_rsp_t'],
    [decodeParamDesc, 'msg_param_desc_rsp_t'], [decodeStatus, 'tlm_status_t'],
    [decodeRate, 'tlm_rate_t'], [decodeImu, 'tlm_imu_t'], [decodeAlt, 'tlm_alt_t'],
  ];
  for (const [fn, name] of cases) {
    fn(new Uint8Array(SIZES[name]));
    assert.throws(() => fn(new Uint8Array(SIZES[name] - 1)), RangeError, name);
  }
});

test('beep tones and mask bits match notify.h', () => {
  const text = read('firmware/app/ui/notify.h').toString();
  const body = /typedef enum \{([^}]+)\} notify_tone_t;/.exec(text)[1];
  const names = body.split(',').map((s) => s.trim().split(/\s|=/)[0]).filter(Boolean);
  assert.equal(names[0], 'TONE_NONE');
  for (const t of TONES) assert.equal(names[t.id], t.key);
  assert.equal(TONES.length, names.indexOf('TONE_COUNT') - 1);
  const masks = cDefines(text);
  const bits = Object.entries(masks).filter(([k]) => k.startsWith('NOTIFY_MASK_')).map(([, v]) => Math.log2(v));
  assert.deepEqual(bits.sort((a, b) => a - b), BEEP_MASK.map((m) => m.bit));
});

test('every parameter has Vietnamese metadata (and no stale entries)', () => {
  const text = read('firmware/app/config/params_list.h').toString();
  const names = [...text.matchAll(/^\s+X\((\w+),/gm)].map((m) => m[1]);
  assert.ok(names.length > 80);
  for (const n of names) assert.ok(META[n], `meta.js: missing ${n}`);
  for (const n of Object.keys(META)) assert.ok(names.includes(n), `meta.js: ${n} is not a parameter`);
});
