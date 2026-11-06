/**
 * The configurator's protocol stack against the native device emulator
 * (tests/emu: the real comms.c / bl_core.c / params / fc.c on a simulated
 * board). Requires `cmake --build --preset host-tests`.
 */
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { after, before, describe, test } from 'node:test';

import { Device } from '../js/proto/device.js';
import { MEM, loadFirmwareFile, parseAppImage, updateFirmware, waitForMode } from '../js/proto/firmware.js';
import { C, versionString } from '../js/proto/proto.js';
import { Stm32Rom, romFlash } from '../js/proto/stm32rom.js';
import { ParamTable } from '../js/params/params.js';
import { sleep } from '../js/util/emitter.js';
import { FW_OUT, ProcessTransport, haveEmu, makeTestImage } from './helpers.js';

describe('device emulator', { skip: !haveEmu() && 'emulator not built' }, () => {
  let tr;
  let dev;
  const logs = [];

  before(async () => {
    tr = new ProcessTransport();
    dev = new Device(tr);
    dev.on('log', (l) => logs.push(l.text));
    await dev.open();
  });

  after(async () => { await dev.close(); });

  test('HELLO identifies the application', async () => {
    const h = await dev.hello();
    assert.equal(h.mode, C.DEV_MODE_APP);
    assert.equal(h.protoVersion, C.PROTO_VERSION);
    assert.equal(h.boardName, 'MINIQUAD-F405');
    assert.ok(h.appValid);
    assert.equal(h.appMaxSize, MEM.APP_MAX_SIZE);
  });

  test('boot log is forwarded once a host talks', async () => {
    await sleep(100);
    assert.ok(logs.some((t) => t.includes('MiniQuad-F405')), logs.join('|'));
  });

  test('parameter table: load, set (clamped), save, survives a power cycle', async () => {
    const pt = new ParamTable(dev);
    let last = 0;
    await pt.load((n) => { last = n; });
    assert.equal(last, pt.list.length);
    assert.ok(pt.list.length > 80);
    const p = pt.get('pid_roll_p');
    assert.equal(p.value, 55);
    assert.equal(await pt.set('pid_roll_p', 64.5), 64.5);
    assert.equal(await pt.set('angle_max', 500), 80, 'clamped to max');
    assert.equal(pt.dirtyCount, 2);
    await pt.save();
    assert.equal(pt.dirtyCount, 0);

    tr.ctrl('power');
    await sleep(500);
    const h = await waitForMode(dev, C.DEV_MODE_APP);
    assert.equal(h.mode, C.DEV_MODE_APP);
    const pt2 = new ParamTable(dev);
    await pt2.load();
    assert.equal(pt2.get('pid_roll_p').value, 64.5);
    assert.equal(pt2.get('angle_max').value, 80);

    // export -> reset -> import
    const json = pt2.toJSON({ board: 'test' });
    await pt2.resetDefaults();
    assert.equal(pt2.get('pid_roll_p').value, 55);
    const res = await pt2.applyJSON(JSON.parse(JSON.stringify(json)));
    assert.ok(res.applied.includes('pid_roll_p'));
    assert.equal(res.failed.length, 0);
    assert.equal(pt2.get('pid_roll_p').value, 64.5);
    await pt2.resetDefaults();
    await pt2.save();
  });

  test('telemetry streams at the requested rate', async () => {
    const r = await dev.stream(C.STREAM_STATUS, 20);
    assert.equal(r.rateHz, 20);
    const rr = await dev.stream(C.STREAM_RATE, 333);
    assert.equal(rr.rateHz, 333);
    let status = 0;
    let rate = 0;
    let lastStatus = null;
    const a = dev.on('status', (s) => { status++; lastStatus = s; });
    const b = dev.on('rate', () => { rate++; });
    await sleep(1000);
    a();
    b();
    await dev.stream(C.STREAM_STATUS, 0);
    await dev.stream(C.STREAM_RATE, 0);
    assert.ok(status >= 17 && status <= 23, `status frames: ${status}`);
    assert.ok(rate >= 280 && rate <= 370, `rate frames: ${rate}`);
    assert.ok(lastStatus.vbat > 3.5 && lastStatus.vbat < 4.3);
    assert.ok(lastStatus.flags & C.STF_IMU_OK);
    assert.equal(lastStatus.loopHz, 1000);
  });

  test('arm with the virtual transmitter, fly, see it in the telemetry', async () => {
    await sleep(2500);                              // gyro calibration + boot grace
    let s = await dev.status();
    assert.equal(s.armed, false);
    assert.equal(s.armBlock & ~(C.ARMB_SWITCH | C.ARMB_BOOT_GRACE), 0, `blockers 0x${s.armBlock.toString(16)}`);
    tr.ctrl('rc 5 2000');
    await sleep(300);
    s = await dev.status();
    assert.ok(s.armed, `not armed, blockers 0x${s.armBlock.toString(16)}`);
    // refused while armed
    await assert.rejects(dev.paramSave(), /ARM/);
    tr.ctrl('rc 3 1600');
    await sleep(1200);
    const st = await tr.state();
    assert.ok(st.h > 0.3, `height ${st.h}`);
    s = await dev.status();
    assert.ok(s.motor.every((m) => m > 0.2));
    tr.ctrl('rc 3 1000');
    tr.ctrl('rc 5 1000');
    await sleep(2500);
    s = await dev.status();
    assert.equal(s.armed, false);
  });

  test('motor test needs the key and stops by itself', async () => {
    await dev.motorTest([100, 200, 300, 400]);
    await sleep(50);                                // status snapshot is published at 100 Hz
    let s = await dev.status();
    assert.ok(s.flags & C.STF_MOTOR_TEST);
    assert.deepEqual(s.motor.map((m) => Math.round(m * 1000)), [100, 200, 300, 400]);
    await sleep(700);
    s = await dev.status();
    assert.equal(s.flags & C.STF_MOTOR_TEST, 0);
  });

  test('firmware update through the bootloader', async () => {
    const real = resolve(FW_OUT, 'miniquad-app.bin');
    let image;
    if (existsSync(real)) {
      image = loadFirmwareFile('miniquad-app.bin', readFileSync(real)).app;
    } else {
      image = parseAppImage(await makeTestImage(0x00090807, 40000));
    }
    assert.ok(image.ok, image.errors.join());
    const logs = [];
    let prog = 0;
    const app = await updateFirmware(dev, image, { log: (t) => logs.push(t), progress: (f) => { prog = f; } });
    assert.equal(prog, 1);
    assert.equal(app.mode, C.DEV_MODE_APP);
    assert.equal(versionString(app.appVersion), image.header.version);
    assert.ok(logs.some((l) => l.includes('Kiểm tra OK')));
  });

  test('a corrupt image is rejected before touching the flash', async () => {
    const img = await makeTestImage(0x00010000, 2000);
    img[0x300] ^= 0xFF;
    const parsed = parseAppImage(img);
    assert.equal(parsed.ok, false);
    await assert.rejects(updateFirmware(dev, parsed), /CRC/);
    assert.equal((await dev.hello()).mode, C.DEV_MODE_APP);
  });

  test('ROM bootloader recovery (AN3155): flash bootloader + app, GO', async () => {
    await dev.reboot(C.REBOOT_ROM);
    await sleep(200);
    assert.equal((await tr.state()).mode, 'rom');
    const rom = new Stm32Rom(dev);
    rom.attach();
    try {
      await rom.sync();
      assert.equal(await rom.getId(), 0x413);
      const g = await rom.get();
      assert.ok(g.commands.includes(0x44));
      const bl = new Uint8Array(5000).map((_, i) => (i * 31 + 3) & 0xFF);
      bl.set([0xC0, 0xFF, 0x01, 0x20, 0x89, 0x01, 0x00, 0x08]);   // SP / reset vector
      const app = await makeTestImage(0x00030201, 9000);
      let prog = 0;
      await romFlash(rom, [
        { address: MEM.BL_BASE, bytes: bl, name: 'bootloader' },
        { address: MEM.APP_BASE, bytes: app, name: 'app' },
      ], { eraseConfig: true, progress: (f) => { prog = f; } });
      assert.equal(prog, 1);
    } finally {
      rom.detach();
    }
    await sleep(500);
    const h = await waitForMode(dev, C.DEV_MODE_APP);
    assert.equal(versionString(h.appVersion), '3.2.1');
  });

  test('BOOT0 jumper + a dead bootloader sector', async () => {
    tr.ctrl('boot0 1');
    tr.ctrl('power');
    await sleep(100);
    assert.equal((await tr.state()).mode, 'rom');
    tr.ctrl('boot0 0');
    tr.ctrl('power');
    await sleep(400);
    assert.equal((await tr.state()).mode, 'app');
  });

  test('fault record is reported after the reset', async () => {
    tr.ctrl('fault');
    await sleep(300);
    const f = await dev.faultInfo();
    assert.ok(f.valid);
    assert.equal(f.type, 1);
    assert.ok(f.pc >= 0x0800C200);
  });
});
