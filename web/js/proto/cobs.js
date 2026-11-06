/** Consistent Overhead Byte Stuffing, identical to firmware/common/util/cobs.c. */

export function cobsEncode(src) {
  const dst = new Uint8Array(src.length + Math.ceil(src.length / 254) + 1);
  let out = 1;
  let codePos = 0;
  let code = 1;
  for (let i = 0; i < src.length; i++) {
    if (src[i] === 0) {
      dst[codePos] = code;
      codePos = out++;
      code = 1;
    } else {
      dst[out++] = src[i];
      code++;
      if (code === 0xFF) {
        dst[codePos] = code;
        codePos = out++;
        code = 1;
      }
    }
  }
  dst[codePos] = code;
  return dst.subarray(0, out);
}

/** @returns decoded bytes, or null if the input is not valid COBS */
export function cobsDecode(src) {
  const dst = new Uint8Array(src.length);
  let i = 0;
  let out = 0;
  while (i < src.length) {
    const code = src[i++];
    if (code === 0) return null;
    const run = code - 1;
    if (i + run > src.length) return null;
    for (let k = 0; k < run; k++) {
      const b = src[i++];
      if (b === 0) return null;
      dst[out++] = b;
    }
    if (code !== 0xFF && i < src.length) dst[out++] = 0;
  }
  return dst.subarray(0, out);
}
