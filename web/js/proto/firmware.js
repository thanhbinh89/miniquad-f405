/**
 * Firmware images and the update procedure through our bootloader
 * (firmware/bootloader, docs/bootloader.md):
 *
 *   app  --REBOOT(1)-->  bootloader  --ERASE/WRITE.../VERIFY/BOOT-->  app
 *
 * Image = 512-byte header (firmware/common/app_header.h) + application,
 * written from MM_APP_BASE (0x0800C000).
 */
import { crc32 } from './crc.js';
import { parseIntelHex } from './ihex.js';
import { BOARDS, C, versionString } from './proto.js';
import { sleep } from '../util/emitter.js';

export const MEM = {
  FLASH_BASE: 0x08000000,
  BL_BASE: 0x08000000,
  BL_SIZE: 0x4000,
  CFG_A_BASE: 0x08004000,
  CFG_B_BASE: 0x08008000,
  APP_BASE: 0x0800C000,
  APP_HEADER_SIZE: 0x200,
  APP_MAX_SIZE: 0xF4000,
};

export const APP_MAGIC = 0x5041514D;           // "MQAP"
const HDR_CRC_OFFSET = 0x1FC;

/** STM32F405 sector map (4 x 16 KB, 64 KB, 7 x 128 KB). */
export const SECTORS = (() => {
  const kb = [16, 16, 16, 16, 64, 128, 128, 128, 128, 128, 128, 128];
  let a = MEM.FLASH_BASE;
  return kb.map((k, i) => {
    const s = { index: i, base: a, size: k * 1024 };
    a += k * 1024;
    return s;
  });
})();

export function sectorsFor(address, length) {
  return SECTORS.filter((s) => s.base < address + length && s.base + s.size > address).map((s) => s.index);
}

/** Typical erase time of the F405 (datasheet t_ERASE, x32). */
export const eraseTimeMs = (sectorIndexes) =>
  sectorIndexes.reduce((t, i) => t + (SECTORS[i].size <= 16384 ? 500 : SECTORS[i].size <= 65536 ? 1100 : 2000), 0);

const cstr = (bytes) => {
  let end = bytes.indexOf(0);
  if (end < 0) end = bytes.length;
  return new TextDecoder().decode(bytes.subarray(0, end));
};

/** Parse and validate an application image. */
export function parseAppImage(bytes) {
  const res = { ok: false, errors: [], bytes, header: null };
  if (bytes.length < MEM.APP_HEADER_SIZE + 8) {
    res.errors.push('File quá ngắn');
    return res;
  }
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u32 = (o) => dv.getUint32(o, true);
  const h = {
    magic: u32(0x00), headerVersion: u32(0x04), headerSize: u32(0x08), imageOffset: u32(0x0C),
    imageSize: u32(0x10), imageCrc: u32(0x14), fwVersion: u32(0x18), buildTime: u32(0x1C),
    boardId: u32(0x20), flags: u32(0x24),
    gitHash: cstr(bytes.subarray(0x28, 0x38)), boardName: cstr(bytes.subarray(0x38, 0x48)),
    fwName: cstr(bytes.subarray(0x48, 0x58)), headerCrc: u32(HDR_CRC_OFFSET),
  };
  res.header = h;
  h.version = versionString(h.fwVersion);
  if (h.magic !== APP_MAGIC) {
    res.errors.push('Không phải image ứng dụng MiniQuad (sai magic)');
    return res;
  }
  if (crc32(bytes.subarray(0, HDR_CRC_OFFSET)) !== h.headerCrc) res.errors.push('Header CRC sai');
  if (h.imageOffset !== MEM.APP_HEADER_SIZE || h.headerSize !== MEM.APP_HEADER_SIZE) res.errors.push('Header không hỗ trợ');
  const total = h.imageOffset + h.imageSize;
  if (h.imageSize < 8 || total > bytes.length) res.errors.push('Kích thước image sai / file bị cắt');
  else if (crc32(bytes.subarray(h.imageOffset, total)) !== h.imageCrc) res.errors.push('CRC image sai');
  if (total > MEM.APP_MAX_SIZE) res.errors.push('Image lớn hơn vùng flash ứng dụng');
  if (!BOARDS[h.boardId]) res.errors.push(`Image cho board lạ (id ${h.boardId})`);
  h.board = BOARDS[h.boardId] || null;
  res.total = total;
  res.ok = res.errors.length === 0;
  if (res.ok) res.bytes = bytes.subarray(0, total);
  return res;
}

/**
 * Accept what the build produces: miniquad-app.bin, miniquad-app.hex,
 * miniquad-full.bin / .hex. Returns {app (parseAppImage result), bootloader?}
 */
export function loadFirmwareFile(name, buffer) {
  let bytes = new Uint8Array(buffer);
  let base = MEM.APP_BASE;
  if (/\.hex$/i.test(name)) {
    const hex = parseIntelHex(new TextDecoder().decode(bytes));
    bytes = hex.data;
    base = hex.base;
  } else if (bytes.length > MEM.APP_BASE - MEM.FLASH_BASE + 8 && new DataView(bytes.buffer, bytes.byteOffset).getUint32(0, true) !== APP_MAGIC) {
    base = MEM.FLASH_BASE;                     // flat image from 0x08000000 (miniquad-full.bin)
  }
  const out = {};
  if (base === MEM.FLASH_BASE) {
    out.bootloader = bytes.subarray(0, Math.min(bytes.length, MEM.BL_SIZE));
    bytes = bytes.subarray(MEM.APP_BASE - MEM.FLASH_BASE);
  } else if (base !== MEM.APP_BASE) {
    throw new Error(`Địa chỉ nạp 0x${base.toString(16)} không phải vùng ứng dụng`);
  }
  out.app = parseAppImage(bytes);
  return out;
}

/** Where CI publishes the latest build for each board revision. */
export const manifestUrl = (boardRev) => (boardRev === 1 ? 'firmware/v1/' : 'firmware/');

/** The latest CI build published next to the page (firmware/manifest.json). */
export async function fetchManifest(baseUrl = 'firmware/') {
  const res = await fetch(baseUrl + 'manifest.json', { cache: 'no-cache' });
  if (!res.ok) throw new Error(`Không có firmware trên máy chủ (${res.status})`);
  const m = await res.json();
  m.baseUrl = baseUrl;
  return m;
}

export async function fetchManifestFile(manifest, which) {
  const f = manifest.files?.[which];
  if (!f) throw new Error(`manifest không có "${which}"`);
  const res = await fetch(manifest.baseUrl + f.file, { cache: 'no-cache' });
  if (!res.ok) throw new Error(`Tải ${f.file} lỗi (${res.status})`);
  const bytes = new Uint8Array(await res.arrayBuffer());
  if (bytes.length !== f.size) throw new Error(`${f.file}: sai kích thước`);
  if (f.crc32 && crc32(bytes) !== Number(f.crc32)) throw new Error(`${f.file}: sai CRC`);
  return { bytes, address: Number(f.address), name: f.file };
}

/* ------------------------------------------------------------------------ */
/* update through our bootloader                                            */
/* ------------------------------------------------------------------------ */

/**
 * Wait until HELLO reports @p mode (after a reboot). Board v2 needs a
 * moment: the USB port disappears with the reset and the host enumerates
 * it again (about 1 s, longer the very first time on Windows).
 */
export const REBOOT_TIMEOUT_MS = 12000;
export async function waitForMode(device, mode, timeoutMs = REBOOT_TIMEOUT_MS) {
  const t0 = Date.now();
  let last = null;
  while (Date.now() - t0 < timeoutMs) {
    try {
      const h = await device.hello({ timeout: 150, retries: 0 });
      if (h.mode === mode) return h;
      last = h;
    } catch (_) { /* resetting */ }
    await sleep(100);
  }
  throw new Error(mode === C.DEV_MODE_BOOTLOADER
    ? 'Không vào được bootloader' + (last ? ' (vẫn ở ứng dụng)' : '')
    : 'Ứng dụng không khởi động sau khi nạp');
}

/**
 * Flash @p image (parseAppImage result) through the bootloader.
 * @param log      (text, level) => void
 * @param progress (fraction 0..1, phase) => void
 */
export async function updateFirmware(device, image, { log = () => {}, progress = () => {}, signal } = {}) {
  if (!image.ok) throw new Error(image.errors.join('; '));
  const bytes = image.bytes;
  const check = () => { if (signal?.aborted) throw new Error('Đã huỷ'); };

  let h = await device.hello();
  if (h.boardId !== image.header.boardId) {
    const want = BOARDS[image.header.boardId]?.name || `id ${image.header.boardId}`;
    const have = BOARDS[h.boardId]?.name || `id ${h.boardId}`;
    throw new Error(`Image dành cho ${want}, board đang nối là ${have}`);
  }
  if (h.mode === C.DEV_MODE_APP) {
    log('Khởi động lại vào bootloader…');
    await device.reboot(C.REBOOT_BOOTLOADER);
    await sleep(150);
    if (device.reconnecting) log('Board khởi động lại, chờ cổng USB của bootloader…');
    h = await waitForMode(device, C.DEV_MODE_BOOTLOADER);
  }
  log(`Bootloader v${versionString(h.fwVersion)} – ứng dụng hiện tại: ${h.appValid ? 'v' + versionString(h.appVersion) : 'không hợp lệ'}`);
  if (bytes.length > h.appMaxSize) throw new Error('Image lớn hơn vùng flash');
  check();

  const sectors = sectorsFor(MEM.APP_BASE, bytes.length);
  log(`Xoá ${sectors.length} sector (${(bytes.length / 1024).toFixed(1)} KB)…`);
  progress(0, 'erase');
  await device.blErase(bytes.length, eraseTimeMs(sectors) + 1500);

  const chunk = C.BL_WRITE_MAX;
  const t0 = Date.now();
  for (let off = 0; off < bytes.length; off += chunk) {
    check();
    const n = Math.min(chunk, bytes.length - off);
    const data = new Uint8Array((n + 3) & ~3).fill(0xFF);
    data.set(bytes.subarray(off, off + n));
    await device.blWrite(off, data);
    progress((off + n) / bytes.length, 'write');
  }
  const secs = (Date.now() - t0) / 1000;
  log(`Đã ghi ${bytes.length} byte trong ${secs.toFixed(1)} s (${(bytes.length / 1024 / secs).toFixed(1)} KB/s)`);

  const v = await device.blVerify();
  if (!v.valid || v.crcActual !== v.crcExpected) {
    throw new Error(`Kiểm tra thất bại (CRC 0x${v.crcActual.toString(16)} ≠ 0x${v.crcExpected.toString(16)})`);
  }
  log(`Kiểm tra OK – v${versionString(v.appVersion)}, CRC 0x${v.crcActual.toString(16).toUpperCase()}`);
  progress(1, 'verify');

  await device.blBoot();
  await sleep(100);
  const app = await waitForMode(device, C.DEV_MODE_APP);
  log(`Ứng dụng đang chạy: v${versionString(app.fwVersion)} (${app.gitHash})`, 'ok');
  return app;
}
