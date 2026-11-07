/**
 * MiniQuad-F405 configurator: application shell (connection, views,
 * telemetry streams, toasts, confirmation dialogs).
 */
import { ParamTable } from './params/params.js';
import { Device } from './proto/device.js';
import { waitForMode } from './proto/firmware.js';
import { BAT_STATES, BOARDS, BOOT_REASONS, C, FLIGHT_MODES, versionString } from './proto/proto.js';
import { DemoTransport } from './transport/demo.js';
import { SerialTransport, serialSupported } from './transport/serial.js';
import { boardSVG } from './ui/board.js';
import { ConfigView } from './ui/config.js';
import { ConsoleView, LogStore } from './ui/console.js';
import { DashboardView } from './ui/dashboard.js';
import { DemoDock } from './ui/demo_dock.js';
import { $, $$, fmtNum, h } from './ui/dom.js';
import { FirmwareView } from './ui/firmware_ui.js';
import { SetupView } from './ui/setup.js';
import { STREAM_IDS, TuningView } from './ui/tuning.js';
import { Emitter, sleep } from './util/emitter.js';

const BAUDS = [460800, 230400, 115200, 921600];
const DEFAULT_STREAMS = { status: 10, rate: 0, imu: 0, alt: 0 };

class App extends Emitter {
  constructor() {
    super();
    this.device = null;
    this.transport = null;
    this.params = null;
    this.hello = null;
    this.status = null;
    this.connected = false;
    this.baud = Number(localStorage.getItem('mq.baud')) || 460800;
    this.boardChoice = Number(localStorage.getItem('mq.board')) === 1 ? 1 : 2;
    this.busy = false;
    this.linkDown = false;
    this.holdReconnect = 0;
    this.logs = new LogStore();
    this.streamRates = {};
    this.views = {
      dashboard: new DashboardView(this),
      tuning: new TuningView(this),
      config: new ConfigView(this),
      setup: new SetupView(this),
      firmware: new FirmwareView(this),
      console: new ConsoleView(this),
    };
    this.viewName = this.views[location.hash.slice(1)] ? location.hash.slice(1) : 'dashboard';
    this.view = null;
    this.lastLine = 0;
  }

  get armed() { return !!this.status?.armed; }
  /** Revision of the connected board (HELLO), else what the user picked. */
  get boardRev() { return BOARDS[this.hello?.boardId]?.rev ?? this.boardChoice; }
  get inApp() { return this.connected && this.hello?.mode === C.DEV_MODE_APP && !!this.params?.loaded; }

  init() {
    $$('#rail button').forEach((b) => b.addEventListener('click', () => this.setView(b.dataset.view)));
    window.addEventListener('hashchange', () => {
      const v = location.hash.slice(1);
      if (this.views[v] && v !== this.viewName) this.setView(v);
    });
    window.addEventListener('beforeunload', (e) => {
      if (this.busy) { e.preventDefault(); e.returnValue = ''; }
    });
    $('#rail-foot').textContent = 'Trang cấu hình v0.1.0';
    this.render();
  }

  /* ---------------- rendering ---------------- */
  allowed(name) {
    if (!this.connected) return false;
    if (this.inApp) return true;
    return name === 'firmware' || name === 'console';
  }

  render() {
    this.renderConn();
    this.renderDeviceLine(true);
    $$('#rail button').forEach((b) => {
      b.disabled = !this.allowed(b.dataset.view);
      if (b.dataset.view === this.viewName) b.setAttribute('aria-current', 'page');
      else b.removeAttribute('aria-current');
    });
    const main = $('#main');
    if (this.view) {
      this.view.unmount?.();
      this.view = null;
    }
    main.replaceChildren();
    if (!this.connected) {
      main.append(this.connectScreen());
      return;
    }
    if (!this.allowed(this.viewName)) this.viewName = 'firmware';
    $$('#rail button').forEach((b) => (b.dataset.view === this.viewName ? b.setAttribute('aria-current', 'page') : b.removeAttribute('aria-current')));
    this.view = this.views[this.viewName];
    if (this.linkDown) {
      main.append(h('div', { class: 'callout warn', style: { marginBottom: '22px' } },
        'Mất kết nối USB với board. Trang sẽ tự kết nối lại khi board xuất hiện (đang khởi động lại, hoặc cắm lại cáp USB-C). ',
        'Nếu board đang ở ROM bootloader (DFU) thì cổng serial không có: xem phần khôi phục bên dưới.'));
    } else if (!this.hello) {
      main.append(h('div', { class: 'callout err', style: { marginBottom: '22px' } }, this.boardChoice === 1 && this.transport?.kind === 'serial' && !this.transport.usb
        ? 'Cổng đã mở nhưng board không trả lời. Kiểm tra dây TX/RX (chéo nhau), GND và nguồn. Nếu board đang ở ROM bootloader (cầu JP1), dùng phần khôi phục bên dưới.'
        : 'Cổng đã mở nhưng board không trả lời. Thử rút và cắm lại cáp USB-C, hoặc bấm reset S2. Nếu vẫn không được, xem phần khôi phục bên dưới.'));
    } else if (this.hello.mode === C.DEV_MODE_BOOTLOADER) {
      const why = BOOT_REASONS[this.hello.bootReason];
      main.append(h('div', { class: ['callout', this.hello.bootReason === C.BLR_CRASH_LOOP ? 'err' : ''], style: { marginBottom: '22px' } },
        this.hello.appValid ? 'Board đang ở bootloader. Nạp firmware mới hoặc bấm “Khởi động lại” để chạy ứng dụng.'
          : 'Board đang ở bootloader vì chưa có ứng dụng hợp lệ. Hãy nạp firmware.',
        why && this.hello.bootReason !== C.BLR_NO_APP ? h('div', { class: 'note', style: { marginTop: '6px' } }, `Lý do: ${why}.`) : null));
    }
    const box = h('div');
    main.append(box);
    this.view.mount(box);
    this.applyStreams(this.view.streams || {});
    if (location.hash.slice(1) !== this.viewName) history.replaceState(null, '', `#${this.viewName}`);
  }

  setView(name) {
    if (!this.views[name]) return;
    if (this.busy && name !== this.viewName) { this.toast('Đang nạp firmware, đợi xong rồi chuyển trang.', 'warn'); return; }
    this.viewName = name;
    if (this.connected && !this.allowed(name)) return;
    this.render();
  }

  connectScreen() {
    const v1 = this.boardChoice === 1;
    const baud = h('select', { 'aria-label': 'Tốc độ baud', onchange: () => { this.baud = Number(baud.value); localStorage.setItem('mq.baud', baud.value); } },
      BAUDS.map((b) => h('option', { value: b, selected: b === this.baud }, `${b} baud`)));
    const supported = serialSupported();
    const pickBoard = (rev) => { this.boardChoice = rev; localStorage.setItem('mq.board', String(rev)); this.render(); };
    const steps = v1
      ? h('ol',
        h('li', 'Nối mạch USB-UART 3,3 V vào ', h('b', 'J2'), ': chân 5 (TX) vào RX, chân 6 (RX) vào TX, chân 7 vào GND.'),
        h('li', 'Bật nguồn quad: pin 1S ở J1 và công tắc S1.'),
        h('li', 'Bấm “Kết nối” rồi chọn cổng của mạch USB-UART.'))
      : h('ol',
        h('li', 'Cắm cáp USB-C vào cổng ', h('b', 'J2'), ' của board. Chỉ cần USB là board chạy (cấu hình, nạp firmware); motor cần pin 1S ở J1.'),
        h('li', 'Bấm “Kết nối” rồi chọn ', h('b', 'MiniQuad-F405'), ' trong danh sách (Windows 10/11, Linux, macOS không cần cài driver).'),
        h('li', 'Khi board khởi động lại (vào bootloader, nạp firmware) cổng USB biến mất vài giây; trang tự kết nối lại.'));
    return h('section', { class: 'connect' },
      h('div', boardSVG({ rev: this.boardChoice, focus: v1 ? 'uart' : 'usb', scale: 1.2 })),
      h('div',
        h('h1', 'Kết nối MiniQuad-F405'),
        h('p', 'Trang này nói chuyện trực tiếp với flight controller qua cổng serial của trình duyệt. Không cần cài phần mềm.'),
        h('div', { class: 'seg', role: 'group', 'aria-label': 'Phiên bản board', style: { marginBottom: '14px' } },
          h('button', { 'aria-pressed': String(!v1), onclick: () => pickBoard(2) }, 'Board v2 · USB-C'),
          h('button', { 'aria-pressed': String(v1), onclick: () => pickBoard(1) }, 'Board v1 · UART J2')),
        steps,
        supported ? null : h('div', { class: 'callout', style: { marginBottom: '16px' } },
          'Trình duyệt này không có Web Serial. Dùng Chrome, Edge hoặc Opera trên máy tính, mở trang qua HTTPS hoặc localhost. Chế độ mô phỏng vẫn chạy được.'),
        h('div', { class: 'btn-row' },
          h('button', { class: 'btn primary', disabled: !supported, onclick: () => this.connectSerial() }, 'Kết nối'),
          v1 ? baud : null,
          h('button', { class: 'btn', onclick: () => this.connectDemo() }, 'Chạy mô phỏng')),
        h('p', { class: 'note' }, 'Mô phỏng chạy chính firmware này (biên dịch sang WebAssembly) trên một quad ảo có cổng USB-C: thử tinh chỉnh PID, bay bằng bàn phím, nạp firmware, rút/cắm cáp, khôi phục mà không cần phần cứng.')));
  }

  renderConn() {
    const box = $('#conn');
    if (!this.connected) {
      box.replaceChildren();
      return;
    }
    const led = this.linkDown ? 'warn' : (this.hello ? 'on' : 'err');
    box.replaceChildren(
      h('span', { class: 'chip', title: this.transport.details?.() || '' }, h('span', { class: ['led', led] }), this.transport.describe()),
      h('button', { class: 'btn small', onclick: () => this.disconnect() }, 'Ngắt kết nối'));
  }

  renderDeviceLine(force = false) {
    const now = performance.now();
    if (!force && now - this.lastLine < 200) return;
    this.lastLine = now;
    const box = $('#device-line');
    if (!this.connected || !this.hello) {
      box.replaceChildren();
      return;
    }
    const hl = this.hello;
    const items = [h('span', { class: 'chip' }, hl.mode === C.DEV_MODE_APP ? `v${versionString(hl.fwVersion)}` : `Bootloader v${versionString(hl.fwVersion)}`,
      h('span', { class: 'faint' }, hl.gitHash))];
    const s = this.status;
    if (s && hl.mode === C.DEV_MODE_APP) {
      items.push(s.armed ? h('span', { class: 'chip armed' }, 'ĐANG ARM') : h('span', { class: ['chip', s.armBlock ? '' : 'ok'] }, s.armBlock ? 'Chưa sẵn sàng ARM' : 'Sẵn sàng ARM'));
      items.push(h('span', { class: ['chip', s.mode === C.FMODE_FAILSAFE ? 'err' : ''] }, FLIGHT_MODES[s.mode]));
      const bat = s.flags & C.STF_BAT_PRESENT;
      items.push(h('span', { class: ['chip', s.batState === C.BAT_CRIT ? 'err' : s.batState === C.BAT_WARN ? 'warn' : ''], title: `Pin: ${BAT_STATES[s.batState]}` },
        h('span', { class: 'num' }, bat ? fmtNum(s.vbat, 2) : '–'), 'V'));
      if ((s.flags & C.STF_USB) && !bat) items.push(h('span', { class: 'chip', title: 'Board chạy bằng nguồn USB-C; motor không chạy khi chưa cắm pin' }, 'Nguồn USB, chưa có pin'));
      if (!(s.flags & C.STF_RX_OK)) items.push(h('span', { class: 'chip warn' }, 'Không có PPM'));
      if (s.flags & C.STF_HSE_FAIL) items.push(h('span', { class: 'chip err' }, 'Lỗi thạch anh'));
    }
    box.replaceChildren(...items);
  }

  /* ---------------- connection ---------------- */
  async connectSerial() {
    let tr;
    try {
      tr = await SerialTransport.pick({ board: this.boardChoice });
    } catch (_) {
      return;                                   /* the user closed the port picker */
    }
    await this.connectWith(tr, { baudRate: this.baud });
  }

  async connectDemo() {
    if (!this.demoTransport) this.demoTransport = new DemoTransport();
    await this.connectWith(this.demoTransport, {});
  }

  async connectWith(tr, opts) {
    const dev = new Device(tr);
    try {
      await dev.open(opts);
    } catch (e) {
      this.toast(`Không mở được cổng: ${e.message}`, 'err', 8000);
      return;
    }
    this.transport = tr;
    this.device = dev;
    this.connected = true;
    this.streamRates = {};
    dev.on('log', (l) => {
      this.logs.add(l.level, l.text);
      if (l.level >= C.LOG_LEVEL_WARN) this.toast(l.text, l.level >= C.LOG_LEVEL_ERROR ? 'err' : 'warn');
    });
    dev.on('status', (s) => this.onStatus(s));
    dev.on('close', (reason) => this.onClosed(reason));
    dev.on('linkdown', (why) => this.onLinkDown(why));
    dev.on('linkup', () => this.onLinkUp());
    this.linkDown = false;
    this.logs.add(1, `Đã mở ${tr.describe()}`, true);
    if (tr.kind === 'demo') {
      this.dock = new DemoDock(this, tr);
      this.dock.mount();
    }
    const t = this.toast('Đang kết nối…', '', 0);
    let hl = null;
    try {
      hl = await dev.hello({ retries: 4 });
    } catch (_) {
      hl = null;
    }
    t.remove();
    if (!hl) {
      this.hello = null;
      this.logs.add(2, 'Board không trả lời HELLO', true);
      this.render();
      return;
    }
    await this.onHello(hl);
  }

  async onHello(hl) {
    this.hello = hl;
    this.emit('hello', hl);
    if (hl.mode === C.DEV_MODE_APP) {
      this.params = new ParamTable(this.device);
      try {
        const t = this.toast('Đang đọc tham số…', '', 0);
        await this.params.load((n, total) => { t.textContent = `Đang đọc tham số ${n}/${total}…`; });
        t.remove();
        this.params.on('changed', () => this.emit('dirty'));
        this.params.on('saved', () => this.emit('dirty'));
      } catch (e) {
        this.toast(`Đọc tham số lỗi: ${e.message}`, 'err');
      }
      this.logs.add(1, `Ứng dụng v${versionString(hl.fwVersion)} (${hl.gitHash}), ${this.params.list.length} tham số`, true);
    } else {
      this.logs.add(1, `Bootloader v${versionString(hl.fwVersion)}, ứng dụng ${hl.appValid ? 'v' + versionString(hl.appVersion) : 'không hợp lệ'}`, true);
    }
    this.streamRates = {};
    this.render();
  }

  onStatus(s) {
    const wasArmed = this.armed;
    this.status = s;
    if (wasArmed !== s.armed) this.emit('lock');
    this.view?.onStatus?.(s);
    this.renderDeviceLine();
  }

  /* ---------------- USB re-enumeration (board v2) ---------------- */
  onLinkDown(why) {
    if (!this.connected) return;
    this.linkDown = true;
    this.status = null;
    this.logs.add(2, `Mất kết nối với board: ${why}`, true);
    if (this.holdReconnect === 0) {
      this.downToast?.remove();
      this.downToast = this.toast('Mất kết nối USB — đang chờ board kết nối lại…', 'warn', 0);
      this.render();
    } else {
      this.renderConn();
    }
  }

  async onLinkUp() {
    if (!this.connected) return;
    this.linkDown = false;
    this.downToast?.remove();
    this.downToast = null;
    this.logs.add(1, 'Đã kết nối lại với board', true);
    this.renderConn();
    if (this.holdReconnect > 0) return;         /* a flow (update, reboot) waits for it itself */
    let hl = null;
    try {
      hl = await this.device.hello({ retries: 6 });
    } catch (_) {
      hl = null;
    }
    if (hl) {
      this.toast('Đã kết nối lại', 'ok');
      await this.onHello(hl);
    } else {
      this.hello = null;
      this.render();
    }
  }

  /** Run @p fn while the board may reset: re-connection is handled by @p fn. */
  async expectReconnect(fn) {
    this.holdReconnect++;
    try {
      return await fn();
    } finally {
      this.holdReconnect--;
      if (this.linkDown && this.holdReconnect === 0) this.render();
    }
  }

  async onClosed(reason) {
    if (!this.connected) return;
    this.toast(`Mất kết nối: ${reason}`, 'err', 6000);
    this.logs.add(3, `Mất kết nối: ${reason}`, true);
    this.teardown();
  }

  teardown() {
    this.connected = false;
    this.linkDown = false;
    this.downToast?.remove();
    this.downToast = null;
    this.hello = null;
    this.status = null;
    this.params = null;
    this.dock?.unmount();
    this.dock = null;
    this.render();
  }

  async disconnect() {
    if (this.busy) {
      const ok = await this.confirm('Đang nạp firmware', 'Ngắt bây giờ có thể để board ở bootloader (không hỏng, nạp lại được).', 'Vẫn ngắt', true);
      if (!ok) return;
    }
    const dev = this.device;
    this.view?.unmount?.();
    this.view = null;
    try {
      await this.applyStreams({ status: 0 });
      await dev?.close();
    } catch (_) { /* ignore */ }
    this.logs.add(1, 'Đã ngắt kết nối', true);
    this.teardown();
  }

  /** Re-open the same port with other settings (ROM bootloader: 115200 8E1). */
  async reopen(settings) {
    const tr = this.transport;
    await tr.close();
    await sleep(50);
    await tr.open(settings);
    this.device.parser.reset();
  }

  /** Wait for the application after a reboot and reload everything. */
  async reconnectApp(timeout) {
    const hl = await waitForMode(this.device, C.DEV_MODE_APP, timeout);
    await this.onHello(hl);
  }

  /* ---------------- telemetry ---------------- */
  async applyStreams(want) {
    if (!this.connected || this.hello?.mode !== C.DEV_MODE_APP) return;
    const target = { ...DEFAULT_STREAMS, ...want };
    for (const [name, hz] of Object.entries(target)) {
      if (this.streamRates[name] === hz) continue;
      this.streamRates[name] = hz;
      try {
        await this.device.stream(STREAM_IDS[name], hz);
      } catch (e) {
        delete this.streamRates[name];
      }
    }
  }

  /* ---------------- shared widgets ---------------- */
  saveButton() {
    const btn = h('button', { class: 'btn primary', onclick: () => this.save() });
    const upd = () => {
      if (!btn.isConnected && btn.dataset.seen) { off(); return; }
      if (btn.isConnected) btn.dataset.seen = '1';
      const n = this.params?.dirtyCount ?? 0;
      btn.textContent = n ? `Lưu vào flash (${n})` : 'Lưu vào flash';
      btn.disabled = !this.inApp || this.armed;
      btn.title = this.armed ? 'Không lưu được khi đang ARM' : '';
    };
    const off = this.on('dirty', upd);
    const off2 = this.on('lock', upd);
    queueMicrotask(upd);
    setTimeout(upd, 0);
    btn.dispose = () => { off(); off2(); };
    return btn;
  }

  async save() {
    await this.run('Đang lưu vào flash…', async () => {
      await this.params.save();
      this.toast('Đã lưu vào flash', 'ok');
    });
  }

  toast(text, kind = '', ms = 3500) {
    const el = h('div', { class: ['toast', kind], role: kind === 'err' ? 'alert' : 'status' }, text);
    $('#toasts').append(el);
    if (ms > 0) setTimeout(() => el.remove(), ms);
    while ($('#toasts').childElementCount > 4) $('#toasts').firstChild.remove();
    return el;
  }

  confirm(title, text, okLabel = 'Đồng ý', danger = false, cancelLabel = 'Huỷ') {
    const dlg = $('#dialog');
    return new Promise((resolve) => {
      const done = (v) => { dlg.close(); resolve(v); };
      dlg.replaceChildren(
        h('h3', title), h('p', text),
        h('div', { class: 'btn-row' },
          h('button', { class: 'btn', onclick: () => done(false) }, cancelLabel),
          h('button', { class: ['btn', danger ? 'danger' : 'primary'], onclick: () => done(true) }, okLabel)));
      dlg.onclose = () => resolve(false);
      dlg.showModal();
      dlg.querySelector('.btn-row button:last-child').focus();
    });
  }

  async run(label, fn) {
    const t = label ? this.toast(label, '', 0) : null;
    try {
      return await fn();
    } catch (e) {
      this.toast(e.message, 'err', 6000);
      this.logs.add(3, e.message, true);
      return undefined;
    } finally {
      t?.remove();
    }
  }

  setBusy(b) {
    this.busy = b;
    $$('#rail button').forEach((btn) => { if (btn.dataset.view !== this.viewName) btn.disabled = b || !this.allowed(btn.dataset.view); });
  }
}

const app = new App();
window.mq = app;              /* handy in the browser console */
app.init();
