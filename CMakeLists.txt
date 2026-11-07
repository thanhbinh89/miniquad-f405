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

# Board revision (firmware/common/board.h):
#   1 = configurator on USART1 / J2 through a USB-UART adapter
#   2 = USB-C (USB CDC console, configuration and bootloader), SWD on test pads
set(MQ_BOARD_REV 2 CACHE STRING "MiniQuad-F405 board revision (1 = UART J2, 2 = USB-C)")
set_property(CACHE MQ_BOARD_REV PROPERTY STRINGS 1 2)
if(NOT MQ_BOARD_REV MATCHES "^[12]$")
  message(FATAL_ERROR "MQ_BOARD_REV must be 1 or 2 (got '${MQ_BOARD_REV}')")
endif()
message(STATUS "MiniQuad-F405 board revision: ${MQ_BOARD_REV}")

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
