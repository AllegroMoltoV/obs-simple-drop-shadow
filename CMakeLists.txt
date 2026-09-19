cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# C++17 を要求する
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# 既存の target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c) を置き換える

# src/ と data/ を自動で拾う (追加のたびに CMakeLists.txt を編集しないため)
file(
  GLOB PLUGIN_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

file(
  GLOB_RECURSE PLUGIN_RESOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/data/*.effect"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/*.ini"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

# Solution Explorer のツリー表示を実ディレクトリに揃える
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PLUGIN_SOURCES} ${PLUGIN_RESOURCES})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ビルド直後に cmake --install を実行する (Windows 開発用)
option(ENABLE_POST_BUILD_INSTALL "Run cmake --install after build" ON)

if(WIN32 AND ENABLE_POST_BUILD_INSTALL)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
    COMMENT "Installing OBS plugin (cmake --install)"
    VERBATIM
  )
endif()

if(WIN32)
  enable_testing()

  add_executable(drop-shadow-effect-test tests/effect-render-test.cpp)
  target_compile_features(drop-shadow-effect-test PRIVATE cxx_std_17)
  target_link_libraries(drop-shadow-effect-test PRIVATE OBS::libobs)

  add_executable(drop-shadow-filter-integration-test tests/filter-integration-test.cpp)
  target_compile_features(drop-shadow-filter-integration-test PRIVATE cxx_std_17)
  target_link_libraries(drop-shadow-filter-integration-test PRIVATE OBS::libobs)
  add_dependencies(drop-shadow-filter-integration-test ${CMAKE_PROJECT_NAME})

  add_test(
    NAME drop-shadow-effect-smoke
    COMMAND
      drop-shadow-effect-test "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll" smoke
  )
  add_test(
    NAME drop-shadow-horizontal
    COMMAND
      drop-shadow-effect-test "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll" horizontal
  )
  add_test(
    NAME drop-shadow-vertical-composite
    COMMAND
      drop-shadow-effect-test "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      vertical-composite
  )
  add_test(
    NAME drop-shadow-filter-integration
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect" "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-blur"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" blur
  )
  add_test(
    NAME drop-shadow-filter-radius-zero
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-radius-zero"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" radius-zero
  )
  add_test(
    NAME drop-shadow-filter-edge-offset
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-edge-offset"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" edge-offset
  )
  add_test(
    NAME drop-shadow-filter-vertical-edge-offset
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-vertical-edge-offset"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" vertical-edge-offset
  )
  add_test(
    NAME drop-shadow-filter-negative-edge-offset
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-negative-edge-offset"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" negative-edge-offset
  )
  add_test(
    NAME drop-shadow-filter-radius-max
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-radius-max"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" radius-max
  )
  add_test(
    NAME drop-shadow-filter-settings-update
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-settings-update"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" settings-update
  )
  add_test(
    NAME drop-shadow-filter-color-opacity
    COMMAND
      drop-shadow-filter-integration-test "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/effects/drop-shadow.effect"
      "${CMAKE_CURRENT_BINARY_DIR}/test-module-data-color-opacity"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/build_x64/libobs-d3d11/Release/libobs-d3d11.dll"
      "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-31.1.1/libobs/data" color-opacity
  )
  set_tests_properties(
    drop-shadow-effect-smoke
    drop-shadow-horizontal
    drop-shadow-vertical-composite
    drop-shadow-filter-integration
    drop-shadow-filter-radius-zero
    drop-shadow-filter-edge-offset
    drop-shadow-filter-vertical-edge-offset
    drop-shadow-filter-negative-edge-offset
    drop-shadow-filter-radius-max
    drop-shadow-filter-settings-update
    drop-shadow-filter-color-opacity
    PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/.deps/bin/64bit;PATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2025-07-11-x64/bin"
  )
endif()
