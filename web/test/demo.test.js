/** Demo mode: the WebAssembly build of the emulator behind DemoTransport. */
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { test } from 'node:test';

import { Device } from '../js/proto/device.js';
import { C } from '../js/proto/proto.js';
import { ParamTable } from '../js/params/params.js';
import { DemoTransport } from '../js/transport/demo.js';
import { sleep } from '../js/util/emitter.js';
import { WASM } from './helpers.js';

test('demo transport runs the firmware in WebAssembly', { skip: !existsSync(WASM) && 'wasm not built' }, async () => {
  const tr = new DemoTransport({ wasmBytes: readFileSync(WASM) });
  const dev = new Device(tr);
  await dev.open();
  try {
    const h = await dev.hello();
    assert.equal(h.mode, C.DEV_MODE_APP);
    assert.equal(tr.mode, 'app');

    const pt = new ParamTable(dev);
    await pt.load();
    assert.ok(pt.list.length > 80);

    let n = 0;
    dev.on('status', () => { n++; });
    await dev.stream(C.STREAM_STATUS, 50);
    await sleep(500);
    assert.ok(n >= 20, `status frames ${n}`);

    // virtual transmitter: arm and climb
    await sleep(2500);
    tr.rc(4, 2000);
    await sleep(300);
    tr.rc(2, 1600);
    await sleep(1000);
    const t = tr.truth();
    assert.ok(t.armed);
    assert.ok(t.h > 0.3, `height ${t.h}`);
    tr.rc(2, 1000);
    tr.rc(4, 1000);
  } finally {
    await dev.close();
    tr.dispose();
  }
});
