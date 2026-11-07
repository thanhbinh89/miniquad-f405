# Trang cấu hình (web)

HTML/CSS/JavaScript thuần (ES modules), không framework, không bước build. Font được kèm theo (`web/fonts`), nên trang chạy cả khi không có Internet (sau khi đã tải).

## Dùng

- Chrome, Edge hoặc Opera trên máy tính (Web Serial). Trang phải mở qua HTTPS (GitLab Pages) hoặc `http://localhost`.
- **Board v2:** cắm cáp USB-C vào J2, bấm *Kết nối*, chọn **MiniQuad-F405** (danh sách chỉ hiện cổng có VID:PID 0483:5740). Không cần driver (Windows 10/11 usbser, Linux cdc_acm, macOS). Chỉ cần USB là cấu hình / nạp firmware được; motor cần pin.
- **Board v1:** chọn *Board v1 · UART J2* trên màn hình kết nối; mạch USB-UART 3,3 V vào J2: chân 5 (TX) → RX, chân 6 (RX) → TX, chân 7 → GND. 460800 baud.
- Linux: thêm người dùng vào nhóm `dialout` nếu không mở được cổng.

### Kết nối lại tự động (board v2)

Cổng USB của board v2 là của chính STM32, nên **mỗi lần MCU reset cổng biến mất** (vào bootloader, nạp firmware xong, HardFault, bấm S2) và xuất hiện lại khoảng 1 s sau; rút cáp cũng vậy. `transport/serial.js` khi đó báo *mất liên kết*, cứ 250 ms (và mỗi sự kiện `connect` của `navigator.serial`) tìm trong `navigator.serial.getPorts()` cổng có cùng VID/PID rồi mở lại với cùng thông số. Chrome giữ quyền truy cập vì bootloader và ứng dụng dùng chung VID, PID và số serial (UID của MCU). Trong lúc chờ, thanh trên cùng hiện “Đang chờ board…”, các lệnh đang chờ bị huỷ ngay (không đợi timeout); khi có lại, trang gửi HELLO và nạp lại tham số. Các thao tác tự khởi động lại board (nạp firmware, *Khởi động lại*, *Vào bootloader*) tự chờ board quay lại (tối đa 12 s).

| Trang | Việc |
|---|---|
| Tổng quan | tư thế 3D, pin, ToF / độ cao, 8 kênh PPM, motor, lý do chặn ARM, sức khoẻ hệ thống |
| Tinh chỉnh PID | biểu đồ setpoint/gyro và các thành phần PID; gyro thô/đã lọc + phổ rung; giữ độ cao; sửa tham số tại chỗ |
| Tham số | toàn bộ tham số theo nhóm, tìm kiếm, xuất/nhập JSON, khôi phục mặc định, hoàn tác |
| Hiệu chuẩn & test | gyro, mặt phẳng, hướng lắp, test motor (phải xác nhận đã tháo cánh), còi, bộ thu |
| Firmware | phiên bản, bản ghi lỗi, cập nhật qua bootloader; v2: hướng dẫn nạp lần đầu qua SWD (TP1–TP5) và ROM DFU, tải `miniquad-full.hex`; v1: khôi phục qua ROM bootloader UART ngay trên trang |
| Nhật ký | log của firmware, thống kê đường truyền, lưu ra file |

Thay đổi tham số có hiệu lực ngay (RAM); *Lưu vào flash* để giữ. Khi đang ARM chỉ sửa được tham số có nhãn *trực tiếp*.

File xuất tham số (`miniquad-params-*.json`) lưu theo **tên** tham số, nên nhập được vào firmware mới hơn: tham số không còn bị bỏ qua, tham số mới giữ mặc định. Offset hiệu chuẩn gia tốc chỉ được nhập khi bạn đồng ý (chúng thuộc về một board cụ thể).

## Chế độ mô phỏng

*Chạy mô phỏng* tải `demo/miniquad-emu.wasm`: chính mã C của firmware (giao thức, bootloader + quyết định khởi động, tham số + lưu flash, flight core, LED/còi) chạy trên một board v2 ảo có flash, cổng USB CDC (biến mất mỗi lần MCU reset, enumerate lại sau ~0,4 s, như phần cứng) và mô hình vật lý của quad 30 g (motor, pin 250 mAh sụt áp, IMU nhiễu + rung, ToF trễ). Trang nói chuyện với nó bằng đúng các byte như với phần cứng.

Khung *Board mô phỏng* là phần “vật lý”: tay điều khiển ảo (chuột, bàn phím W/S/A/D + mũi tên + Space + 1/2/3, hoặc tay cầm game), cáp USB-C (rút ra để thấy trang chờ kết nối lại), cắm lại pin, nút reset, cầu JP1 (BOOT0), pin mới, tắt bộ phát (thử failsafe), tắt ToF, đẩy quad, gây HardFault (3 lần liền = crash loop), LED trạng thái và tiếng còi.

Build lại bản WebAssembly (cần clang, lld, wasi-libc):

```bash
sudo apt install clang lld wasi-libc libclang-rt-18-dev-wasm32
cmake --preset wasm-emu && cmake --build --preset wasm-emu    # -> web/demo/miniquad-emu.wasm
```

File `.wasm` được commit để trang chạy được ngay khi mở từ repo; CI build lại khi deploy.

## Chạy tại máy

```bash
cd web
python3 -m http.server 8080          # rồi mở http://localhost:8080
```

Để thử mục *Bản build mới nhất*, chép `build/release/out/{manifest.json,miniquad-app.bin,miniquad-bootloader.bin,miniquad-full.hex}` vào `web/firmware/` (board v1: `build/release-v1/out/…` vào `web/firmware/v1/`; hai thư mục bị git bỏ qua).

## Cấu trúc mã

```
web/js/
  main.js               khung ứng dụng: kết nối, chuyển trang, stream telemetry, thông báo
  proto/                không phụ thuộc DOM, chạy cả trong Node
    crc.js cobs.js link.js   khung truyền (giống firmware/common/util)
    proto.js            hằng số + giải mã cấu trúc, bản sao của proto.h
    device.js           client: seq, timeout, gửi lại, telemetry, heartbeat, mất/có lại liên kết
    firmware.js         đọc/kiểm tra image, nạp qua bootloader
    stm32rom.js         ROM bootloader STM32 (AN3155)
    ihex.js             đọc Intel HEX
  params/               bảng tham số (cache mô tả theo hash), nhãn tiếng Việt (meta.js)
  transport/            serial.js (Web Serial, tự mở lại cổng USB), demo.js (WebAssembly)
  ui/  widgets/         các trang, sơ đồ board, đồ thị kiểu dao động ký, mô hình 3D
```

Thêm một tham số vào firmware: chỉ cần thêm dòng trong `params_list.h` và một mục trong `web/js/params/meta.js` (nhãn, đơn vị, giải thích). Test `proto_sync` báo lỗi nếu quên.

## Kiểm thử

```bash
cmake --preset host-tests && cmake --build --preset host-tests   # build emulator native
cd web && npm test
```

- `link.test.js`: CRC, COBS, khung tham chiếu giống test C.
- `proto_sync.test.js`: mọi `#define` của `proto.h`, kích thước cấu trúc, âm còi, mask, nhãn tham số.
- `emu.test.js`: toàn bộ client chạy với emulator native (`tests/emu`, mã C thật) của **cả hai board**. Board v2 (`miniquad-emu`): HELLO, tham số qua lần cắt nguồn (cổng USB enumerate lại), xuất/nhập, telemetry đúng tần số, ARM và bay bằng tay điều khiển ảo, test motor, rút/cắm cáp USB, nạp `build/release/out/miniquad-app.bin` qua bootloader (hai lần re-enumeration), từ chối image v1, crash loop, ROM DFU. Board v1 (`miniquad-emu-v1`): nạp qua UART, khôi phục qua ROM bootloader AN3155, BOOT0, bản ghi lỗi. `test/helpers.js` (`ProcessTransport`) giả lập hành vi mất/có lại cổng của Web Serial.
- `demo.test.js`: bản WebAssembly qua `DemoTransport`.
- `test/e2e/screens.mjs` (tuỳ chọn, Playwright): mở trang thật, đi qua mọi trang, bay, nạp firmware, rút/cắm cáp USB, vào/ra bootloader trong chế độ mô phỏng, chụp màn hình.

## Deploy lên GitLab Pages

Job `pages` trong `.gitlab-ci.yml` (nhánh mặc định) chép `web/` và firmware vừa build (`manifest.json`, `.bin`, `.hex`) vào `public/firmware/` (board v2) và `public/firmware/v1/` (board v1). Trang chọn đúng thư mục theo board đang nối. Trang dùng đường dẫn tương đối nên chạy được ở bất kỳ URL nào của Pages.
