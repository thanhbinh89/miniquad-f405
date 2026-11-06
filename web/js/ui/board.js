/**
 * Top view of the MiniQuad-F405 PCB (44 x 48 mm, origin at the centre),
 * drawn from the design notes: MCU U3 in the middle, IMU U4 below it,
 * J1 battery / J3 RC on the top edge, J2 debug (UART1) on the right edge,
 * mounting holes on a 20 x 20 mm square, motor connectors M1..M4 at the
 * corners (M1 top right, then counter-clockwise), ToF U5 on the bottom side.
 */
import { s } from './dom.js';

/* board coordinates: x right, y up (mm) */
export const MOTOR_POS = [[17, 19.5], [-17, 19.5], [-17, -19.5], [17, -19.5]];

/** Prop spin per motor (+1 CCW seen from above) for the mixer geometry. */
export function motorSpins(boardYaw = 0, propsOut = false) {
  const F = [[0, 1], [1, 0], [0, -1], [-1, 0]][boardYaw & 3];
  return MOTOR_POS.map(([bx, by]) => {
    const sx = Math.sign(bx);
    const sy = Math.sign(by);
    const x = sx * F[0] + sy * F[1];              // forward
    const y = sx * F[1] - sy * F[0];              // right
    const spin = x * y > 0 ? 1 : -1;              // props-in: FR and RL counter-clockwise
    return propsOut ? -spin : spin;
  });
}

const Y = (y) => -y;                               // SVG y grows downwards

function rect(x, y, w, hgt, cls, rx = 0.3) {
  return s('rect', { x: x - w / 2, y: Y(y) - hgt / 2, width: w, height: hgt, rx, class: cls });
}

function text(x, y, str, cls = '', anchor = 'middle') {
  return s('text', { x, y: Y(y), 'text-anchor': anchor, 'dominant-baseline': 'middle', class: cls }, str);
}

function arc(cx, cy, r, ccw) {
  /* 300 degree arc with an arrow head showing the rotation direction */
  const a0 = ccw ? -60 : 240;
  const a1 = ccw ? 240 : -60;
  const p = (a) => [cx + r * Math.cos((a * Math.PI) / 180), Y(cy) - r * Math.sin((a * Math.PI) / 180)];
  const [x0, y0] = p(a0);
  const [x1, y1] = p(a1);
  const [xa, ya] = p(a1 + (ccw ? -14 : 14));
  return [
    s('path', { d: `M${x0},${y0} A${r},${r} 0 1 ${ccw ? 0 : 1} ${x1},${y1}`, class: 'prop' }),
    s('path', { d: `M${xa},${ya} L${x1},${y1}`, class: 'leader' }),
  ];
}

/**
 * @param opts.focus     'uart' | 'ppm' | 'boot0' | null - pins to call out
 * @param opts.props     draw props with their spin direction
 * @param opts.boardYaw  nose edge (0 top .. 3 left), drawn as a gold arrow
 * @param opts.motorLabels optional text per motor (e.g. live duty)
 */
export function boardSVG({ focus = null, props = false, boardYaw = null, propsOut = false, motorLabels = null, scale = 1 } = {}) {
  const pad = props ? 34 : 27;
  const extraRight = focus === 'uart' ? 30 : 0;
  const extraTop = focus === 'ppm' ? 10 : 0;
  const svg = s('svg', {
    class: 'board-svg',
    viewBox: `${-pad} ${-pad - extraTop} ${2 * pad + extraRight} ${2 * pad + extraTop}`,
    role: 'img',
    'aria-label': 'Sơ đồ mặt trên board MiniQuad-F405',
    style: { maxWidth: `${(2 * pad + extraRight) * 9 * scale}px` },
  });

  if (props) {
    const spins = motorSpins(boardYaw ?? 0, propsOut);
    MOTOR_POS.forEach(([x, y], i) => {
      svg.append(s('circle', { cx: x, cy: Y(y), r: 14, class: 'prop' }));
      svg.append(...arc(x, y, 10.5, spins[i] > 0));
    });
  }

  svg.append(s('rect', { x: -22, y: -24, width: 44, height: 48, rx: 2, class: 'outline' }));

  /* mounting holes on the 20 x 20 mm square */
  for (const [x, y] of [[10, 10], [-10, 10], [-10, -10], [10, -10]]) {
    svg.append(s('circle', { cx: x, cy: Y(y), r: 1.9, class: 'hole' }));
  }

  /* MCU U3 (LQFP64, 10 x 10 mm) and IMU U4 */
  svg.append(rect(0, 0, 10, 10, 'silk'));
  svg.append(s('circle', { cx: -3.8, cy: Y(3.8), r: 0.5, class: 'pad' }));
  svg.append(text(0, 0.6, 'U3'), text(0, -1.8, 'STM32F405', 'small'));
  svg.append(rect(0, -8.2, 3, 3, 'silk'));
  svg.append(text(0, -11.2, 'U4 IMU', 'small'));
  /* axis symbol next to U4: +X right, +Y up */
  svg.append(s('path', { d: `M3,${Y(-8.2)} h3.4 M5.6,${Y(-7.6)} l0.8,-0.6 l-0.8,-0.6 M3,${Y(-8.2)} v-3.4`, class: 'silk' }));

  /* top edge: J1 battery, J3 RC (PPM on pin 4), LED, reset */
  svg.append(rect(-7, 21, 6, 3.4, 'silk'), text(-7, 17.6, 'J1', 'small'));
  svg.append(rect(5, 21.2, 5.4, 2.4, 'silk'), text(5, 18.4, 'J3', 'small'));
  for (let i = 0; i < 4; i++) {
    const x = 3.3 + i * 1.1;
    svg.append(rect(x, 21.2, 0.6, 1.2, focus === 'ppm' && i === 3 ? 'pad' : 'pad lo', 0.1));
  }
  svg.append(rect(13, 21.6, 1.6, 0.8, 'pad lo', 0.1), text(13, 19.8, 'LED', 'small'));

  /* right edge: J2 debug 7P (UART1 on pins 5/6, GND on 7) */
  svg.append(rect(19.6, 0, 2.6, 14.4, 'silk'));
  for (let i = 0; i < 7; i++) {
    const y = 6 - i * 2;
    const hot = focus === 'uart' && i >= 4;
    svg.append(rect(19.6, y, 1.3, 1.3, hot ? 'pad hi' : 'pad lo', 0.15));
  }
  svg.append(text(19.6, 8.6, 'J2', 'small'));

  /* bottom edge: S1 power switch, LS1 buzzer, ToF on the other side */
  svg.append(rect(-8, -21.6, 5, 2, 'silk'), text(-8, -19, 'S1', 'small'));
  svg.append(s('circle', { cx: 6, cy: Y(-19.6), r: 2.4, class: 'silk' }), text(6, -19.6, 'LS1', 'small'));
  svg.append(s('rect', { x: -3, y: Y(-15) - 1.2, width: 4.4, height: 2.4, class: 'silk', 'stroke-dasharray': '0.6 0.5' }));
  svg.append(text(-3.8, -13.4, 'U5 ToF (mặt dưới)', 'small', 'end'));

  /* motor connectors */
  MOTOR_POS.forEach(([x, y], i) => {
    svg.append(rect(x, y, 3.2, 2.2, 'silk'));
    svg.append(text(x, y + (y > 0 ? -2.9 : 2.9), `M${i + 1}`));
    if (motorLabels) svg.append(text(x, y + (y > 0 ? -5.3 : 5.3), motorLabels[i] ?? '', 'call'));
  });

  if (boardYaw !== null) {
    const [ax, ay, rot] = [[0, 26.5, 0], [24.5, 0, 90], [0, -26.5, 180], [-24.5, 0, 270]][boardYaw & 3];
    svg.append(s('path', { d: 'M0,-2.2 L2,1.4 L-2,1.4 Z', class: 'nose', transform: `translate(${ax},${Y(ay)}) rotate(${rot})` }));
  }

  if (focus === 'uart') {
    const labels = ['5 TX  →  RX của USB-UART', '6 RX  ←  TX của USB-UART', '7 GND  →  GND'];
    labels.forEach((t, k) => {
      const y = 6 - (4 + k) * 2;            /* pin */
      const ly = 1 - k * 4.2;               /* label, fanned out */
      svg.append(s('path', { d: `M21,${Y(y)} H24 L26,${Y(ly)} H27`, class: 'leader' }));
      svg.append(text(27.8, ly, t, 'call', 'start'));
    });
  }
  if (focus === 'ppm') {
    svg.append(s('path', { d: `M6.6,${Y(22)} V${Y(28)} H10`, class: 'leader' }));
    svg.append(text(10.6, 28, 'J3.4 = tín hiệu PPM', 'call', 'start'));
  }
  return svg;
}
