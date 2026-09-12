cmake_minimum_required(VERSION 3.20)
project(dog_rgb_display LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(FIRMWARE "${REPO_ROOT}/Platformio/Dog-RGB")
set(LVGL_SOURCE_DIR "${FIRMWARE}/.pio/libdeps/waveshare_lcd169_displaycheck/lvgl" CACHE PATH "Pinned LVGL source")
if(NOT EXISTS "${LVGL_SOURCE_DIR}/library.json")
  message(FATAL_ERROR "Resolve dependencies first: pio pkg install -e waveshare_lcd169_displaycheck (in Platformio/Dog-RGB)")
endif()
file(READ "${LVGL_SOURCE_DIR}/library.json" LVGL_METADATA)
string(JSON LVGL_VERSION GET "${LVGL_METADATA}" version)
if(NOT LVGL_VERSION STREQUAL "8.4.0")
  message(FATAL_ERROR "Firmware and simulator require LVGL 8.4.0")
endif()
set(LV_CONF_PATH "${FIRMWARE}/include/lv_conf.h" CACHE STRING "Shared configuration" FORCE)
add_subdirectory("${LVGL_SOURCE_DIR}" lvgl EXCLUDE_FROM_ALL)
add_executable(display_simulator main.cpp
  "${FIRMWARE}/src/display/ui/walk_view.cpp"
  "${FIRMWARE}/src/display/ui/connection_view.cpp"
  "${FIRMWARE}/src/display/connection.cpp"
  "${FIRMWARE}/src/display/text_view.cpp"
  "${FIRMWARE}/src/led/led_state.cpp")
target_include_directories(display_simulator PRIVATE "${FIRMWARE}/include")
target_link_libraries(display_simulator PRIVATE lvgl)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_simulator PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_simulator PRIVATE /UNDEBUG)
endif()
enable_testing()
add_test(NAME display_static_contract COMMAND display_simulator "${CMAKE_CURRENT_BINARY_DIR}/test-output")
add_executable(display_port_test test_port.cpp
  "${FIRMWARE}/src/display/display.cpp"
  "${FIRMWARE}/src/display/lvgl_port.cpp"
  "${FIRMWARE}/src/display/ui/walk_view.cpp"
  "${FIRMWARE}/src/display/ui/connection_view.cpp"
  "${FIRMWARE}/src/display/connection.cpp"
  "${FIRMWARE}/src/display/text_view.cpp"
  "${FIRMWARE}/src/led/led_state.cpp")
target_include_directories(display_port_test PRIVATE test-support "${FIRMWARE}/include")
target_compile_definitions(display_port_test PRIVATE DOG_RGB_BOARD_WAVESHARE_LCD169_V2=1
  DOG_RGB_DISPLAY_ENABLED=1 DOG_RGB_DISPLAY_LVGL=1 DOG_RGB_BRINGUP_STAGE=3)
target_link_libraries(display_port_test PRIVATE lvgl)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_port_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_port_test PRIVATE /UNDEBUG)
endif()
add_test(NAME display_port_contract COMMAND display_port_test)
add_test(NAME display_port_failure COMMAND display_port_test fail)
add_executable(display_connection_adapter_test test_connection_adapter.cpp
  "${FIRMWARE}/src/display/connection_snapshot.cpp")
target_include_directories(display_connection_adapter_test PRIVATE test-support "${FIRMWARE}/include")
target_compile_definitions(display_connection_adapter_test PRIVATE DOG_RGB_DISPLAY_LVGL=1)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_connection_adapter_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_connection_adapter_test PRIVATE /UNDEBUG)
endif()
add_test(NAME display_connection_adapter_contract COMMAND display_connection_adapter_test)
