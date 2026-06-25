option(BUILD_SKYRIM "Build for Skyrim SE/AE" OFF)
option(BUILD_SKYRIMVR "Build for Skyrim VR" OFF)

set(COMMONLIBSSE_NG_PATH "" CACHE PATH "Path to an existing CommonLibSSE-NG checkout")

set(_commonlib_source "${PROJECT_SOURCE_DIR}/extern/CommonLibSSE-NG")

if(NOT EXISTS "${_commonlib_source}/CMakeLists.txt")
  if(COMMONLIBSSE_NG_PATH)
    set(_commonlib_source "${COMMONLIBSSE_NG_PATH}")
  elseif(DEFINED ENV{COMMONLIBSSE_NG_PATH} AND NOT "$ENV{COMMONLIBSSE_NG_PATH}" STREQUAL "")
    set(_commonlib_source "$ENV{COMMONLIBSSE_NG_PATH}")
  endif()
endif()

if(NOT EXISTS "${_commonlib_source}/CMakeLists.txt")
  message(FATAL_ERROR "CommonLibSSE-NG not found. Add it at extern/CommonLibSSE-NG, or set COMMONLIBSSE_NG_PATH.")
endif()

if(BUILD_SKYRIM AND BUILD_SKYRIMVR)
  message(FATAL_ERROR "Select only one game target: BUILD_SKYRIM or BUILD_SKYRIMVR")
elseif(BUILD_SKYRIM)
  set(_game_compile_definition SKYRIM)
elseif(BUILD_SKYRIMVR)
  set(_game_compile_definition SKYRIMVR)
else()
  message(FATAL_ERROR "Select a game preset: -DBUILD_SKYRIM=ON or -DBUILD_SKYRIMVR=ON")
endif()

add_library("${PROJECT_NAME}" SHARED)

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

target_compile_definitions(
  "${PROJECT_NAME}"
  PRIVATE
  ${_game_compile_definition}
  "$<$<CONFIG:Debug>:DEBUG>"
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(TARGET "${PROJECT_NAME}" PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
set_property(TARGET "${PROJECT_NAME}" PROPERTY INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE)

include(AddCXXFiles)
add_cxx_files("${PROJECT_NAME}")

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/Plugin.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
  @ONLY
)

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/Version.rc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/Version.rc"
  @ONLY
)

target_sources(
  "${PROJECT_NAME}"
  PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/Version.rc"
)

target_precompile_headers("${PROJECT_NAME}" PRIVATE "${PROJECT_SOURCE_DIR}/src/PCH.h")

if(MSVC)
  target_compile_options(
    "${PROJECT_NAME}"
    PRIVATE
    /MP
    /W4
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /wd4200
  )
endif()

add_subdirectory("${_commonlib_source}" "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE" EXCLUDE_FROM_ALL)

target_include_directories(
  "${PROJECT_NAME}"
  PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake"
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/core"
  "${PROJECT_SOURCE_DIR}/src/core/container_menu"
  "${PROJECT_SOURCE_DIR}/src/core/hooks"
  "${PROJECT_SOURCE_DIR}/src/core/identity"
  "${PROJECT_SOURCE_DIR}/src/core/input"
  "${PROJECT_SOURCE_DIR}/src/core/logging"
  "${PROJECT_SOURCE_DIR}/src/core/memory"
  "${PROJECT_SOURCE_DIR}/src/core/relocations"
  "${PROJECT_SOURCE_DIR}/src/core/serialization"
  "${PROJECT_SOURCE_DIR}/src/core/settings"
  "${PROJECT_SOURCE_DIR}/src/core/state"
  "${PROJECT_SOURCE_DIR}/src/core/ui"
  "${PROJECT_SOURCE_DIR}/src/features/quick_trade"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip/align"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip/consumption"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip/override"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip/preference"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip/restore"
  "${PROJECT_SOURCE_DIR}/src/features/combat_equip/scoring"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode/core"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode/fixes"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode/modes"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode/preference"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode/selection"
  "${PROJECT_SOURCE_DIR}/src/features/equip_mode/util"
  "${PROJECT_SOURCE_DIR}/src/features/equip_gate"
  "${PROJECT_SOURCE_DIR}/src/features/equip_suppression"
  "${PROJECT_SOURCE_DIR}/src/features/equip_suppression/equip"
  "${PROJECT_SOURCE_DIR}/src/features/equip_suppression/injection"
  "${PROJECT_SOURCE_DIR}/src/features/equip_suppression/loot"
  "${PROJECT_SOURCE_DIR}/src/features/equip_suppression/sanitize"
  "${PROJECT_SOURCE_DIR}/src/features/outfit_sync"
  "${PROJECT_SOURCE_DIR}/src/features/stats_display"
  "${PROJECT_SOURCE_DIR}/src/features/ui_indicator"
  "${PROJECT_SOURCE_DIR}/src/features/ui_indicator/button_indicator"
  "${PROJECT_SOURCE_DIR}/src/features/ui_indicator/icon_indicator"
  "${PROJECT_SOURCE_DIR}/src/features/weapon_recharge"
  "${PROJECT_SOURCE_DIR}/src/plugin"
)

target_link_libraries(
  "${PROJECT_NAME}"
  PRIVATE
  CommonLibSSE::CommonLibSSE
)
