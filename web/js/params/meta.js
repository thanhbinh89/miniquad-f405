/**
 * Vietnamese labels, units and help texts for the parameters of
 * firmware/app/config/params_list.h. Parameters missing here still work
 * (group "Khác", raw name) - the firmware table is the source of truth.
 */

export const GROUPS = [
  ['pid', 'PID tốc độ góc'],
  ['rates', 'Tốc độ & cảm giác lái'],
  ['filters', 'Bộ lọc'],
  ['attitude', 'Ước lượng tư thế'],
  ['motors', 'Motor'],
  ['battery', 'Pin & bù điện áp'],
  ['alt', 'Giữ độ cao (ToF)'],
  ['rc', 'Bộ thu PPM'],
  ['failsafe', 'Failsafe & an toàn'],
  ['board', 'Khung & hướng lắp'],
  ['cal', 'Hiệu chuẩn'],
  ['notify', 'Còi & LED'],
  ['other', 'Khác'],
].map(([id, label]) => ({ id, label }));

const MODE_ENUM = { 0: 'ACRO', 1: 'ANGLE (tự cân bằng)', 2: 'ALT HOLD (giữ độ cao)' };
const AXIS = { roll: 'Roll', pitch: 'Pitch', yaw: 'Yaw' };

const pid = (axis, term) => {
  const names = { p: 'P', i: 'I', d: 'D', f: 'Feedforward' };
  const help = {
    p: 'Phản ứng với sai lệch tốc độ góc. Cao quá: rung nhanh; thấp quá: lỏng, trôi.',
    i: 'Bù sai lệch kéo dài (gió, lệch trọng tâm). Cao quá: dao động chậm sau khi thả cần.',
    d: 'Hãm dao động / overshoot. Cao quá: motor nóng, rung tần số cao.',
    f: 'Phản ứng theo tốc độ đánh cần, giúp bám lệnh nhanh mà không tăng P.',
  };
  return ['pid', `${AXIS[axis]} ${names[term]}`, '', help[term], { decimals: 0, step: 1 }];
};

export const META = {
  /* ---- board ---- */
  board_yaw: ['board', 'Hướng mũi quad', '', 'Cạnh PCB nào là mũi. Áp dụng cho cả mixer và IMU.',
    { enum: { 0: 'Cạnh trên (J1)', 1: 'Cạnh phải (J2)', 2: 'Cạnh dưới', 3: 'Cạnh trái' } }],
  props_out: ['board', 'Cánh quạt quay ra ngoài', '', 'Tắt = props-in (FR/RL quay CCW). Bật = đảo chiều yaw.', { bool: true }],

  /* ---- RC ---- */
  rc_map_roll: ['rc', 'Kênh Roll', '', 'Kênh PPM (1..12) cho roll.', {}],
  rc_map_pitch: ['rc', 'Kênh Pitch', '', 'Kênh PPM cho pitch.', {}],
  rc_map_thr: ['rc', 'Kênh Ga', '', 'Kênh PPM cho ga (throttle).', {}],
  rc_map_yaw: ['rc', 'Kênh Yaw', '', 'Kênh PPM cho yaw.', {}],
  rc_ch_arm: ['rc', 'Kênh ARM', '', 'Công tắc ARM (cao hơn ngưỡng ARM = bật).', {}],
  rc_ch_mode: ['rc', 'Kênh chế độ bay', '', '0 = không dùng (luôn ở chế độ "thấp").', {}],
  rc_ch_beeper: ['rc', 'Kênh còi tìm quad', '', '0 = không dùng.', {}],
  rc_min_us: ['rc', 'Xung nhỏ nhất', 'µs', 'Giá trị kênh khi cần ở tận cùng dưới/trái.', { decimals: 0 }],
  rc_mid_us: ['rc', 'Xung giữa', 'µs', 'Giá trị kênh khi cần ở giữa.', { decimals: 0 }],
  rc_max_us: ['rc', 'Xung lớn nhất', 'µs', 'Giá trị kênh khi cần ở tận cùng trên/phải.', { decimals: 0 }],
  rc_deadband_us: ['rc', 'Vùng chết quanh tâm', 'µs', 'Bỏ qua rung nhỏ của cần quanh vị trí giữa.', { decimals: 0 }],
  rc_arm_us: ['rc', 'Ngưỡng ARM', 'µs', 'Kênh ARM lớn hơn giá trị này = ARM.', { decimals: 0 }],
  rc_smooth_hz: ['rc', 'Làm mượt cần', 'Hz', 'Lọc thông thấp lệnh cần (khung PPM 50 Hz). 0 = tắt.', { decimals: 0 }],
  mode_low: ['rc', 'Công tắc ở THẤP', '', 'Chế độ bay khi công tắc chế độ ở vị trí thấp.', { enum: MODE_ENUM }],
  mode_mid: ['rc', 'Công tắc ở GIỮA', '', 'Chế độ bay khi công tắc ở giữa.', { enum: MODE_ENUM }],
  mode_high: ['rc', 'Công tắc ở CAO', '', 'Chế độ bay khi công tắc ở vị trí cao.', { enum: MODE_ENUM }],
  rx_fs_ms: ['rc', 'Mất tín hiệu sau', 'ms', 'Không nhận được khung PPM hợp lệ quá thời gian này = failsafe.', { decimals: 0 }],
  rx_fs_thr_us: ['rc', 'Ngưỡng ga failsafe của RX', 'µs', 'Một số RX báo mất sóng bằng ga cực thấp. 0 = tắt.', { decimals: 0 }],
  ppm_falling: ['rc', 'PPM theo sườn xuống', '', 'Đổi cực tính nếu RX xuất PPM đảo.', { bool: true }],

  /* ---- rates ---- */
  rate_roll: ['rates', 'Tốc độ Roll tối đa', '°/s', 'Tốc độ xoay khi cần roll ở tận cùng (ACRO).', { decimals: 0, step: 10 }],
  rate_pitch: ['rates', 'Tốc độ Pitch tối đa', '°/s', 'Tốc độ xoay khi cần pitch ở tận cùng (ACRO).', { decimals: 0, step: 10 }],
  rate_yaw: ['rates', 'Tốc độ Yaw tối đa', '°/s', 'Tốc độ quay đầu khi cần yaw ở tận cùng.', { decimals: 0, step: 10 }],
  expo_rp: ['rates', 'Expo Roll/Pitch', '', 'Làm mềm vùng quanh tâm cần (0 = tuyến tính).', { decimals: 2, step: 0.05 }],
  expo_yaw: ['rates', 'Expo Yaw', '', 'Làm mềm vùng quanh tâm cần yaw.', { decimals: 2, step: 0.05 }],
  angle_max: ['rates', 'Góc nghiêng tối đa', '°', 'Góc nghiêng khi cần ở tận cùng (ANGLE / ALT HOLD).', { decimals: 0 }],
  angle_p: ['rates', 'Độ cứng tự cân bằng', '(°/s)/°', 'Tốc độ quay về góc mong muốn trên mỗi độ sai lệch.', { decimals: 1, step: 0.5 }],
  angle_rate_max: ['rates', 'Tốc độ tự cân bằng tối đa', '°/s', 'Giới hạn tốc độ góc do vòng góc yêu cầu.', { decimals: 0 }],
  thr_mid: ['rates', 'Điểm giữa đường cong ga', '', 'Ga ra ở vị trí giữa cần (đặt ≈ ga treo để dễ giữ độ cao).', { decimals: 2, step: 0.01 }],
  thr_expo: ['rates', 'Expo ga', '', 'Làm phẳng đường cong ga quanh điểm giữa.', { decimals: 2, step: 0.05 }],

  /* ---- PID ---- */
  pid_roll_p: pid('roll', 'p'), pid_roll_i: pid('roll', 'i'), pid_roll_d: pid('roll', 'd'), pid_roll_f: pid('roll', 'f'),
  pid_pitch_p: pid('pitch', 'p'), pid_pitch_i: pid('pitch', 'i'), pid_pitch_d: pid('pitch', 'd'), pid_pitch_f: pid('pitch', 'f'),
  pid_yaw_p: pid('yaw', 'p'), pid_yaw_i: pid('yaw', 'i'), pid_yaw_d: pid('yaw', 'd'), pid_yaw_f: pid('yaw', 'f'),
  iterm_limit: ['pid', 'Giới hạn I-term', '', 'Phần lực đẩy tối đa I-term được dùng (chống "windup").', { decimals: 2, step: 0.05 }],
  pid_sum_limit: ['pid', 'Giới hạn tổng PID', '', 'Lực đẩy chênh lệch tối đa cho roll/pitch.', { decimals: 2, step: 0.05 }],
  airmode: ['pid', 'Airmode', '', 'Giữ điều khiển khi ga = 0 (cho bay nhào lộn). Nên tắt với quad nhỏ bay trong nhà.', { bool: true }],

  /* ---- filters ---- */
  gyro_lpf_type: ['filters', 'Kiểu lọc gyro', '', 'Biquad dốc hơn (12 dB/oct) nhưng trễ pha nhiều hơn PT1.', { enum: { 0: 'PT1', 1: 'Biquad' } }],
  gyro_lpf_hz: ['filters', 'Lọc thông thấp gyro', 'Hz', '0 = tắt. Quad coreless rung mạnh ở 300–500 Hz.', { decimals: 0, step: 5 }],
  notch1_hz: ['filters', 'Notch 1 – tâm', 'Hz', 'Tần số trung tâm bộ lọc chắn dải 1 (0 = tắt).', { decimals: 0, step: 5 }],
  notch1_cut: ['filters', 'Notch 1 – cạnh dưới', 'Hz', 'Tần số cắt dưới (độ rộng notch).', { decimals: 0, step: 5 }],
  notch2_hz: ['filters', 'Notch 2 – tâm', 'Hz', 'Tần số trung tâm bộ lọc chắn dải 2 (0 = tắt).', { decimals: 0, step: 5 }],
  notch2_cut: ['filters', 'Notch 2 – cạnh dưới', 'Hz', 'Tần số cắt dưới.', { decimals: 0, step: 5 }],
  dterm_lpf_hz: ['filters', 'Lọc D-term (biquad)', 'Hz', 'Thành phần D khuếch đại nhiễu – lọc mạnh hơn gyro.', { decimals: 0, step: 5 }],
  dterm_lpf2_hz: ['filters', 'Lọc D-term tầng 2 (PT1)', 'Hz', '0 = tắt.', { decimals: 0, step: 5 }],
  acc_lpf_hz: ['filters', 'Lọc gia tốc kế', 'Hz', 'Cho ước lượng tư thế và độ cao.', { decimals: 0 }],

  /* ---- attitude ---- */
  att_kp: ['attitude', 'Mahony Kp', '', 'Mức tin gia tốc kế để kéo về phương thẳng đứng. Cao: ít trôi nhưng nhạy gia tốc ngang.', { decimals: 3, step: 0.05 }],
  att_ki: ['attitude', 'Mahony Ki', '', 'Ước lượng bias gyro còn lại.', { decimals: 3, step: 0.01 }],

  /* ---- motors ---- */
  motor_idle: ['motors', 'Duty chờ khi ARM', '', 'Motor quay chậm khi đã ARM và ga = 0 (báo hiệu đã ARM).', { decimals: 3, step: 0.005 }],
  motor_max: ['motors', 'Duty tối đa', '', 'Giới hạn duty PWM (giảm nếu motor quá nóng).', { decimals: 2, step: 0.05 }],
  thr_lin: ['motors', 'Tuyến tính hoá lực đẩy', '', 'Tỉ lệ bậc hai của đường cong lực đẩy (0 = tuyến tính). Coreless ≈ 0.4–0.6.', { decimals: 2, step: 0.05 }],

  /* ---- battery ---- */
  bat_vscale: ['battery', 'Hệ số chia áp', '', 'Hiệu chỉnh đo điện áp: đo bằng đồng hồ rồi chỉnh cho khớp.', { decimals: 3, step: 0.005 }],
  bat_warn_v: ['battery', 'Ngưỡng pin yếu', 'V', 'Cảnh báo (còi + LED) khi điện áp nghỉ dưới mức này 2 s.', { decimals: 2, step: 0.05 }],
  bat_crit_v: ['battery', 'Ngưỡng pin rất yếu', 'V', 'Cảnh báo khẩn, chặn ARM.', { decimals: 2, step: 0.05 }],
  bat_comp: ['battery', 'Bù lực đẩy theo điện áp', '', 'Tăng duty khi pin sụt áp để lực đẩy không đổi.', { bool: true }],
  bat_vref: ['battery', 'Điện áp tham chiếu', 'V', 'Điện áp mà PID được chỉnh. Hệ số bù = vref / V hiện tại.', { decimals: 2, step: 0.05 }],
  bat_comp_min: ['battery', 'Hệ số bù nhỏ nhất', '', 'Khi pin đầy (V > vref).', { decimals: 2, step: 0.05 }],
  bat_comp_max: ['battery', 'Hệ số bù lớn nhất', '', 'Giới hạn khi pin sụt sâu.', { decimals: 2, step: 0.05 }],
  bat_lpf_hz: ['battery', 'Lọc điện áp cho bù', 'Hz', 'Đủ nhanh để theo kịp sụt áp khi tăng ga.', { decimals: 1, step: 0.5 }],

  /* ---- altitude ---- */
  alt_hover: ['alt', 'Ga treo ban đầu', '', 'Ước lượng ban đầu; firmware tự học trong lúc bay.', { decimals: 2, step: 0.01 }],
  alt_p: ['alt', 'P độ cao', '(m/s)/m', 'Tốc độ leo/hạ trên mỗi mét sai lệch.', { decimals: 2, step: 0.1 }],
  alt_vel_p: ['alt', 'P vận tốc đứng', 'ga/(m/s)', 'Lực đẩy thêm trên mỗi m/s sai lệch vận tốc.', { decimals: 2, step: 0.05 }],
  alt_vel_i: ['alt', 'I vận tốc đứng', '', 'Bù sai lệch ga treo còn lại.', { decimals: 2, step: 0.05 }],
  alt_vz_max: ['alt', 'Tốc độ leo tối đa', 'm/s', 'Khi cần ga ở tận cùng (ALT HOLD).', { decimals: 2, step: 0.1 }],
  alt_deadband: ['alt', 'Vùng giữ độ cao', '', 'Phần hành trình cần ga quanh tâm coi như "giữ".', { decimals: 2, step: 0.01 }],
  alt_max: ['alt', 'Trần bay', 'm', 'Không leo cao hơn (ToF long mode đo tới ~4 m trong nhà).', { decimals: 1, step: 0.1 }],
  alt_land_vz: ['alt', 'Tốc độ hạ cánh', 'm/s', 'Dùng khi failsafe có ToF.', { decimals: 2, step: 0.05 }],
  tof_mode: ['alt', 'Chế độ ToF', '', 'Short chịu ánh nắng tốt hơn, Long đo xa hơn. Cần khởi động lại.', { enum: { 1: 'Short (≤ 1.3 m)', 2: 'Long (≤ 4 m)' } }],
  tof_tb_ms: ['alt', 'Timing budget ToF', 'ms', 'Thời gian mỗi phép đo. Dài hơn = ít nhiễu, chậm hơn. Cần khởi động lại.', { decimals: 0 }],
  tof_offset_mm: ['alt', 'Bù lệch ToF', 'mm', 'Cộng vào khoảng cách đo được.', { decimals: 0 }],
  tof_delay_ms: ['alt', 'Độ trễ đo ToF', 'ms', 'Bộ lọc Kalman bù độ trễ này.', { decimals: 0 }],
  kf_acc_noise: ['alt', 'KF: nhiễu gia tốc', 'm/s²', 'Lớn = tin ToF hơn.', { decimals: 2, step: 0.1 }],
  kf_tof_noise: ['alt', 'KF: nhiễu ToF', 'm', 'Lớn = tin gia tốc kế hơn.', { decimals: 3, step: 0.005 }],
  kf_bias_noise: ['alt', 'KF: trôi bias gia tốc', '', 'Tốc độ học bias gia tốc kế.', { decimals: 3, step: 0.005 }],

  /* ---- failsafe ---- */
  fs_mode: ['failsafe', 'Khi mất tín hiệu', '', '', { enum: { 0: 'Tắt motor ngay', 1: 'Tự hạ cánh' } }],
  fs_thr: ['failsafe', 'Ga hạ cánh không có ToF', '', '0 = 0.85 × ga treo đã học.', { decimals: 2, step: 0.01 }],
  fs_land_s: ['failsafe', 'Thời gian hạ cánh tối đa', 's', 'Sau đó tắt motor.', { decimals: 1 }],
  arm_angle: ['failsafe', 'Góc nghiêng tối đa để ARM', '°', '', { decimals: 0 }],
  crash_angle: ['failsafe', 'Góc phát hiện va chạm', '°', 'Nghiêng quá góc này lâu hơn thời gian bên dưới = tắt motor. 0 = tắt.', { decimals: 0 }],
  crash_ms: ['failsafe', 'Thời gian xác nhận va chạm', 'ms', '', { decimals: 0 }],

  /* ---- calibration ---- */
  acc_off_x: ['cal', 'Offset gia tốc X', 'g', 'Ghi bởi "Hiệu chuẩn mặt phẳng".', { decimals: 4 }],
  acc_off_y: ['cal', 'Offset gia tốc Y', 'g', 'Ghi bởi "Hiệu chuẩn mặt phẳng".', { decimals: 4 }],
  acc_off_z: ['cal', 'Offset gia tốc Z', 'g', 'Ghi bởi "Hiệu chuẩn mặt phẳng".', { decimals: 4 }],
  trim_roll: ['cal', 'Trim Roll', '°', 'Bù trôi ngang ở chế độ ANGLE.', { decimals: 1, step: 0.1 }],
  trim_pitch: ['cal', 'Trim Pitch', '°', 'Bù trôi dọc ở chế độ ANGLE.', { decimals: 1, step: 0.1 }],

  /* ---- notify ---- */
  beep_mask: ['notify', 'Âm báo được bật', '', 'Chọn loại âm báo.', { bitmask: true }],
  beep_hz: ['notify', 'Tần số còi', 'Hz', 'Cộng hưởng của buzzer (thường 2.7–4 kHz).', { decimals: 0, step: 100 }],
};

export function metaFor(name) {
  const m = META[name];
  if (!m) return { group: 'other', label: name, unit: '', help: '', extra: {} };
  const [group, label, unit, help, extra] = m;
  return { group, label, unit, help, extra: extra || {} };
}
