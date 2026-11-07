# Giao thức serial

Một giao thức cho cả bootloader và ứng dụng. Board v2: USB CDC ACM trên cổng USB-C (VID:PID 0483:5740, thông số baud bị bỏ qua, mỗi lần MCU reset cổng biến mất rồi xuất hiện lại). Board v1: USART1 (J2), 460800 baud 8N1. Nguồn sự thật: `firmware/common/proto.h`; bản JavaScript `web/js/proto/proto.js` được test `web/test/proto_sync.test.js` so khớp từng hằng số và kích thước cấu trúc.

## Khung

```
COBS( id | seq | payload[0..288] | crc16_lo | crc16_hi )  0x00
```

- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) trên `id`, `seq`, `payload`.
- COBS bảo đảm byte 0x00 chỉ xuất hiện ở cuối khung → đồng bộ lại ngay sau một khung hỏng.
- Ví dụ: `id 0x23, seq 5, payload 00 01 02 00` → `03 23 05 03 01 02 03 FD 87 00`.
- Số nhiều byte: little-endian. Cấu trúc: packed.

| `id` | Hướng | Ý nghĩa |
|---|---|---|
| 0x01–0x3F | máy tính → board | yêu cầu; board trả lời `id | 0x80` cùng `seq`, byte đầu payload là mã trạng thái |
| 0x40–0x7F | – | bỏ qua (dành cho mở rộng) |
| 0xC0–0xFF | board → máy tính | bản tin tự phát (log, telemetry), `seq = 0` |

Mã trạng thái: 0 OK, 1 lệnh không hỗ trợ, 2 sai độ dài, 3 tham số sai, 4 không cho phép lúc này (đang ARM…), 5 lỗi flash, 6 kiểm tra image lỗi, 7 bận.

## Lệnh

| id | Tên | Yêu cầu | Trả lời (sau byte trạng thái) | Ở |
|---|---|---|---|---|
| 0x01 | HELLO | – | `msg_hello_rsp_t` 68 B: chế độ (0 bootloader, 1 ứng dụng), board id (1 = v1, 2 = v2), phiên bản, app hợp lệ, `boot_reason` (`BLR_*`, lý do bootloader ở lại: 1 ứng dụng yêu cầu, 2 không có ứng dụng, 3 bấm reset 2 lần, 4 crash loop, 5 bắt được cửa sổ UART), UID, git, tên board | cả hai |
| 0x02 | REBOOT | `u8 target` (0 bình thường, 1 bootloader, 2 ROM STM32) | – (reset sau khi gửi xong; USB: nhả D+ 20 ms trước) | cả hai |
| 0x10 | BL_ERASE | `u32 image_size` | – (trả lời sau khi xoá xong, tới vài giây) | bootloader |
| 0x11 | BL_WRITE | `u32 offset`, dữ liệu ≤ 256 B (bội của 4) | `u32 offset` | bootloader |
| 0x12 | BL_READ | `u32 offset`, `u16 length ≤ 256` | `u32 offset`, dữ liệu | bootloader |
| 0x13 | BL_VERIFY | – | `valid`, kích thước, CRC mong đợi / thực tế, phiên bản | bootloader |
| 0x14 | BL_BOOT | – | – (chạy ứng dụng nếu hợp lệ) | bootloader |
| 0x20 | PARAM_INFO | – | `u16 count`, `u32 table_hash` | ứng dụng |
| 0x21 | PARAM_DESC | `u16 index` | index, kiểu, cờ, giá trị, min, max, mặc định (u32 thô), tên 24 B | ứng dụng |
| 0x22 | PARAM_GET | `u16 first`, `u8 count ≤ 64` | `first`, `count`, `count × u32` | ứng dụng |
| 0x23 | PARAM_SET | `u16 index`, `u32 value` | index, giá trị thực sự áp dụng (đã kẹp min/max) | ứng dụng |
| 0x24 | PARAM_SAVE | – | – (ghi flash) | ứng dụng |
| 0x25 | PARAM_RESET | – | – (về mặc định trong RAM) | ứng dụng |
| 0x28 | STREAM | `u8 stream`, `u16 rate_hz` (0 = tắt) | stream, tần số thực sự áp dụng | ứng dụng |
| 0x30 | MOTOR_TEST | `u8 0xA5`, `u8 0`, `4 × u16` duty 0–1000 | – (tự dừng sau 500 ms; `ST_ERR_STATE` khi đang ARM hoặc chưa có pin) | ứng dụng |
| 0x31 | CALIBRATE | `u8` 1 gyro, 2 gia tốc (mặt phẳng), 3 xoá offset gia tốc | – | ứng dụng |
| 0x32 | BEEP | `u8` âm (1 khởi động … 9 test, `notify.h`) | – | ứng dụng |
| 0x33 | FAULT_INFO | – | bản ghi lỗi trước reset (PC, LR, CFSR…), `RCC->CSR`, số lần khởi động | ứng dụng |
| 0x34 | STATUS | – | `tlm_status_t` (84 B) | ứng dụng |

Giá trị tham số luôn 32 bit: `FLOAT` là bit của float32, `INT` là int32, `BOOL` là 0/1. Kiểu `PARAM_T_*` 0 float, 1 int, 2 bool; cờ `PARAM_F_*` 0x01 cần khởi động lại, 0x02 chỉ đọc, 0x04 hiệu chuẩn, 0x08 đổi được khi đang bay. `table_hash` đổi khi tên, kiểu hay thứ tự tham số đổi: máy tính có thể cache mô tả theo hash.

## Telemetry

| Stream | id bản tin | Tối đa | Nội dung |
|---|---|---|---|
| 0 STATUS | 0xC1 | 50 Hz | `tlm_status_t` 84 B: cờ, lý do chặn ARM, chế độ, tư thế, pin, vòng lặp, nhiệt độ, 8 kênh PPM, 4 motor, ToF, độ cao, bộ đếm lỗi |
| 1 RATE | 0xC2 | 500 Hz | `tlm_rate_t` 50 B: setpoint, gyro (°/s × 10), P/I/D/F (× 10000), ga, motor |
| 2 IMU | 0xC3 | 500 Hz | `tlm_imu_t` 22 B: gyro trước/sau lọc (°/s × 10), gia tốc (mg) |
| 3 ALT | 0xC4 | 100 Hz | `tlm_alt_t` 38 B: float độ cao/vận tốc ước lượng và đặt, ToF, gia tốc đứng, ga, ga treo; cờ (bit 0 đo được dùng, bit 1 ALT HOLD, bit 2 trên đất, bit 4–7 pha) |
| – LOG | 0xC0 | – | `u8 mức` (0 debug … 3 lỗi) + văn bản |

RATE và IMU được lấy mẫu ngay trong flight loop mỗi `1000 / rate` vòng nên đều đặn tuyệt đối; nếu đường truyền đầy, gói bị bỏ và đếm vào `link_drops`. Qua USB (board v2, bulk 64 byte, vài trăm KB/s) mọi tổ hợp stream đều dư. Ở 460800 baud (board v1, ≈ 46 KB/s) chạy thoải mái STATUS 20 Hz + RATE 500 Hz (≈ 30 KB/s); RATE + IMU cùng 500 Hz thì sát giới hạn và sẽ mất gói.

Board coi máy tính đã đi khi 3 s không có yêu cầu nào: tắt mọi stream, dừng test motor. Trang web gửi STATUS mỗi giây khi rảnh.

Cờ mới của board v2 trong `tlm_status_t`: `STF_USB` (bit 16, có VBUS ở cổng USB-C). Lý do chặn ARM mới: `ARMB_NO_BAT` (bit 13, chưa có pin, ví dụ chỉ cắm USB).

## Lớp vận chuyển

- **USB CDC (board v2):** thiết bị full speed, endpoint bulk 64 byte (0x01 OUT, 0x81 IN) + interrupt 0x82 (khai báo, không dùng). Gói COBS đi liền nhau trong dòng byte, ranh giới gói USB không có ý nghĩa; gói cuối dài đúng 64 byte được kết thúc bằng ZLP. Board nhận khi có chỗ trống ≥ 64 byte (NAK nếu không). `SET_LINE_CODING` được lưu và trả lại nhưng không ảnh hưởng tốc độ. DTR về 0 (máy tính đóng cổng) thì board bỏ dữ liệu đang chờ gửi. Mỗi lần MCU reset, thiết bị rời bus và enumerate lại: công cụ trên máy tính phải mở lại cổng (cùng VID/PID/serial).
- **UART (board v1):** USART1 460800 8N1, RX bằng DMA vòng 1 KB, TX ring 4 KB bằng DMA.

## Quy tắc an toàn trong giao thức

Khi đang ARM, board trả `4` cho: REBOOT, PARAM_SET với tham số không có cờ LIVE, PARAM_SAVE, PARAM_RESET, CALIBRATE, MOTOR_TEST.

## Bootloader ROM của STM32

Ngoài giao thức trên, với board v1 trang web nói chuyện được với bootloader trong ROM của STM32 (ST AN3155, USART1, 115200 8E1; board v2 thì ROM là USB DFU, dùng STM32CubeProgrammer): đồng bộ `0x7F`, GET, GET_ID (0x0413), READ, WRITE (≤ 256 B), EXTENDED ERASE, GO. Xem `web/js/proto/stm32rom.js`.
