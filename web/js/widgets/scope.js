/**
 * Oscilloscope-style strip chart on a canvas: fixed time window, one ring
 * buffer per trace, min/max decimation per pixel column so 500 Hz data
 * stays crisp, grid in divisions, auto or fixed vertical range.
 */
function cssVar(name, fallback) {
  try { return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback; } catch (_) { return fallback; }
}

class Ring {
  constructor(n) {
    this.t = new Float64Array(n);
    this.v = new Float32Array(n);
    this.n = n;
    this.head = 0;
    this.count = 0;
  }
  push(t, v) {
    this.t[this.head] = t;
    this.v[this.head] = v;
    this.head = (this.head + 1) % this.n;
    if (this.count < this.n) this.count++;
  }
  clear() { this.head = 0; this.count = 0; }
  *iter() {
    const start = (this.head - this.count + this.n) % this.n;
    for (let k = 0; k < this.count; k++) {
      const i = (start + k) % this.n;
      yield [this.t[i], this.v[i]];
    }
  }
}

export class Scope {
  /**
   * @param series [{key, label, color, dash, width}]
   * @param opts   {window: s, range: [min,max] | null (auto), unit, divisions, capacity}
   */
  constructor(canvas, series, { window = 4, range = null, unit = '', capacity = 4096, minSpan = 1 } = {}) {
    this.c = canvas;
    this.ctx = canvas.getContext('2d');
    this.series = series.map((s) => ({ visible: true, width: 1.4, ...s, ring: new Ring(capacity) }));
    this.window = window;
    this.range = range;
    this.unit = unit;
    this.minSpan = minSpan;
    this.paused = false;
    this.tNow = 0;
    this.autoLo = -1;
    this.autoHi = 1;
    this.font = '500 12px "Barlow Semi Condensed", sans-serif';
    this.grid = 'rgba(233,236,226,0.10)';
    this.axis = 'rgba(233,236,226,0.32)';
    this.text = cssVar('--silk-3', '#7f988f');
  }

  push(t, values) {
    if (this.paused) return;
    this.tNow = Math.max(this.tNow, t);
    for (const s of this.series) {
      const v = values[s.key];
      if (v !== undefined && Number.isFinite(v)) s.ring.push(t, v);
    }
  }

  clear() {
    for (const s of this.series) s.ring.clear();
    this.tNow = 0;
  }

  setVisible(key, on) {
    const s = this.series.find((x) => x.key === key);
    if (s) s.visible = on;
  }

  /** Latest value of a trace (for numeric readouts next to the chart). */
  last(key) {
    const s = this.series.find((x) => x.key === key);
    if (!s || s.ring.count === 0) return NaN;
    return s.ring.v[(s.ring.head - 1 + s.ring.n) % s.ring.n];
  }

  _size() {
    const dpr = window.devicePixelRatio || 1;
    const w = this.c.clientWidth;
    const h = this.c.clientHeight;
    if (this.c.width !== Math.round(w * dpr) || this.c.height !== Math.round(h * dpr)) {
      this.c.width = Math.round(w * dpr);
      this.c.height = Math.round(h * dpr);
    }
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return [w, h];
  }

  draw() {
    const [w, h] = this._size();
    if (!w || !h) return;
    const g = this.ctx;
    g.clearRect(0, 0, w, h);
    const left = 44;
    const right = 8;
    const top = 6;
    const bottom = 18;
    const pw = w - left - right;
    const ph = h - top - bottom;
    const t1 = this.tNow;
    const t0 = t1 - this.window;

    /* vertical range */
    let lo;
    let hi;
    if (this.range) {
      [lo, hi] = this.range;
    } else {
      lo = Infinity;
      hi = -Infinity;
      for (const s of this.series) {
        if (!s.visible) continue;
        for (const [t, v] of s.ring.iter()) {
          if (t < t0) continue;
          if (v < lo) lo = v;
          if (v > hi) hi = v;
        }
      }
      if (!Number.isFinite(lo)) { lo = -1; hi = 1; }
      const span = Math.max(hi - lo, this.minSpan);
      const mid = (hi + lo) / 2;
      lo = mid - span * 0.6;
      hi = mid + span * 0.6;
      /* smooth so the trace does not jump around */
      this.autoLo += (lo - this.autoLo) * (lo < this.autoLo ? 0.5 : 0.08);
      this.autoHi += (hi - this.autoHi) * (hi > this.autoHi ? 0.5 : 0.08);
      lo = this.autoLo;
      hi = this.autoHi;
    }
    const yOf = (v) => top + ph - ((v - lo) / (hi - lo)) * ph;
    const xOf = (t) => left + ((t - t0) / this.window) * pw;

    /* grid: 10 x 8 divisions like a scope */
    g.lineWidth = 1;
    g.strokeStyle = this.grid;
    g.beginPath();
    for (let i = 0; i <= 10; i++) { const x = Math.round(left + (pw * i) / 10) + 0.5; g.moveTo(x, top); g.lineTo(x, top + ph); }
    for (let i = 0; i <= 8; i++) { const y = Math.round(top + (ph * i) / 8) + 0.5; g.moveTo(left, y); g.lineTo(left + pw, y); }
    g.stroke();
    if (lo < 0 && hi > 0) {
      g.strokeStyle = this.axis;
      g.beginPath();
      const y0 = Math.round(yOf(0)) + 0.5;
      g.moveTo(left, y0);
      g.lineTo(left + pw, y0);
      g.stroke();
    }

    /* labels */
    g.fillStyle = this.text;
    g.font = this.font;
    g.textAlign = 'right';
    g.textBaseline = 'middle';
    const step = (hi - lo) / 4;
    const dec = Math.abs(step) >= 10 ? 0 : Math.abs(step) >= 1 ? 1 : Math.abs(step) >= 0.1 ? 2 : 3;
    for (let i = 0; i <= 4; i++) {
      const v = lo + step * i;
      const txt = v.toFixed(dec);
      g.fillText(/^-0(\.0+)?$/.test(txt) ? txt.slice(1) : txt, left - 6, yOf(v));
    }
    g.textAlign = 'center';
    g.textBaseline = 'top';
    g.fillText(`${(this.window / 10).toFixed(this.window >= 10 ? 0 : 1)} s/div`, left + pw / 2, top + ph + 3);
    if (this.unit) { g.textAlign = 'right'; g.fillText(this.unit, left + pw, top + ph + 3); }

    /* traces */
    g.save();
    g.beginPath();
    g.rect(left, top, pw, ph);
    g.clip();
    for (const s of this.series) {
      if (!s.visible || s.ring.count < 2) continue;
      g.strokeStyle = s.color;
      g.lineWidth = s.width;
      g.setLineDash(s.dash ? [5, 4] : []);
      g.beginPath();
      let col = -1;
      let cMin = 0;
      let cMax = 0;
      let first = true;
      let lastV = 0;
      const flush = () => {
        if (col < 0) return;
        const x = left + col + 0.5;
        if (first) { g.moveTo(x, yOf(cMin)); first = false; } else g.lineTo(x, yOf(cMin));
        if (cMax !== cMin) g.lineTo(x, yOf(cMax));
        g.lineTo(x, yOf(lastV));
      };
      for (const [t, v] of s.ring.iter()) {
        if (t < t0 - 0.05) continue;
        const cc = Math.floor(xOf(t) - left);
        if (cc !== col) {
          flush();
          col = cc;
          cMin = v;
          cMax = v;
        } else {
          if (v < cMin) cMin = v;
          if (v > cMax) cMax = v;
        }
        lastV = v;
      }
      flush();
      g.stroke();
    }
    g.restore();
    g.setLineDash([]);
  }
}
