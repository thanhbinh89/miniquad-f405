/** Nhật ký: firmware log (MSG_LOG) and configurator events. */
import { download, h } from './dom.js';

const LEVELS = ['DEBUG', 'INFO', 'WARN', 'LỖI'];
const MAX_LINES = 2000;

export class LogStore {
  constructor() {
    this.lines = [];
    this.listeners = new Set();
  }

  add(level, text, host = false) {
    const line = { t: new Date(), level, text, host };
    this.lines.push(line);
    if (this.lines.length > MAX_LINES) this.lines.splice(0, this.lines.length - MAX_LINES);
    for (const fn of this.listeners) fn(line);
  }

  clear() { this.lines = []; for (const fn of this.listeners) fn(null); }
}

export class ConsoleView {
  constructor(app) {
    this.app = app;
    this.title = 'Nhật ký';
    this.streams = { status: 5 };
    this.minLevel = 0;
  }

  mount(root) {
    const app = this.app;
    this.box = h('div', { class: 'console', role: 'log', 'aria-live': 'polite' });
    const lvl = h('select', { 'aria-label': 'Mức tối thiểu', onchange: () => { this.minLevel = Number(lvl.value); this.renderAll(); } },
      LEVELS.map((l, i) => h('option', { value: i, selected: i === this.minLevel }, `Từ ${l}`)));
    this.stats = h('span', { class: 'note' });
    root.append(
      h('div', { class: 'view-head' }, h('h1', 'Nhật ký'), h('p', 'Thông báo từ firmware và từ trang cấu hình.'),
        h('span', { class: 'spacer' }), lvl,
        h('button', { class: 'btn small', onclick: () => this.save() }, 'Lưu file'),
        h('button', { class: 'btn small', onclick: () => app.logs.clear() }, 'Xoá')),
      this.box,
      h('div', { style: { marginTop: '8px' } }, this.stats));
    this.off = (() => {
      const fn = (line) => { if (line === null) this.renderAll(); else this.append(line); };
      app.logs.listeners.add(fn);
      return () => app.logs.listeners.delete(fn);
    })();
    this.renderAll();
    this.timer = setInterval(() => this.updateStats(), 1000);
    this.updateStats();
  }

  unmount() {
    this.off();
    clearInterval(this.timer);
  }

  line(l) {
    return h('div', { class: ['ln', `l${l.level}`, l.host ? 'host' : ''] },
      h('span', { class: 't' }, l.t.toLocaleTimeString('vi-VN', { hour12: false })),
      h('span', { class: 'lv' }, l.host ? 'trang' : LEVELS[l.level] ?? l.level),
      h('span', { class: 'm' }, l.text));
  }

  renderAll() {
    this.box.replaceChildren(...this.app.logs.lines.filter((l) => l.host || l.level >= this.minLevel).map((l) => this.line(l)));
    this.box.scrollTop = this.box.scrollHeight;
  }

  append(l) {
    if (!l.host && l.level < this.minLevel) return;
    const stick = this.box.scrollTop + this.box.clientHeight >= this.box.scrollHeight - 30;
    this.box.append(this.line(l));
    while (this.box.childElementCount > MAX_LINES) this.box.firstChild.remove();
    if (stick) this.box.scrollTop = this.box.scrollHeight;
  }

  updateStats() {
    const d = this.app.device;
    if (!d) { this.stats.textContent = ''; return; }
    const s = d.stats;
    const p = d.parser.stats;
    this.stats.textContent = `Gửi ${s.tx} gói (${(s.txBytes / 1024).toFixed(1)} KB), nhận ${p.frames} gói (${(s.rxBytes / 1024).toFixed(1)} KB), lỗi CRC ${p.crc}, lỗi khung ${p.cobs + p.overflow}, hết giờ ${s.timeouts}, gửi lại ${s.retries}`;
  }

  save() {
    const text = this.app.logs.lines.map((l) => `${l.t.toISOString()} ${l.host ? 'PAGE' : LEVELS[l.level]} ${l.text}`).join('\n');
    download(`miniquad-log-${Date.now()}.txt`, text, 'text/plain');
  }
}
