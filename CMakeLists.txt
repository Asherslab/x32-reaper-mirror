cmake_minimum_required(VERSION 3.19)
project(x32-reaper-mirror LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(X32MIRROR_BUILD_TESTS  "Build unit tests and headless integration" ON)
option(X32MIRROR_BUILD_REAPER "Build the REAPER extension + embed companion (needs SDK/SWELL)" ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# x32core — protocol core, REAPER-free. Shared by the extension and the tests.
# ---------------------------------------------------------------------------
add_library(x32core STATIC
  src/ext/osc.cpp
  src/ext/udp_socket.cpp
  src/ext/x32_addresses.cpp
  src/ext/x32_connection.cpp)
target_include_directories(x32core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ext)
target_link_libraries(x32core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(x32core PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(X32MIRROR_BUILD_TESTS)
  enable_testing()

  foreach(t test_osc test_fader_math test_addresses)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE x32core)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()

  # The headless integration test spawns python3 (POSIX only).
  if(NOT WIN32)
    add_executable(integration_x32conn tests/integration_x32conn.cpp)
    target_link_libraries(integration_x32conn PRIVATE x32core)
    add_test(
      NAME integration_x32conn
      COMMAND integration_x32conn
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/x32sim/x32sim.py 10023)
    # Give the reconnect cycle room; keep it well under CI limits.
    set_tests_properties(integration_x32conn PROPERTIES TIMEOUT 60)
  endif()
endif()

# ---------------------------------------------------------------------------
# REAPER extension + embedded-FX companion
# ---------------------------------------------------------------------------
if(X32MIRROR_BUILD_REAPER)
  include(Dependencies)
  x32mirror_fetch_reaper_sdk()          # sets REAPER_SDK_DIR, WDL_DIR
  x32mirror_add_swell(swell "${WDL_DIR}")

  # --- reaper_x32mirror (extension) ---
  add_library(reaper_x32mirror MODULE
    src/ext/main.cpp
    src/ext/reaper_api.cpp
    src/ext/log.cpp
    src/ext/settings.cpp
    src/ext/binding_store.cpp
    src/ext/mirror_engine.cpp
    src/ext/actions.cpp
    src/ext/menus.cpp
    src/ext/panel.cpp
    src/ext/api_export.cpp)
  target_link_libraries(reaper_x32mirror PRIVATE x32core swell)
  target_include_directories(reaper_x32mirror PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ext
    ${REAPER_SDK_DIR}/sdk
    ${WDL_DIR})
  set_target_properties(reaper_x32mirror PROPERTIES PREFIX "" OUTPUT_NAME "reaper_x32mirror")
  if(APPLE)
    set_target_properties(reaper_x32mirror PROPERTIES SUFFIX ".dylib")
  elseif(UNIX)
    set_target_properties(reaper_x32mirror PROPERTIES SUFFIX ".so")
  endif()

  # --- x32mirror_embed (VST2 companion) ---
  # NOTE: does NOT link the REAPER API import layer; it uses only runtime
  # pointers resolved via audioMaster.
  add_library(x32mirror_embed MODULE
    src/embed/embed_main.cpp
    src/embed/extension_link.cpp
    src/embed/embed_ui.cpp)
  target_include_directories(x32mirror_embed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/embed
    ${REAPER_SDK_DIR}/sdk)
  set_target_properties(x32mirror_embed PROPERTIES PREFIX "" OUTPUT_NAME "x32mirror_embed")
  if(APPLE)
    set_target_properties(x32mirror_embed PROPERTIES SUFFIX ".dylib")
  elseif(UNIX)
    set_target_properties(x32mirror_embed PROPERTIES SUFFIX ".so")
  endif()
endif()
