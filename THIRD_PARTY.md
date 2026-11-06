# Thành phần của bên thứ ba

Mã nguồn của dự án (firmware, công cụ, trang cấu hình) do chủ dự án giữ bản quyền; giấy phép cho phần này do chủ dự án quyết định. Các thành phần dưới đây đến từ bên ngoài và giữ nguyên giấy phép gốc.

| Thành phần | Nằm ở | Nguồn | Giấy phép |
|---|---|---|---|
| CMSIS Core (Cortex-M4 headers) | `firmware/lib/cmsis/core/` | ARM-software/CMSIS_5, commit `55b19837f5703e418ca37894d5745b1dc05e4c91` | Apache-2.0 (`firmware/lib/cmsis/core/LICENSE.txt`) |
| CMSIS Device STM32F4 | `firmware/lib/cmsis/device/` | STMicroelectronics/cmsis_device_f4, commit `9192c7b9df75a142f2027ab266601fe061fc00b3` | Apache-2.0 (`firmware/lib/cmsis/device/LICENSE.md`) |
| Bảng cấu hình mặc định, bảng timing và bảng mã trạng thái của VL53L1 | `firmware/app/sensors/vl53l1.c` | ST VL53L1X Ultra Lite Driver (STSW-IMG009) | BSD-3-Clause (ULD được ST cấp phép kép, ST Proprietary hoặc BSD-3-Clause; dự án dùng BSD-3-Clause, văn bản bên dưới) |
| Font Be Vietnam Pro | `web/fonts/be-vietnam-pro-*` | bettergui/BeVietnamPro qua @fontsource/be-vietnam-pro 5.3.0 | SIL Open Font License 1.1 (`web/fonts/LICENSE-BeVietnamPro.txt`) |
| Font Barlow Semi Condensed | `web/fonts/barlow-semi-condensed-*` | jpt/barlow qua @fontsource/barlow-semi-condensed 5.3.0 | SIL Open Font License 1.1 (`web/fonts/LICENSE-Barlow.txt`) |
| wasi-libc (gồm một phần musl) liên kết tĩnh vào bản WebAssembly | `web/demo/miniquad-emu.wasm` (chỉ file build) | WebAssembly/wasi-libc (gói Ubuntu `wasi-libc`) | MIT / Apache-2.0 WITH LLVM-exception; phần musl: MIT |
| compiler-rt builtins liên kết vào bản WebAssembly | `web/demo/miniquad-emu.wasm` (chỉ file build) | LLVM | Apache-2.0 WITH LLVM-exception |

Driver VL53L1 trong `vl53l1.c` được viết lại hoàn toàn (máy trạng thái bất đồng bộ, I2C qua DMA); chỉ các bảng số liệu và trình tự thanh ghi là lấy từ ULD.

## BSD-3-Clause (VL53L1X Ultra Lite Driver)

```
Copyright (c) 2017, STMicroelectronics - All Rights Reserved

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## MIT (musl / wasi-libc, cho `web/demo/miniquad-emu.wasm`)

```
Copyright © 2005-2020 Rich Felker, et al.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
