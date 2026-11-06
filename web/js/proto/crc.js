/**
 * CRC-16/CCITT-FALSE (link frames) and CRC-32/ISO-HDLC (firmware images),
 * identical to firmware/common/util/crc.c.
 */

const CRC32_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c >>> 0;
  }
  return t;
})();

const CRC16_TABLE = (() => {
  const t = new Uint16Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n << 8;
    for (let k = 0; k < 8; k++) c = (c & 0x8000) ? ((c << 1) ^ 0x1021) : (c << 1);
    t[n] = c & 0xFFFF;
  }
  return t;
})();

/** CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xor-out. */
export function crc16(bytes, crc = 0xFFFF) {
  for (let i = 0; i < bytes.length; i++) {
    crc = ((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ bytes[i]) & 0xFF]) & 0xFFFF;
  }
  return crc;
}

/** CRC-32 (zlib / Ethernet). Pass a previous result to continue. */
export function crc32(bytes, prev = 0) {
  let c = (prev ^ 0xFFFFFFFF) >>> 0;
  for (let i = 0; i < bytes.length; i++) c = CRC32_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
