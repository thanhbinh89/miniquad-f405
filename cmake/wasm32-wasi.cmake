# Toolchain: clang + wasi-libc -> WebAssembly (device emulator for the web demo).
#
#   Ubuntu/Debian: apt install clang lld wasi-libc libclang-rt-18-dev-wasm32
#   cmake --preset wasm-emu && cmake --build --preset wasm-emu
#
# Output: web/demo/miniquad-emu.wasm (no imports needed at run time).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

find_program(WASM_CLANG NAMES clang clang-18 clang-17 clang-16 REQUIRED)
set(CMAKE_C_COMPILER "${WASM_CLANG}")
set(CMAKE_C_COMPILER_TARGET wasm32-wasi)
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(DEFINED ENV{WASI_SYSROOT})
  set(CMAKE_SYSROOT "$ENV{WASI_SYSROOT}")
endif()

set(MQ_WASM ON)
