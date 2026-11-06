cmake_minimum_required(VERSION 3.20)

# The same tree builds three things, selected by the toolchain:
#   * cross build  (-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake): bootloader + application
#   * host build   (native compiler): unit tests, closed-loop simulation, device emulator
#   * wasm build   (-DCMAKE_TOOLCHAIN_FILE=cmake/wasm32-wasi.cmake): emulator for the web demo
project(miniquad_f405
        VERSION 0.1.0
        DESCRIPTION "MiniQuad-F405 bare-metal flight controller"
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)               # gnu11: inline asm, attributes
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MQ_WASM)
  set(CMAKE_C_FLAGS_RELEASE "-O2")
  add_subdirectory(tests)
elseif(CMAKE_CROSSCOMPILING)
  set(CMAKE_C_FLAGS_RELEASE        "-O2 -g")
  set(CMAKE_C_FLAGS_DEBUG          "-Og -g3")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3")
  set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -g")
  add_subdirectory(firmware)
else()
  enable_testing()
  add_subdirectory(tests)
endif()
