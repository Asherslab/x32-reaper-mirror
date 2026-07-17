# Fetches the REAPER SDK headers and WDL/SWELL, pinned to specific commits so
# the build is reproducible. Only pulled in when the extension / embed targets
# are enabled (X32MIRROR_BUILD_REAPER); the protocol core and tests build with
# no external dependencies at all.

include(FetchContent)

# Pinned commits (see PLAN.md). Bump deliberately, never float.
set(X32MIRROR_REAPER_SDK_REPO "https://github.com/justinfrankel/reaper-sdk.git")
set(X32MIRROR_REAPER_SDK_TAG  "9260b757bb7a7253b2f4ef173b90628899845ce5")
set(X32MIRROR_WDL_REPO        "https://github.com/justinfrankel/WDL.git")
set(X32MIRROR_WDL_TAG         "f5221c3e3927f6d476493c580bb5a027dc45359e")

function(x32mirror_fetch_reaper_sdk)
  FetchContent_Declare(
    reaper_sdk
    GIT_REPOSITORY ${X32MIRROR_REAPER_SDK_REPO}
    GIT_TAG        ${X32MIRROR_REAPER_SDK_TAG}
    GIT_SHALLOW    FALSE)
  FetchContent_Declare(
    wdl
    GIT_REPOSITORY ${X32MIRROR_WDL_REPO}
    GIT_TAG        ${X32MIRROR_WDL_TAG}
    GIT_SHALLOW    FALSE)
  FetchContent_MakeAvailable(reaper_sdk wdl)

  set(REAPER_SDK_DIR "${reaper_sdk_SOURCE_DIR}" PARENT_SCOPE)
  set(WDL_DIR        "${wdl_SOURCE_DIR}/WDL"     PARENT_SCOPE)
endfunction()

# Build a static SWELL library from WDL sources on macOS / Linux. On Windows we
# use the real Win32 API and SWELL is not needed (swell.h maps to <windows.h>
# only where a project opts in; we simply don't link SWELL there).
#
# This mirrors the generic-GDK build used by other open-source REAPER
# extensions. Linux needs the GTK3/GDK dev headers at configure time.
function(x32mirror_add_swell out_target wdl_dir)
  if(WIN32)
    add_library(${out_target} INTERFACE)
    target_include_directories(${out_target} INTERFACE "${wdl_dir}")
    return()
  endif()

  set(SWELL_DIR "${wdl_dir}/swell")

  if(APPLE)
    set(SWELL_SOURCES
      "${SWELL_DIR}/swell-modstub.mm")
    set_source_files_properties(${SWELL_SOURCES} PROPERTIES
      COMPILE_FLAGS "-fobjc-arc")
    add_library(${out_target} STATIC ${SWELL_SOURCES})
    target_link_libraries(${out_target} PUBLIC
      "-framework Cocoa" "-framework Carbon" "-framework AppKit")
  else()
    # Linux: the module links the SWELL modstub and dynamically loads the host
    # SWELL provided by REAPER at runtime. The generic modstub is the correct,
    # lightweight choice for a plug-in module (REAPER supplies libSwell).
    set(SWELL_SOURCES
      "${SWELL_DIR}/swell-modstub-generic.cpp")
    add_library(${out_target} STATIC ${SWELL_SOURCES})
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(GDK3 QUIET gdk-3.0)
      if(GDK3_FOUND)
        target_include_directories(${out_target} PUBLIC ${GDK3_INCLUDE_DIRS})
      endif()
    endif()
    target_link_libraries(${out_target} PUBLIC dl)
  endif()

  target_include_directories(${out_target} PUBLIC "${wdl_dir}")
  target_compile_definitions(${out_target} PUBLIC SWELL_PROVIDED_BY_APP)
endfunction()
