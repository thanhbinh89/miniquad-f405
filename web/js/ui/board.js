/**
 * Top view of the MiniQuad-F405 PCB (44 x 48 mm, origin at the centre).
 * Positions come from the final layouts (pick-and-place / pad data):
 * board v1 of 2026-10-30, board v2 of 2026-11-06 (Gerber of 2026-11-07,
 * only the USB parts differ). MCU U3 in the middle, IMU U4 below it,
 * S2 reset / J1 battery / J3 RC / LED on the top edge, S1 power switch and
 * LS1 buzzer on the bottom edge, ToF U5 on the bottom side, mounting holes
 * on a 20 x 20 mm square, motor connectors J4..J7 (M1..M4) on the left and
 * right edges (M1 top right, then counter-clockwise).
 *
 * Right edge, J2: board v1 = 7-pin 1.25 mm header (SWD + UART1),
 * board v2 = USB-C receptacle; v2 has the SWD test pads TP1..TP5 on the
 * bottom side instead.
 */
import { s } from './dom.js';

/* board coordinates: x right, y up (mm) */
/** Motors / props (frame geometry, used for the spin directions and the 3D
 *  view): only the quadrant matters. The connectors are MOTOR_CONN. */
export const MOTOR_POS = [[17, 19.5], [-17, 19.5], [-17, -19.5], [17, -19.5]];
/** Motor connectors J4..J7 = M1..M4: [x, y, y of pin 1 (+VBAT)]. */
const MOTOR_CONN = [[19.56, 11.43, 10.79], [-19.63, 11.43, 10.79], [-19.56, -11.43, -10.79], [19.63, -11.43, -10.79]];
const MCU_Y = 1.524;                               /* U3 centre (0, 1.524) */
const IMU_Y = -10.16;                              /* U4 centre (0, -10.16) */
/** J3 (SH1.0 4P) signal pads, pin 1..4, at y = 18.52; pin 4 = PPM (PC11). */
const J3_PIN_X = [6.58, 5.58, 4.58, 3.58];
const J3_PIN_Y = 18.52;
/** Board v1 J2: pin 1..7 at x = 17.62, pin 1 (+3V3) at the bottom. */
const J2V1_PIN_Y = (pin) => -3.75 + (pin - 1) * 1.25;

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

/** SWD test pads of board v2: bottom side, 2 mm round pads at 3.0 mm pitch,
 *  under the upper part of U3, [name, net, ST-LINK pin, silkscreen,
 *  x (mm, top view)], y = 5.334 mm; silkscreen names below the pads. */
export const SWD_PADS = [
  ['TP1', '+3V3', 'VTref / 3,3 V của ST-Link', '3V3', 9.432], ['TP2', 'SWDIO', 'SWDIO', 'DIO', 6.432],
  ['TP3', 'SWCLK', 'SWCLK', 'CLK', 3.432], ['TP4', 'NRST', 'NRST (RST)', 'RST', 0.432], ['TP5', 'GND', 'GND', 'GND', 12.432],
];
const TP_Y = 5.334;
const TP_SILK_Y = 2.6;
const USBC = { x: 17.1, y: 3.52 };              /* J2 centre, opening on the right edge */

/**
 * @param opts.rev       board revision: 2 = USB-C (default), 1 = UART header
 * @param opts.focus     'usb' | 'swd' (v2), 'uart' (v1), 'ppm' | null - what to call out
 * @param opts.props     draw props with their spin direction
 * @param opts.boardYaw  nose edge (0 top .. 3 left), drawn as a gold arrow
 * @param opts.motorLabels optional text per motor (e.g. live duty)
 */
export function boardSVG({ rev = 2, focus = null, props = false, boardYaw = null, propsOut = false, motorLabels = null, scale = 1 } = {}) {
  const pad = props ? 34 : 27;
  const extraRight = ['uart', 'usb', 'swd'].includes(focus) ? 30 : 0;
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

  /* MCU U3 (LQFP64, 10 x 10 mm, pin 1 top left) and IMU U4 */
  svg.append(rect(0, MCU_Y, 10, 10, 'silk'));
  svg.append(s('circle', { cx: -3.8, cy: Y(MCU_Y + 3.8), r: 0.5, class: 'pad' }));
  svg.append(text(0, MCU_Y - 1.6, 'U3'), text(0, MCU_Y - 3.7, 'STM32F405', 'small'));
  svg.append(rect(0, IMU_Y, 3, 2.6, 'silk'));
  svg.append(text(1.9, IMU_Y + 0.85, 'U4', 'small', 'start'), text(1.9, IMU_Y - 0.85, 'IMU', 'small', 'start'));
  /* axis symbol left of U4 (as on the silkscreen): +X right, +Y up, +Z out of the board */
  {
    const [ox, oy] = [-4.9, IMU_Y - 2.64];
    svg.append(s('path', {
      d: `M${ox},${Y(oy)} H${ox + 2.6} M${ox + 2},${Y(oy) - 0.6} L${ox + 2.6},${Y(oy)} L${ox + 2},${Y(oy) + 0.6}`
        + ` M${ox},${Y(oy)} V${Y(oy + 2.6)} M${ox - 0.6},${Y(oy + 2)} L${ox},${Y(oy + 2.6)} L${ox + 0.6},${Y(oy + 2)}`,
      class: 'silk',
    }));
    svg.append(s('circle', { cx: ox, cy: Y(oy), r: 0.35, class: 'silk' }));
    svg.append(text(ox + 3.3, oy, 'X', 'tiny'), text(ox, oy + 3.3, 'Y', 'tiny'), text(ox - 1, oy, 'Z', 'tiny'));
  }

  /* top edge: S2 reset, J1 battery (pin 1 = +, square), J3 RC (pin 4 = PPM), LED D2 */
  svg.append(rect(-10.67, 21.34, 4.6, 2.6, 'silk', 0.4), s('circle', { cx: -10.67, cy: Y(21.34), r: 0.9, class: 'silk' }));
  svg.append(text(-10.67, 18.6, 'S2 reset', 'small'));
  svg.append(rect(-3.05, 20.42, 5.8, 4.6, 'silk'));
  svg.append(rect(-2.05, 21.12, 1.2, 1.2, 'pad lo', 0.1), s('circle', { cx: -4.05, cy: Y(21.12), r: 0.6, class: 'pad lo' }));
  svg.append(text(-3.05, 16.6, 'J1', 'small'));
  svg.append(rect(5.08, 21, 5, 3, 'silk'));
  svg.append(rect(2.33, 22.12, 1, 2.3, 'pad lo', 0.1), rect(7.83, 22.12, 1, 2.3, 'pad lo', 0.1));
  J3_PIN_X.forEach((x, i) => svg.append(rect(x, J3_PIN_Y, 0.7, 1.6, focus === 'ppm' && i === 3 ? 'pad' : 'pad lo', 0.1)));
  svg.append(text(J3_PIN_X[0], 16.9, '1', 'tiny'), text(9, 21, 'J3', 'small', 'start'));
  svg.append(rect(13.46, 20.32, 2.2, 0.9, 'pad lo', 0.1), text(13.46, 18.5, 'LED', 'small'));

  if (rev === 1) {
    /* right edge: J2 7-pin 1.25 mm header, opening to the right; pin 1 (+3V3)
     * at the bottom, UART1 TX/RX on pins 5/6, GND on 7 */
    svg.append(rect(20.6, 0, 3.4, 11.6, 'silk'));
    svg.append(rect(20.22, 6.13, 2.2, 2.4, 'pad lo', 0.2), rect(20.22, -6.13, 2.2, 2.4, 'pad lo', 0.2));
    for (let pin = 1; pin <= 7; pin++) {
      const hot = focus === 'uart' && pin >= 5;
      svg.append(rect(17.62, J2V1_PIN_Y(pin), 2, 0.7, hot ? 'pad hi' : 'pad lo', 0.1));
    }
    svg.append(text(16.2, J2V1_PIN_Y(1), '1', 'tiny', 'end'), text(16, 0, 'J2', 'small', 'end'));
  } else {
    /* right edge: J2 USB-C receptacle (opening to the right) */
    svg.append(rect(USBC.x + 1.4, USBC.y, 7, 9, focus === 'usb' ? 'pad hi' : 'silk', 1.2));
    svg.append(rect(USBC.x + 3.9, USBC.y, 1.4, 6.6, 'pad lo', 0.6));
    svg.append(text(16.8, USBC.y, 'J2', focus === 'usb' ? 'small on-gold' : 'small'));
    /* SWD test pads TP1..TP5, bottom side (dashed), silkscreen names below */
    for (const [, , , silk, x] of SWD_PADS) {
      svg.append(s('circle', { cx: x, cy: Y(TP_Y), r: 1, class: focus === 'swd' ? 'pad hi' : 'pad lo', 'stroke-dasharray': '0.4 0.3' }));
      svg.append(text(x, TP_SILK_Y, silk, 'tiny'));
    }
  }

  /* bottom edge: S1 power switch, LS1 buzzer, ToF U5 on the other side */
  svg.append(rect(-12.19, -21.84, 8.6, 2.4, 'silk'), text(-12.19, -19.4, 'S1', 'small'));
  svg.append(rect(15.24, -20.7, 5, 5, 'silk', 0.4), s('circle', { cx: 15.24, cy: Y(-20.7), r: 1.5, class: 'silk' }));
  svg.append(text(15.24, -16.9, 'LS1', 'small'));
  svg.append(s('rect', { x: -2.45, y: Y(-22.1) - 1.25, width: 4.9, height: 2.5, class: 'silk', 'stroke-dasharray': '0.6 0.5' }));
  svg.append(text(0, -19.4, 'U5 ToF (mặt dưới)', 'small'));

  /* motor connectors J4..J7 on the side edges, pin 1 = + (VBAT) */
  MOTOR_CONN.forEach(([x, y, y1], i) => {
    const sx = Math.sign(x);
    const sy = Math.sign(y);
    svg.append(rect(x, y, 3, 4.2, 'silk'));
    svg.append(s('circle', { cx: x, cy: Y(y1), r: 0.45, class: 'pad lo' }), s('circle', { cx: x, cy: Y(2 * y - y1), r: 0.45, class: 'pad lo' }));
    svg.append(text(x - sx * 2.3, y1, '+', 'tiny'));
    svg.append(text(x, y + sy * 3.5, `M${i + 1}`));
    if (motorLabels) svg.append(text(x, y + sy * 6, motorLabels[i] ?? '', 'call'));
  });

  if (boardYaw !== null) {
    const [ax, ay, rot] = [[0, 26.5, 0], [24.5, 0, 90], [0, -26.5, 180], [-24.5, 0, 270]][boardYaw & 3];
    svg.append(s('path', { d: 'M0,-2.2 L2,1.4 L-2,1.4 Z', class: 'nose', transform: `translate(${ax},${Y(ay)}) rotate(${rot})` }));
  }

  if (focus === 'uart') {
    const labels = ['5 TX  →  RX của USB-UART', '6 RX  ←  TX của USB-UART', '7 GND  →  GND'];
    labels.forEach((t, k) => {
      const y = J2V1_PIN_Y(5 + k);          /* pin 5, 6, 7 (upwards) */
      const ly = -3 + k * 4.2;              /* label, fanned out in the same order */
      svg.append(s('path', { d: `M18.62,${Y(y)} H24 L26,${Y(ly)} H27`, class: 'leader' }));
      svg.append(text(27.8, ly, t, 'call', 'start'));
    });
  }
  if (focus === 'usb' && rev !== 1) {
    svg.append(s('path', { d: `M22.2,${Y(USBC.y)} H27`, class: 'leader' }));
    svg.append(text(27.8, USBC.y + 1.6, 'cáp USB-C', 'call', 'start'));
    svg.append(text(27.8, USBC.y - 1.6, '→ máy tính', 'call', 'start'));
  }
  if (focus === 'swd' && rev !== 1) {
    /* one leader from the pad row (under J2), the pin map as a legend */
    svg.append(s('path', { d: `M13.6,${Y(TP_Y)} L15,${Y(-2.5)} H24 L26.4,${Y(-6)} H27`, class: 'leader' }));
    SWD_PADS.forEach(([name, net, , silk], k) => {
      svg.append(text(27.8, 2 - k * 3.6, `${name} ${silk} = ${net}`, 'call', 'start'));
    });
    svg.append(text(27.8, -16.6, '(mặt dưới, bước 3 mm)', 'small', 'start'));
  }
  if (focus === 'ppm') {
    /* from pin 4 (left-most signal pad) out between J1 and J3 */
    svg.append(s('path', { d: `M${J3_PIN_X[3]},${Y(J3_PIN_Y - 0.8)} V${Y(16.9)} H1.1 V${Y(26.4)}`, class: 'leader' }));
    svg.append(text(1.1, 28, 'J3.4 = tín hiệu PPM', 'call'));
  }
  return svg;
}
