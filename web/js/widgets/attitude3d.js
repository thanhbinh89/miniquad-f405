/**
 * Chase-camera wireframe of the quad: board outline, arms, props, motor
 * thrust bars and the body axes in the scope channel colours
 * (roll axis = CH1 yellow, pitch = CH2 cyan, yaw = CH3 magenta).
 * Heading-up: yaw is shown as a compass ring under the quad.
 */
import { MOTOR_POS } from '../ui/board.js';

const D2R = Math.PI / 180;

function css(name, fallback) {
  try {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
  } catch (_) { return fallback; }
}

export class Attitude3D {
  constructor(canvas) {
    this.c = canvas;
    this.ctx = canvas.getContext('2d');
    this.roll = 0;
    this.pitch = 0;
    this.yaw = 0;
    this.motors = [0, 0, 0, 0];
    this.boardYaw = 0;
    this.armed = false;
    this.colors = {
      silk: css('--silk', '#e9ece2'), silk2: css('--silk-2', '#b4c5bd'), silk3: css('--silk-3', '#7f988f'),
      gold: css('--gold', '#d9b25b'), ch1: css('--ch1', '#f5d547'), ch2: css('--ch2', '#46d2f0'),
      ch3: css('--ch3', '#f06bd6'), err: css('--err', '#ff6f61'), line: 'rgba(233,236,226,0.16)',
    };
  }

  set(st) {
    this.roll = st.roll;
    this.pitch = st.pitch;
    this.yaw = st.yaw;
    this.motors = st.motor || this.motors;
    this.armed = !!st.armed;
  }

  /** body (FRD, m) -> world (NED, m), heading-up */
  _rot(p) {
    const cr = Math.cos(this.roll * D2R), sr = Math.sin(this.roll * D2R);
    const cp = Math.cos(this.pitch * D2R), sp = Math.sin(this.pitch * D2R);
    const [x, y, z] = p;
    const y1 = cr * y - sr * z;
    const z1 = sr * y + cr * z;
    return [cp * x + sp * z1, y1, -sp * x + cp * z1];
  }

  _proj(w) {
    const e = 26 * D2R;
    const D = 0.17;
    const cam = [-D * Math.cos(e), 0, -D * Math.sin(e)];
    const f = [Math.cos(e), 0, Math.sin(e)];
    const up = [Math.sin(e), 0, -Math.cos(e)];
    const v = [w[0] - cam[0], w[1] - cam[1], w[2] - cam[2]];
    const zc = v[0] * f[0] + v[1] * f[1] + v[2] * f[2];
    const xc = v[1];
    const yc = v[0] * up[0] + v[1] * up[1] + v[2] * up[2];
    return [this.cx + (this.F * xc) / zc, this.cy - (this.F * yc) / zc];
  }

  _line(pts, color, width = 1.2, dash = null) {
    const g = this.ctx;
    g.beginPath();
    pts.forEach((p, i) => { const [x, y] = this._proj(p); if (i) g.lineTo(x, y); else g.moveTo(x, y); });
    g.strokeStyle = color;
    g.lineWidth = width;
    g.setLineDash(dash || []);
    g.stroke();
    g.setLineDash([]);
  }

  /* board coordinates (mm, x right / y up) -> body FRD (m) for the nose edge */
  _b2body(bx, by, bz = 0) {
    const F = [[0, 1], [1, 0], [0, -1], [-1, 0]][this.boardYaw & 3];
    return [(bx * F[0] + by * F[1]) / 1000, (bx * F[1] - by * F[0]) / 1000, bz / 1000];
  }

  draw() {
    const c = this.c;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth;
    const hgt = c.clientHeight;
    if (!w || !hgt) return;
    if (c.width !== Math.round(w * dpr) || c.height !== Math.round(hgt * dpr)) {
      c.width = Math.round(w * dpr);
      c.height = Math.round(hgt * dpr);
    }
    const g = this.ctx;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, hgt);
    this.cx = w / 2;
    this.cy = hgt * 0.47;
    this.F = Math.min(w, hgt * 1.5) * 0.85;
    const C = this.colors;

    /* ground grid below the quad: horizon reference (heading-up) */
    const gz = 0.07;
    for (let i = -3; i <= 3; i++) {
      this._line([[-0.08, i * 0.035, gz], [0.16, i * 0.035, gz]], C.line, 1);
    }
    for (let i = -2; i <= 4; i++) {
      this._line([[i * 0.035, -0.105, gz], [i * 0.035, 0.105, gz]], C.line, 1);
    }

    /* compass ring around the vehicle */
    const ring = [];
    for (let a = 0; a <= 360; a += 10) ring.push([0.05 * Math.cos(a * D2R), 0.05 * Math.sin(a * D2R), gz]);
    this._line(ring, 'rgba(233,236,226,0.28)', 1);
    const yr = -this.yaw * D2R;          /* north mark rotates against the heading */
    const n0 = [0.05 * Math.cos(yr), 0.05 * Math.sin(yr), gz];
    const n1 = [0.064 * Math.cos(yr), 0.064 * Math.sin(yr), gz];
    this._line([n0, n1], C.ch3, 2);
    const [nx, ny] = this._proj([0.075 * Math.cos(yr), 0.075 * Math.sin(yr), gz]);
    g.fillStyle = C.ch3;
    g.font = '600 12px "Be Vietnam Pro", sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText('0°', nx, ny);         /* heading at boot (no magnetometer) */

    /* vehicle */
    const R = (p) => this._rot(p);
    const outline = [[-22, -24], [22, -24], [22, 24], [-22, 24], [-22, -24]].map(([x, y]) => R(this._b2body(x, y)));
    this._line(outline, C.silk, 1.6);
    const mcu = [[-5, -5], [5, -5], [5, 5], [-5, 5], [-5, -5]].map(([x, y]) => R(this._b2body(x, y)));
    this._line(mcu, C.silk3, 1);

    MOTOR_POS.forEach(([bx, by], i) => {
      const m = this._b2body(bx * 1.5, by * 1.35);
      this._line([R(this._b2body(bx * 0.45, by * 0.45)), R(m)], C.silk2, 1.4);
      const prop = [];
      for (let a = 0; a <= 360; a += 15) {
        prop.push(R([m[0] + 0.0155 * Math.cos(a * D2R), m[1] + 0.0155 * Math.sin(a * D2R), m[2] - 0.004]));
      }
      const front = m[0] > 0;
      this._line(prop, front ? C.gold : C.silk2, front ? 1.6 : 1.2);
      /* thrust bar: duty 0..1 -> 0..40 mm up */
      const d = Math.max(0, Math.min(1, this.motors[i] || 0));
      if (d > 0.005) this._line([R([m[0], m[1], m[2] - 0.004]), R([m[0], m[1], m[2] - 0.004 - 0.04 * d])], this.armed ? C.err : C.gold, 3);
    });

    /* body axes */
    this._line([R([0, 0, 0]), R([0.05, 0, 0])], C.ch1, 2);
    this._line([R([0, 0, 0]), R([0, 0.04, 0])], C.ch2, 2);
    this._line([R([0, 0, 0]), R([0, 0, -0.035])], C.ch3, 2);
  }
}
