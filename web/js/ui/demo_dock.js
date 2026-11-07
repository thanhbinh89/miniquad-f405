/**
 * Demo mode: virtual transmitter (PPM) and the physical side of the
 * emulated board - power, reset, USB-C cable, BOOT0 jumper, battery,
 * status LED and buzzer. Keyboard: W/S ga, A/D yaw, mũi tên roll/pitch, Space ARM,
 * 1/2/3 công tắc chế độ. A gamepad works too (mode 2).
 */
import { fmtNum, h } from './dom.js';

const CH = { roll: 0, pitch: 1, thr: 2, yaw: 3, arm: 4, mode: 5, aux: 6 };

function stickPad(caption, { springX = true, springY = true, x = 0, y = 0 } = {}) {
  const knob = h('div', { class: 'knob' });
  const pad = h('div', { class: 'stick', role: 'slider', tabindex: 0, 'aria-label': caption }, knob, h('span', { class: 'cap' }, caption));
  const st = { x, y, active: false, springX, springY };
  const place = () => {
    knob.style.left = `${50 + st.x * 42}%`;
    knob.style.top = `${50 - st.y * 42}%`;
  };
  const fromEvent = (e) => {
    const r = pad.getBoundingClientRect();
    st.x = Math.max(-1, Math.min(1, ((e.clientX - r.left) / r.width - 0.5) / 0.42));
    st.y = Math.max(-1, Math.min(1, -((e.clientY - r.top) / r.height - 0.5) / 0.42));
    place();
  };
  pad.addEventListener('pointerdown', (e) => { st.active = true; pad.setPointerCapture(e.pointerId); fromEvent(e); });
  pad.addEventListener('pointermove', (e) => { if (st.active) fromEvent(e); });
  const release = () => {
    st.active = false;
    if (st.springX) st.x = 0;
    if (st.springY) st.y = 0;
    place();
  };
  pad.addEventListener('pointerup', release);
  pad.addEventListener('pointercancel', release);
  place();
  return { el: pad, st, place };
}

export class DemoDock {
  constructor(app, transport) {
    this.app = app;
    this.tr = transport;
    this.keys = new Set();
    this.arm = false;
    this.mode = 0;            /* switch position 0 low, 1 mid, 2 high */
    this.aux = false;
    this.sound = false;
    this.audio = null;
  }

  mount() {
    const tr = this.tr;
    this.left = stickPad('Ga / Yaw', { springY: false, y: -1 });
    this.right = stickPad('Pitch / Roll');
    this.armBtn = h('button', { class: 'btn small', onclick: () => this.setArm(!this.arm) }, 'ARM: tắt');
    this.modeSeg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Công tắc chế độ (CH6)' },
      ['Thấp', 'Giữa', 'Cao'].map((l, i) => h('button', { 'aria-pressed': String(i === this.mode), onclick: () => this.setMode(i) }, l)));
    this.auxBtn = h('button', { class: 'btn small', onclick: () => { this.aux = !this.aux; this.auxBtn.textContent = `CH7: ${this.aux ? 'bật' : 'tắt'}`; } }, 'CH7: tắt');
    this.led = h('span', { class: 'vled', title: 'LED trạng thái (xanh)' });
    this.boot0 = h('input', { type: 'checkbox', onchange: () => tr.boot0(this.boot0.checked) });
    this.usbOn = h('input', { type: 'checkbox', checked: tr.usbCableIn, onchange: () => tr.usbCable(this.usbOn.checked) });
    this.txOn = h('input', { type: 'checkbox', checked: true, onchange: () => tr.rcEnable(this.txOn.checked) });
    this.tofOn = h('input', { type: 'checkbox', checked: true, onchange: () => tr.tofEnable(this.tofOn.checked) });
    this.soundCb = h('input', { type: 'checkbox', onchange: () => { this.sound = this.soundCb.checked; if (!this.sound) this.beep(0); } });

    this.kv = h('dl', { class: 'kv' });
    this.el = h('aside', { class: ['dock', window.innerWidth <= 1280 ? 'min' : ''], 'aria-label': 'Tay điều khiển ảo và board mô phỏng' },
      h('div', { class: 'dock-head' }, this.led, h('h3', 'Board mô phỏng'),
        this.minBtn = h('button', { class: 'btn small ghost min-btn', onclick: () => this.toggleMin() }, '')),
      h('div', { class: 'dock-body' },
        h('h4', 'Tay điều khiển ảo (PPM)'),
        h('div', { class: 'sticks' }, this.left.el, this.right.el),
        h('div', { class: 'switches' }, this.armBtn, this.auxBtn),
        h('div', { class: 'switches' }, h('span', { class: 'note' }, 'CH6 chế độ'), this.modeSeg),
        h('p', { class: 'note', style: { margin: 0 } }, 'Phím: W/S ga, A/D yaw, mũi tên roll/pitch, Space ARM, 1/2/3 chế độ. Tay cầm game cũng dùng được.'),
        h('h4', 'Quad thật (không phải telemetry)'),
        this.kv,
        h('h4', 'Board'),
        h('div', { class: 'board-ctl' },
          h('button', { class: 'btn small', onclick: () => tr.powerCycle(), title: 'Rút và cắm lại pin' }, 'Cắm lại pin'),
          h('button', { class: 'btn small', onclick: () => tr.resetPin(), title: 'Nút S2' }, 'Reset S2'),
          h('button', { class: 'btn small', onclick: () => tr.battery(1) }, 'Pin mới'),
          h('button', { class: 'btn small', onclick: () => tr.vehicleReset() }, 'Đặt về đất'),
          h('button', { class: 'btn small', onclick: () => tr.kick(250, -150, 0) }, 'Đẩy quad'),
          h('button', { class: 'btn small', onclick: () => tr.fault() }, 'Gây HardFault')),
        h('div', { class: 'board-ctl' },
          ...(tr.boardRev >= 2 ? [h('label', { class: 'check', title: 'Rút cáp: board vẫn bay (bằng pin), trang chờ cắm lại' }, this.usbOn, 'Cáp USB-C')] : []),
          h('label', { class: 'check' }, this.txOn, 'Bộ phát bật'),
          h('label', { class: 'check' }, this.tofOn, 'ToF hoạt động'),
          h('label', { class: 'check' }, this.boot0, 'Cầu JP1 (BOOT0)'),
          h('label', { class: 'check' }, this.soundCb, 'Tiếng còi'))));
    this.minBtn.textContent = this.el.classList.contains('min') ? 'Mở' : 'Thu gọn';
    document.querySelector('.app').append(this.el);
    document.body.classList.add('has-dock');

    this.onKey = (e) => {
      if (e.target.closest('input, select, textarea, button, [role=slider]')) return;
      const k = e.key;
      if (e.type === 'keydown') {
        if (k === ' ') { this.setArm(!this.arm); e.preventDefault(); return; }
        if (k === '1' || k === '2' || k === '3') { this.setMode(Number(k) - 1); return; }
        if (/^(w|s|a|d|ArrowUp|ArrowDown|ArrowLeft|ArrowRight)$/i.test(k)) { this.keys.add(k.toLowerCase()); e.preventDefault(); }
      } else {
        this.keys.delete(k.toLowerCase());
      }
    };
    window.addEventListener('keydown', this.onKey);
    window.addEventListener('keyup', this.onKey);
    this.last = performance.now();
    const loop = () => { this.tick(); this.raf = requestAnimationFrame(loop); };
    this.raf = requestAnimationFrame(loop);
  }

  unmount() {
    cancelAnimationFrame(this.raf);
    window.removeEventListener('keydown', this.onKey);
    window.removeEventListener('keyup', this.onKey);
    this.beep(0);
    this.el?.remove();
    document.body.classList.remove('has-dock');
  }

  toggleMin() {
    const min = this.el.classList.toggle('min');
    this.minBtn.textContent = min ? 'Mở' : 'Thu gọn';
  }

  setArm(on) {
    this.arm = on;
    this.armBtn.textContent = `ARM: ${on ? 'BẬT' : 'tắt'}`;
    this.armBtn.classList.toggle('danger', on);
  }

  setMode(i) {
    this.mode = i;
    this.modeSeg.querySelectorAll('button').forEach((b, k) => b.setAttribute('aria-pressed', String(k === i)));
  }

  beep(hz) {
    if (!this.sound || hz <= 0) {
      if (this.osc) { this.osc.stop(); this.osc = null; }
      return;
    }
    if (!this.audio) this.audio = new AudioContext();
    if (!this.osc) {
      this.osc = this.audio.createOscillator();
      this.osc.type = 'square';
      const g = this.audio.createGain();
      g.gain.value = 0.04;
      this.osc.connect(g).connect(this.audio.destination);
      this.osc.start();
    }
    this.osc.frequency.setValueAtTime(hz, this.audio.currentTime);
  }

  tick() {
    const now = performance.now();
    const dt = Math.min(0.1, (now - this.last) / 1000);
    this.last = now;
    const L = this.left.st;
    const R = this.right.st;
    const k = this.keys;
    if (!L.active) {
      if (k.has('w')) L.y = Math.min(1, L.y + dt * 1.2);
      if (k.has('s')) L.y = Math.max(-1, L.y - dt * 1.2);
      L.x = k.has('a') ? -0.7 : k.has('d') ? 0.7 : (L.springX ? 0 : L.x);
    }
    if (!R.active) {
      R.y = k.has('arrowup') ? 0.6 : k.has('arrowdown') ? -0.6 : 0;
      R.x = k.has('arrowleft') ? -0.6 : k.has('arrowright') ? 0.6 : 0;
    }
    /* gamepad, mode 2 */
    const gp = navigator.getGamepads?.()[0];
    if (gp && gp.axes.length >= 4) {
      const dz = (v) => (Math.abs(v) < 0.06 ? 0 : v);
      L.x = dz(gp.axes[0]);
      L.y = -dz(gp.axes[1]);
      R.x = dz(gp.axes[2]);
      R.y = -dz(gp.axes[3]);
      const a = gp.buttons[0]?.pressed;
      if (a && !this.gpA) this.setArm(!this.arm);
      this.gpA = a;
      const b = gp.buttons[1]?.pressed;
      if (b && !this.gpB) this.setMode((this.mode + 1) % 3);
      this.gpB = b;
    }
    this.left.place();
    this.right.place();

    const us = (v) => Math.round(1500 + 500 * v);
    const tr = this.tr;
    tr.rc(CH.roll, us(R.x));
    tr.rc(CH.pitch, us(R.y));
    tr.rc(CH.thr, us(L.y));
    tr.rc(CH.yaw, us(L.x));
    tr.rc(CH.arm, this.arm ? 2000 : 1000);
    tr.rc(CH.mode, [1000, 1500, 2000][this.mode]);
    tr.rc(CH.aux, this.aux ? 2000 : 1000);

    this.led.classList.toggle('on', tr.led);
    this.beep(tr.buzzerHz);
    const t = tr.truth();
    if (t && (!this.lastTruth || now - this.lastTruth > 150)) {
      this.lastTruth = now;
      const mode = { app: 'ứng dụng', bootloader: 'bootloader', rom: 'ROM bootloader', dead: 'không có bootloader' }[tr.mode] || tr.mode;
      this.kv.replaceChildren(
        h('dt', 'Độ cao'), h('dd', `${fmtNum(t.h, 2)} m`),
        h('dt', 'Roll / Pitch'), h('dd', `${fmtNum(t.roll, 1)}° / ${fmtNum(t.pitch, 1)}°`),
        h('dt', 'Pin'), h('dd', `${fmtNum(t.vbat, 2)} V, ${fmtNum(t.current, 1)} A, còn ${Math.round(t.soc * 100)} %`),
        h('dt', 'MCU đang chạy'), h('dd', { class: 'text' }, mode));
    }
  }
}
