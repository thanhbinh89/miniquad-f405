# Bootloader (USB CDC on board rev 2, USART1 on rev 1) - must fit in sector 0 (16 KB).

add_executable(bootloader
  ${FW_COMMON_SOURCES}
  bl_main.c
  bl_core.c
  bl_image.c
  bl_policy.c)

target_include_directories(bootloader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bootloader PRIVATE fw_options)
target_compile_options(bootloader PRIVATE -Os)
add_dependencies(bootloader fw_version)

fw_use_linker_script(bootloader "${CMAKE_CURRENT_SOURCE_DIR}/bootloader.ld.in")
fw_post_build(bootloader)
