# Flight controller application (linked at MM_APP_BASE, behind the bootloader).

set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(app
  ${FW_COMMON_SOURCES}
  ${APP_DIR}/main.c
  ${APP_DIR}/app_header.c
  ${APP_DIR}/app_services.c
  # system
  ${APP_DIR}/system/systime.c
  ${APP_DIR}/system/sched.c
  ${APP_DIR}/system/fault.c
  ${APP_DIR}/system/watchdog.c
  ${APP_DIR}/system/log.c
  # drivers
  ${APP_DIR}/drivers/exti.c
  ${APP_DIR}/drivers/spi1.c
  ${APP_DIR}/drivers/i2c2.c
  ${APP_DIR}/drivers/adc.c
  ${APP_DIR}/drivers/motor_pwm.c
  ${APP_DIR}/drivers/ppm_input.c
  ${APP_DIR}/drivers/buzzer.c
  ${APP_DIR}/drivers/led.c
  # sensors
  ${APP_DIR}/sensors/icm42688.c
  ${APP_DIR}/sensors/vl53l1.c
  ${APP_DIR}/sensors/battery.c
  ${APP_DIR}/sensors/imu_frame.c
  # algorithms (hardware independent, also built by tests/)
  ${APP_DIR}/lib/filters.c
  ${APP_DIR}/estimation/attitude.c
  ${APP_DIR}/estimation/altitude.c
  ${APP_DIR}/estimation/imu_cal.c
  ${APP_DIR}/control/pid.c
  ${APP_DIR}/control/mixer.c
  ${APP_DIR}/control/motor_out.c
  ${APP_DIR}/control/alt_ctrl.c
  ${APP_DIR}/flight/rc.c
  ${APP_DIR}/flight/ppm_decoder.c
  ${APP_DIR}/flight/fc.c
  ${APP_DIR}/flight/fc_events.c
  ${APP_DIR}/flight/flight_task.c
  # configuration + protocol
  ${APP_DIR}/config/params.c
  ${APP_DIR}/config/config_store.c
  ${APP_DIR}/config/config_store_target.c
  ${APP_DIR}/comms/comms.c
  ${APP_DIR}/comms/telemetry.c
  ${APP_DIR}/comms/tlm_format.c
  ${APP_DIR}/comms/tlm_rings.c
  # user interface
  ${APP_DIR}/ui/notify.c)

target_include_directories(app PRIVATE
  "${APP_DIR}"
  "${APP_DIR}/system" "${APP_DIR}/drivers" "${APP_DIR}/sensors" "${APP_DIR}/lib"
  "${APP_DIR}/estimation" "${APP_DIR}/control" "${APP_DIR}/flight" "${APP_DIR}/config"
  "${APP_DIR}/comms" "${APP_DIR}/ui")
target_link_libraries(app PRIVATE fw_options)
add_dependencies(app fw_version)

fw_use_linker_script(app "${APP_DIR}/app.ld.in")
fw_post_build(app)
