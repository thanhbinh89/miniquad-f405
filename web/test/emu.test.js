/**
 * The configurator's protocol stack against the native device emulator
 * (tests/emu: the real comms.c / bl_core.c / bl_policy.c / params / fc.c on
 * a simulated board). Requires `cmake --build --preset host-tests`, which
 * builds miniquad-emu (board v2, USB CDC) and miniquad-emu-v1 (UART J2).
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
import { EMU, EMU_V1, FW_OUT, FW_OUT_V1, ProcessTransport, haveEmu, makeTestImage } from './helpers.js';

/** The application image built for that board, or a synthetic one. */
async function appImage(outDir, boardId, version = 0x00090807) {
  const real = resolve(outDir, 'miniquad-app.bin');
  if (existsSync(real)) {
    const img = loadFirmwareFile('miniquad-app.bin', readFileSync(real)).app;
    if (img.ok && img.header.boardId === boardId) return img;
  }
  return parseAppImage(await makeTestImage(version, 40000, boardId));
}

async function until(pred, timeout, what) {
  const t0 = Date.now();
  while (!pred()) {
    if (Date.now() - t0 > timeout) throw new Error(`timeout: ${what}`);
    await sleep(20);
  }
}

describe('device emulator, board v2 (USB-C, CDC)', { skip: !haveEmu() && 'emulator not built' }, () => {
  let tr;
  let dev;
  const logs = [];
  const links = [];

  before(async () => {
    tr = new ProcessTransport();
    dev = new Device(tr);
    dev.on('log', (l) => logs.push(l.text));
    dev.on('linkdown', () => links.push('down'));
    dev.on('linkup', () => links.push('up'));
    await dev.open();
  });

  after(async () => { await dev.close(); });

  test('HELLO identifies the application on board v2', async () => {
    assert.equal(tr.boardRev, 2);
    const h = await dev.hello();
    assert.equal(h.mode, C.DEV_MODE_APP);
    assert.equal(h.protoVersion, C.PROTO_VERSION);
    assert.equal(h.boardName, 'MINIQUAD-F405V2');
    assert.equal(h.boardId, 2);
    assert.equal(h.bootReason, C.BLR_NONE);
    assert.ok(h.appValid);
    assert.equal(h.appMaxSize, MEM.APP_MAX_SIZE);
  });

  test('boot log is forwarded once a host talks', async () => {
    await sleep(100);
    assert.ok(logs.some((t) => t.includes('MINIQUAD-F405V2')), logs.join('|'));
  });

  test('status: USB power flag, motors refused without arming rules broken', async () => {
    const s = await dev.status();
    assert.ok(s.flags & C.STF_USB);
    assert.ok(s.flags & C.STF_BAT_PRESENT);
    assert.equal(s.armBlock & C.ARMB_NO_BAT, 0);
  });

  test('parameters survive a power cycle (USB port re-enumerates)', async () => {
    const pt = new ParamTable(dev);
    let last = 0;
    await pt.load((n) => { last = n; });
    assert.equal(last, pt.list.length);
    assert.ok(pt.list.length > 80);
    assert.equal(pt.get('pid_roll_p').value, 55);
    assert.equal(await pt.set('pid_roll_p', 64.5), 64.5);
    assert.equal(await pt.set('angle_max', 500), 80, 'clamped to max');
    assert.equal(pt.dirtyCount, 2);
    await pt.save();
    assert.equal(pt.dirtyCount, 0);

    const lost = tr.lost;
    tr.ctrl('power');
    await until(() => tr.lost > lost, 2000, 'link down after the power cycle');
    assert.ok(!dev.isOpen);
    await assert.rejects(dev.hello({ retries: 0 }), /kết nối/);
    const h = await waitForMode(dev, C.DEV_MODE_APP);
    assert.equal(h.mode, C.DEV_MODE_APP);
    assert.deepEqual(links.slice(-2), ['down', 'up']);
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

  test('telemetry streams at the requested rate (USB is not the bottleneck)', async () => {
    const r = await dev.stream(C.STREAM_STATUS, 20);
    assert.equal(r.rateHz, 20);
    const rr = await dev.stream(C.STREAM_RATE, 500);
    assert.equal(rr.rateHz, 500);
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
    assert.ok(rate >= 420 && rate <= 560, `rate frames: ${rate}`);
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
    await assert.rejects(dev.paramSave(), /ARM/);   // refused while armed
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

  test('USB cable pulled and plugged back: the transport finds the board again', async () => {
    const lost = tr.lost;
    tr.ctrl('usb 0');
    await until(() => tr.lost > lost, 1000, 'link down');
    await sleep(500);
    assert.ok(tr.reconnecting);
    tr.ctrl('usb 1');
    await until(() => !tr.reconnecting, 2000, 'link up');
    const h = await dev.hello();
    assert.equal(h.mode, C.DEV_MODE_APP);
    assert.ok((await tr.state()).uptime > 1000, 'the board kept running');
  });

  test('firmware update through the bootloader over USB', async () => {
    const image = await appImage(FW_OUT, 2);
    assert.ok(image.ok, image.errors.join());
    const logs2 = [];
    let prog = 0;
    const lost = tr.lost;
    const app = await updateFirmware(dev, image, { log: (t) => logs2.push(t), progress: (f) => { prog = f; } });
    assert.equal(prog, 1);
    assert.equal(app.mode, C.DEV_MODE_APP);
    assert.equal(versionString(app.appVersion), image.header.version);
    assert.ok(logs2.some((l) => l.includes('Kiểm tra OK')));
    assert.ok(tr.lost >= lost + 2, 'app -> bootloader -> app: two re-enumerations');
  });

  test('an image for board v1 is refused', async () => {
    const v1 = parseAppImage(await makeTestImage(0x00010000, 2000, 1));
    assert.ok(v1.ok);
    await assert.rejects(updateFirmware(dev, v1), /v1/);
    assert.equal((await dev.hello()).mode, C.DEV_MODE_APP);
  });

  test('a corrupt image is rejected before touching the flash', async () => {
    const img = await makeTestImage(0x00010000, 2000);
    img[0x300] ^= 0xFF;
    const parsed = parseAppImage(img);
    assert.equal(parsed.ok, false);
    await assert.rejects(updateFirmware(dev, parsed), /CRC/);
    assert.equal((await dev.hello()).mode, C.DEV_MODE_APP);
  });

  test('bootloader on request: HELLO says why, idle board goes back to the app', async () => {
    await dev.reboot(C.REBOOT_BOOTLOADER);
    const h = await waitForMode(dev, C.DEV_MODE_BOOTLOADER);
    assert.equal(h.bootReason, C.BLR_REQUEST);
    assert.equal(h.boardId, 2);
    await dev.reboot(C.REBOOT_NORMAL);
    assert.equal((await waitForMode(dev, C.DEV_MODE_APP)).mode, C.DEV_MODE_APP);
  });

  test('crash loop: the bootloader keeps the board and tells why', async () => {
    for (let i = 0; i < 3; i++) {
      await waitForMode(dev, C.DEV_MODE_APP);
      tr.ctrl('fault');
      await sleep(400);
    }
    const h = await waitForMode(dev, C.DEV_MODE_BOOTLOADER);
    assert.equal(h.bootReason, C.BLR_CRASH_LOOP);
    tr.ctrl('reset');                                // the reset button: back to the app
    const a = await waitForMode(dev, C.DEV_MODE_APP);
    assert.equal(a.mode, C.DEV_MODE_APP);
    const f = await dev.faultInfo();
    assert.ok(f.valid);
    assert.equal(f.type, 1);
    assert.ok(f.pc >= 0x0800C200);
  });

  test('ROM bootloader: USB DFU, no serial port until the next normal start', async () => {
    await dev.reboot(C.REBOOT_ROM);
    await sleep(1500);
    assert.equal((await tr.state()).mode, 'rom');
    assert.ok(tr.reconnecting, 'no CDC port in ROM mode');
    tr.ctrl('power');
    assert.equal((await waitForMode(dev, C.DEV_MODE_APP)).mode, C.DEV_MODE_APP);
  });
});

describe('device emulator, board v1 (UART J2)', { skip: !haveEmu(EMU_V1) && 'v1 emulator not built' }, () => {
  let tr;
  let dev;

  before(async () => {
    tr = new ProcessTransport([], { exe: EMU_V1 });
    dev = new Device(tr);
    await dev.open();
  });

  after(async () => { await dev.close(); });

  test('HELLO identifies board v1', async () => {
    assert.equal(tr.boardRev, 1);
    const h = await dev.hello();
    assert.equal(h.mode, C.DEV_MODE_APP);
    assert.equal(h.boardName, 'MINIQUAD-F405');
    assert.equal(h.boardId, 1);
    const s = await dev.status();
    assert.equal(s.flags & C.STF_USB, 0);
  });

  test('firmware update through the UART bootloader', async () => {
    const image = await appImage(FW_OUT_V1, 1, 0x00010203);
    assert.ok(image.ok, image.errors.join());
    const lost = tr.lost;
    const app = await updateFirmware(dev, image);
    assert.equal(versionString(app.appVersion), image.header.version);
    assert.equal(tr.lost, lost, 'the UART port never goes away');
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
      const app = await makeTestImage(0x00030201, 9000, 1);
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

  test('BOOT0 jumper + power cycle', async () => {
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
    await sleep(400);
    const f = await dev.faultInfo();
    assert.ok(f.valid);
    assert.equal(f.type, 1);
  });
});
