cmake_minimum_required(VERSION 3.21)

project(
  FollowerEquipControl
  VERSION 2.0.0
  LANGUAGES CXX
)

set(MOD_FOLDER_NAME "Follower Equip Control SKSE")

if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
  set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
  set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${MOD_FOLDER_NAME}")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(XSEPlugin)

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

find_path(DETOURS_INCLUDE_DIRS NAMES detours/detours.h REQUIRED)
find_library(DETOURS_LIBRARY NAMES detours REQUIRED)

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE
  UNICODE
  _UNICODE
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
  spdlog::spdlog
  fmt::fmt
  ${DETOURS_LIBRARY}
)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
  ${DETOURS_INCLUDE_DIRS}
)

if(DEFINED OUTPUT_FOLDER)
  message(STATUS "SKSE plugin deploy root: ${OUTPUT_FOLDER}")

  add_custom_command(
    TARGET "${PROJECT_NAME}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
      "-DOUTPUT_FOLDER=${OUTPUT_FOLDER}"
      "-DDLL_PATH=$<TARGET_FILE:${PROJECT_NAME}>"
      "-DPDB_PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${PROJECT_NAME}>.pdb"
      -P "${PROJECT_SOURCE_DIR}/cmake/Deploy.cmake"
    VERBATIM
  )
endif()

# Disable MSVC STL modules in the Visual Studio generator.
# https://gitlab.kitware.com/cmake/cmake/-/issues/24922
if(MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE)
  file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props" CONTENT [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==] @ONLY)
endif()
