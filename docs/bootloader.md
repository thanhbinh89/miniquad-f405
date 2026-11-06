# Bootloader, nạp firmware và khôi phục

## Bố trí flash

| Sector | Địa chỉ | Nội dung |
|---|---|---|
| 0 (16 KB) | 0x08000000 | Bootloader MiniQuad (~6 KB) |
| 1, 2 (16 KB) | 0x08004000, 0x08008000 | Cấu hình, 2 bank luân phiên |
| 3–11 | 0x0800C000 | Header 512 byte + ứng dụng (tối đa 976 KB) |

Header ứng dụng (`firmware/common/app_header.h`), do `tools/fwpack.py` điền sau khi link:

| Offset | Trường |
|---|---|
| 0x000 | magic `0x5041514D` ("MQAP") |
| 0x004 / 0x008 / 0x00C | phiên bản header (1), kích thước header (0x200), offset image (0x200) |
| 0x010 / 0x014 | kích thước image, CRC-32 image |
| 0x018 / 0x01C | phiên bản firmware (`major << 16 | minor << 8 | patch`), thời điểm build |
| 0x020 | board id (1 = MiniQuad-F405) |
| 0x028 / 0x038 / 0x048 | git hash, tên board, tên firmware (16 byte mỗi trường) |
| 0x1FC | CRC-32 của 0x000–0x1FB |

Bootloader chỉ chạy ứng dụng khi magic, hai CRC, board id, kích thước và vector (SP trong RAM/CCM, PC trong image) đều đúng.

## Cập nhật qua trang web (thường dùng)

Trang *Firmware* > *Cập nhật*: chọn `miniquad-app.bin` (hoặc `.hex`, `miniquad-full.bin`), hoặc *Bản build mới nhất* khi trang chạy trên GitLab Pages. Trang kiểm tra file trước (CRC, board), rồi:

1. `REBOOT(1)` → board khởi động lại và ở lại bootloader (yêu cầu nằm trong RAM chia sẻ).
2. `BL_ERASE(size)`: xoá đúng các sector cần (16 KB ≈ 0,3 s, 64 KB ≈ 0,6 s, 128 KB ≈ 1 s).
3. `BL_WRITE` từng khối 256 byte (bootloader kiểm tra lại sau khi ghi).
4. `BL_VERIFY`: bootloader tự tính lại CRC toàn bộ image.
5. `BL_BOOT`: chạy ứng dụng mới; trang chờ HELLO của ứng dụng.

Cấu hình (sector 1–2) không bị đụng tới. Mất điện giữa chừng: board ở lại bootloader (image không hợp lệ), chỉ cần nạp lại.

## Khi ứng dụng hỏng nhưng bootloader còn

- Ứng dụng không hợp lệ → bootloader tự ở lại, LED nháy; kết nối trang web như bình thường, trang sẽ chuyển sang *Firmware*.
- Ứng dụng hợp lệ nhưng treo / reset liên tục → sau mỗi reset bootloader nghe 300 ms (LED sáng). Bấm *Kết nối* rồi bấm reset S2 (hoặc cắm lại pin): HELLO đầu tiên giữ board ở bootloader.
- Reset hai lần liên tiếp trong cửa sổ 300 ms cũng giữ board ở bootloader.

## Khôi phục bằng ROM bootloader của STM32

Dùng khi sector 0 hỏng, khi nạp lần đầu, hoặc khi mọi cách trên không được. Không cần ST-LINK.

1. Vào ROM bootloader:
   - board còn trả lời: trang *Firmware* > *Vào ROM bootloader* (`REBOOT(2)`), hoặc
   - hàn tắt **JP1** (BOOT0, mặt dưới, dưới MCU) rồi bấm reset S2 / cắm lại pin.
2. Chọn file: *Bản build mới nhất*, hoặc `miniquad-bootloader.bin` + `miniquad-app.bin` (hoặc `miniquad-full.bin`).
3. *Nạp khôi phục*: trang mở lại cổng ở 115200 8E1, đồng bộ (0x7F), kiểm tra chip (0x413), xoá sector 0 + các sector của ứng dụng (tuỳ chọn: cả cấu hình), ghi, đọc lại so sánh, rồi `GO 0x08000000`.
4. Nếu đã hàn JP1: gỡ cầu rồi reset.

Dòng lệnh, với STM32CubeProgrammer (STM32CubeCLT có sẵn):

```powershell
# ST-LINK trên J2 (SWDIO 2, SWCLK 3, NRST 4, GND 7)
powershell -ExecutionPolicy Bypass -File tools\flash.ps1
# ROM bootloader qua USB-UART (JP1 đã hàn)
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Via uart -Port COM5
# thêm -EraseConfig để xoá cả cấu hình
```

## Bản ghi lỗi (fault)

Khi có HardFault / MemManage / BusFault / UsageFault, tràn stack (vùng cấm MPU ở đáy stack) hoặc NMI: motor bị tắt ngay, các thanh ghi (PC, LR, xPSR, CFSR, HFSR, MMFAR, BFAR) và thời gian chạy được ghi vào RAM `.noinit`, rồi MCU tự reset. Sau đó trang *Firmware* > *Fault* hiển thị bản ghi, giải nghĩa các bit CFSR, lý do reset (`RCC->CSR`: bật nguồn, chân reset, phần mềm, watchdog, sụt áp…) và số lần khởi động. Tra dòng code:

```
arm-none-eabi-addr2line -e build/release/out/miniquad-app.elf 0x0800XXXX
```

(dùng đúng file `.elf` của bản đang chạy – git hash ở trang *Firmware*).
