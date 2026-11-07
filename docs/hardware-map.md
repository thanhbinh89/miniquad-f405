# Phần cứng ↔ firmware

Nguồn sự thật: `firmware/common/board.h`. Board 44 × 48 mm, lỗ bắt khung 20 × 20 mm.

Có hai phiên bản board, chọn khi build bằng `MQ_BOARD_REV` (mặc định 2):

- **v2** (schematic final `renders/MiniQuad-F405_Schematic.pdf` = `sch_v5`, netlist `netlist_after4` – bản final chỉ khác ở chỗ U6 dùng chân 3/6, chân 1/4 bỏ trống; vị trí linh kiện theo layout final 06–07/11/2026): J2 là cổng **USB-C**, nối thẳng vào USB OTG FS của STM32. Console, cấu hình và nạp firmware đều qua USB CDC. SWD chuyển ra 5 pad test TP1–TP5 ở mặt dưới, chỉ dùng để nạp bootloader lần đầu.
- **v1**: J2 là header 7 chân (SWD + UART1), cấu hình qua mạch USB-UART rời.

Mọi chân khác giống hệt nhau giữa hai phiên bản.

## Gán chân

| Chức năng | Chân MCU | Ngoại vi | Ghi chú |
|---|---|---|---|
| M1 / M2 / M3 / M4 | PB4 / PB5 / PB0 / PB1 | TIM3 CH1–4, AF2 | PWM 32 kHz, MOSFET IRLML2502 low-side |
| IMU SCK / MISO / MOSI | PA5 / PA6 / PA7 | SPI1, AF5 | mode 3, 10,5 MHz (656 kHz khi cấu hình thanh ghi) |
| IMU CS / INT1 | PA4 / PA3 | GPIO / EXTI3 | INT1 xung, kích hoạt mức cao |
| ToF SCL / SDA | PB10 / PB11 | I2C2, AF4 | 400 kHz qua bộ dịch mức 3V3 ↔ 2V8 |
| ToF XSHUT / GPIO1 | PB12 / PB13 | open-drain / EXTI13 | miền 2V8: **không bao giờ đẩy XSHUT lên 3,3 V** |
| Đo pin | PC2 | ADC1 IN12 | cầu chia 100k/100k sau MOSFET chống ngược |
| Buzzer | PB14 | TIM12 CH1, AF9 | còi từ 4 kHz cấp từ VBAT |
| LED trạng thái (xanh) | PC12 | GPIO | tích cực mức thấp |
| PPM | PC11 | EXTI11 + TIM2 | J3.4; PC10 (J3.3) còn trống |
| SWDIO / SWCLK | PA13 / PA14 | SWD | v2: TP2 / TP3; v1: J2.2 / J2.3 |
| BOOT0 | – | JP1 | v2: điện trở 0 Ω +3V3 → BOOT0 (không hàn); v1: cầu hàn mặt dưới. Để hở khi bay |
| **v2** USB D− / D+ | PA11 / PA12 | OTG_FS, AF10 | qua mảng ESD U6 (USBLC6-2SC6) |
| **v2** USB_VBUS_DET | PA9 | GPIO vào | VBUS qua R39 10k / R40 10k → 2,5 V khi cắm cáp |
| **v2** không nối | PA10 | analog | trước là UART1_RX |
| **v1** UART link TX / RX | PA9 / PA10 | USART1, AF7 | J2.5 / J2.6, 460800 8N1; ROM bootloader 115200 8E1 |

## Connector

| Connector | Chân |
|---|---|
| J1 pin (PH2.0) | 1 VBAT_IN, 2 GND (song song với pad BAT+/BAT− mặt dưới) |
| **v2** J2 USB-C (TYPE-C-31-M-12) | VBUS, D+/D− (A6/B6, A7/B7), CC1/CC2 qua 5,1 kΩ (R37/R38) xuống GND = thiết bị, GND |
| **v2** TP1–TP5 (mặt dưới, pad tròn 2 mm, bước 3 mm, cho kim test / pogo pin) | TP1 +3V3 (“3V3”), TP2 SWDIO (“DIO”), TP3 SWCLK (“CLK”), TP4 NRST (“RST”), TP5 GND (“GND”). Nhìn từ mặt trên, hàng pad nằm dưới góc phải-trên của MCU, cạnh J2, thứ tự trái → phải: RST, CLK, DIO, 3V3, GND (lật board nhìn mặt dưới thì ngược lại: GND, 3V3, DIO, CLK, RST; chữ in lụa nằm dưới mỗi pad) |
| **v1** J2 debug 7P (1,25 mm) | 1 +3V3, 2 SWDIO, 3 SWCLK, 4 NRST, 5 UART1_TX, 6 UART1_RX, 7 GND. Nhìn từ mặt trên, chân 1 ở phía dưới, chân 7 ở phía trên |
| J3 RC (SH1.0 4P) | 1 GND, 2 RX_VCC (+3V3 qua JP2, hoặc VBAT qua JP3 – chỉ một trong hai), 3 PC10, 4 PC11 = PPM. Nhìn từ mặt trên, chân 4 là chân ngoài cùng bên trái, chân 1 bên phải |
| J4–J7 motor | chân 1 (“+”) = VBAT, chân 2 = cực máng MOSFET |

Board v1: mạch USB-UART phải là loại **3,3 V**. Nối TX của mạch vào J2.6, RX của mạch vào J2.5, GND vào J2.7. Không cần nối +3V3 nếu quad có pin.

Hình board trên trang web lấy vị trí J2 và TP1–TP5 từ file pick-and-place của layout ngày 06/11/2026; nếu layout còn đổi, tin silkscreen trên PCB.

## USB (board v2)

- USB 2.0 full speed, thiết bị CDC ACM "cổng COM ảo": VID:PID **0483:5740** (ID "Virtual COM Port" của ST, như phần lớn flight controller STM32), số serial = UID 96 bit của MCU (24 chữ số hex, giống UID trên trang web). Windows 10/11 (usbser), Linux (cdc_acm) và macOS nhận ngay, không cần driver. Bootloader và ứng dụng dùng cùng VID/PID/serial, chỉ khác tên sản phẩm (“MiniQuad-F405” / “MiniQuad-F405 Bootloader”): Windows giữ nguyên số COM, Chrome giữ quyền truy cập cổng khi board khởi động lại. Sản phẩm bán ra ngoài cần VID/PID riêng (ví dụ pid.codes).
- Bộ so sánh VBUS bên trong OTG_FS cần 5 V trên PA9, mà PA9 chỉ nhận VBUS/2, nên firmware tắt nó (`NOVBUSSENS`) và đọc PA9 như GPIO: điện trở kéo D+ chỉ bật khi thấy VBUS ổn định 10 ms (`DCTL.SDIS`). Board chạy pin không bao giờ cấp ngược vào D+ của một máy tính đang tắt, và rút/cắm cáp là một phiên kết nối mới.
- USB cần thạch anh 8 MHz (PLL Q = 48 MHz). Nếu thạch anh hỏng, MCU chạy HSI (±1 %) và USB có thể không ổn định: log báo lỗi, cờ `Lỗi thạch anh`.
- Trước mỗi lần reset có chủ đích (khởi động lại, vào bootloader, nạp xong), firmware nhả D+ 20 ms để máy tính thấy thiết bị rời đi.

## Nguồn

- Pin 1S → MOSFET chống ngược → VBAT → U1 RT9193 (bật/tắt bằng S1) → +3V3. Motor và còi luôn lấy từ VBAT.
- **v2:** VBUS → D8 (B5819W) → U7 XC6206P332 → +3V3, **song song** với U1. Chỉ cắm USB-C là MCU, IMU, ToF, LED chạy: cấu hình, nạp firmware, xem cảm biến không cần pin. Motor thì cần pin.
- Khi chỉ có USB, ổn áp của pin có thể dẫn ngược (diode thân / transistor thông) từ +3V3 về VBAT, nên ADC đọc được 2,7–3,3 V dù chưa cắm pin. Firmware vì vậy, khi có VBUS, chỉ coi là có pin khi VBAT > 3,45 V (mất pin khi < 3,35 V): không báo “pin cạn” giả trên bàn. Không có pin thì chặn ARM (`ARMB_NO_BAT`) và từ chối test motor (U7 chỉ cho ~200 mA, motor kéo qua đường dẫn ngược sẽ làm sụt +3V3).
- Gợi ý phần cứng cho lần sửa sau: một diode/ideal-diode ở đầu ra U1 (hoặc ổn áp có chặn dòng ngược) sẽ loại bỏ hẳn hiện tượng dẫn ngược.

## Hướng lắp

- IMU U4 thẳng hàng với PCB: +X hướng mép phải (J2), +Y hướng mép trên (J1/J3), +Z hướng lên (mặt linh kiện).
- Vị trí motor nhìn từ trên: M1 phải-trên, M2 trái-trên, M3 trái-dưới, M4 phải-dưới.
- Tham số `board_yaw` chọn mép nào là mũi (0 = mép trên, 1 = phải, 2 = dưới, 3 = trái) và áp dụng cho **cả** mixer lẫn IMU.
- Chiều quay mặc định (props-in, nhìn từ trên): trước-phải và sau-trái quay ngược chiều kim đồng hồ, hai motor còn lại cùng chiều kim đồng hồ. Đổi bằng `props_out`. Motor coreless đổi chiều bằng cách đổi dây; trang *Hiệu chuẩn & test* vẽ chiều cần có cho từng motor.

## Clock và thời gian

- HSE 8 MHz → PLL (M 8, N 336, P 2, Q 7) → SYSCLK 168 MHz, USB 48 MHz, APB1 42 MHz (timer 84 MHz), APB2 84 MHz, flash 5 wait state, cache bật.
- Hỏng thạch anh: Clock Security System chuyển sang HSI (vẫn 168 MHz), cờ `Lỗi thạch anh` trên trang web; USB của board v2 có thể mất ổn định.
- TIM2: đồng hồ µs 32 bit tự do chạy; SysTick 1 kHz.
