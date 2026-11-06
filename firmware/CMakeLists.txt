# Cross build: bootloader + application + packaged images.

set(FW_DIR        "${CMAKE_CURRENT_SOURCE_DIR}")
set(FW_COMMON_DIR "${FW_DIR}/common")
set(FW_GEN_DIR    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FW_OUT_DIR    "${CMAKE_BINARY_DIR}/out")
file(MAKE_DIRECTORY "${FW_GEN_DIR}" "${FW_OUT_DIR}")

option(FW_WERROR "Treat compiler warnings as errors" ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---------------------------------------------------------------------------
# fw_version.h - regenerated on every build, rewritten only when it changes
# ---------------------------------------------------------------------------
add_custom_target(fw_version
  COMMAND "${CMAKE_COMMAND}"
          -DOUT=${FW_GEN_DIR}/fw_version.h
          -DSRC_DIR=${PROJECT_SOURCE_DIR}
          -DVERSION_MAJOR=${PROJECT_VERSION_MAJOR}
          -DVERSION_MINOR=${PROJECT_VERSION_MINOR}
          -DVERSION_PATCH=${PROJECT_VERSION_PATCH}
          -P "${PROJECT_SOURCE_DIR}/cmake/gen_version.cmake"
  BYPRODUCTS "${FW_GEN_DIR}/fw_version.h"
  COMMENT "Generating fw_version.h"
  VERBATIM)

# ---------------------------------------------------------------------------
# Options shared by every firmware target
# ---------------------------------------------------------------------------
add_library(fw_options INTERFACE)
target_compile_definitions(fw_options INTERFACE STM32F405xx)
target_include_directories(fw_options INTERFACE
  "${FW_DIR}/lib/cmsis/core"
  "${FW_DIR}/lib/cmsis/device"
  "${FW_COMMON_DIR}"
  "${FW_COMMON_DIR}/hal"
  "${FW_COMMON_DIR}/util"
  "${FW_GEN_DIR}")
target_compile_options(fw_options INTERFACE
  -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef -Wformat=2
  -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-align
  -Wno-unused-parameter
  $<$<BOOL:${FW_WERROR}>:-Werror>)
target_link_libraries(fw_options INTERFACE m)

# Sources shared by bootloader and application
set(FW_COMMON_SOURCES
  "${FW_COMMON_DIR}/hal/startup_stm32f405.c"
  "${FW_COMMON_DIR}/hal/clock.c"
  "${FW_COMMON_DIR}/hal/uart_link.c"
  "${FW_COMMON_DIR}/hal/flash_if.c"
  "${FW_COMMON_DIR}/util/crc.c"
  "${FW_COMMON_DIR}/util/cobs.c"
  "${FW_COMMON_DIR}/util/link.c")

# Preprocess <in>.ld.in with the C preprocessor and link <target> with it.
function(fw_use_linker_script target in)
  set(out "${CMAKE_CURRENT_BINARY_DIR}/${target}.ld")
  add_custom_command(OUTPUT "${out}"
    COMMAND "${CMAKE_C_COMPILER}" -E -P -x c -I "${FW_COMMON_DIR}" "${in}" -o "${out}"
    DEPENDS "${in}" "${FW_COMMON_DIR}/memory_map.h"
    COMMENT "Preprocessing ${target} linker script"
    VERBATIM)
  add_custom_target(${target}_ld DEPENDS "${out}")
  add_dependencies(${target} ${target}_ld)
  target_link_options(${target} PRIVATE
    "-T${out}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map,--cref"
    "-Wl,--print-memory-usage")
  set_target_properties(${target} PROPERTIES LINK_DEPENDS "${out}" SUFFIX ".elf")
endfunction()

# Raw binary next to the ELF + size report.
function(fw_post_build target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary --gap-fill 0xFF
            "$<TARGET_FILE:${target}>" "$<TARGET_FILE_DIR:${target}>/${target}.raw.bin"
    COMMAND "${CMAKE_SIZE}" -A -x "$<TARGET_FILE:${target}>"
    COMMENT "objcopy + size ${target}"
    VERBATIM)
endfunction()

add_subdirectory(bootloader)
add_subdirectory(app)

# ---------------------------------------------------------------------------
# Packaging: patch the app header (size + CRC), produce bin/hex/elf images,
# a merged full image and manifest.json in build/<preset>/out
# ---------------------------------------------------------------------------
add_custom_target(firmware_package ALL
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/fwpack.py"
          --app-elf "$<TARGET_FILE:app>"
          --app-raw "$<TARGET_FILE_DIR:app>/app.raw.bin"
          --bl-raw  "$<TARGET_FILE_DIR:bootloader>/bootloader.raw.bin"
          --bl-elf  "$<TARGET_FILE:bootloader>"
          --objcopy "${CMAKE_OBJCOPY}"
          --version "${PROJECT_VERSION}"
          --memory-map "${FW_COMMON_DIR}/memory_map.h"
          --out-dir "${FW_OUT_DIR}"
  DEPENDS app bootloader "${PROJECT_SOURCE_DIR}/tools/fwpack.py"
  COMMENT "Packaging firmware images into ${FW_OUT_DIR}"
  VERBATIM)
