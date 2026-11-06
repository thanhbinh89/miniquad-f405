/** Tiny DOM helpers (no framework). */
const SVG_NS = 'http://www.w3.org/2000/svg';

function apply(el, attrs) {
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.setAttribute('class', Array.isArray(v) ? v.filter(Boolean).join(' ') : v);
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
    else if (k === 'text') el.textContent = v;
    else if (k === 'html') el.innerHTML = v;
    else if (v === true) el.setAttribute(k, '');
    else if (k in el && !(el instanceof SVGElement) && typeof v !== 'string') el[k] = v;
    else el.setAttribute(k, v);
  }
}

function add(el, children) {
  for (const c of children.flat(Infinity)) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
}

export function h(tag, attrs, ...children) {
  const el = document.createElement(tag);
  if (attrs && (attrs instanceof Node || typeof attrs !== 'object' || Array.isArray(attrs))) {
    children.unshift(attrs);
    attrs = null;
  }
  apply(el, attrs);
  add(el, children);
  return el;
}

export function s(tag, attrs, ...children) {
  const el = document.createElementNS(SVG_NS, tag);
  apply(el, attrs);
  add(el, children);
  return el;
}

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

/** <div class="panel"><span class="ref">U4<span class="part">ICM-42688-P</span></span> ...</div> */
export function panel(ref, part, ...children) {
  return h('section', { class: 'panel' },
    h('span', { class: 'ref' }, ref, part ? h('span', { class: 'part' }, part) : null),
    ...children);
}

export function readout(label, unit, cls = '') {
  const v = h('span', { class: 'num-v' }, '–');
  const el = h('div', { class: ['readout', cls] },
    h('span', { class: 'value' }, v, unit ? h('small', unit) : null),
    h('span', { class: 'label' }, label));
  el.set = (text) => { v.textContent = text; };
  return el;
}

export const fmtNum = (v, d = 1) => {
  if (!Number.isFinite(v)) return '–';
  const s = v.toFixed(d);
  return /^-0(\.0*)?$/.test(s) ? s.slice(1) : s;      /* no "-0.0" */
};
export const hex32 = (v) => '0x' + (v >>> 0).toString(16).toUpperCase().padStart(8, '0');

export function fmtDuration(ms) {
  const s = Math.floor(ms / 1000);
  const m = Math.floor(s / 60);
  return `${m}:${String(s % 60).padStart(2, '0')}`;
}

export function download(name, text, type = 'application/json') {
  const a = h('a', { href: URL.createObjectURL(new Blob([text], { type })), download: name });
  document.body.append(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

export function pickFile(accept) {
  return new Promise((resolve) => {
    const input = h('input', { type: 'file', accept, style: { display: 'none' } });
    input.addEventListener('change', () => { resolve(input.files[0] || null); input.remove(); });
    document.body.append(input);
    input.click();
  });
}
