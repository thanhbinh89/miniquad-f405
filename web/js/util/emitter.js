/** Minimal event emitter (browser and Node). */
export class Emitter {
  constructor() { this._ev = new Map(); }

  on(name, fn) {
    if (!this._ev.has(name)) this._ev.set(name, new Set());
    this._ev.get(name).add(fn);
    return () => this.off(name, fn);
  }

  once(name, fn) {
    const off = this.on(name, (...a) => { off(); fn(...a); });
    return off;
  }

  off(name, fn) { this._ev.get(name)?.delete(fn); }

  emit(name, ...args) {
    const set = this._ev.get(name);
    if (!set) return;
    for (const fn of [...set]) {
      try { fn(...args); } catch (e) { console.error(`listener "${name}"`, e); }
    }
  }
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
