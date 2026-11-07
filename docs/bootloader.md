# Bootloader, nạp firmware và khôi phục

## Bố trí flash

| Sector | Địa chỉ | Nội dung |
|---|---|---|
| 0 (16 KB) | 0x08000000 | Bootloader MiniQuad (~9,5 KB board v2 với stack USB, ~6 KB board v1) |
| 1, 2 (16 KB) | 0x08004000, 0x08008000 | Cấu hình, 2 bank luân phiên |
| 3–11 | 0x0800C000 | Header 512 byte + ứng dụng (tối đa 976 KB) |

Header ứng dụng (`firmware/common/app_header.h`), do `tools/fwpack.py` điền sau khi link:

| Offset | Trường |
|---|---|
| 0x000 | magic `0x5041514D` ("MQAP") |
| 0x004 / 0x008 / 0x00C | phiên bản header (1), kích thước header (0x200), offset image (0x200) |
| 0x010 / 0x014 | kích thước image, CRC-32 image |
| 0x018 / 0x01C | phiên bản firmware (`major << 16 | minor << 8 | patch`), thời điểm build |
| 0x020 | board id: 1 = MiniQuad-F405 v1 (UART J2), 2 = MiniQuad-F405 v2 (USB-C) |
| 0x028 / 0x038 / 0x048 | git hash, tên board, tên firmware (16 byte mỗi trường) |
| 0x1FC | CRC-32 của 0x000–0x1FB |

Bootloader chỉ chạy ứng dụng khi magic, hai CRC, board id, kích thước và vector (SP trong RAM/CCM, PC trong image) đều đúng. Image của board v1 không chạy trên board v2 và ngược lại (trang web cũng từ chối trước khi nạp).

## Khởi động

```
reset ─► startup_early_hook()   (trước mọi khởi tạo)
          ├─ token BOOT_APP trong RAM chia sẻ ─► xoá cờ reset, nhảy vào ứng dụng
          └─ yêu cầu ROM ─► nhảy vào bootloader ROM của STM32
       ─► main(): bl_boot_decide()  (firmware/bootloader/bl_policy.c)
          ├─ BLR_REQUEST     ứng dụng yêu cầu (nạp firmware)            ┐
          ├─ BLR_NO_APP      không có ứng dụng hợp lệ                   │ ở lại bootloader,
          ├─ BLR_DBL_RESET   bấm reset lần 2 trong cửa sổ 300 ms        │ bật USB (v2) / UART (v1)
          ├─ BLR_CRASH_LOOP  ứng dụng khởi động 3 lần liền chưa "xác nhận" ┘
          └─ BLR_NONE        LED sáng 300 ms (cửa sổ double reset) rồi chạy ứng dụng
```

- **Board v2 không bật USB trong lần khởi động bình thường**: nếu bật, máy tính sẽ thấy “MiniQuad-F405 Bootloader” xuất hiện rồi biến mất ngay. USB chỉ bật khi bootloader ở lại; khi đó thiết bị enumerate với cùng VID/PID/serial như ứng dụng.
- Board v1 vẫn nghe UART trong cửa sổ 300 ms: trang web gửi HELLO đúng lúc đó sẽ giữ board lại (`BLR_HOST`).
- **Crash loop**: bootloader đếm mỗi lần chạy ứng dụng (`app_starts` trong RAM chia sẻ); ứng dụng xoá bộ đếm sau 5 s chạy ổn, và trước mỗi lần tự khởi động lại có chủ đích. Reset do phần mềm (HardFault → tự reset) hoặc watchdog mà chưa xác nhận thì bộ đếm tăng; đến 3 thì bootloader ở lại và HELLO báo `BLR_CRASH_LOOP`. Reset bằng nút S2, bật nguồn, sụt áp: không tính là crash (bộ đếm về 0), nên bấm S2 là chạy lại ứng dụng.
- Ở lại vì ứng dụng yêu cầu mà 30 s không có lệnh nào: tự quay về ứng dụng.
- Trang web hiển thị lý do bootloader đang giữ board (HELLO `boot_reason`).
- LED: sáng liên tục = cửa sổ 300 ms; nháy nhanh = bootloader chờ máy tính; nháy chậm = máy tính đã kết nối (v2: đã enumerate).

## Cập nhật qua trang web (thường dùng)

Trang *Firmware* > *Cập nhật*: chọn `miniquad-app.bin` (hoặc `.hex`, `miniquad-full.bin`), hoặc *Bản build mới nhất* khi trang chạy trên GitLab Pages. Trang kiểm tra file trước (CRC, board), rồi:

1. `REBOOT(1)` → board nhả D+ (v2), khởi động lại và ở lại bootloader (yêu cầu nằm trong RAM chia sẻ).
2. Board v2: cổng USB biến mất rồi xuất hiện lại với tên “MiniQuad-F405 Bootloader”; trang tự mở lại cổng (Chrome giữ quyền vì VID/PID/serial không đổi) và chờ HELLO của bootloader (tối đa 12 s).
3. `BL_ERASE(size)`: xoá đúng các sector cần (16 KB ≈ 0,3 s, 64 KB ≈ 0,6 s, 128 KB ≈ 1 s).
4. `BL_WRITE` từng khối 256 byte (bootloader kiểm tra lại sau khi ghi).
5. `BL_VERIFY`: bootloader tự tính lại CRC toàn bộ image.
6. `BL_BOOT`: chạy ứng dụng mới; cổng USB lại khởi động lại, trang chờ HELLO của ứng dụng.

Cấu hình (sector 1–2) không bị đụng tới. Mất điện giữa chừng: board ở lại bootloader (image không hợp lệ), chỉ cần nạp lại.

## Khi ứng dụng hỏng nhưng bootloader còn

- Ứng dụng không hợp lệ → bootloader tự ở lại, LED nháy; kết nối trang web như bình thường, trang chuyển sang *Firmware*.
- Ứng dụng hợp lệ nhưng reset liên tục (HardFault, watchdog) → sau 3 lần bootloader tự giữ board (crash loop), cắm USB-C và nạp lại.
- Ứng dụng chạy nhưng không muốn để nó chạy: bấm reset S2 hai lần liền (lần 2 trong lúc LED sáng 300 ms đầu) → bootloader ở lại.

## Nạp bootloader lần đầu / khôi phục

### Board v2: SWD trên TP1–TP5 (cách chính)

MCU mới chưa có bootloader nên chưa có USB. Nạp một lần `miniquad-full.hex` (bootloader + ứng dụng, cấu hình giữ nguyên) bằng ST-LINK:

| Pad (mặt dưới, silkscreen) | ST-LINK |
|---|---|
| TP1 +3V3 (“3V3”) | VTref / 3,3 V (chỉ để đo mức, ST-LINK không cấp nguồn) |
| TP2 SWDIO (“DIO”) | SWDIO |
| TP3 SWCLK (“CLK”) | SWCLK |
| TP4 NRST (“RST”) | NRST / RST |
| TP5 GND (“GND”) | GND |

Pad tròn 2 mm, bước 3 mm: dùng kim test / pogo pin (P70) hoặc hàn dây tạm.

Cấp nguồn cho board bằng USB-C (hoặc pin), rồi:

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash.ps1          # mặc định: -Via swd, board v2
```

hoặc STM32CubeProgrammer → ST-LINK, mode *Under reset* → Connect → mở `miniquad-full.hex` → Download. Sau đó tháo ST-LINK, cắm USB-C, mở trang web.

### Board v2: ROM bootloader USB DFU (dự phòng, không cần ST-LINK)

ROM của STM32F405 có chế độ USB DFU trên PA11/PA12 (chính cổng USB-C):

1. Board còn trả lời: trang *Firmware* > *Vào ROM bootloader (DFU)* (`REBOOT(2)`). Không trả lời: nối **JP1** (0 Ω, BOOT0 → 3,3 V, mặt dưới) rồi bấm reset S2.
2. Máy tính thấy “STM32 BOOTLOADER” (DFU, không phải cổng COM: trang web không nạp được). Nạp bằng STM32CubeProgrammer (USB → Connect), hoặc:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Via dfu
   # tương đương: STM32_Programmer_CLI -c port=USB1 -d miniquad-full.hex -v -g 0x08000000
   ```

3. Gỡ JP1 (nếu đã nối) rồi reset.

> Chưa kiểm chứng trên board v2: DFU của ROM cần thạch anh 8 MHz chạy tốt, và PA9 của board nhận VBUS/2 (~2,5 V) chứ không phải VBUS. Ngoài USB, ROM còn dò USART1_RX (PA10), USART3_RX (PC11, PB11) và CAN2_RX (PB5); AN2606 yêu cầu các chân này không được thả nổi lúc ROM tìm máy tính, nếu không nhiễu có thể làm nó chọn nhầm UART. Trên v2, PB11 có điện trở kéo I2C, PB5 có điện trở kéo xuống của cổng MOSFET motor, còn PA10 để trống và PC11 (J3.4) không có điện trở kéo. Vì vậy: **rút bộ thu khỏi J3** trước khi vào ROM (xung PPM cũng bị coi là tín hiệu UART). Vào bằng `REBOOT(2)` thì bootloader MiniQuad bật điện trở kéo lên trong chip cho PA10 và PC11 trước khi nhảy vào ROM; vào bằng JP1 thì không có bước này. Nếu máy tính không thấy “STM32 BOOTLOADER”, thử reset vào lại, hoặc dùng SWD.

### Board v1: ROM bootloader qua UART (AN3155), ngay trên trang web

Dùng khi sector 0 hỏng, khi nạp lần đầu, hoặc khi mọi cách trên không được. Không cần ST-LINK.

1. Vào ROM bootloader:
   - board còn trả lời: trang *Firmware* > *Vào ROM bootloader* (`REBOOT(2)`), hoặc
   - hàn tắt **JP1** (BOOT0, mặt dưới, dưới MCU) rồi bấm reset S2 / cắm lại pin.
2. Chọn file: *Bản build mới nhất*, hoặc `miniquad-bootloader.bin` + `miniquad-app.bin` (hoặc `miniquad-full.bin`).
3. *Nạp khôi phục*: trang mở lại cổng ở 115200 8E1, đồng bộ (0x7F), kiểm tra chip (0x413), xoá sector 0 + các sector của ứng dụng (tuỳ chọn: cả cấu hình), ghi, đọc lại so sánh, rồi `GO 0x08000000`.
4. Nếu đã hàn JP1: gỡ cầu rồi reset.

Dòng lệnh:

```powershell
# ST-LINK trên J2 (SWDIO 2, SWCLK 3, NRST 4, GND 7)
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board 1
# ROM bootloader qua USB-UART (JP1 đã hàn)
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board 1 -Via uart -Port COM5
# thêm -EraseConfig để xoá cả cấu hình
```

## Bản ghi lỗi (fault)

Khi có HardFault / MemManage / BusFault / UsageFault, tràn stack (vùng cấm MPU ở đáy stack) hoặc NMI: motor bị tắt ngay, các thanh ghi (PC, LR, xPSR, CFSR, HFSR, MMFAR, BFAR) và thời gian chạy được ghi vào RAM `.noinit`, rồi MCU tự reset. Sau đó trang *Firmware* > *Fault* hiển thị bản ghi, giải nghĩa các bit CFSR, lý do reset (`RCC->CSR`: bật nguồn, chân reset, phần mềm, watchdog, sụt áp…) và số lần khởi động. Tra dòng code:

```
arm-none-eabi-addr2line -e build/release/out/miniquad-app.elf 0x0800XXXX
```

(dùng đúng file `.elf` của bản đang chạy – git hash ở trang *Firmware*). Nếu lỗi xảy ra ngay khi khởi động, sau 3 lần bootloader giữ board lại (crash loop): nạp bản sửa qua USB, hoặc bấm S2 để chạy lại và đọc bản ghi.
