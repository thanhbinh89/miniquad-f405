/**
 * Tinh chỉnh: live rate-loop traces (setpoint vs gyro, PID terms), raw vs
 * filtered gyro with a spectrum, altitude hold; parameters edited in place
 * are applied immediately (PARAM_F_LIVE ones also while flying).
 */
import { C } from '../proto/proto.js';
import { Scope } from '../widgets/scope.js';
import { fmtNum, h, panel } from './dom.js';
import { paramInput, paramRow } from './param_input.js';

const AXES = [['roll', 'Roll', 'var(--ch1)', '#f5d547'], ['pitch', 'Pitch', 'var(--ch2)', '#46d2f0'], ['yaw', 'Yaw', 'var(--ch3)', '#f06bd6']];

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wr = Math.cos(ang);
    const wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1;
      let ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k;
        const b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci;
        const ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr; im[a] += ti;
        const ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

/** Magnitude spectrum (dB) of the last n samples, Hann window. */
function spectrum(samples, n) {
  const re = new Float64Array(n);
  const im = new Float64Array(n);
  const off = samples.length - n;
  let mean = 0;
  for (let i = 0; i < n; i++) mean += samples[off + i];
  mean /= n;
  for (let i = 0; i < n; i++) re[i] = (samples[off + i] - mean) * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1)));
  fft(re, im);
  const out = new Float32Array(n / 2);
  for (let i = 0; i < n / 2; i++) out[i] = 20 * Math.log10(Math.hypot(re[i], im[i]) / n + 1e-6);
  return out;
}

function scopeBox(series, opts, height = 170) {
  const canvas = h('canvas', { style: { height: `${height}px` } });
  const scope = new Scope(canvas, series, opts);
  const legend = h('div', { class: 'scope-legend' }, series.map((s) => {
    const cb = h('input', { type: 'checkbox', checked: true, onchange: () => scope.setVisible(s.key, cb.checked) });
    return h('label', cb, h('span', { class: ['sw', s.dash ? 'dash' : ''], style: { background: s.color, color: s.color } }), s.label);
  }));
  return { el: h('div', { class: 'scope' }, legend, canvas), scope };
}

export class TuningView {
  constructor(app) {
    this.app = app;
    this.title = 'Tinh chỉnh PID';
    this.sub = 'rate';
    this.rateHz = 200;
    this.window = 4;
    this.termAxis = 0;
    this.specAxis = 0;
  }

  get streams() {
    if (this.sub === 'rate') return { status: 5, rate: this.rateHz };
    if (this.sub === 'filters') return { status: 5, imu: 500 };
    return { status: 5, alt: 50 };
  }

  mount(root) {
    this.root = root;
    const seg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Chọn phần tinh chỉnh' },
      [['rate', 'Vòng tốc độ góc'], ['filters', 'Gyro & bộ lọc'], ['alt', 'Giữ độ cao']].map(([k, label]) =>
        h('button', { 'aria-pressed': String(this.sub === k), onclick: () => this.switchTo(k) }, label)));
    this.body = h('div');
    root.append(
      h('div', { class: 'view-head' }, h('h1', 'Tinh chỉnh'), seg, h('span', { class: 'spacer' }), this.app.saveButton()),
      this.body);
    this.renderSub();
    const loop = () => { this.draw(); this.raf = requestAnimationFrame(loop); };
    this.raf = requestAnimationFrame(loop);
  }

  unmount() {
    cancelAnimationFrame(this.raf);
    this.offs?.forEach((f) => f());
  }

  switchTo(k) {
    if (k === this.sub) return;
    this.sub = k;
    this.root.querySelectorAll('.seg button').forEach((b, i) => b.setAttribute('aria-pressed', String(['rate', 'filters', 'alt'][i] === k)));
    this.renderSub();
    this.app.applyStreams(this.streams);
  }

  renderSub() {
    this.offs?.forEach((f) => f());
    this.offs = [];
    this.scopes = [];
    this.body.replaceChildren();
    if (this.sub === 'rate') this.renderRate();
    else if (this.sub === 'filters') this.renderFilters();
    else this.renderAlt();
  }

  toolbar(extra = []) {
    const rate = h('select', { 'aria-label': 'Tần số lấy mẫu', onchange: () => { this.rateHz = Number(rate.value); this.app.applyStreams(this.streams); } },
      [50, 100, 200, 333, 500].map((v) => h('option', { value: v, selected: v === this.rateHz }, `${v} Hz`)));
    const win = h('select', { 'aria-label': 'Khung thời gian', onchange: () => { this.window = Number(win.value); this.scopes.forEach((s) => { s.window = this.window; }); } },
      [1, 2, 4, 8, 16].map((v) => h('option', { value: v, selected: v === this.window }, `${v} s`)));
    const pause = h('button', { class: 'btn small', onclick: () => {
      const p = !this.scopes[0]?.paused;
      this.scopes.forEach((s) => { s.paused = p; });
      pause.textContent = p ? 'Chạy tiếp' : 'Tạm dừng';
    } }, 'Tạm dừng');
    return h('div', { class: 'toolbar' },
      this.sub === 'rate' ? h('label', { class: 'field' }, h('span', 'Lấy mẫu'), rate) : null,
      h('label', { class: 'field' }, h('span', 'Khung hình'), win),
      h('div', { class: 'field' }, h('span', ' '), pause),
      ...extra);
  }

  /* ---------------- rate loop ---------------- */
  renderRate() {
    const app = this.app;
    const boxes = AXES.map(([k, label, , hex]) => {
      const b = scopeBox([
        { key: 'sp', label: 'Đặt', color: hex, dash: true, width: 1.2 },
        { key: 'gyro', label: 'Gyro', color: hex, width: 1.8 },
      ], { window: this.window, unit: '°/s', minSpan: 40 });
      this.scopes.push(b.scope);
      const val = h('span', { class: 'num' });
      return { k, el: h('div', h('div', { class: 'scope-title' }, h('h3', { style: { color: `var(--ch${AXES.findIndex((a) => a[0] === k) + 1})` } }, label), val), b.el), scope: b.scope, val };
    });
    const terms = scopeBox([
      { key: 'p', label: 'P', color: '#e9ece2' },
      { key: 'i', label: 'I', color: '#d9b25b' },
      { key: 'd', label: 'D', color: '#8ea6ff' },
      { key: 'f', label: 'F', color: '#7f988f' },
      { key: 'sum', label: 'Tổng', color: '#ff6f61', width: 1.2 },
    ], { window: this.window, unit: 'lực đẩy', minSpan: 0.05 }, 150);
    this.scopes.push(terms.scope);
    const axisSel = h('select', { onchange: () => { this.termAxis = Number(axisSel.value); terms.scope.clear(); } },
      AXES.map(([, l], i) => h('option', { value: i, selected: i === this.termAxis }, l)));

    this.offs.push(app.device.on('rate', (r) => {
      const t = r.timeUs / 1e6;
      boxes.forEach((b, i) => {
        b.scope.push(t, { sp: r.sp[i], gyro: r.gyro[i] });
      });
      const a = this.termAxis;
      terms.scope.push(t, { p: r.p[a], i: r.i[a], d: r.d[a], f: r.f[a], sum: r.p[a] + r.i[a] + r.d[a] + r.f[a] });
      this.lastRate = r;
    }));

    const pidRows = AXES.map(([k, label], i) => h('tr',
      h('td', { class: ['axis', k] }, label),
      ...['p', 'i', 'd', 'f'].map((t) => h('td', paramInput(app, `pid_${k}_${t}`, { showUnit: false }))),
      h('td', paramInput(app, `rate_${k}`, { showUnit: false })),
    ));
    const pidTable = h('table', { class: 'pid' },
      h('thead', h('tr', h('th', ''), h('th', 'P'), h('th', 'I'), h('th', 'D'), h('th', 'FF'), h('th', { title: 'Tốc độ góc khi cần ở tận cùng (ACRO)' }, '°/s max'))),
      h('tbody', pidRows));

    this.body.append(
      this.toolbar([h('p', { class: 'note', style: { margin: 0 } }, 'Đường đứt = tốc độ góc mong muốn, đường liền = gyro sau lọc. Ổn khi hai đường gần trùng, không dao động sau khi thả cần.')]),
      h('div', { class: 'grid cols-3' }, boxes.map((b) => b.el)),
      h('div', { class: 'grid cols-2', style: { marginTop: '28px' } },
        panel('PID', 'tốc độ góc (đổi trực tiếp)', pidTable,
          h('div', { style: { marginTop: '14px' } }, ['iterm_limit', 'pid_sum_limit', 'expo_rp', 'expo_yaw', 'angle_p', 'angle_max'].map((n) => paramRow(app, n)))),
        h('div', h('div', { class: 'scope-title' }, h('h3', 'Thành phần PID'), axisSel), terms.el,
          h('p', { class: 'note' }, 'Giá trị là phần lực đẩy chuẩn hoá mỗi thành phần đóng góp. D dao động nhanh = nhiễu, tăng lọc D-term hoặc giảm D.'))),
    );
    this.rateBoxes = boxes;
  }

  /* ---------------- gyro + filters ---------------- */
  renderFilters() {
    const app = this.app;
    const axisSel = h('select', { onchange: () => { this.specAxis = Number(axisSel.value); this.buf = { raw: [], filt: [] }; gyro.scope.clear(); } },
      AXES.map(([, l], i) => h('option', { value: i, selected: i === this.specAxis }, l)));
    const gyro = scopeBox([
      { key: 'raw', label: 'Gyro thô', color: 'rgba(233,236,226,0.45)', width: 1 },
      { key: 'filt', label: 'Gyro sau lọc', color: '#f5d547', width: 1.6 },
    ], { window: this.window, unit: '°/s', minSpan: 5, capacity: 8192 }, 200);
    this.scopes.push(gyro.scope);
    const acc = scopeBox([
      { key: 'x', label: 'X', color: '#f5d547' }, { key: 'y', label: 'Y', color: '#46d2f0' }, { key: 'z', label: 'Z', color: '#f06bd6' },
    ], { window: this.window, unit: 'g', minSpan: 0.2, capacity: 8192 }, 140);
    this.scopes.push(acc.scope);
    this.specCanvas = h('canvas', { style: { height: '220px', width: '100%', display: 'block' } });
    this.buf = { raw: [], filt: [] };

    this.offs.push(app.device.on('imu', (m) => {
      const t = m.timeUs / 1e6;
      const a = this.specAxis;
      gyro.scope.push(t, { raw: m.gyroRaw[a], filt: m.gyroFilt[a] });
      acc.scope.push(t, { x: m.acc[0], y: m.acc[1], z: m.acc[2] });
      this.buf.raw.push(m.gyroRaw[a]);
      this.buf.filt.push(m.gyroFilt[a]);
      if (this.buf.raw.length > 2048) { this.buf.raw.splice(0, 1024); this.buf.filt.splice(0, 1024); }
    }));

    this.body.append(
      this.toolbar([h('label', { class: 'field' }, h('span', 'Trục'), axisSel)]),
      h('div', { class: 'grid cols-2' },
        h('div', gyro.el, h('div', { style: { marginTop: '18px' } }, acc.el)),
        h('div',
          h('div', { class: 'scope-title' }, h('h3', 'Phổ rung gyro'), h('span', { class: 'num' }, '0–250 Hz')),
          this.specCanvas,
          h('p', { class: 'note' }, 'Lấy mẫu 500 Hz nên rung trên 250 Hz bị gập về dải thấp (aliasing). Bật quad (đã ARM, cầm chắc hoặc bay) để thấy đỉnh rung của motor; đặt notch hoặc hạ lọc thông thấp ngay dưới đỉnh đó.'))),
      h('div', { style: { marginTop: '28px' } }, panel('Bộ lọc', 'đổi trực tiếp',
        ['gyro_lpf_type', 'gyro_lpf_hz', 'notch1_hz', 'notch1_cut', 'notch2_hz', 'notch2_cut', 'dterm_lpf_hz', 'dterm_lpf2_hz', 'acc_lpf_hz'].map((n) => paramRow(app, n)))),
    );
  }

  drawSpectrum() {
    const c = this.specCanvas;
    if (!c) return;
    const n = 512;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth;
    const hh = c.clientHeight;
    if (!w) return;
    if (c.width !== Math.round(w * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(hh * dpr); }
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, hh);
    const left = 40;
    const bottom = 18;
    const pw = w - left - 8;
    const ph = hh - bottom - 6;
    const lo = -60;
    const hi = 20;
    g.strokeStyle = 'rgba(233,236,226,0.10)';
    g.fillStyle = '#7f988f';
    g.font = '500 12px "Barlow Semi Condensed", sans-serif';
    g.beginPath();
    for (let f = 0; f <= 250; f += 50) {
      const x = left + (f / 250) * pw + 0.5;
      g.moveTo(x, 6); g.lineTo(x, 6 + ph);
      g.textAlign = 'center'; g.textBaseline = 'top'; g.fillText(`${f}`, x, 8 + ph);
    }
    for (let d = lo; d <= hi; d += 20) {
      const y = 6 + ph - ((d - lo) / (hi - lo)) * ph + 0.5;
      g.moveTo(left, y); g.lineTo(left + pw, y);
      g.textAlign = 'right'; g.textBaseline = 'middle'; g.fillText(`${d}`, left - 6, y);
    }
    g.stroke();
    g.textAlign = 'left';
    g.textBaseline = 'top';
    g.fillText('dB', 2, 8 + ph);

    /* filter markers from the parameters */
    const pv = (k) => this.app.params?.get(k)?.value || 0;
    const marks = [[pv('gyro_lpf_hz'), 'LPF'], [pv('notch1_hz'), 'N1'], [pv('notch2_hz'), 'N2']].filter(([f]) => f > 0 && f <= 250);
    for (const [f, label] of marks) {
      const x = left + (f / 250) * pw;
      g.strokeStyle = 'rgba(217,178,91,0.6)';
      g.setLineDash([3, 3]);
      g.beginPath(); g.moveTo(x, 6); g.lineTo(x, 6 + ph); g.stroke();
      g.setLineDash([]);
      g.fillStyle = '#d9b25b';
      g.fillText(label, x + 3, 8);
    }
    if (this.buf.raw.length < n) {
      g.fillStyle = '#7f988f';
      g.textAlign = 'center';
      g.fillText('Đang gom dữ liệu…', left + pw / 2, 6 + ph / 2);
      return;
    }
    const plot = (spec, color, width) => {
      g.strokeStyle = color;
      g.lineWidth = width;
      g.beginPath();
      for (let i = 1; i < spec.length; i++) {
        const x = left + (i / spec.length) * pw;
        const y = 6 + ph - ((Math.max(lo, Math.min(hi, spec[i])) - lo) / (hi - lo)) * ph;
        if (i === 1) g.moveTo(x, y); else g.lineTo(x, y);
      }
      g.stroke();
    };
    plot(spectrum(this.buf.raw, n), 'rgba(233,236,226,0.55)', 1);
    plot(spectrum(this.buf.filt, n), '#f5d547', 1.5);
  }

  /* ---------------- altitude ---------------- */
  renderAlt() {
    const app = this.app;
    const hBox = scopeBox([
      { key: 'h', label: 'Ước lượng (KF)', color: '#46d2f0', width: 1.8 },
      { key: 'sp', label: 'Đặt', color: '#d9b25b', dash: true },
      { key: 'tof', label: 'ToF (bù nghiêng)', color: 'rgba(233,236,226,0.5)', width: 1 },
    ], { window: this.window * 2, unit: 'm', minSpan: 0.3 }, 200);
    const vBox = scopeBox([
      { key: 'vz', label: 'Vận tốc đứng', color: '#46d2f0', width: 1.6 },
      { key: 'vsp', label: 'Đặt', color: '#d9b25b', dash: true },
    ], { window: this.window * 2, unit: 'm/s', minSpan: 0.4 }, 150);
    const tBox = scopeBox([
      { key: 'thr', label: 'Ga tổng', color: '#f06bd6', width: 1.6 },
      { key: 'hover', label: 'Ga treo ước lượng', color: '#d9b25b', dash: true },
    ], { window: this.window * 2, unit: 'ga', minSpan: 0.1 }, 150);
    this.scopes.push(hBox.scope, vBox.scope, tBox.scope);
    const info = h('p', { class: 'note' });
    this.offs.push(app.device.on('alt', (a) => {
      const t = a.timeUs / 1e6;
      hBox.scope.push(t, { h: a.h, sp: a.altActive ? a.hSp : NaN, tof: a.tofStatus === 0 ? a.hTof : NaN });
      vBox.scope.push(t, { vz: a.vz, vsp: a.altActive ? a.vzSp : NaN });
      tBox.scope.push(t, { thr: a.thrust, hover: a.hover });
      info.textContent = `${a.altActive ? 'ALT HOLD đang bật' : 'ALT HOLD tắt'}, ${a.landed ? 'trên đất' : 'đang bay'}, gia tốc đứng ${fmtNum(a.accUp, 2)} m/s²`;
    }));
    this.body.append(
      this.toolbar([info]),
      h('div', { class: 'grid cols-2' },
        h('div', hBox.el, h('div', { style: { marginTop: '18px' } }, vBox.el), h('div', { style: { marginTop: '18px' } }, tBox.el)),
        panel('ALT', 'giữ độ cao bằng ToF',
          ['alt_p', 'alt_vel_p', 'alt_vel_i', 'alt_vz_max', 'alt_deadband', 'alt_hover', 'alt_max', 'kf_acc_noise', 'kf_tof_noise', 'kf_bias_noise', 'tof_delay_ms'].map((n) => paramRow(app, n)))),
    );
  }

  draw() {
    for (const s of this.scopes || []) s.draw();
    if (this.sub === 'filters') this.drawSpectrum();
    if (this.sub === 'rate' && this.lastRate) {
      this.rateBoxes.forEach((b, i) => {
        b.val.textContent = `${fmtNum(this.lastRate.gyro[i], 0)} / ${fmtNum(this.lastRate.sp[i], 0)} °/s`;
      });
    }
  }

  onStatus() {}
}

export const STREAM_IDS = { status: C.STREAM_STATUS, rate: C.STREAM_RATE, imu: C.STREAM_IMU, alt: C.STREAM_ALT };
