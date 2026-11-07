/**
 * Firmware: what is running, update through our bootloader, recovery and
 * first flashing, last fault record.
 *
 *   board v2 (USB-C): update through the bootloader over USB CDC; first
 *     flash / recovery of the bootloader with an ST-Link on the SWD test
 *     pads TP1..TP5, or the STM32 ROM bootloader in USB DFU mode (desktop
 *     tool: STM32CubeProgrammer / dfu-util).
 *   board v1 (UART J2): update through the bootloader over UART; recovery
 *     with the ROM bootloader over UART (AN3155) right from this page.
 */
import {
  MEM, fetchManifest, fetchManifestFile, loadFirmwareFile, manifestUrl, parseAppImage, updateFirmware, waitForMode,
} from '../proto/firmware.js';
import { BOARDS, C, FAULT_TYPES, versionString } from '../proto/proto.js';
import { ROM_BAUD, Stm32Rom, romFlash } from '../proto/stm32rom.js';
import { sleep } from '../util/emitter.js';
import { SWD_PADS, boardSVG } from './board.js';
import { fmtDuration, h, hex32, panel } from './dom.js';

const CFSR_BITS = [
  [0, 'IACCVIOL: lấy lệnh từ vùng cấm (MPU)'], [1, 'DACCVIOL: truy cập dữ liệu vùng cấm (MPU / stack guard)'],
  [3, 'MUNSTKERR'], [4, 'MSTKERR: tràn stack khi vào ngắt'], [5, 'MLSPERR'], [7, 'MMARVALID: MMFAR hợp lệ'],
  [8, 'IBUSERR: lỗi bus khi lấy lệnh'], [9, 'PRECISERR: lỗi bus dữ liệu (BFAR hợp lệ)'], [10, 'IMPRECISERR: lỗi bus dữ liệu (không chính xác)'],
  [11, 'UNSTKERR'], [12, 'STKERR: lỗi bus khi đẩy stack'], [13, 'LSPERR'], [15, 'BFARVALID: BFAR hợp lệ'],
  [16, 'UNDEFINSTR: lệnh không xác định'], [17, 'INVSTATE: sai trạng thái Thumb'], [18, 'INVPC: PC không hợp lệ khi return'],
  [19, 'NOCP: dùng coprocessor chưa bật (FPU?)'], [24, 'UNALIGNED: truy cập không thẳng hàng'], [25, 'DIVBYZERO: chia cho 0'],
];
const CSR_BITS = [[31, 'mất nguồn thấp'], [30, 'watchdog cửa sổ'], [29, 'watchdog (IWDG)'], [28, 'phần mềm'], [27, 'bật nguồn'], [26, 'chân reset'], [25, 'sụt áp (BOR)']];

const dateOf = (t) => (t ? new Date(t * 1000).toLocaleString('vi-VN') : '–');

export class FirmwareView {
  constructor(app) {
    this.app = app;
    this.title = 'Firmware';
    this.streams = {};
    this.image = null;
    this.recovery = { bootloader: null, app: null };
    this.busy = false;
    this.lines = { fw: [], rec: [] };     /* survive the re-render after a reboot */
    this.prog = { fw: 0, rec: 0 };
  }

  mount(root) {
    const app = this.app;
    const rev = app.boardRev;
    this.rev = rev;
    this.infoKv = h('dl', { class: 'kv' });
    this.imageInfo = h('div');
    this.progress = h('div', { class: 'progress' }, h('div'));
    this.log = h('ul', { class: 'fw-log' });
    this.flashBtn = h('button', { class: 'btn primary', disabled: true, onclick: () => this.flash() }, 'Nạp firmware');
    this.manifestBox = h('div', { class: 'note' }, 'Đang tìm bản build trên máy chủ…');
    const drop = h('div', { class: 'file-drop' },
      h('div', 'Thả file vào đây hoặc'),
      h('div', { class: 'btn-row' }, h('button', { class: 'btn', onclick: () => this.pickLocal() }, 'Chọn file .bin / .hex')),
      h('div', { class: 'note' }, 'miniquad-app.bin (khuyên dùng), miniquad-app.hex hoặc miniquad-full.bin từ thư mục build/…/out.'));
    drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
    drop.addEventListener('dragleave', () => drop.classList.remove('over'));
    drop.addEventListener('drop', (e) => {
      e.preventDefault();
      drop.classList.remove('over');
      const f = e.dataTransfer.files[0];
      if (f) this.useFile(f);
    });

    /* recovery */
    this.recInfo = h('div', { class: 'note' });
    this.recErase = h('input', { type: 'checkbox' });
    this.recVerify = h('input', { type: 'checkbox', checked: true });
    this.recProgress = h('div', { class: 'progress' }, h('div'));
    this.recLog = h('ul', { class: 'fw-log' });
    this.recBtn = h('button', { class: 'btn primary', disabled: true, onclick: () => this.recover() }, 'Nạp khôi phục');

    this.faultBox = h('div', h('p', { class: 'note' }, 'Kết nối ứng dụng để đọc.'));

    this.downloads = h('div', { class: 'btn-row', style: { margin: '10px 0' } });
    root.append(
      h('div', { class: 'view-head' }, h('h1', 'Firmware'),
        h('p', rev === 1
          ? 'Nạp firmware mới qua bootloader của MiniQuad, hoặc khôi phục bằng bootloader ROM của STM32 khi board không còn phản hồi.'
          : 'Nạp firmware mới qua USB-C (bootloader của MiniQuad). Lần nạp bootloader đầu tiên, hoặc khi bootloader hỏng: dùng SWD trên 5 pad test.')),
      h('div', { class: 'grid cols-2' },
        panel('U3', 'đang chạy', this.infoKv,
          h('div', { class: 'btn-row', style: { marginTop: '12px' } },
            h('button', { class: 'btn small', onclick: () => this.refreshInfo() }, 'Đọc lại'),
            h('button', { class: 'btn small', onclick: () => this.reboot(C.REBOOT_NORMAL) }, 'Khởi động lại'),
            h('button', { class: 'btn small', onclick: () => this.reboot(C.REBOOT_BOOTLOADER) }, 'Vào bootloader'))),
        panel('Fault', 'lỗi gần nhất & lý do reset', this.faultBox)),
      h('div', { style: { marginTop: '28px' } }, panel('Cập nhật', rev === 1 ? 'qua bootloader UART (J2)' : 'qua bootloader, cổng USB-C',
        h('div', { class: 'grid cols-2', style: { alignItems: 'start' } },
          h('div', h('h3', { style: { fontSize: 'var(--fs-m)', marginBottom: '8px' } }, 'Bản build mới nhất'), this.manifestBox,
            h('h3', { style: { fontSize: 'var(--fs-m)', margin: '16px 0 8px' } }, 'Hoặc file trên máy'), drop),
          h('div', this.imageInfo, h('div', { class: 'btn-row', style: { margin: '14px 0 10px' } }, this.flashBtn), this.progress, this.log)))),
      ...(rev === 1 ? [this.romPanelV1()] : this.recoveryPanelsV2()),
    );
    for (const [list, key] of [[this.log, 'fw'], [this.recLog, 'rec']]) {
      for (const [t, k] of this.lines[key]) list.append(h('li', { class: k }, t));
    }
    this.progress.firstChild.style.width = `${this.prog.fw * 100}%`;
    this.recProgress.firstChild.style.width = `${this.prog.rec * 100}%`;
    if (this.image) this.setImage(this.image, this.imageName);
    this.updateRecovery();
    this.refreshInfo();
    this.loadManifest();
  }

  unmount() {}

  /* board v1: ROM bootloader over UART, flashed from this page */
  romPanelV1() {
    return h('div', { style: { marginTop: '28px' } }, panel('JP1', 'khôi phục bằng ROM bootloader STM32',
      h('div', { class: 'grid cols-2', style: { alignItems: 'start' } },
        h('div',
          h('p', { class: 'note' }, 'Dùng khi bootloader của MiniQuad bị hỏng, khi nạp lần đầu, hoặc khi board không trả lời. Ghi cả bootloader (0x08000000) và ứng dụng (0x0800C000) qua J2 ở 115200 baud, 8E1.'),
          h('ol', { class: 'note' },
            h('li', 'Nếu board còn trả lời: bấm “Vào ROM bootloader”.'),
            h('li', 'Nếu không: hàn tắt cầu JP1 (BOOT0, mặt dưới, dưới MCU) rồi bấm reset S2 hoặc cắm lại pin. Nạp xong nhớ bỏ cầu JP1.')),
          h('div', { class: 'btn-row', style: { margin: '10px 0' } },
            h('button', { class: 'btn', onclick: () => this.enterRom() }, 'Vào ROM bootloader'),
            h('button', { class: 'btn', onclick: () => this.pickRecovery() }, 'Chọn file…'),
            h('button', { class: 'btn', onclick: () => this.recoveryFromServer() }, 'Dùng bản build mới nhất')),
          this.recInfo,
          h('label', { class: 'check', style: { marginTop: '10px' } }, this.recErase, 'Xoá luôn cấu hình (sector 1–2)'),
          h('label', { class: 'check', style: { marginTop: '6px' } }, this.recVerify, 'Đọc lại để kiểm tra sau khi ghi')),
        h('div', h('div', { class: 'btn-row', style: { marginBottom: '10px' } }, this.recBtn), this.recProgress, this.recLog,
          h('div', { style: { maxWidth: '300px', marginTop: '12px' } }, boardSVG({ rev: 1, focus: 'uart', scale: 0.7 }))))));
  }

  /* board v2: SWD pads (first flash) and ROM USB DFU, both with desktop tools */
  recoveryPanelsV2() {
    const code = (t) => h('code', t);
    return [
      h('div', { style: { marginTop: '28px' } }, panel('SWD', 'nạp bootloader lần đầu / khôi phục bằng ST-Link',
        h('div', { class: 'grid cols-2', style: { alignItems: 'start' } },
          h('div',
            h('p', { class: 'note' }, 'Board mới hàn xong chưa có bootloader nên chưa có cổng USB. Nạp một lần miniquad-full.hex (bootloader + ứng dụng, giữ nguyên cấu hình) qua 5 pad SWD ở mặt dưới; từ đó mọi lần cập nhật đều qua USB-C ở phần trên.'),
            h('ol', { class: 'note' },
              h('li', 'Cấp nguồn cho board: cáp USB-C (đủ cho MCU) hoặc pin 1S.'),
              h('li', 'Nối ST-Link (V2/V3; kim test, pogo pin hoặc hàn dây tạm):',
                h('dl', { class: 'kv', style: { maxWidth: '300px', margin: '6px 0' } },
                  ...SWD_PADS.flatMap(([n, net, st, silk]) => [h('dt', `${n} “${silk}” (${net})`), h('dd', { class: 'text' }, st)]))),
              h('li', 'Windows + STM32CubeCLT: ', code('powershell -ExecutionPolicy Bypass -File tools\\flash.ps1 -Via swd'),
                '. Hoặc STM32CubeProgrammer → ST-LINK, mode “Under reset” → Connect → mở miniquad-full.hex → Download.'),
              h('li', 'Tháo ST-Link, cắm USB-C và bấm “Kết nối”. LED nháy nhanh = bootloader đang chờ (chưa có ứng dụng).')),
            this.downloads),
          h('div', { style: { maxWidth: '360px' } }, boardSVG({ rev: 2, focus: 'swd', scale: 0.75 }),
            h('p', { class: 'note' }, 'Vị trí theo layout final 07/11/2026, nhìn từ mặt trên; pad nằm ở mặt dưới, dưới vùng MCU, cạnh J2. Lật board thì thứ tự ngược lại (GND … RST); tên in dưới mỗi pad.'))))),
      h('div', { style: { marginTop: '28px' } }, panel('DFU', 'ROM bootloader STM32 qua USB (dự phòng)',
        h('div',
          h('p', { class: 'note' }, 'Không có ST-Link: bootloader ROM trong STM32F405 có chế độ USB DFU trên chính cổng USB-C. Trang web không nạp được DFU; dùng công cụ trên máy tính.'),
          h('ol', { class: 'note' },
            h('li', 'Rút bộ thu khỏi J3. Board còn trả lời: bấm “Vào ROM bootloader (DFU)”. Không trả lời: nối JP1 (0 Ω, BOOT0 lên 3,3 V, mặt dưới) rồi bấm reset S2.'),
            h('li', 'STM32CubeProgrammer → USB → Refresh → Connect, nạp miniquad-full.hex; hoặc ', code('STM32_Programmer_CLI -c port=USB1 -w miniquad-full.hex -v -rst'),
              ', hoặc ', code('tools\\flash.ps1 -Via dfu'), '.'),
            h('li', 'Gỡ JP1 (nếu đã nối) rồi reset để chạy lại bootloader MiniQuad.')),
          h('p', { class: 'note warn' }, 'Chưa kiểm chứng trên board v2. ROM cần thạch anh 8 MHz chạy tốt, và nó dò cả cổng UART: PA10 để trống, J3.4 (PC11) không có điện trở kéo, nhiễu hoặc xung PPM có thể làm ROM chọn nhầm UART. Vào bằng nút bên dưới thì bootloader MiniQuad bật điện trở kéo lên trong chip cho hai chân này trước; vào bằng JP1 thì không. Không thấy “STM32 BOOTLOADER”: reset vào lại, hoặc dùng SWD.'),
          h('div', { class: 'btn-row', style: { marginTop: '10px' } },
            h('button', { class: 'btn', onclick: () => this.enterRom() }, 'Vào ROM bootloader (DFU)'))))),
    ];
  }

  renderDownloads() {
    const m = this.manifest;
    if (!this.downloads) return;
    if (!m) {
      this.downloads.replaceChildren(h('span', { class: 'note' }, 'File nằm trong build/release/out sau khi build (miniquad-full.hex).'));
      return;
    }
    const files = [['full_hex', 'miniquad-full.hex'], ['bootloader', 'miniquad-bootloader.bin'], ['app', 'miniquad-app.bin']];
    this.downloads.replaceChildren(...files.filter(([k]) => m.files?.[k]).map(([k, label]) =>
      h('a', { class: 'btn small', href: m.baseUrl + m.files[k].file, download: m.files[k].file }, `Tải ${label}`)));
  }

  onHello() { this.refreshInfo(); }

  /* ---------------- info ---------------- */
  async refreshInfo() {
    const app = this.app;
    const hl = app.hello;
    if (!hl) {
      this.infoKv.replaceChildren(h('dt', 'Thiết bị'), h('dd', { class: 'text' }, app.connected ? 'Không trả lời (ROM bootloader? sai dây?)' : 'Chưa kết nối'));
      return;
    }
    const inBl = hl.mode === C.DEV_MODE_BOOTLOADER;
    this.infoKv.replaceChildren(
      h('dt', 'Đang chạy'), h('dd', { class: 'text' }, inBl ? 'Bootloader' : 'Ứng dụng'),
      h('dt', inBl ? 'Phiên bản bootloader' : 'Phiên bản'), h('dd', `v${versionString(hl.fwVersion)}`),
      h('dt', 'Ứng dụng trong flash'), h('dd', { class: 'text' }, hl.appValid ? `v${versionString(hl.appVersion)} (hợp lệ)` : 'không hợp lệ / trống'),
      h('dt', 'Git'), h('dd', { class: 'text' }, hl.gitHash || '–'),
      h('dt', 'Thời điểm build'), h('dd', { class: 'text' }, dateOf(hl.buildTime)),
      h('dt', 'Board'), h('dd', { class: 'text' }, `${hl.boardName} (id ${hl.boardId})`),
      h('dt', 'UID'), h('dd', { class: 'text' }, hl.uidHex),
      h('dt', 'Vùng ứng dụng'), h('dd', `${(hl.appMaxSize / 1024).toFixed(0)} KB`),
    );
    if (!inBl) this.readFault();
    else this.faultBox.replaceChildren(h('p', { class: 'note' }, 'Bootloader không giữ bản ghi lỗi; khởi động ứng dụng để đọc.'));
  }

  async readFault() {
    try {
      const f = await this.app.device.faultInfo();
      const csr = CSR_BITS.filter(([b]) => f.resetCsr & (1 << b)).map(([, t]) => t);
      const kv = h('dl', { class: 'kv' },
        h('dt', 'Lý do reset gần nhất'), h('dd', { class: 'text' }, csr.length ? csr.join(', ') : '–'),
        h('dt', 'Số lần khởi động'), h('dd', String(f.bootCount)));
      if (!f.valid) {
        this.faultBox.replaceChildren(kv, h('p', { class: 'note' }, 'Không có lỗi nào được ghi trước lần reset gần nhất.'));
        return;
      }
      const bits = CFSR_BITS.filter(([b]) => f.cfsr & (1 << b)).map(([, t]) => h('li', t));
      if (f.hfsr & (1 << 30)) bits.push(h('li', 'HFSR.FORCED: lỗi được nâng lên HardFault'));
      if (f.hfsr & (1 << 1)) bits.push(h('li', 'HFSR.VECTTBL: lỗi đọc bảng vector'));
      this.faultBox.replaceChildren(
        h('div', { class: 'callout err', style: { marginBottom: '12px' } }, `${FAULT_TYPES[f.type] || 'Lỗi'} sau ${fmtDuration(f.uptimeMs)} chạy; motor đã bị tắt và MCU tự reset.`),
        kv,
        h('dl', { class: 'kv', style: { marginTop: '10px' } },
          h('dt', 'PC'), h('dd', hex32(f.pc)), h('dt', 'LR'), h('dd', hex32(f.lr)), h('dt', 'xPSR'), h('dd', hex32(f.psr)),
          h('dt', 'CFSR'), h('dd', hex32(f.cfsr)), h('dt', 'HFSR'), h('dd', hex32(f.hfsr)),
          ...(f.cfsr & (1 << 7) ? [h('dt', 'MMFAR'), h('dd', hex32(f.mmfar))] : []),
          ...(f.cfsr & (1 << 15) ? [h('dt', 'BFAR'), h('dd', hex32(f.bfar))] : [])),
        ...(bits.length ? [h('ul', { class: 'note' }, bits)] : []),
        h('p', { class: 'note' }, 'Tra dòng code: ', h('code', `arm-none-eabi-addr2line -e miniquad-app.elf ${hex32(f.pc)} ${hex32(f.lr)}`)),
      );
    } catch (e) {
      this.faultBox.replaceChildren(h('p', { class: 'note' }, `Không đọc được: ${e.message}`));
    }
  }

  async reboot(target) {
    const app = this.app;
    await app.run(this.rev === 1 ? null : 'Khởi động lại, chờ cổng USB…', () => app.expectReconnect(async () => {
      await app.device.reboot(target);
      await sleep(200);
      const hl = await waitForMode(app.device, target === C.REBOOT_BOOTLOADER ? C.DEV_MODE_BOOTLOADER : C.DEV_MODE_APP);
      await app.onHello(hl);
    }));
  }

  /* ---------------- image selection ---------------- */
  async loadManifest() {
    try {
      this.manifest = await fetchManifest(manifestUrl(this.rev));
      const m = this.manifest;
      this.renderDownloads();
      this.manifestBox.replaceChildren(
        h('dl', { class: 'kv' },
          h('dt', 'Phiên bản'), h('dd', `v${m.version}`),
          h('dt', 'Git'), h('dd', { class: 'text' }, m.git),
          h('dt', 'Build lúc'), h('dd', { class: 'text' }, dateOf(m.build_time)),
          h('dt', 'Kích thước'), h('dd', `${(m.files.app.size / 1024).toFixed(1)} KB`)),
        h('div', { class: 'btn-row', style: { marginTop: '10px' } },
          h('button', { class: 'btn', onclick: () => this.useServer() }, 'Dùng bản này')));
    } catch (e) {
      this.manifest = null;
      this.renderDownloads();
      this.manifestBox.textContent = 'Trang này chưa kèm bản build (chỉ có trên GitHub Pages do CI deploy). Hãy chọn file từ máy.';
    }
  }

  async useServer() {
    await this.app.run('Đang tải firmware…', async () => {
      const f = await fetchManifestFile(this.manifest, 'app');
      this.setImage(parseAppImage(f.bytes), `${f.name} (máy chủ)`);
    });
  }

  async pickLocal() {
    const { pickFile } = await import('./dom.js');
    const f = await pickFile('.bin,.hex');
    if (f) this.useFile(f);
  }

  async useFile(file) {
    try {
      const res = loadFirmwareFile(file.name, await file.arrayBuffer());
      this.setImage(res.app, file.name);
      if (res.bootloader) this.recovery = { bootloader: { bytes: res.bootloader, name: `${file.name} (bootloader)` }, app: res.app.ok ? { bytes: res.app.bytes, name: file.name } : null };
      this.updateRecovery();
    } catch (e) {
      this.app.toast(`${file.name}: ${e.message}`, 'err');
    }
  }

  setImage(img, name) {
    this.image = img;
    this.imageName = name;
    const hd = img.header;
    const hl = this.app.hello;
    const same = hl && hd && hl.appValid && hl.appVersion === hd.fwVersion;
    const other = hl && hd && img.ok && hl.boardId !== hd.boardId;
    this.imageInfo.replaceChildren(
      h('h3', { style: { fontSize: 'var(--fs-m)', marginBottom: '8px' } }, name),
      img.ok ? h('dl', { class: 'kv' },
        h('dt', 'Phiên bản'), h('dd', `v${hd.version}`),
        h('dt', 'Git'), h('dd', { class: 'text' }, hd.gitHash || '–'),
        h('dt', 'Build lúc'), h('dd', { class: 'text' }, dateOf(hd.buildTime)),
        h('dt', 'Kích thước'), h('dd', `${img.total} byte`),
        h('dt', 'Board'), h('dd', { class: 'text' }, BOARDS[hd.boardId]?.name || hd.boardName))
        : h('div', { class: 'callout err' }, img.errors.join('. ')),
      ...(same ? [h('p', { class: 'note' }, 'Board đang chạy đúng phiên bản này.')] : []),
      ...(other ? [h('div', { class: 'callout err', style: { marginTop: '8px' } },
        `File này dành cho ${BOARDS[hd.boardId]?.name || 'board khác'}, board đang nối là ${BOARDS[hl.boardId]?.name || hl.boardName}.`)] : []),
    );
    this.flashBtn.disabled = !img.ok || !!other;
  }

  logLine(list, text, kind = '') {
    const key = list === this.recLog ? 'rec' : 'fw';
    this.lines[key].push([text, kind]);
    const ul = key === 'rec' ? this.recLog : this.log;     /* the current DOM */
    ul.append(h('li', { class: kind }, text));
    ul.scrollTop = ul.scrollHeight;
  }

  /* ---------------- update through our bootloader ---------------- */
  async flash() {
    const app = this.app;
    if (!this.image?.ok || this.busy) return;
    if (!app.hello) { app.toast(this.rev === 1 ? 'Board không trả lời. Dùng phần khôi phục ROM bên dưới.' : 'Board không trả lời. Xem phần SWD / DFU bên dưới.', 'err'); return; }
    if (app.armed) { app.toast('Đang ARM: hạ cánh và DISARM trước.', 'err'); return; }
    const ok = await app.confirm('Nạp firmware', `Ghi v${this.image.header.version} (${this.image.total} byte). Cấu hình được giữ nguyên. Không rút cáp hay pin trong khi nạp.`, 'Nạp firmware');
    if (!ok) return;
    this.busy = true;
    this.flashBtn.disabled = true;
    this.log.replaceChildren();
    this.lines.fw = [];
    app.setBusy(true);
    try {
      const res = await app.expectReconnect(() => updateFirmware(app.device, this.image, {
        log: (t, k) => this.logLine(this.log, t, k),
        progress: (f) => { this.prog.fw = f; this.progress.firstChild.style.width = `${f * 100}%`; },
      }));
      app.toast(`Đã nạp v${versionString(res.appVersion)}`, 'ok');
      await app.onHello(res);
    } catch (e) {
      this.logLine(this.log, e.message, 'err');
      app.toast(`Nạp lỗi: ${e.message}`, 'err', 8000);
      try { await app.onHello(await app.device.hello()); } catch (_) { /* board in bootloader or gone */ }
    } finally {
      this.busy = false;
      this.flashBtn.disabled = !this.image?.ok;
      app.setBusy(false);
    }
  }

  /* ---------------- ROM bootloader recovery ---------------- */
  async pickRecovery() {
    const { pickFile } = await import('./dom.js');
    const f = await pickFile('.bin,.hex');
    if (!f) return;
    try {
      const res = loadFirmwareFile(f.name, await f.arrayBuffer());
      if (res.bootloader) this.recovery.bootloader = { bytes: res.bootloader, name: `${f.name} (bootloader)` };
      if (res.app?.ok) this.recovery.app = { bytes: res.app.bytes, name: f.name };
      else if (/bootloader/i.test(f.name)) this.recovery.bootloader = { bytes: new Uint8Array(await f.arrayBuffer()), name: f.name };
      else this.app.toast(`${f.name}: ${res.app.errors.join('. ')}`, 'err');
    } catch (e) {
      if (/bootloader/i.test(f.name) && /\.bin$/i.test(f.name)) this.recovery.bootloader = { bytes: new Uint8Array(await f.arrayBuffer()), name: f.name };
      else this.app.toast(`${f.name}: ${e.message}`, 'err');
    }
    this.updateRecovery();
  }

  async recoveryFromServer() {
    await this.app.run('Đang tải firmware…', async () => {
      if (!this.manifest) this.manifest = await fetchManifest();
      const bl = await fetchManifestFile(this.manifest, 'bootloader');
      const ap = await fetchManifestFile(this.manifest, 'app');
      this.recovery = { bootloader: { bytes: bl.bytes, name: bl.name }, app: { bytes: ap.bytes, name: ap.name } };
      this.updateRecovery();
    });
  }

  updateRecovery() {
    const r = this.recovery;
    const items = [];
    if (r.bootloader) items.push(`Bootloader: ${r.bootloader.name}, ${r.bootloader.bytes.length} byte`);
    if (r.app) items.push(`Ứng dụng: ${r.app.name}, ${r.app.bytes.length} byte`);
    this.recInfo.replaceChildren(...(items.length ? items.map((t) => h('div', t)) : [h('div', 'Chưa chọn file. Cần miniquad-bootloader.bin và miniquad-app.bin (hoặc miniquad-full.bin).')]));
    this.recBtn.disabled = !(r.bootloader && r.app) || !this.app.connected;
  }

  async enterRom() {
    const app = this.app;
    if (!app.hello) { app.toast('Board không trả lời: dùng cầu JP1 (BOOT0) rồi reset.', 'err'); return; }
    if (this.rev !== 1) {
      const ok = await app.confirm('Vào ROM bootloader (DFU)',
        'Board sẽ rời cổng serial và hiện ra dưới tên “STM32 BOOTLOADER” (USB DFU) cho STM32CubeProgrammer / dfu-util. Trang này mất kết nối cho tới khi bạn reset board.', 'Vào DFU');
      if (!ok) return;
    }
    await app.run('Chuyển sang ROM bootloader…', async () => {
      await app.device.reboot(C.REBOOT_ROM);
      await sleep(200);
      app.hello = null;
      app.render();
      app.toast(this.rev === 1 ? 'Board đang ở ROM bootloader. Chọn file rồi bấm “Nạp khôi phục”.'
        : 'Board đang ở ROM bootloader (USB DFU). Nạp bằng STM32CubeProgrammer, xong bấm reset S2.', 'ok', 8000);
    });
  }

  async recover() {
    const app = this.app;
    const r = this.recovery;
    if (!r.bootloader || !r.app || this.busy) return;
    const ok = await app.confirm('Nạp khôi phục', `Xoá và ghi lại bootloader + ứng dụng${this.recErase.checked ? ', xoá cả cấu hình,' : ''} qua ROM bootloader. Không rút cáp hay pin trong khi nạp.`, 'Nạp khôi phục', true);
    if (!ok) return;
    this.busy = true;
    this.recBtn.disabled = true;
    app.setBusy(true);
    this.recLog.replaceChildren();
    this.lines.rec = [];
    const log = (t, k) => this.logLine(this.recLog, t, k);
    const rom = new Stm32Rom(app.device);
    try {
      if (app.hello) {
        log('Board đang trả lời: chuyển sang ROM bootloader (REBOOT 2)…');
        await app.device.reboot(C.REBOOT_ROM);
        await sleep(300);
        app.hello = null;
      }
      log(`Mở lại cổng ở ${ROM_BAUD} baud, 8E1…`);
      await app.reopen({ baudRate: ROM_BAUD, parity: 'even' });
      rom.attach();
      await rom.sync();
      const id = await rom.getId();
      log(`Đã đồng bộ ROM bootloader, chip 0x${id.toString(16).toUpperCase()}`);
      if (id !== 0x413) throw new Error(`Chip 0x${id.toString(16)} không phải STM32F405/407`);
      await romFlash(rom, [
        { address: MEM.BL_BASE, bytes: r.bootloader.bytes, name: r.bootloader.name },
        { address: MEM.APP_BASE, bytes: r.app.bytes, name: r.app.name },
      ], { eraseConfig: this.recErase.checked, verify: this.recVerify.checked, log, progress: (f) => { this.prog.rec = f; this.recProgress.firstChild.style.width = `${f * 100}%`; } });
      rom.detach();
      log('Mở lại cổng ở 460800 baud…');
      await app.reopen({ baudRate: app.baud, parity: 'none' });
      await sleep(500);
      const hl = await waitForMode(app.device, C.DEV_MODE_APP, 6000).catch(() => null);
      if (hl) {
        log(`Ứng dụng v${versionString(hl.appVersion)} đang chạy`, 'ok');
        await app.onHello(hl);
      } else {
        log('Đã ghi xong. Nếu đang nối JP1 (BOOT0), bỏ cầu rồi reset board.', 'ok');
      }
    } catch (e) {
      rom.detach();
      log(e.message, 'err');
      app.toast(`Khôi phục lỗi: ${e.message}`, 'err', 8000);
      await app.reopen({ baudRate: app.baud, parity: 'none' }).catch(() => {});
    } finally {
      this.busy = false;
      app.setBusy(false);
      this.updateRecovery();
    }
  }
}
