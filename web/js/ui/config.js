/** Tham số: every firmware parameter, grouped, searchable, import / export. */
import { versionString } from '../proto/proto.js';
import { download, h, pickFile } from './dom.js';
import { paramRow } from './param_input.js';

export class ConfigView {
  constructor(app) {
    this.app = app;
    this.title = 'Tham số';
    this.streams = { status: 5 };
    this.query = '';
    this.onlyChanged = false;
  }

  mount(root) {
    const app = this.app;
    this.list = h('div');
    const search = h('input', {
      type: 'search', placeholder: 'Tìm tên hoặc mô tả…', 'aria-label': 'Tìm tham số', value: this.query,
      oninput: () => { this.query = search.value.trim().toLowerCase(); this.filter(); },
    });
    const changed = h('input', { type: 'checkbox', checked: this.onlyChanged, onchange: () => { this.onlyChanged = changed.checked; this.filter(); } });

    root.append(
      h('div', { class: 'view-head' }, h('h1', 'Tham số'),
        h('p', 'Thay đổi có hiệu lực ngay trên quad; bấm “Lưu vào flash” để giữ sau khi tắt nguồn. Tham số “trực tiếp” đổi được cả khi đang bay.')),
      h('div', { class: 'params-tools' },
        search,
        h('label', { class: 'check' }, changed, 'Chỉ hiện giá trị khác mặc định'),
        h('span', { class: 'spacer', style: { flex: 1 } }),
        h('button', { class: 'btn', onclick: () => this.export() }, 'Xuất file'),
        h('button', { class: 'btn', onclick: () => this.import() }, 'Nhập file'),
        h('button', { class: 'btn danger', onclick: () => this.reset() }, 'Khôi phục mặc định')),
      this.list,
      h('div', { class: 'savebar' }, app.saveButton(), h('button', { class: 'btn', onclick: () => this.revert() }, 'Hoàn tác thay đổi chưa lưu'),
        h('span', { class: 'note' }, `${app.params.list.length} tham số, bảng 0x${app.params.hash.toString(16).toUpperCase()}`)),
    );
    this.render();
  }

  unmount() {}

  render() {
    const app = this.app;
    this.list.replaceChildren(...app.params.groups().map((g) =>
      h('section', { class: 'pgroup', dataset: { group: g.id } },
        h('h2', g.label, h('span', { class: 'count' }, `${g.params.length}`)),
        g.params.map((p) => paramRow(app, p.name)))));
    this.filter();
  }

  filter() {
    const q = this.query;
    for (const sec of this.list.children) {
      let shown = 0;
      for (const row of sec.querySelectorAll('.prow')) {
        const p = this.app.params.get(row.dataset.name);
        const text = `${p.name} ${p.meta.label} ${p.meta.help}`.toLowerCase();
        const ok = (!q || text.includes(q)) && (!this.onlyChanged || !p.isDefault);
        row.classList.toggle('hidden', !ok);
        shown += ok ? 1 : 0;
      }
      sec.classList.toggle('hidden', shown === 0);
    }
  }

  export() {
    const app = this.app;
    const info = app.hello;
    const json = app.params.toJSON({
      board: info?.boardName, firmware: info ? versionString(info.fwVersion) : undefined, git: info?.gitHash, uid: info?.uidHex,
    });
    const stamp = new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
    download(`miniquad-params-${stamp}.json`, JSON.stringify(json, null, 2));
    app.toast('Đã xuất file tham số', 'ok');
  }

  async import() {
    const app = this.app;
    const file = await pickFile('.json,application/json');
    if (!file) return;
    let obj;
    try {
      obj = JSON.parse(await file.text());
    } catch (e) {
      app.toast(`File không đọc được: ${e.message}`, 'err');
      return;
    }
    const n = Object.keys(obj.params || {}).length;
    const includeCal = await app.confirm('Nhập tham số', `Áp dụng ${n} giá trị từ “${file.name}”. Có ghi đè cả offset hiệu chuẩn gia tốc kế không? (Chọn “Không” nếu file từ quad khác.)`, 'Có, ghi cả hiệu chuẩn', false, 'Không');
    await app.run('Đang nhập tham số…', async () => {
      const res = await app.params.applyJSON(obj, { includeCal });
      const parts = [`${res.applied.length} giá trị đã đổi`, `${res.unchanged.length} giữ nguyên`];
      if (res.clamped.length) parts.push(`${res.clamped.length} bị giới hạn (${res.clamped.join(', ')})`);
      if (res.unknown.length) parts.push(`${res.unknown.length} không có trong firmware này`);
      if (res.skipped.length) parts.push(`${res.skipped.length} bỏ qua`);
      if (res.failed.length) parts.push(`${res.failed.length} lỗi`);
      app.toast(`${parts.join(', ')}. Bấm “Lưu vào flash” để giữ lại.`, res.failed.length ? 'err' : 'ok', 8000);
    });
  }

  async reset() {
    const app = this.app;
    const ok = await app.confirm('Khôi phục mặc định', 'Mọi tham số (kể cả hiệu chuẩn gia tốc kế) về giá trị mặc định của firmware. Flash chỉ bị ghi khi bạn bấm “Lưu vào flash”.', 'Khôi phục', true);
    if (!ok) return;
    await app.run('Đang khôi phục…', async () => {
      await app.params.resetDefaults();
      app.toast('Đã về mặc định (chưa lưu vào flash)', 'ok');
    });
  }

  async revert() {
    const app = this.app;
    const dirty = app.params.list.filter((p) => p.dirty);
    if (!dirty.length) { app.toast('Không có thay đổi chưa lưu'); return; }
    await app.run('Đang hoàn tác…', async () => {
      for (const p of dirty) await app.device.paramSet(p.index, p.savedRaw).then((r) => { p.raw = r.value; });
      app.params.emit('changed', null);
      app.toast(`Đã hoàn tác ${dirty.length} thay đổi`, 'ok');
    });
  }
}
