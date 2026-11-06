/**
 * Input bound to one firmware parameter. Applies on change (PARAM_SET),
 * shows the value the firmware actually kept (clamping), marks values that
 * differ from flash, and follows changes made elsewhere in the UI.
 */
import { BEEP_MASK, C } from '../proto/proto.js';
import { h } from './dom.js';

function decimalsOf(p) {
  if (p.type !== C.PARAM_T_FLOAT) return 0;
  return p.meta.extra.decimals ?? 3;
}

export function paramInput(app, name, { showUnit = true, width } = {}) {
  const pt = app.params;
  const p = pt?.get(name);
  if (!p) return h('span', { class: 'faint' }, `(${name}?)`);
  const ex = p.meta.extra;
  let input;
  let read;
  let write;

  if (ex.bitmask) {
    const boxes = BEEP_MASK.map((m) => {
      const cb = h('input', { type: 'checkbox' });
      cb.dataset.bit = m.bit;
      return h('label', { class: 'check' }, cb, m.label);
    });
    input = h('div', { class: 'bitmask' }, boxes);
    read = () => boxes.reduce((v, l) => {
      const cb = l.firstChild;
      return cb.checked ? v | (1 << Number(cb.dataset.bit)) : v & ~(1 << Number(cb.dataset.bit));
    }, p.value);
    write = (v) => { for (const l of boxes) l.firstChild.checked = (v & (1 << Number(l.firstChild.dataset.bit))) !== 0; };
  } else if (ex.enum) {
    input = h('select', Object.entries(ex.enum).map(([v, label]) => h('option', { value: v }, label)));
    read = () => Number(input.value);
    write = (v) => { input.value = String(v); };
  } else if (p.type === C.PARAM_T_BOOL || ex.bool) {
    input = h('input', { type: 'checkbox' });
    read = () => (input.checked ? 1 : 0);
    write = (v) => { input.checked = !!v; };
  } else {
    const d = decimalsOf(p);
    input = h('input', {
      type: 'number',
      step: ex.step ?? (d === 0 ? 1 : 10 ** -Math.min(d, 3)),
      min: p.minValue,
      max: p.maxValue,
      inputmode: 'decimal',
      style: width ? { width } : undefined,
    });
    read = () => Number(input.value);
    write = (v) => { input.value = p.format(v); };
  }
  input.setAttribute('aria-label', p.meta.label);
  input.title = `${p.name}: ${p.format(p.minValue)} … ${p.format(p.maxValue)}, mặc định ${p.format(p.defValue)}`;

  let seen = false;
  const refresh = () => {
    /* detached by a view change: stop following the table */
    if (input.isConnected) seen = true;
    else if (seen) { input.dispose(); return; }
    write(p.value);
    const target = ex.bitmask ? input : input;
    target.classList.toggle('changed', p.dirty);
    const lock = p.readOnly || (app.armed && !p.live) || !app.connected;
    for (const el of ex.bitmask ? input.querySelectorAll('input') : [input]) el.disabled = lock;
  };

  const apply = async () => {
    const want = read();
    if (!Number.isFinite(want)) { refresh(); return; }
    try {
      const got = await pt.set(name, want);
      const clamped = p.type === C.PARAM_T_FLOAT ? Math.abs(got - want) > 1e-6 * Math.max(1, Math.abs(want)) : got !== want;
      if (clamped) {
        input.classList.add('clamped');
        app.toast(`${p.meta.label}: giới hạn ${p.format(p.minValue)} … ${p.format(p.maxValue)}, đã đặt ${p.format(got)}`, 'warn');
        setTimeout(() => input.classList.remove('clamped'), 1500);
      }
    } catch (e) {
      app.toast(`${p.meta.label}: ${e.message}`, 'err');
    }
    refresh();
  };

  input.addEventListener('change', apply);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') { refresh(); input.blur(); }
  });
  const off = pt.on('changed', (q) => { if (!q || q === p) refresh(); });
  const off2 = pt.on('saved', refresh);
  const off3 = app.on('lock', refresh);
  input.dispose = () => { off(); off2(); off3(); };
  refresh();

  if (!showUnit || !p.meta.unit) return input;
  return h('span', { class: 'pval' }, input, h('span', { class: 'unit' }, p.meta.unit));
}

/** Label + input + help, as one row of a parameter list. */
export function paramRow(app, name) {
  const p = app.params.get(name);
  if (!p) return null;
  const tags = [];
  if (p.live) tags.push(h('span', { class: 'tag live', title: 'Đổi được khi đang bay' }, 'trực tiếp'));
  if (p.needsReboot) tags.push(h('span', { class: 'tag reboot', title: 'Có hiệu lực sau khi khởi động lại' }, 'cần khởi động lại'));
  const def = h('span', { class: 'def' });
  const row = h('div', { class: 'prow', dataset: { name } },
    h('div', { class: 'pname' }, h('span', p.meta.label, h('span', { class: 'tags' }, tags)), h('span', { class: 'code' }, p.name)),
    h('div', { class: 'pval' }, paramInput(app, name), def),
    h('div', { class: 'phelp' }, p.meta.help));
  const upd = () => { def.textContent = p.isDefault ? '' : `mặc định ${p.format(p.defValue)}`; };
  let seenRow = false;
  const off = app.params.on('changed', (q) => {
    if (row.isConnected) seenRow = true;
    else if (seenRow) { off(); return; }
    if (!q || q === p) upd();
  });
  upd();
  return row;
}
