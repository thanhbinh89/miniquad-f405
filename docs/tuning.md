# Bay thử và tinh chỉnh

Thông số mặc định được kiểm chứng trên mô hình quad 30 g, motor coreless, pin 1S (xem `tests/sim`). Quad thật sẽ khác: đi lần lượt các bước dưới đây, lưu vào flash sau mỗi bước ổn.

## 1. Trước chuyến bay đầu

1. **Kết nối** trang cấu hình (board v2: cáp USB-C; v1: USB-UART vào J2, 460800 baud). Bay thử thì rút cáp ra. Trang *Tổng quan* phải có cờ `IMU`, `Gyro cal`, `Pin`; vòng điều khiển 1000 Hz.
2. **Hướng lắp:** nghiêng quad bằng tay. Chúc mũi xuống → Pitch âm; nghiêng sang phải → Roll dương; quay mũi sang phải → Yaw tăng. Sai thì sửa `board_yaw` ở *Hiệu chuẩn & test*.
3. **Hiệu chuẩn gia tốc kế:** đặt quad trên mặt phẳng nằm ngang, bấm *Hiệu chuẩn mặt phẳng*, giữ yên ~1 s. Offset được tự lưu vào flash. Gyro tự hiệu chuẩn mỗi lần bật nguồn khi quad đứng yên (cờ `Gyro cal`).
4. **Pin:** so điện áp trên trang với đồng hồ đo, chỉnh `bat_vscale` nếu lệch hơn 0,03 V.
5. **Bộ thu:** trang *Hiệu chuẩn & test*, phần J3: gạt từng cần, kiểm tra đúng kênh và chiều (roll phải → tăng, pitch lên trước → tăng, ga lên → tăng, yaw phải → tăng). Đặt `rc_ch_arm`, `rc_ch_mode`, chế độ cho 3 vị trí công tắc. Nếu không có sóng: kiểm tra J3.4 và `ppm_falling`.
6. **Motor (THÁO CÁNH):** phần *Test motor*, kéo từng motor: đúng vị trí (M1 phải-trên…) và đúng chiều quay như hình vẽ. Sai chiều thì đổi hai dây motor đó.
7. Lắp cánh đúng chiều, trang *Tổng quan* phải báo *Sẵn sàng ARM* khi ga thấp và công tắc ARM đang OFF.

## 2. ARM và chế độ bay

- ARM bằng công tắc (`rc_ch_arm`, ngưỡng `rc_arm_us`). Nếu bị chặn, trang *Tổng quan* liệt kê lý do và còi kêu 3 tiếng trầm.
- Công tắc phải được gạt về OFF trước khi ARM (sau khi bật nguồn, sau failsafe, sau va chạm).
- Chế độ mặc định: công tắc thấp = **ANGLE** (tự cân bằng), giữa = **ALT HOLD** (giữ độ cao bằng ToF), cao = **ACRO** (giữ tốc độ góc).

## 3. LED và còi

| LED xanh | Ý nghĩa |
|---|---|
| sáng liên tục | đang ARM (tắt chớp ngắn mỗi 1,6 s khi pin yếu) |
| chớp 1 lần mỗi chu kỳ | DISARM, sẵn sàng |
| chớp 2 lần | DISARM, đang bị chặn ARM |
| nháy rất nhanh | đang hiệu chuẩn |
| nháy 5 Hz | failsafe |
| nháy 2,5 Hz | pin rất yếu |
| sáng/tắt 0,8 s | IMU lỗi |
| nháy so le | đang test motor |

| Còi | Ý nghĩa |
|---|---|
| 3 nốt lên | khởi động |
| 1 tiếng dài / 2 tiếng ngắn | ARM / DISARM |
| 2 tiếng cao | hiệu chuẩn xong; 1 tiếng trầm dài = hiệu chuẩn lỗi |
| 3 tiếng trầm | lệnh bị từ chối (ARM bị chặn, ALT HOLD không dùng được) |
| bíp đều 5 lần/giây | failsafe |
| bíp ngắn mỗi 0,5 s | pin rất yếu; bíp rất ngắn mỗi 2 s = pin yếu |
| bíp 0,3 s mỗi giây sau khi rơi | tìm quad (sau va chạm, khi DISARM) |

Từng nhóm âm bật/tắt bằng `beep_mask`; tần số cộng hưởng của còi là `beep_hz`.

## 4. PID tốc độ góc

Trang *Tinh chỉnh*, phần *Vòng tốc độ góc*, lấy mẫu 200–500 Hz. Mỗi trục có đường đứt (tốc độ góc mong muốn) và đường liền (gyro đã lọc). Mọi thay đổi PID có hiệu lực ngay, kể cả khi đang bay.

1. **P:** tăng dần đến khi thấy rung nhanh sau khi giật cần, rồi lùi 20–30 %. P thấp: quad “lỏng”, gyro trễ xa setpoint.
2. **D:** tăng để triệt overshoot (đường liền vượt đường đứt rồi dao động). D quá cao: motor nóng, rung tần số cao, thành phần D nhảy loạn trong biểu đồ *Thành phần PID*.
3. **I:** tăng đến khi quad không trôi khi bay thẳng, giữ góc sau khi thả cần. I quá cao: dao động chậm (~1–2 Hz) sau khi thả cần.
4. **Feedforward:** tăng để gyro bám kịp setpoint khi đánh cần nhanh, không làm tăng rung.
5. Yaw: thường chỉ cần P và I.

Thang đo giống Betaflight (P 55, I 60, D 28 mặc định) nên kinh nghiệm từ Betaflight dùng được, nhưng quad coreless có quán tính nhỏ và motor chậm: thường cần D thấp hơn, I cao hơn.

`iterm_limit` giới hạn I-term (chống windup); `pid_sum_limit` giới hạn lực đẩy chênh lệch cho roll/pitch. `airmode` (giữ điều khiển khi ga 0) chỉ nên bật cho bay nhào lộn.

## 5. Bộ lọc

Phần *Gyro & bộ lọc*: gyro thô vs đã lọc, và phổ rung 0–250 Hz (luồng IMU 500 Hz).

- Cầm chắc quad (hoặc bay treo), tăng ga dần: đỉnh lớn trong phổ là rung của motor/cánh.
- Đỉnh ổn định ở một tần số → đặt `notch1_hz` ngay tại đó, `notch1_cut` thấp hơn ~30–40 % (độ rộng). Đỉnh trên 250 Hz bị gập xuống (aliasing): nhìn gyro thô có “răng cưa” đều mà phổ lại thấp, hãy hạ `gyro_lpf_hz`.
- `gyro_lpf_hz` mặc định 120 Hz (biquad). Thấp hơn: êm hơn nhưng trễ pha, phải giảm P/D.
- D-term khuếch đại nhiễu nhiều nhất: `dterm_lpf_hz` (biquad) + `dterm_lpf2_hz` (PT1).

## 6. Bù điện áp pin

Lực đẩy motor brushed tỉ lệ gần với bình phương điện áp. Firmware nhân duty với `bat_vref / V_pin` (lọc `bat_lpf_hz`, kẹp trong `bat_comp_min…bat_comp_max`) để lực đẩy và độ nhạy PID không đổi khi pin sụt.

- `bat_vref` là điện áp mà bạn chỉnh PID: để mặc định 3,9 V, hoặc bằng điện áp trung bình khi bay.
- Kiểm tra: trong ALT HOLD, *ga treo đã học* (trang *Tổng quan*) gần như không đổi từ pin đầy đến pin yếu. Trong mô phỏng: ga treo đổi 0,006 khi có bù, 0,084 khi tắt bù.

## 7. Giữ độ cao (ALT HOLD)

Cần ToF đo được mặt đất: bề mặt không quá tối/bóng, ánh sáng không quá mạnh, nghiêng dưới ~32°, độ cao 4 cm – 4 m (`tof_mode` Long; Short tới ~1,3 m nhưng chịu nắng tốt hơn).

- Cần ga ở giữa (± `alt_deadband`): giữ độ cao. Đẩy lên/xuống: leo/hạ với tốc độ tới `alt_vz_max`.
- Từ dưới đất: đẩy ga quá tâm → cất cánh (ga tăng dần, ga lúc nhấc lên được dùng làm ga treo), rồi giữ độ cao khi ToF thấy ≥ 10 cm.
- Hạ cánh: giữ ga thấp; dưới 20 cm (ToF hết tầm) firmware giảm ga từ từ, phát hiện chạm đất rồi để motor ở mức chờ. DISARM bằng công tắc.
- Gạt sang ALT HOLD khi đang bay: giữ độ cao hiện tại (có tính quãng phanh). Không có số đo độ cao hợp lệ → còi báo lỗi, ở lại ANGLE cho tới khi gạt công tắc ra khỏi ALT.
- Tinh chỉnh (phần *Giữ độ cao*): `alt_vel_p` trước (dao động đứng nhanh = cao quá), rồi `alt_vel_i` (bù ga treo còn lệch), rồi `alt_p` (độ cứng giữ độ cao). Bộ lọc Kalman: `kf_tof_noise` lớn = tin gia tốc kế hơn; `tof_delay_ms` là độ trễ phép đo (≈ timing budget / 2 + truyền).

## 8. Failsafe

Mất PPM quá `rx_fs_ms`: với `fs_mode = 1` quad tự hạ cánh (có ToF: theo `alt_land_vz`; không: ga 0,85 × ga treo hoặc `fs_thr`) rồi tắt motor khi chạm đất hoặc sau `fs_land_s`; có lại sóng ≥ 0,5 s thì trả quyền điều khiển. `fs_mode = 0`: tắt motor ngay.

Thử an toàn: tháo cánh (hoặc cầm chắc quad), ARM, tắt tay điều khiển: LED nháy 5 Hz, còi bíp, motor giảm dần rồi tắt. Trong chế độ mô phỏng: bỏ chọn *Bộ phát bật*.

## 9. Gặp sự cố

| Hiện tượng | Kiểm tra |
|---|---|
| Không ARM được | danh sách lý do ở *Tổng quan*; thường là ga chưa về thấp nhất (`rc_min_us`), công tắc ARM chưa gạt lại, quad nghiêng hơn `arm_angle` |
| Quad lật ngay khi cất cánh | thứ tự motor, chiều quay, `board_yaw`, `props_out` |
| Trôi về một phía trong ANGLE | hiệu chuẩn mặt phẳng lại; tinh chỉnh `trim_roll` / `trim_pitch` |
| Rung, motor nóng | giảm D, hạ `dterm_lpf_hz`, thêm notch theo phổ rung, kiểm tra cánh cong |
| ALT HOLD nhấp nhô | giảm `alt_vel_p`, tăng `kf_tof_noise`, kiểm tra mặt đất dưới ToF |
| Board tự reset khi bay | trang *Firmware* > *Fault* (lý do reset: sụt áp, watchdog, HardFault + địa chỉ PC) |
