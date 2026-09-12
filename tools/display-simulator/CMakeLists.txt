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
  "${FIRMWARE}/src/display/ui/status_view.cpp"
  "${FIRMWARE}/src/display/status.cpp"
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
add_executable(display_identity_store_test test_identity_store.cpp
  "${FIRMWARE}/src/display/identity_store.cpp"
  "${FIRMWARE}/src/display/identity.cpp"
  "${FIRMWARE}/src/display/contact.cpp")
target_include_directories(display_identity_store_test PRIVATE test-support "${FIRMWARE}/include")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_identity_store_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_identity_store_test PRIVATE /UNDEBUG)
endif()
add_test(NAME display_identity_store_contract COMMAND display_identity_store_test)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FIRMWARE}/src/web/portal_http.cpp")
file(READ "${FIRMWARE}/src/web/portal_http.cpp" PORTAL_SOURCE)
string(FIND "${PORTAL_SOURCE}" "void send_identity_error(" IDENTITY_API_START)
string(FIND "${PORTAL_SOURCE}" "void handle_config_get(" IDENTITY_API_END)
if(IDENTITY_API_START LESS 0 OR IDENTITY_API_END LESS IDENTITY_API_START)
  message(FATAL_ERROR "Identity API handler extraction bounds changed")
endif()
math(EXPR IDENTITY_API_LENGTH "${IDENTITY_API_END} - ${IDENTITY_API_START}")
string(SUBSTRING "${PORTAL_SOURCE}" ${IDENTITY_API_START} ${IDENTITY_API_LENGTH} IDENTITY_API_SOURCE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/identity_routes.inc" "${IDENTITY_API_SOURCE}")
foreach(IDENTITY_DISPLAY IN ITEMS 0 1)
  set(API_TARGET "display_identity_api_${IDENTITY_DISPLAY}_test")
  add_executable(${API_TARGET} test_identity_api.cpp
    "${FIRMWARE}/src/display/identity_store.cpp"
    "${FIRMWARE}/src/display/identity.cpp"
    "${FIRMWARE}/src/display/contact.cpp")
  target_include_directories(${API_TARGET} PRIVATE test-support "${CMAKE_CURRENT_BINARY_DIR}"
    "${FIRMWARE}/include" "${FIRMWARE}/.pio/libdeps/waveshare_lcd169_displaycheck/ArduinoJson/src")
  target_compile_definitions(${API_TARGET} PRIVATE DOG_RGB_DISPLAY_LVGL=${IDENTITY_DISPLAY})
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${API_TARGET} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  elseif(MSVC)
    target_compile_options(${API_TARGET} PRIVATE /UNDEBUG)
  endif()
  add_test(NAME "display_identity_api_${IDENTITY_DISPLAY}_contract" COMMAND ${API_TARGET})
endforeach()
set(IDENTITY_FONT_DIR "${FIRMWARE}/src/display/fonts" CACHE PATH "Identity font variant")
add_executable(display_identity_test test_identity.cpp
  "${FIRMWARE}/src/display/ui/walk_view.cpp"
  "${FIRMWARE}/src/display/ui/connection_view.cpp"
  "${FIRMWARE}/src/display/ui/status_view.cpp"
  "${FIRMWARE}/src/display/identity.cpp"
  "${FIRMWARE}/src/display/contact.cpp"
  "${FIRMWARE}/src/display/ui/contact_qr.cpp"
  "${FIRMWARE}/src/display/ui/identity_view.cpp"
  "${IDENTITY_FONT_DIR}/dog_name_28.c"
  "${IDENTITY_FONT_DIR}/dog_phone_18.c")
target_include_directories(display_identity_test PRIVATE "${FIRMWARE}/include")
target_link_libraries(display_identity_test PRIVATE lvgl)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_identity_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_identity_test PRIVATE /UNDEBUG /utf-8)
endif()
add_test(NAME display_identity_contract COMMAND display_identity_test "${CMAKE_CURRENT_BINARY_DIR}/identity-test-output")
add_executable(display_contact_qr_test test_contact_qr.cpp
  "${FIRMWARE}/src/display/contact.cpp"
  "${FIRMWARE}/src/display/ui/contact_qr.cpp")
target_include_directories(display_contact_qr_test PRIVATE "${FIRMWARE}/include")
target_link_libraries(display_contact_qr_test PRIVATE lvgl)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_contact_qr_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_contact_qr_test PRIVATE /UNDEBUG)
endif()
add_test(NAME display_contact_qr_contract COMMAND display_contact_qr_test "${CMAKE_CURRENT_BINARY_DIR}/qr-test-output")
add_test(NAME display_static_contract COMMAND display_simulator "${CMAKE_CURRENT_BINARY_DIR}/test-output")
add_executable(display_port_test test_port.cpp
  "${FIRMWARE}/src/display/display.cpp"
  "${FIRMWARE}/src/display/lvgl_port.cpp"
  "${FIRMWARE}/src/display/ui/walk_view.cpp"
  "${FIRMWARE}/src/display/ui/connection_view.cpp"
  "${FIRMWARE}/src/display/ui/status_view.cpp"
  "${FIRMWARE}/src/display/status.cpp"
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

add_executable(display_status_adapter_test test_status_adapter.cpp
  "${FIRMWARE}/src/display/status_snapshot.cpp")
target_include_directories(display_status_adapter_test PRIVATE test-support "${FIRMWARE}/include")
target_compile_definitions(display_status_adapter_test PRIVATE DOG_RGB_DISPLAY_LVGL=1)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_status_adapter_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
elseif(MSVC)
  target_compile_options(display_status_adapter_test PRIVATE /UNDEBUG)
endif()
add_test(NAME display_status_adapter_contract COMMAND display_status_adapter_test)
