# MiniQuad-F405

Firmware bare-metal và trang cấu hình web cho flight controller MiniQuad-F405: STM32F405RGT6, IMU ICM-42688-P, cảm biến khoảng cách VL53L1 (ToF), 4 motor coreless (brushed) qua MOSFET, pin 1S, thu PPM.

- **Không HAL, không RTOS.** CMSIS + driver tự viết, mọi luồng dữ liệu lớn chạy bằng DMA (IMU SPI, ToF I2C, UART, ADC).
- **Vòng điều khiển 1 kHz** khởi chạy từ chân ngắt data-ready của IMU: lọc gyro (notch + LPF), ước lượng tư thế Mahony, PID tốc độ góc kiểu Betaflight, tự cân bằng (ANGLE), giữ độ cao bằng ToF + bộ lọc Kalman (ALT HOLD), bù lực đẩy theo điện áp pin, tuyến tính hoá lực đẩy.
- **An toàn:** chặn ARM khi chưa đủ điều kiện, failsafe tự hạ cánh, phát hiện va chạm, watchdog, giám sát vòng điều khiển bị treo, bản ghi HardFault, bảo vệ tràn stack bằng MPU.
- **Còi + LED** báo trạng thái, pin yếu, failsafe, tìm quad sau va chạm.
- **Bootloader UART** riêng (16 KB) + **trang cấu hình web** (Web Serial, deploy lên GitLab Pages): bảng điều khiển, tinh chỉnh PID với biểu đồ trực tiếp, phổ rung gyro, tham số, hiệu chuẩn, test motor, nạp firmware, khôi phục bằng ROM bootloader của STM32.
- **Chế độ mô phỏng trên web:** chính firmware này biên dịch sang WebAssembly chạy trên một quad ảo, bay được bằng bàn phím hay tay cầm game.

## Cấu trúc repo

```
firmware/
  common/        board.h (gán chân), memory_map.h, proto.h (giao thức), HAL tối thiểu, CRC/COBS/link
  bootloader/    bootloader UART (bl_core.c dùng chung với emulator)
  app/           ứng dụng: drivers/, sensors/, estimation/, control/, flight/, comms/, config/, system/, ui/
  lib/cmsis/     CMSIS core + device (vendored)
tests/
  unit/          test đơn vị (tinytest)
  sim/           mô hình vật lý quad + mô phỏng bay vòng kín với fc.c thật
  emu/           emulator cả board (bootloader, ROM bootloader, ứng dụng) – bản native và WebAssembly
web/             trang cấu hình (HTML/CSS/JS thuần, không cần build)
tools/           build.ps1, build.sh, flash.ps1, fwpack.py
docs/            tài liệu (tiếng Việt)
```

## Build

### Windows (STM32CubeCLT / STM32CubeIDE đã cài)

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1            # Release
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Config Debug -Clean
```

Script tự tìm `arm-none-eabi-gcc` của STM32CubeCLT (hoặc plug-in của CubeIDE), CMake, `make`/`ninja` và Python. Kết quả nằm trong `build\release\out`:

| File | Dùng cho |
|---|---|
| `miniquad-app.bin` | nạp qua trang web / bootloader (header 512 byte + ứng dụng) |
| `miniquad-full.hex` | nạp lần đầu bằng ST-LINK hoặc ROM bootloader (bootloader + ứng dụng, giữ cấu hình) |
| `miniquad-bootloader.bin`, `miniquad-app.hex`, `*.elf` | khôi phục, debug, `addr2line` |
| `miniquad-full.bin` | ảnh phẳng từ 0x08000000 (**xoá cả cấu hình**) |
| `manifest.json` | phiên bản, CRC, SHA-256 cho trang web |

### Linux / macOS / WSL

```bash
sudo apt install cmake make python3 gcc-arm-none-eabi libnewlib-arm-none-eabi gcc
tools/build.sh            # firmware Release + test trên máy
tools/build.sh all        # + Debug, emulator WebAssembly, test web (cần clang, wasi-libc, node)
```

Hoặc trực tiếp bằng preset CMake: `cmake --preset release && cmake --build --preset release`.

## Nạp firmware

1. **Lần đầu** (board trắng): ST-LINK vào J2 (SWDIO chân 2, SWCLK chân 3, NRST chân 4, GND chân 7) rồi
   `powershell -File tools\flash.ps1`, hoặc không cần ST-LINK: hàn tắt JP1 (BOOT0), cắm USB-UART vào J2 và dùng mục *Khôi phục bằng ROM bootloader* trên trang web.
2. **Các lần sau:** trang web, trang *Firmware*, chọn `miniquad-app.bin` (hoặc bản build mới nhất trên GitLab Pages), bấm *Nạp firmware*. Cấu hình được giữ.

Chi tiết: [docs/bootloader.md](docs/bootloader.md).

## Trang cấu hình

- Bản deploy: GitLab Pages của repo (job `pages`, nhánh mặc định). Web Serial chỉ chạy trên Chrome / Edge / Opera máy tính, qua HTTPS hoặc `localhost`.
- Chạy tại máy: `cd web && python -m http.server 8080` rồi mở `http://localhost:8080`.
- Nối mạch USB-UART 3,3 V vào J2: chân 5 (TX) → RX, chân 6 (RX) → TX, chân 7 → GND; 460800 baud.
- Không có phần cứng: bấm *Chạy mô phỏng*.

Chi tiết: [docs/web.md](docs/web.md). Hướng dẫn bay thử và tinh chỉnh: [docs/tuning.md](docs/tuning.md).

## Kiểm thử

```bash
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
cd web && npm test
```

- 11 chương trình test C: framing, bộ lọc, tham số + lưu flash, ước lượng, điều khiển, RC, bootloader, I/O (PPM, pin, printf), giao thức ứng dụng, mô phỏng bay vòng kín (12 kịch bản: cân bằng, nhảy bậc, nhiễu, ALT HOLD, cất cánh, failsafe có/không ToF, bù pin, va chạm, xoay board…), và emulator đầu-cuối.
- Test web (Node): COBS/CRC so với vector của C, đồng bộ `proto.js` ↔ `proto.h`, toàn bộ ngăn giao thức của trang web chạy với emulator native (đọc/ghi/lưu tham số, telemetry, ARM và bay bằng tay điều khiển ảo, nạp firmware thật qua bootloader, khôi phục qua ROM bootloader), bản WebAssembly.
- `web/test/e2e/screens.mjs`: kiểm thử trên trình duyệt thật bằng Playwright (tuỳ chọn).

CI GitLab (`.gitlab-ci.yml`) chạy tất cả rồi deploy trang web kèm firmware vừa build lên GitLab Pages.

## Tài liệu

- [docs/architecture.md](docs/architecture.md): kiến trúc firmware, bộ nhớ, ngắt, DMA, luồng dữ liệu
- [docs/hardware-map.md](docs/hardware-map.md): gán chân, connector, hướng lắp
- [docs/protocol.md](docs/protocol.md): giao thức serial (cho ai viết công cụ khác)
- [docs/tuning.md](docs/tuning.md): chuẩn bị bay, hiệu chuẩn, tinh chỉnh PID / bộ lọc / độ cao, ý nghĩa LED và còi
- [docs/bootloader.md](docs/bootloader.md): bố trí flash, cập nhật, khôi phục, đọc bản ghi lỗi
- [docs/web.md](docs/web.md): trang cấu hình, chế độ mô phỏng, deploy
- [THIRD_PARTY.md](THIRD_PARTY.md): thành phần của bên thứ ba và giấy phép
