/** Tổng quan: attitude, battery, altitude, receiver, motors, system health. */
import { ARM_BLOCKERS, BAT_STATES, C, FLIGHT_MODES, FS_PHASES, STATUS_FLAGS } from '../proto/proto.js';
import { Attitude3D } from '../widgets/attitude3d.js';
import { fmtDuration, fmtNum, h, panel, readout } from './dom.js';

/* VL53L1 range status after the ULD mapping (vl53l1_map_status) */
export const TOF_STATUS = {
  0: 'Hợp lệ', 1: 'Nhiễu cao (sigma)', 2: 'Tín hiệu yếu', 3: 'Quá gần (đã kẹp)', 4: 'Ngoài tầm đo',
  5: 'Lỗi phần cứng', 6: 'Hợp lệ (chưa kiểm tra wrap)', 7: 'Vượt tầm (wrap-around)', 9: 'Nhiễu xuyên kính',
  10: 'Đang đồng bộ', 11: 'Nhiều mục tiêu', 12: 'Có vật, tín hiệu yếu', 13: 'Quá gần', 255: 'Chưa có số đo',
};

export function channelRoles(params) {
  const roles = {};
  const get = (n, d) => params?.get(n)?.value ?? d;
  roles[get('rc_map_roll', 1) - 1] = ['Roll', 'ch1'];
  roles[get('rc_map_pitch', 2) - 1] = ['Pitch', 'ch2'];
  roles[get('rc_map_thr', 3) - 1] = ['Ga', 'gold'];
  roles[get('rc_map_yaw', 4) - 1] = ['Yaw', 'ch3'];
  roles[get('rc_ch_arm', 5) - 1] = ['ARM', ''];
  if (get('rc_ch_mode', 6) > 0) roles[get('rc_ch_mode', 6) - 1] = ['Chế độ', ''];
  if (get('rc_ch_beeper', 0) > 0) roles[get('rc_ch_beeper', 0) - 1] = ['Còi', ''];
  return roles;
}

export function meter(label, cls = '') {
  const fill = h('div', { class: 'fill' });
  const v = h('span', { class: 'v' }, '–');
  const el = h('div', { class: ['meter', cls] }, h('span', label), h('div', { class: 'track' }, fill), v);
  el.update = (frac, text) => {
    fill.style.width = `${Math.max(0, Math.min(1, frac)) * 100}%`;
    v.textContent = text;
  };
  el.label = el.firstChild;
  return el;
}

export class DashboardView {
  constructor(app) {
    this.app = app;
    this.title = 'Tổng quan';
    this.streams = { status: 20 };
  }

  mount(root) {
    const app = this.app;
    this.att = new Attitude3D(h('canvas', { class: 'attitude-canvas', 'aria-label': 'Mô hình 3D tư thế quad' }));
    this.att.boardYaw = app.params?.get('board_yaw')?.value ?? 0;
    this.rRoll = readout('Roll', '°', 'roll');
    this.rPitch = readout('Pitch', '°', 'pitch');
    this.rYaw = readout('Yaw (so với lúc bật)', '°', 'yaw');
    this.rMode = readout('Chế độ bay', '');

    this.rVbat = readout('Điện áp (lọc cho bù lực đẩy)', 'V');
    this.batKv = h('dl', { class: 'kv' });
    this.rAlt = readout('Độ cao ước lượng', 'm');
    this.altKv = h('dl', { class: 'kv' });
    this.armBox = h('div');
    this.rcMeters = [];
    for (let i = 0; i < 8; i++) this.rcMeters.push(meter(`CH${i + 1}`));
    this.motorMeters = [0, 1, 2, 3].map((i) => meter(`M${i + 1}`, 'gold'));
    this.sysKv = h('dl', { class: 'kv' });
    this.flags = h('div', { class: 'flags' }, STATUS_FLAGS.map((f) => h('span', { class: ['flag', /FAILSAFE|HSE|CRASH|SAT/.test(f.key) ? 'bad' : ''], dataset: { bit: f.bit } }, f.label)));
    this.rcInfo = h('p', { class: 'note' });

    const roles = channelRoles(app.params);
    this.rcMeters.forEach((m, i) => {
      const r = roles[i];
      if (r) {
        m.label.textContent = `CH${i + 1} ${r[0]}`;
        if (r[1]) m.classList.add(r[1]);
      }
    });

    root.append(
      h('div', { class: 'view-head' }, h('h1', 'Tổng quan'),
        h('p', 'Trạng thái trực tiếp của quad. Nghiêng quad bằng tay để kiểm tra hướng lắp và tư thế.')),
      h('div', { class: 'dash' },
        h('section', { class: 'panel hero-view' },
          h('span', { class: 'ref' }, 'U4', h('span', { class: 'part' }, 'ICM-42688-P, tư thế ước lượng')),
          this.att.c,
          h('div', { class: 'att-readouts' }, this.rRoll, this.rPitch, this.rYaw, this.rMode)),
        panel('J1', 'Pin 1S', h('div', { class: 'readouts' }, this.rVbat), this.batKv),
        panel('U5', 'VL53L1 ToF, giữ độ cao', h('div', { class: 'readouts' }, this.rAlt), this.altKv),
      ),
      h('div', { class: 'grid auto', style: { marginTop: '28px' } },
        panel('ARM', 'điều kiện', this.armBox),
        panel('J3', 'PPM', h('div', { class: 'meters' }, this.rcMeters), this.rcInfo),
        panel('M1–M4', 'duty PWM', h('div', { class: 'meters' }, this.motorMeters)),
        panel('U3', 'STM32F405', this.sysKv),
      ),
      h('div', { style: { marginTop: '22px' } }, panel('Cờ', 'trạng thái firmware', this.flags)),
    );

    const loop = () => {
      this.att.draw();
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
    if (app.status) this.onStatus(app.status);
  }

  unmount() {
    cancelAnimationFrame(this.raf);
  }

  onParams() {
    this.att.boardYaw = this.app.params?.get('board_yaw')?.value ?? 0;
  }

  onStatus(s) {
    this.att.set(s);
    this.rRoll.set(fmtNum(s.roll, 1));
    this.rPitch.set(fmtNum(s.pitch, 1));
    this.rYaw.set(fmtNum(s.yaw, 0));
    this.rMode.set(FLIGHT_MODES[s.mode] ?? s.mode);

    const bat = (s.flags & C.STF_BAT_PRESENT) !== 0;
    this.rVbat.set(bat ? fmtNum(s.vbat, 2) : '–');
    this.batKv.replaceChildren(
      h('dt', 'Điện áp nghỉ (lọc chậm)'), h('dd', bat ? `${fmtNum(s.vbatSlow, 2)} V` : '–'),
      h('dt', 'Hệ số bù lực đẩy'), h('dd', `× ${fmtNum(s.batComp, 3)}`),
      h('dt', 'Trạng thái'), h('dd', { class: 'text' }, BAT_STATES[s.batState] ?? s.batState),
    );

    const altOk = (s.flags & C.STF_ALT_VALID) !== 0;
    this.rAlt.set(fmtNum(s.alt, 2));
    this.rAlt.classList.toggle('faint', !altOk);
    this.altKv.replaceChildren(
      h('dt', 'ToF'), h('dd', s.tofStatus === 255 ? '–' : `${s.tofMm} mm`),
      h('dt', 'Kết quả đo'), h('dd', { class: 'text' }, TOF_STATUS[s.tofStatus] ?? `mã ${s.tofStatus}`),
      h('dt', 'Tốc độ đo'), h('dd', `${s.tofRate} Hz`),
      h('dt', 'Vận tốc đứng'), h('dd', `${fmtNum(s.vz, 2)} m/s`),
      h('dt', 'Ga treo đã học'), h('dd', fmtNum(s.hover, 3)),
      ...(s.mode === C.FMODE_ALT ? [h('dt', 'Độ cao đặt'), h('dd', `${fmtNum(s.altSp, 2)} m`)] : []),
    );

    if (s.armed) {
      this.armBox.replaceChildren(h('p', { style: { margin: 0, color: 'var(--err)', fontWeight: 600 } }, 'Đang ARM: motor có thể quay'),
        ...(s.fsPhase ? [h('p', { class: 'note warn' }, `Failsafe: ${FS_PHASES[s.fsPhase]}`)] : []));
    } else {
      const list = ARM_BLOCKERS.filter((b) => s.armBlock & b.bit);
      this.armBox.replaceChildren(list.length === 0
        ? h('ul', { class: 'blockers' }, h('li', { class: 'ready' }, 'Sẵn sàng ARM'))
        : h('ul', { class: 'blockers' }, list.map((b) => h('li', b.text))));
    }

    const rcOk = (s.flags & C.STF_RX_OK) !== 0;
    this.rcMeters.forEach((m, i) => {
      const us = s.rc[i];
      m.update(us ? (us - 900) / 1200 : 0, us ? String(us) : '–');
    });
    this.rcInfo.textContent = rcOk ? `${s.rcCount} kênh, ${s.rcRate} khung/s` : 'Không có tín hiệu PPM';
    this.motorMeters.forEach((m, i) => m.update(s.motor[i], `${Math.round(s.motor[i] * 100)} %`));

    this.sysKv.replaceChildren(
      h('dt', 'Vòng điều khiển'), h('dd', `${s.loopHz} Hz`),
      h('dt', 'Thời gian vòng'), h('dd', `${s.loopUsAvg} / ${s.loopUsMax} µs`),
      h('dt', 'Tải CPU (main loop)'), h('dd', `${s.cpuLoad} %`),
      h('dt', 'Nhiệt độ MCU / IMU'), h('dd', `${s.mcuTemp} / ${s.imuTemp} °C`),
      h('dt', 'Lỗi IMU / ToF / PPM'), h('dd', `${s.imuErrors} / ${s.tofErrors} / ${s.rxErrors}`),
      h('dt', 'Mất gói telemetry'), h('dd', String(s.linkDrops)),
      h('dt', 'Thời gian chạy'), h('dd', fmtDuration(s.timeMs)),
    );
    for (const el of this.flags.children) el.classList.toggle('on', (s.flags & Number(el.dataset.bit)) !== 0);
  }
}
