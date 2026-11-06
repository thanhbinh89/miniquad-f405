# Phần cứng ↔ firmware

Nguồn sự thật: `firmware/common/board.h`. Board 44 × 48 mm, lỗ bắt khung 20 × 20 mm.

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
| UART link TX / RX | PA9 / PA10 | USART1, AF7 | J2.5 / J2.6, 460800 8N1; ROM bootloader 115200 8E1 |
| PPM | PC11 | EXTI11 + TIM2 | J3.4; PC10 (J3.3) còn trống |
| SWDIO / SWCLK | PA13 / PA14 | SWD | J2.2 / J2.3 |
| BOOT0 | – | JP1 | cầu hàn mặt dưới, để hở khi bay |

## Connector

| Connector | Chân |
|---|---|
| J1 pin (PH2.0) | 1 VBAT_IN, 2 GND (song song với pad BAT+/BAT− mặt dưới) |
| J2 debug 7P | 1 +3V3, 2 SWDIO, 3 SWCLK, 4 NRST, 5 UART1_TX, 6 UART1_RX, 7 GND |
| J3 RC (SH1.0 4P) | 1 GND, 2 RX_VCC (+3V3 qua JP2, hoặc VBAT qua JP3 – chỉ một trong hai), 3 PC10, 4 PC11 = PPM |
| J4–J7 motor | chân 1 (“+”) = VBAT, chân 2 = cực máng MOSFET |

Mạch USB-UART phải là loại **3,3 V**. Nối TX của mạch vào J2.6, RX của mạch vào J2.5, GND vào J2.7. Không cần nối +3V3 nếu quad có pin.

## Hướng lắp

- IMU U4 thẳng hàng với PCB: +X hướng mép phải (J2), +Y hướng mép trên (J1/J3), +Z hướng lên (mặt linh kiện).
- Vị trí motor nhìn từ trên: M1 phải-trên, M2 trái-trên, M3 trái-dưới, M4 phải-dưới.
- Tham số `board_yaw` chọn mép nào là mũi (0 = mép trên, 1 = phải, 2 = dưới, 3 = trái) và áp dụng cho **cả** mixer lẫn IMU.
- Chiều quay mặc định (props-in, nhìn từ trên): trước-phải và sau-trái quay ngược chiều kim đồng hồ, hai motor còn lại cùng chiều kim đồng hồ. Đổi bằng `props_out`. Motor coreless đổi chiều bằng cách đổi dây; trang *Hiệu chuẩn & test* vẽ chiều cần có cho từng motor.

## Clock và thời gian

- HSE 8 MHz → PLL → SYSCLK 168 MHz, APB1 42 MHz (timer 84 MHz), APB2 84 MHz, flash 5 wait state, cache bật.
- Hỏng thạch anh: Clock Security System chuyển sang HSI (vẫn 168 MHz), cờ `Lỗi thạch anh` trên trang web.
- TIM2: đồng hồ µs 32 bit tự do chạy; SysTick 1 kHz.
