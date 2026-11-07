/**
 * Hiệu chuẩn & test: gyro / accelerometer calibration, mounting
 * orientation, motor test (propellers off), buzzer tones, receiver check.
 */
import { C, TONES } from '../proto/proto.js';
import { boardSVG } from './board.js';
import { channelRoles, meter } from './dashboard.js';
import { fmtNum, h, panel } from './dom.js';
import { paramInput, paramRow } from './param_input.js';

const MT_REFRESH_MS = 200;      /* firmware stops the test 500 ms after the last command */

export class SetupView {
  constructor(app) {
    this.app = app;
    this.title = 'Hiệu chuẩn & test';
    this.streams = { status: 20 };
    this.duty = [0, 0, 0, 0];
    this.mtActive = false;
  }

  mount(root) {
    const app = this.app;

    /* --- calibration --- */
    this.calInfo = h('dl', { class: 'kv' });
    const cal = panel('U4', 'hiệu chuẩn IMU',
      h('p', { class: 'note' }, 'Gyro: đặt quad đứng yên (bất kỳ hướng nào) khoảng 1 giây. Gia tốc kế: đặt quad trên mặt phẳng nằm ngang, mặt linh kiện lên trên. Offset gia tốc được lưu vào flash tự động.'),
      h('div', { class: 'btn-row', style: { margin: '10px 0 14px' } },
        h('button', { class: 'btn', onclick: () => this.calibrate(C.CAL_GYRO, 'gyro') }, 'Hiệu chuẩn gyro'),
        h('button', { class: 'btn primary', onclick: () => this.calibrate(C.CAL_ACCEL_LEVEL, 'mặt phẳng') }, 'Hiệu chuẩn mặt phẳng'),
        h('button', { class: 'btn', onclick: () => this.calibrate(C.CAL_ACCEL_RESET, 'xoá offset') }, 'Xoá offset gia tốc')),
      this.calInfo,
      h('div', { style: { marginTop: '12px' } }, ['trim_roll', 'trim_pitch'].map((n) => paramRow(app, n))));

    /* --- orientation --- */
    this.orientBox = h('div', { style: { maxWidth: '300px', margin: '0 auto 8px' } });
    const orient = panel('Hướng lắp', 'mũi quad và chiều quay cánh',
      this.orientBox,
      ['board_yaw', 'props_out'].map((n) => paramRow(app, n)),
      h('p', { class: 'note' }, 'Kiểm tra ở trang Tổng quan: chúc mũi xuống thì Pitch âm, nghiêng sang phải thì Roll dương. Mũi tên vàng là mũi quad.'));

    /* --- motor test --- */
    this.mtAck = h('input', { type: 'checkbox', onchange: () => this.updateMotorUi() });
    this.master = h('input', { type: 'range', min: 0, max: 1000, step: 10, value: 0, 'aria-label': 'Tất cả motor', oninput: () => this.setAll(Number(this.master.value)) });
    this.sliders = [0, 1, 2, 3].map((i) => {
      const r = h('input', { type: 'range', min: 0, max: 1000, step: 10, value: 0, 'aria-label': `Motor ${i + 1}`, oninput: () => { this.duty[i] = Number(r.value); this.kick(); } });
      const v = h('span', { class: 'num' }, '0 %');
      r.v = v;
      return r;
    });
    this.mtStop = h('button', { class: 'btn danger', onclick: () => this.stopMotors() }, 'Dừng tất cả');
    this.mtBoard = h('div', { style: { maxWidth: '300px' } });
    const motors = panel('M1–M4', 'test motor',
      h('div', { class: 'callout', style: { marginBottom: '14px' } }, 'Tháo cánh quạt trước khi test. Motor quay ngay khi kéo thanh trượt và tự dừng sau 0,5 s nếu mất kết nối.'),
      h('label', { class: 'check', style: { marginBottom: '14px' } }, this.mtAck, 'Tôi đã tháo hết cánh quạt'),
      h('div', { class: 'motors-layout' },
        this.mtBoard,
        h('div', { class: 'motor-sliders' },
          h('div', { class: 'row' }, h('span', 'Tất cả'), this.master, h('span')),
          this.sliders.map((r, i) => h('div', { class: 'row' }, h('span', `M${i + 1}`), r, r.v)),
          h('div', { class: 'btn-row' }, this.mtStop))));

    /* --- buzzer --- */
    const buzz = panel('LS1', 'còi',
      h('div', { class: 'btn-row' }, TONES.map((t) => h('button', { class: 'btn small', onclick: () => app.run(null, () => app.device.beep(t.id)) }, t.label))),
      h('div', { style: { marginTop: '12px' } }, paramRow(app, 'beep_hz'), paramRow(app, 'beep_mask')));

    /* --- receiver --- */
    this.rcMeters = [];
    const roles = channelRoles(app.params);
    for (let i = 0; i < 8; i++) {
      const r = roles[i];
      const m = meter(r ? `CH${i + 1} ${r[0]}` : `CH${i + 1}`, r?.[1] || '');
      this.rcMeters.push(m);
    }
    this.rcNote = h('p', { class: 'note' });
    const rx = panel('J3', 'bộ thu PPM',
      h('div', { class: 'grid cols-2', style: { alignItems: 'start' } },
        h('div', h('div', { class: 'meters' }, this.rcMeters), this.rcNote,
          h('div', { style: { maxWidth: '320px', marginTop: '12px' } }, boardSVG({ rev: this.app.boardRev, focus: 'ppm', scale: 0.8 }))),
        h('div', ['rc_map_roll', 'rc_map_pitch', 'rc_map_thr', 'rc_map_yaw', 'rc_ch_arm', 'rc_ch_mode', 'mode_low', 'mode_mid', 'mode_high', 'rc_ch_beeper', 'ppm_falling'].map((n) => paramRow(app, n)))));

    root.append(
      h('div', { class: 'view-head' }, h('h1', 'Hiệu chuẩn & test'), h('span', { class: 'spacer' }), app.saveButton()),
      h('div', { class: 'grid cols-2' }, cal, orient),
      h('div', { style: { marginTop: '28px' } }, motors),
      h('div', { class: 'grid cols-2', style: { marginTop: '28px' } }, rx, buzz),
    );
    this.offs = [app.params.on('changed', () => this.renderBoards())];
    this.renderBoards();
    this.updateMotorUi();
    if (app.status) this.onStatus(app.status);
  }

  unmount() {
    this.stopMotors(true);
    this.offs.forEach((f) => f());
  }

  renderBoards() {
    const pt = this.app.params;
    const yaw = pt.get('board_yaw')?.value ?? 0;
    const out = !!pt.get('props_out')?.value;
    this.orientBox.replaceChildren(boardSVG({ rev: this.app.boardRev, props: true, boardYaw: yaw, propsOut: out, scale: 0.9 }));
    this.mtBoard.replaceChildren(boardSVG({ rev: this.app.boardRev, props: true, boardYaw: yaw, propsOut: out, motorLabels: this.duty.map((d) => `${Math.round(d / 10)}%`), scale: 0.8 }));
  }

  async calibrate(what, label) {
    const app = this.app;
    await app.run(`Hiệu chuẩn ${label}…`, async () => {
      await app.device.calibrate(what);
      if (what === C.CAL_ACCEL_RESET) {
        await app.params.refresh();
        app.toast('Đã xoá offset gia tốc (chưa lưu vào flash)', 'ok');
      } else {
        app.toast(`Đang hiệu chuẩn ${label}, giữ quad yên…`);
        let failed = false;
        const off = app.device.on('log', (l) => { if (/calibration failed/i.test(l.text)) failed = true; });
        try {
          const t0 = Date.now();
          await new Promise((r) => setTimeout(r, 400));
          while (Date.now() - t0 < 15000) {
            const s = await app.device.status();
            if (!(s.flags & C.STF_CALIBRATING)) break;
            await new Promise((r) => setTimeout(r, 250));
          }
          await new Promise((r) => setTimeout(r, 300));     /* result + auto-save */
        } finally {
          off();
        }
        if (failed) throw new Error('Hiệu chuẩn thất bại: giữ quad đứng yên, nằm ngang rồi thử lại');
        await app.params.refresh();
        if (what === C.CAL_ACCEL_LEVEL) app.params.markSaved();
        app.toast(`Hiệu chuẩn ${label} xong`, 'ok');
      }
    });
  }

  /* ---------------- motor test ---------------- */
  updateMotorUi() {
    const ok = this.mtAck.checked && this.app.connected && !this.app.armed;
    for (const el of [this.master, ...this.sliders]) el.disabled = !ok;
    if (!ok && this.mtActive) this.stopMotors();
  }

  setAll(v) {
    this.duty = [v, v, v, v];
    this.sliders.forEach((r) => { r.value = v; });
    this.kick();
  }

  kick() {
    this.sliders.forEach((r, i) => { r.v.textContent = `${Math.round(this.duty[i] / 10)} %`; });
    const any = this.duty.some((d) => d > 0);
    if (any && !this.mtTimer) {
      this.mtActive = true;
      this.mtTimer = setInterval(() => this.send(), MT_REFRESH_MS);
    }
    this.send();
    this.renderBoards();
    if (!any) this.stopMotors();
  }

  send() {
    if (!this.mtActive || !this.app.connected) return;
    this.app.device.motorTest(this.duty).catch((e) => {
      const noBat = this.app.status && !(this.app.status.flags & C.STF_BAT_PRESENT);
      this.app.toast(noBat ? 'Test motor: chưa cắm pin (board đang chạy bằng nguồn USB, motor không có điện).' : `Test motor: ${e.message}`, 'err');
      this.stopMotors();
    });
  }

  stopMotors(silent = false) {
    clearInterval(this.mtTimer);
    this.mtTimer = null;
    const was = this.mtActive;
    this.mtActive = false;
    this.duty = [0, 0, 0, 0];
    this.sliders?.forEach((r) => { r.value = 0; r.v.textContent = '0 %'; });
    if (this.master) this.master.value = 0;
    if (was && this.app.connected) this.app.device.motorTest([0, 0, 0, 0]).catch(() => {});
    if (!silent) this.renderBoards();
  }

  onStatus(s) {
    const pt = this.app.params;
    const off = ['acc_off_x', 'acc_off_y', 'acc_off_z'].map((n) => pt.get(n)?.value ?? 0);
    this.calInfo.replaceChildren(
      h('dt', 'Gyro đã hiệu chuẩn'), h('dd', { class: 'text' }, s.flags & C.STF_GYRO_CAL ? 'Có' : 'Chưa'),
      h('dt', 'Đang hiệu chuẩn'), h('dd', { class: 'text' }, s.flags & C.STF_CALIBRATING ? 'Đang chạy…' : 'Không'),
      h('dt', 'Offset gia tốc X / Y / Z'), h('dd', off.map((v) => fmtNum(v, 3)).join(' / ') + ' g'),
      h('dt', 'Roll / Pitch hiện tại'), h('dd', `${fmtNum(s.roll, 1)}° / ${fmtNum(s.pitch, 1)}°`),
    );
    this.rcMeters.forEach((m, i) => {
      const us = s.rc[i];
      m.update(us ? (us - 900) / 1200 : 0, us ? String(us) : '–');
    });
    this.rcNote.textContent = s.flags & C.STF_RX_OK ? `${s.rcCount} kênh, ${s.rcRate} khung/s, lỗi xung ${s.rxErrors}` : 'Không có tín hiệu PPM trên J3.4';
    if (s.armed && this.mtActive) this.stopMotors();
    this.updateMotorUi();
  }
}

export { paramInput };
