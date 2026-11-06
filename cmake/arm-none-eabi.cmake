# CMake toolchain file for the GNU Arm Embedded toolchain (arm-none-eabi).
#
# Works with the compiler shipped in STM32CubeCLT / STM32CubeIDE as well as
# with Linux distribution packages. The toolchain is searched in this order:
#   1. -DARM_TOOLCHAIN_DIR=<dir containing arm-none-eabi-gcc>
#   2. environment variable ARM_TOOLCHAIN_DIR
#   3. PATH
#   4. default STM32CubeCLT install locations (Windows)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
else()
  set(_exe "")
endif()

if(NOT ARM_TOOLCHAIN_DIR AND DEFINED ENV{ARM_TOOLCHAIN_DIR})
  set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()

if(NOT ARM_TOOLCHAIN_DIR)
  find_program(_arm_gcc_on_path arm-none-eabi-gcc)
  if(_arm_gcc_on_path)
    get_filename_component(ARM_TOOLCHAIN_DIR "${_arm_gcc_on_path}" DIRECTORY)
  elseif(CMAKE_HOST_WIN32)
    file(GLOB _cube_candidates
         "C:/ST/STM32CubeCLT*/GNU-tools-for-STM32/bin"
         "C:/ST/STM32CubeIDE*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32*/tools/bin")
    if(_cube_candidates)
      list(GET _cube_candidates 0 ARM_TOOLCHAIN_DIR)
    endif()
  endif()
endif()

if(ARM_TOOLCHAIN_DIR)
  file(TO_CMAKE_PATH "${ARM_TOOLCHAIN_DIR}" ARM_TOOLCHAIN_DIR)
  set(_prefix "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
  set(_prefix "arm-none-eabi-")
endif()
set(ARM_TOOLCHAIN_DIR "${ARM_TOOLCHAIN_DIR}" CACHE PATH "Directory containing arm-none-eabi-gcc")

set(CMAKE_C_COMPILER   "${_prefix}gcc${_exe}")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_exe}")
set(CMAKE_OBJCOPY      "${_prefix}objcopy${_exe}" CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP      "${_prefix}objdump${_exe}" CACHE FILEPATH "objdump")
set(CMAKE_SIZE         "${_prefix}size${_exe}"    CACHE FILEPATH "size")

set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT
    "${MCU_FLAGS} -ffunction-sections -fdata-sections -fno-common -fno-math-errno -fsingle-precision-constant")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -nostartfiles -Wl,--gc-sections -specs=nano.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
