# Host build: unit tests, closed-loop flight simulation and the device emulator
# (native, and WebAssembly when configured with cmake/wasm32-wasi.cmake).

set(FW "${PROJECT_SOURCE_DIR}/firmware")
set(HOST_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HOST_GEN_DIR}")

# version header for host builds (same generator as the firmware)
execute_process(COMMAND "${CMAKE_COMMAND}"
  -DOUT=${HOST_GEN_DIR}/fw_version.h -DSRC_DIR=${PROJECT_SOURCE_DIR}
  -DVERSION_MAJOR=${PROJECT_VERSION_MAJOR} -DVERSION_MINOR=${PROJECT_VERSION_MINOR}
  -DVERSION_PATCH=${PROJECT_VERSION_PATCH}
  -P "${PROJECT_SOURCE_DIR}/cmake/gen_version.cmake")

# Hardware independent firmware modules, compiled for the host. Built once per
# board revision: board.h (BOARD_ID, link) changes bl_image.c, bl_core.c ...
set(FW_PURE_SOURCES
  ${FW}/common/util/crc.c
  ${FW}/common/util/cobs.c
  ${FW}/common/util/link.c
  ${FW}/bootloader/bl_image.c
  ${FW}/bootloader/bl_core.c
  ${FW}/bootloader/bl_policy.c
  ${FW}/app/lib/filters.c
  ${FW}/app/estimation/attitude.c
  ${FW}/app/estimation/altitude.c
  ${FW}/app/estimation/imu_cal.c
  ${FW}/app/control/pid.c
  ${FW}/app/control/mixer.c
  ${FW}/app/control/motor_out.c
  ${FW}/app/control/alt_ctrl.c
  ${FW}/app/flight/rc.c
  ${FW}/app/flight/fc.c
  ${FW}/app/flight/fc_events.c
  ${FW}/app/config/params.c
  ${FW}/app/config/config_store.c
  ${FW}/app/flight/ppm_decoder.c
  ${FW}/app/sensors/battery.c
  ${FW}/app/sensors/imu_frame.c
  ${FW}/app/system/log.c
  ${FW}/app/comms/tlm_rings.c
  ${FW}/app/comms/telemetry.c
  ${FW}/app/comms/tlm_format.c
  ${FW}/app/comms/comms.c)
# comms.c / telemetry.c need the svc_* functions (app_services.h): they are
# provided by each executable that uses them (test_comms, the emulators).

function(add_fw_pure name rev)
  add_library(${name} STATIC ${FW_PURE_SOURCES})
  target_include_directories(${name} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/host_include"
    "${FW}/common" "${FW}/common/util"
    "${FW}/bootloader"
    "${FW}/app/lib" "${FW}/app/estimation" "${FW}/app/control" "${FW}/app/flight"
    "${FW}/app/config" "${FW}/app/comms" "${FW}/app/sensors" "${FW}/app/ui"
    "${FW}/app/system"
    "${HOST_GEN_DIR}")
  target_compile_definitions(${name} PUBLIC HOST_BUILD=1 MQ_BOARD_REV=${rev})
  target_compile_options(${name} PUBLIC -Wall -Wextra -Wshadow -Wno-unused-parameter -fno-strict-aliasing)
  target_compile_options(${name} PRIVATE -Wdouble-promotion -Werror)
  target_link_libraries(${name} PUBLIC m)
endfunction()

# Device emulator core (bootloader, ROM loader, application on the simulated
# quad); svc_* for comms.c come from emu_device.c.
function(add_emu_core name pure)
  add_library(${name} OBJECT
    emu/emu_device.c
    emu/emu_rom.c
    sim/sim_harness.c
    sim/quad_model.c
    ${FW}/app/ui/notify.c)
  target_include_directories(${name} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/emu" "${CMAKE_CURRENT_SOURCE_DIR}/sim" "${CMAKE_CURRENT_SOURCE_DIR}/unit"
    "${FW}/app/drivers" "${FW}/app/ui")
  target_link_libraries(${name} PUBLIC ${pure})
  target_compile_options(${name} PRIVATE -Wno-double-promotion)
endfunction()

add_fw_pure(fw_pure ${MQ_BOARD_REV})
add_emu_core(emu_core fw_pure)

if(MQ_WASM)
  # WebAssembly build of the emulator for the web configurator demo mode
  add_executable(miniquad-emu emu/emu_wasm.c)
  target_link_libraries(miniquad-emu PRIVATE emu_core)
  target_link_options(miniquad-emu PRIVATE -mexec-model=reactor -Wl,--strip-debug)
  add_custom_command(TARGET miniquad-emu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/web/demo"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:miniquad-emu>" "${PROJECT_SOURCE_DIR}/web/demo/miniquad-emu.wasm"
    COMMENT "web/demo/miniquad-emu.wasm")
  return()
endif()

function(host_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit" "${CMAKE_CURRENT_SOURCE_DIR}/sim")
  target_link_libraries(${name} PRIVATE fw_pure)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

host_test(test_link       unit/test_link.c)
host_test(test_filters    unit/test_filters.c)
host_test(test_params     unit/test_params.c)
host_test(test_estimation unit/test_estimation.c)
host_test(test_control    unit/test_control.c)
host_test(test_rc         unit/test_rc.c)
host_test(test_bootloader unit/test_bootloader.c)
host_test(test_io         unit/test_io.c)
host_test(test_comms      unit/test_comms.c)
host_test(test_usb        unit/test_usb.c ${FW}/common/usb/usb_cdc.c)
target_include_directories(test_usb PRIVATE "${FW}/common/usb")

# closed-loop flight simulation (physics model in double precision)
add_executable(test_sim_flight sim/test_sim_flight.c sim/sim_harness.c sim/quad_model.c)
target_include_directories(test_sim_flight PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/unit" "${CMAKE_CURRENT_SOURCE_DIR}/sim")
target_link_libraries(test_sim_flight PRIVATE fw_pure)
target_compile_options(test_sim_flight PRIVATE -Wno-double-promotion)
add_test(NAME test_sim_flight COMMAND test_sim_flight)

# emulator: end-to-end C test and the stdio front-end used by web/test
add_executable(test_emu emu/test_emu.c)
target_link_libraries(test_emu PRIVATE emu_core)
add_test(NAME test_emu COMMAND test_emu)

add_executable(miniquad-emu emu/emu_stdio.c)
target_link_libraries(miniquad-emu PRIVATE emu_core)

# the other board revision too: the configurator supports both
if(MQ_BOARD_REV EQUAL 2)
  set(MQ_OTHER_REV 1)
else()
  set(MQ_OTHER_REV 2)
endif()
add_fw_pure(fw_pure_v${MQ_OTHER_REV} ${MQ_OTHER_REV})
add_emu_core(emu_core_v${MQ_OTHER_REV} fw_pure_v${MQ_OTHER_REV})
add_executable(test_emu_v${MQ_OTHER_REV} emu/test_emu.c)
target_link_libraries(test_emu_v${MQ_OTHER_REV} PRIVATE emu_core_v${MQ_OTHER_REV})
add_test(NAME test_emu_v${MQ_OTHER_REV} COMMAND test_emu_v${MQ_OTHER_REV})
add_executable(miniquad-emu-v${MQ_OTHER_REV} emu/emu_stdio.c)
target_link_libraries(miniquad-emu-v${MQ_OTHER_REV} PRIVATE emu_core_v${MQ_OTHER_REV})
