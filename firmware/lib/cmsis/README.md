# CMSIS (vendored)

Only the headers required for a bare-metal STM32F405 build are vendored here.

| Directory | Upstream | Commit | License |
|-----------|----------|--------|---------|
| `core/`   | https://github.com/ARM-software/CMSIS_5 (`CMSIS/Core/Include`) | 55b19837f5703e418ca37894d5745b1dc05e4c91 | Apache-2.0 (`core/LICENSE.txt`) |
| `device/` | https://github.com/STMicroelectronics/cmsis_device_f4 (`Include`) | 9192c7b9df75a142f2027ab266601fe061fc00b3 | Apache-2.0 (`device/LICENSE.md`) |

Files are unmodified. `system_stm32f4xx.h` only declares `SystemInit()`/`SystemCoreClock`;
the implementations live in `firmware/common/hal/clock.c` and the startup file.
