# MiniQuad-F405

[![CI](https://github.com/thanhbinh89/miniquad-f405/actions/workflows/ci.yml/badge.svg)](https://github.com/thanhbinh89/miniquad-f405/actions/workflows/ci.yml)

Firmware bare-metal và trang cấu hình web cho flight controller MiniQuad-F405: STM32F405RGT6, IMU ICM-42688-P, cảm biến khoảng cách VL53L1 (ToF), 4 motor coreless (brushed) qua MOSFET, pin 1S, thu PPM.

Hai phiên bản board dùng chung mã nguồn (tuỳ chọn CMake `MQ_BOARD_REV`):

| | Board v2 (mặc định) | Board v1 |
|---|---|---|
| J2 | **USB-C** (USB 2.0 full speed của chính STM32) | header 7 chân SWD + UART1 |
| Console, cấu hình, nạp code | USB CDC (cổng COM ảo, không cần driver) | USART1 qua mạch USB-UART rời, 460800 baud |
| Nạp bootloader lần đầu | ST-LINK vào 5 pad SWD TP1–TP5 (mặt dưới) | ST-LINK vào J2, hoặc ROM bootloader qua UART |
| Nguồn khi cắm máy tính | VBUS → LDO U7 3,3 V: chạy được không cần pin (motor vẫn cần pin) | lấy 3,3 V từ J2 |

- **Không HAL, không RTOS.** CMSIS + driver tự viết, mọi luồng dữ liệu lớn chạy bằng DMA (IMU SPI, ToF I2C, ADC, UART của board v1); USB OTG FS (CDC ACM) viết bare-metal, chạy bằng ngắt.
- **Vòng điều khiển 1 kHz** khởi chạy từ chân ngắt data-ready của IMU: lọc gyro (notch + LPF), ước lượng tư thế Mahony, PID tốc độ góc kiểu Betaflight, tự cân bằng (ANGLE), giữ độ cao bằng ToF + bộ lọc Kalman (ALT HOLD), bù lực đẩy theo điện áp pin, tuyến tính hoá lực đẩy.
- **An toàn:** chặn ARM khi chưa đủ điều kiện, failsafe tự hạ cánh, phát hiện va chạm, watchdog, giám sát vòng điều khiển bị treo, bản ghi HardFault, bảo vệ tràn stack bằng MPU.
- **Còi + LED** báo trạng thái, pin yếu, failsafe, tìm quad sau va chạm.
- **Bootloader** riêng (16 KB) nạp firmware qua USB-C (board v2) hoặc UART (v1), phát hiện ứng dụng crash liên tục, + **trang cấu hình web** (Web Serial, deploy lên GitHub Pages): bảng điều khiển, tinh chỉnh PID với biểu đồ trực tiếp, phổ rung gyro, tham số, hiệu chuẩn, test motor, nạp firmware (tự kết nối lại khi cổng USB khởi động lại), nhật ký firmware.
- **Chế độ mô phỏng trên web:** chính firmware này biên dịch sang WebAssembly chạy trên một quad ảo, bay được bằng bàn phím hay tay cầm game.

## Cấu trúc repo

```
firmware/
  common/        board.h (gán chân, 2 phiên bản board), memory_map.h, proto.h (giao thức), HAL tối thiểu,
                 usb/ (USB OTG FS + CDC ACM), link_port (USB hoặc UART), CRC/COBS/link
  bootloader/    bootloader (bl_core.c, bl_policy.c dùng chung với emulator)
  app/           ứng dụng: drivers/, sensors/, estimation/, control/, flight/, comms/, config/, system/, ui/
  lib/cmsis/     CMSIS core + device (vendored)
tests/
  unit/          test đơn vị (tinytest), kể cả stack USB với host giả lập
  sim/           mô hình vật lý quad + mô phỏng bay vòng kín với fc.c thật
  emu/           emulator cả board (bootloader, ROM bootloader, ứng dụng, cổng USB) – native và WebAssembly
web/             trang cấu hình (HTML/CSS/JS thuần, không cần build)
tools/           build.ps1, build.sh, flash.ps1, fwpack.py
docs/            tài liệu (tiếng Việt)
```

## Build

### Windows (STM32CubeCLT / STM32CubeIDE đã cài)

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1            # Release, board v2 (USB-C)
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Config Debug -Clean
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Board 1   # board v1 (UART J2) -> build\release-v1
```

Script tự tìm `arm-none-eabi-gcc` của STM32CubeCLT (hoặc plug-in của CubeIDE), CMake, `make`/`ninja` và Python. Kết quả nằm trong `build\release\out`:

| File | Dùng cho |
|---|---|
| `miniquad-app.bin` | nạp qua trang web / bootloader (header 512 byte + ứng dụng) |
| `miniquad-full.hex` | nạp lần đầu bằng ST-LINK (pad SWD) hoặc ROM bootloader (bootloader + ứng dụng, giữ cấu hình) |
| `miniquad-bootloader.bin`, `miniquad-app.hex`, `*.elf` | khôi phục, debug, `addr2line` |
| `miniquad-full.bin` | ảnh phẳng từ 0x08000000 (**xoá cả cấu hình**) |
| `manifest.json` | phiên bản, CRC, SHA-256 cho trang web |

### Linux / macOS / WSL

```bash
sudo apt install cmake make python3 gcc-arm-none-eabi libnewlib-arm-none-eabi gcc
tools/build.sh            # firmware Release (board v2) + test trên máy
tools/build.sh v1         # firmware cho board v1
tools/build.sh all        # v2 Release + Debug + v1, emulator WebAssembly, test web (cần clang, wasi-libc, node)
```

Hoặc trực tiếp bằng preset CMake: `cmake --preset release && cmake --build --preset release` (`release-v1` cho board v1). Image của board này không nạp được vào board kia: bootloader kiểm tra board id (v1 = 1, v2 = 2).

## Nạp firmware

1. **Lần đầu, board v2** (MCU trắng, chưa có cổng USB): cắm USB-C để cấp nguồn, ST-LINK vào 5 pad mặt dưới
   (TP1 +3V3 → VTref, TP2 SWDIO, TP3 SWCLK, TP4 NRST, TP5 GND) rồi `powershell -File tools\flash.ps1`.
   Không có ST-LINK: thử ROM bootloader USB DFU (nối JP1 = BOOT0, reset, `tools\flash.ps1 -Via dfu`).
   **Board v1:** ST-LINK vào J2 (SWDIO chân 2, SWCLK chân 3, NRST chân 4, GND chân 7) và `tools\flash.ps1 -Board 1`,
   hoặc hàn tắt JP1 (BOOT0), cắm USB-UART vào J2 và dùng mục khôi phục ROM trên trang web.
2. **Các lần sau:** cắm USB-C, trang web, trang *Firmware*, chọn `miniquad-app.bin` (hoặc bản build mới nhất trên GitHub Pages), bấm *Nạp firmware*. Board tự vào bootloader, cổng USB biến mất rồi xuất hiện lại, trang tự kết nối lại. Cấu hình được giữ.

Chi tiết: [docs/bootloader.md](docs/bootloader.md).

## Trang cấu hình

- Bản deploy: <https://thanhbinh89.github.io/miniquad-f405/> (GitHub Pages, job `pages` của workflow CI, nhánh `main`). Web Serial chỉ chạy trên Chrome / Edge / Opera máy tính, qua HTTPS hoặc `localhost`.
- Chạy tại máy: `cd web && python -m http.server 8080` rồi mở `http://localhost:8080`.
- Board v2: cắm cáp USB-C, bấm *Kết nối*, chọn **MiniQuad-F405** (Windows 10/11, Linux, macOS không cần driver; VID:PID 0483:5740).
- Board v1: nối mạch USB-UART 3,3 V vào J2: chân 5 (TX) → RX, chân 6 (RX) → TX, chân 7 → GND; 460800 baud.
- Không có phần cứng: bấm *Chạy mô phỏng*.

Chi tiết: [docs/web.md](docs/web.md). Hướng dẫn bay thử và tinh chỉnh: [docs/tuning.md](docs/tuning.md).

## Kiểm thử

```bash
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
cd web && npm test
```

- 13 chương trình test C: framing, bộ lọc, tham số + lưu flash, ước lượng, điều khiển, RC, bootloader (kể cả quyết định khởi động: double reset, crash loop), I/O (PPM, pin, printf), giao thức ứng dụng, **stack USB CDC với một host giả lập** (enumeration, descriptor, CDC, flow control, ZLP, rút cáp), mô phỏng bay vòng kín (12 kịch bản), và emulator đầu-cuối cho **cả hai board**.
- Test web (Node): COBS/CRC so với vector của C, đồng bộ `proto.js` ↔ `proto.h`, toàn bộ ngăn giao thức của trang web chạy với emulator native của board v2 (tham số qua power cycle, telemetry, bay bằng tay điều khiển ảo, nạp firmware thật qua bootloader USB với hai lần re-enumeration, rút/cắm cáp USB, crash loop, ROM DFU) và board v1 (nạp qua UART, khôi phục AN3155), bản WebAssembly.
- `web/test/e2e/screens.mjs`: kiểm thử trên trình duyệt thật bằng Playwright (tuỳ chọn).

GitHub Actions (`.github/workflows/ci.yml`) chạy tất cả ở mỗi lần push; firmware vừa build tải được ở mục *Artifacts* của từng lần chạy. Trên nhánh `main`, CI deploy trang web kèm firmware đó lên GitHub Pages.

## Tài liệu

- [docs/architecture.md](docs/architecture.md): kiến trúc firmware, bộ nhớ, ngắt, DMA, luồng dữ liệu
- [docs/hardware-map.md](docs/hardware-map.md): gán chân, connector, USB-C và pad SWD, nguồn, hướng lắp
- [docs/protocol.md](docs/protocol.md): giao thức qua USB CDC / UART (cho ai viết công cụ khác)
- [docs/tuning.md](docs/tuning.md): chuẩn bị bay, hiệu chuẩn, tinh chỉnh PID / bộ lọc / độ cao, ý nghĩa LED và còi
- [docs/bootloader.md](docs/bootloader.md): bố trí flash, cập nhật, khôi phục, đọc bản ghi lỗi
- [docs/web.md](docs/web.md): trang cấu hình, chế độ mô phỏng, deploy
- [THIRD_PARTY.md](THIRD_PARTY.md): thành phần của bên thứ ba và giấy phép
