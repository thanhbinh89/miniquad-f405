/**
 * Parameter table of the connected board.
 *
 * Descriptors (name/type/flags/limits) are cached per table hash in
 * localStorage, so a reconnect only reads the values (PARAM_GET, 64 per
 * request). Values travel as raw 32-bit words: float bits or int32.
 */
import { C, f32ToU32, u32ToF32, u32ToI32 } from '../proto/proto.js';
import { Emitter } from '../util/emitter.js';
import { GROUPS, metaFor } from './meta.js';

const CACHE_KEY = 'mq.paramdesc.';

function storage() {
  try { return globalThis.localStorage || null; } catch (_) { return null; }
}

export function rawToValue(type, raw) {
  if (type === C.PARAM_T_FLOAT) return u32ToF32(raw);
  if (type === C.PARAM_T_BOOL) return raw !== 0 ? 1 : 0;
  return u32ToI32(raw);
}

export function valueToRaw(type, value) {
  if (type === C.PARAM_T_FLOAT) return f32ToU32(Number(value));
  if (type === C.PARAM_T_BOOL) return value ? 1 : 0;
  return Math.round(Number(value)) >>> 0;
}

/** Shortest decimal that round-trips the float32 (no 0.30000001 in the UI). */
export function fmtFloat(v, maxDecimals = 6) {
  if (!Number.isFinite(v)) return String(v);
  for (let d = 0; d <= maxDecimals; d++) {
    const s = v.toFixed(d);
    if (Math.fround(Number(s)) === Math.fround(v)) return s;
  }
  return v.toFixed(maxDecimals);
}

export class Param {
  constructor(desc) {
    const { value, ...rest } = desc;    // index, name, type, flags, min, max, def (raw)
    Object.assign(this, rest);
    this.raw = value;
    this.savedRaw = value;
    this.meta = metaFor(desc.name);
  }

  get value() { return rawToValue(this.type, this.raw); }
  get minValue() { return rawToValue(this.type, this.min); }
  get maxValue() { return rawToValue(this.type, this.max); }
  get defValue() { return rawToValue(this.type, this.def); }
  get live() { return (this.flags & C.PARAM_F_LIVE) !== 0; }
  get needsReboot() { return (this.flags & C.PARAM_F_REBOOT) !== 0; }
  get readOnly() { return (this.flags & C.PARAM_F_READONLY) !== 0; }
  get isCal() { return (this.flags & C.PARAM_F_CAL) !== 0; }
  get dirty() { return this.raw !== this.savedRaw; }
  get isDefault() { return this.raw === this.def; }

  format(v = this.value) {
    if (this.type === C.PARAM_T_FLOAT) {
      const d = this.meta.extra.decimals;
      return d === undefined ? fmtFloat(v) : Number(v).toFixed(d);
    }
    return String(v);
  }
}

export class ParamTable extends Emitter {
  constructor(device) {
    super();
    this.device = device;
    this.list = [];
    this.byName = new Map();
    this.hash = 0;
    this.loaded = false;
  }

  get dirtyCount() { return this.list.filter((p) => p.dirty).length; }
  get(name) { return this.byName.get(name); }

  groups() {
    return GROUPS.map((g) => ({ ...g, params: this.list.filter((p) => p.meta.group === g.id) }))
      .filter((g) => g.params.length > 0);
  }

  /** Read the whole table (descriptors from cache when the hash matches). */
  async load(onProgress = () => {}) {
    const info = await this.device.paramInfo();
    this.hash = info.tableHash;
    const st = storage();
    let descs = null;
    try {
      const cached = st && st.getItem(CACHE_KEY + info.tableHash.toString(16));
      if (cached) {
        const c = JSON.parse(cached);
        if (Array.isArray(c) && c.length === info.count) descs = c;
      }
    } catch (_) { descs = null; }

    if (descs) {
      for (let first = 0; first < info.count; first += C.PARAM_GET_MAX) {
        const n = Math.min(C.PARAM_GET_MAX, info.count - first);
        const r = await this.device.paramGet(first, n);
        r.values.forEach((v, k) => { descs[first + k].value = v; });
        onProgress(Math.min(info.count, first + n), info.count);
      }
    } else {
      descs = new Array(info.count);
      let done = 0;
      const batch = 4;
      for (let i = 0; i < info.count; i += batch) {
        const jobs = [];
        for (let k = i; k < Math.min(info.count, i + batch); k++) {
          jobs.push(this.device.paramDesc(k).then((d) => {
            descs[k] = { index: d.index, name: d.name, type: d.type, flags: d.flags, value: d.value, min: d.min, max: d.max, def: d.def };
            onProgress(++done, info.count);
          }));
        }
        await Promise.all(jobs);
      }
      try {
        st?.setItem(CACHE_KEY + info.tableHash.toString(16),
          JSON.stringify(descs.map(({ value, ...rest }) => rest)));
      } catch (_) { /* quota: ignore */ }
    }

    this.list = descs.map((d) => new Param(d));
    this.byName = new Map(this.list.map((p) => [p.name, p]));
    this.loaded = true;
    this.emit('loaded', this);
    return this;
  }

  /** Re-read all values (after reset / calibration). */
  async refresh() {
    for (let first = 0; first < this.list.length; first += C.PARAM_GET_MAX) {
      const n = Math.min(C.PARAM_GET_MAX, this.list.length - first);
      const r = await this.device.paramGet(first, n);
      r.values.forEach((v, k) => {
        const p = this.list[first + k];
        p.raw = v;
      });
    }
    this.emit('changed', null);
  }

  /** Set a value; resolves with the value the firmware applied (clamped). */
  async set(name, value) {
    const p = this.byName.get(name);
    if (!p) throw new Error(`Không có tham số ${name}`);
    const r = await this.device.paramSet(p.index, valueToRaw(p.type, value));
    p.raw = r.value;
    this.emit('changed', p);
    return p.value;
  }

  async save() {
    await this.device.paramSave();
    for (const p of this.list) p.savedRaw = p.raw;
    this.emit('saved', this);
  }

  async resetDefaults() {
    await this.device.paramReset();
    await this.refresh();
  }

  markSaved() { for (const p of this.list) p.savedRaw = p.raw; }

  /* ---------------- import / export ---------------- */
  toJSON(meta = {}) {
    const params = {};
    for (const p of this.list) params[p.name] = p.type === C.PARAM_T_FLOAT ? Number(fmtFloat(p.value)) : p.value;
    return { format: 'miniquad-params', version: 1, created: new Date().toISOString(), ...meta, params };
  }

  /**
   * Apply an exported file. Returns {applied, unchanged, unknown, clamped, failed}.
   * Calibration values are skipped unless includeCal.
   */
  async applyJSON(obj, { includeCal = false, onProgress = () => {} } = {}) {
    if (!obj || typeof obj.params !== 'object') throw new Error('File không đúng định dạng (thiếu "params")');
    const res = { applied: [], unchanged: [], unknown: [], clamped: [], failed: [], skipped: [] };
    const entries = Object.entries(obj.params);
    let i = 0;
    for (const [name, value] of entries) {
      onProgress(++i, entries.length);
      const p = this.byName.get(name);
      if (!p) { res.unknown.push(name); continue; }
      if (p.readOnly || (p.isCal && !includeCal)) { res.skipped.push(name); continue; }
      const raw = valueToRaw(p.type, value);
      if (raw === p.raw) { res.unchanged.push(name); continue; }
      try {
        const got = await this.set(name, value);
        if (p.type === C.PARAM_T_FLOAT ? Math.fround(got) !== Math.fround(Number(value)) : got !== Number(value)) res.clamped.push(name);
        else res.applied.push(name);
      } catch (e) {
        res.failed.push(`${name}: ${e.message}`);
      }
    }
    return res;
  }
}
