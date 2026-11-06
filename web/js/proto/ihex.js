/** Intel HEX -> {base, data} (gaps filled with 0xFF). */
export function parseIntelHex(text) {
  const chunks = [];
  let upper = 0;
  let lo = Infinity;
  let hi = 0;
  const lines = text.split(/\r?\n/);
  for (let n = 0; n < lines.length; n++) {
    const line = lines[n].trim();
    if (!line) continue;
    if (line[0] !== ':') throw new Error(`HEX dòng ${n + 1}: thiếu ':'`);
    const bytes = new Uint8Array((line.length - 1) / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(line.substr(1 + 2 * i, 2), 16);
    const len = bytes[0];
    if (bytes.length !== len + 5) throw new Error(`HEX dòng ${n + 1}: sai độ dài`);
    const sum = bytes.reduce((a, b) => (a + b) & 0xFF, 0);
    if (sum !== 0) throw new Error(`HEX dòng ${n + 1}: sai checksum`);
    const addr = (bytes[1] << 8) | bytes[2];
    const type = bytes[3];
    const data = bytes.subarray(4, 4 + len);
    if (type === 0x00) {
      const a = upper + addr;
      chunks.push([a, data.slice()]);
      lo = Math.min(lo, a);
      hi = Math.max(hi, a + len);
    } else if (type === 0x01) {
      break;
    } else if (type === 0x04) {
      upper = ((data[0] << 8) | data[1]) * 0x10000;
    } else if (type === 0x02) {
      upper = ((data[0] << 8) | data[1]) * 16;
    }
  }
  if (!chunks.length) throw new Error('HEX không có dữ liệu');
  const out = new Uint8Array(hi - lo).fill(0xFF);
  for (const [a, d] of chunks) out.set(d, a - lo);
  return { base: lo >>> 0, data: out };
}
