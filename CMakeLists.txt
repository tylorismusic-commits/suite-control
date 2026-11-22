cmake_minimum_required(VERSION 3.16)
project(suite_bundle VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(HEARTH_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hearth-dsp"
    CACHE PATH "Path to the hearth-dsp checkout")
set(DUSK_EQ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dusk-4k"
    CACHE PATH "Path to the vendored Dusk 4K EQ core")

# Plug-ins and UIs from different bundles share a host process.  With default
# visibility the dynamic linker can bind suite::Panel/kParamSpecs/Shared to a
# different plug-in's copy (REAPER dlopens with global scope), so hide
# everything except the entry points, which the export macros mark.
function(suite_hide_symbols target)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
endfunction()

enable_testing()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)

set(SUITE_UI_SOURCES src/ui/panel.cpp)
set(SUITE_DSP_SOURCES third_party/dusk-4k/FourKEQDSP.cpp)
set(SUITE_RESOURCE_FILES
  "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
  "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt")

# ---------------------------------------------------------------------------
# CLAP plug-in
# ---------------------------------------------------------------------------
add_library(suite_control MODULE
  src/clap/suite_control_clap.cpp
  ${SUITE_UI_SOURCES}
  ${SUITE_DSP_SOURCES})
target_include_directories(suite_control PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${HEARTH_DSP_DIR}/include" "${DUSK_EQ_DIR}"
  "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(suite_control PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11 Threads::Threads
  ${CMAKE_DL_LIBS})
target_compile_features(suite_control PRIVATE cxx_std_17)
target_compile_options(suite_control PRIVATE -Wall -Wextra -fvisibility=hidden)
set_target_properties(suite_control PROPERTIES
  PREFIX ""
  OUTPUT_NAME "SuiteControl")

set(CLAP_BUNDLE "${CMAKE_BINARY_DIR}/clap/SuiteControl.clap")
set(CLAP_RES "${CMAKE_BINARY_DIR}/clap/SuiteControl.resources")
add_custom_target(suite_control_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clap"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:suite_control>" "${CLAP_BUNDLE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_RES}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SUITE_RESOURCE_FILES}
          "${CLAP_RES}/"
  DEPENDS suite_control ${SUITE_RESOURCE_FILES})

# ---------------------------------------------------------------------------
# LV2 bundle
# ---------------------------------------------------------------------------
set(SC_LV2_DIR "${CMAKE_BINARY_DIR}/lv2/suite-control.lv2")

add_library(suite_control_lv2 MODULE
  src/suite_control_lv2.cpp
  ${SUITE_DSP_SOURCES})
target_include_directories(suite_control_lv2 PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${HEARTH_DSP_DIR}/include" "${DUSK_EQ_DIR}")
target_link_libraries(suite_control_lv2 PRIVATE PkgConfig::LV2)
target_compile_features(suite_control_lv2 PRIVATE cxx_std_17)
set_target_properties(suite_control_lv2 PROPERTIES
  PREFIX ""
  OUTPUT_NAME "suite_control"
  LIBRARY_OUTPUT_DIRECTORY "${SC_LV2_DIR}")

add_library(suite_control_lv2_ui MODULE
  src/ui/lv2_ui.cpp
  ${SUITE_UI_SOURCES})
target_include_directories(suite_control_lv2_ui PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${HEARTH_DSP_DIR}/include" "${DUSK_EQ_DIR}")
target_link_libraries(suite_control_lv2_ui PRIVATE
  PkgConfig::LV2 PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11)
target_compile_features(suite_control_lv2_ui PRIVATE cxx_std_17)
set_target_properties(suite_control_lv2_ui PROPERTIES
  PREFIX ""
  OUTPUT_NAME "suite_control_ui"
  LIBRARY_OUTPUT_DIRECTORY "${SC_LV2_DIR}")

add_executable(suite_control_ttl_gen tools/gen_ttl.cpp)
target_include_directories(suite_control_ttl_gen PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${HEARTH_DSP_DIR}/include" "${DUSK_EQ_DIR}")
target_compile_features(suite_control_ttl_gen PRIVATE cxx_std_17)

add_custom_target(suite_control_lv2_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SC_LV2_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/lv2/manifest.ttl"
          "${CMAKE_SOURCE_DIR}/lv2/suite_control.ttl"
          ${SUITE_RESOURCE_FILES}
          "${SC_LV2_DIR}/"
  DEPENDS suite_control_lv2 suite_control_lv2_ui ${SUITE_RESOURCE_FILES})

add_test(NAME suite_control_ttl
  COMMAND suite_control_ttl_gen --check --root "${CMAKE_SOURCE_DIR}")

add_executable(suite_lv2_host_test test/lv2_host_test.cpp)
target_include_directories(suite_lv2_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(suite_lv2_host_test PRIVATE
  PkgConfig::LV2 ${CMAKE_DL_LIBS})
target_compile_features(suite_lv2_host_test PRIVATE cxx_std_17)
add_dependencies(suite_lv2_host_test suite_control_lv2_bundle)
add_test(NAME suite_lv2_host
  COMMAND suite_lv2_host_test "${SC_LV2_DIR}/suite_control.so")

add_executable(suite_lv2_ui_host_test test/lv2_ui_host_test.cpp)
target_include_directories(suite_lv2_ui_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(suite_lv2_ui_host_test PRIVATE
  PkgConfig::X11 PkgConfig::LV2 PkgConfig::CAIRO PkgConfig::FREETYPE
  ${CMAKE_DL_LIBS})
target_compile_features(suite_lv2_ui_host_test PRIVATE cxx_std_17)
add_dependencies(suite_lv2_ui_host_test suite_control_lv2_bundle)
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
  add_test(NAME suite_lv2_ui_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1200x24"
      "$<TARGET_FILE:suite_lv2_ui_host_test>" "${SC_LV2_DIR}")
else()
  add_test(NAME suite_lv2_ui_host
    COMMAND suite_lv2_ui_host_test "${SC_LV2_DIR}")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(suite_control_dsp_test test/test_dsp.cpp ${SUITE_DSP_SOURCES})
target_include_directories(suite_control_dsp_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${HEARTH_DSP_DIR}/include" "${DUSK_EQ_DIR}")
target_compile_features(suite_control_dsp_test PRIVATE cxx_std_17)
add_test(NAME suite_control_dsp COMMAND suite_control_dsp_test)

add_executable(suite_control_preview test/ui_preview.cpp ${SUITE_UI_SOURCES})
target_include_directories(suite_control_preview PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${HEARTH_DSP_DIR}/include" "${DUSK_EQ_DIR}")
target_link_libraries(suite_control_preview PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(suite_control_preview PRIVATE cxx_std_17)

add_executable(suite_control_panel_test test/test_panel.cpp ${SUITE_UI_SOURCES})
target_include_directories(suite_control_panel_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(suite_control_panel_test PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(suite_control_panel_test PRIVATE cxx_std_17)
add_test(NAME suite_control_panel COMMAND suite_control_panel_test)

add_executable(suite_control_host_test test/clap_host_test.cpp)
target_include_directories(suite_control_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(suite_control_host_test PRIVATE ${CMAKE_DL_LIBS})
target_compile_features(suite_control_host_test PRIVATE cxx_std_17)
add_dependencies(suite_control_host_test suite_control_bundle)
add_test(NAME suite_control_host
  COMMAND suite_control_host_test "${CLAP_BUNDLE}")

# End-to-end peer test against the installed Suite Channel Plus binary.
set(PLUS_CLAP "$ENV{HOME}/.clap/SuiteChannelPlus.clap")
add_executable(suite_peer_integration test/integration_test.cpp)
target_include_directories(suite_peer_integration PRIVATE
  "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(suite_peer_integration PRIVATE ${CMAKE_DL_LIBS})
target_compile_features(suite_peer_integration PRIVATE cxx_std_17)
add_test(NAME suite_peer_integration
  COMMAND suite_peer_integration "${PLUS_CLAP}")

suite_hide_symbols(suite_control)

suite_hide_symbols(suite_control_lv2)

suite_hide_symbols(suite_control_lv2_ui)

# ---------------------------------------------------------------------------
# Suite Channel Plus (same distribution, separate plug-ins)
# ---------------------------------------------------------------------------
add_subdirectory(channel-plus)

# ---------------------------------------------------------------------------
# The installed LV2 bundle: both plug-ins, one manifest, one set of resources.
# ---------------------------------------------------------------------------
set(SUITE_LV2_DIR "${CMAKE_BINARY_DIR}/lv2/suite.lv2")
add_custom_target(suite_lv2_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SUITE_LV2_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:suite_control_lv2>
          $<TARGET_FILE:suite_control_lv2_ui>
          $<TARGET_FILE:suite_channel_plus_lv2>
          $<TARGET_FILE:suite_channel_plus_lv2_ui>
          "${CMAKE_SOURCE_DIR}/lv2/suite_control.ttl"
          "${CMAKE_SOURCE_DIR}/channel-plus/lv2/suite_channel_plus.ttl"
          "${CMAKE_SOURCE_DIR}/channel-plus/lv2/presets.ttl"
          "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
          "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt"
          "${SUITE_LV2_DIR}/"
  COMMAND ${CMAKE_COMMAND}
          -DIN1=${CMAKE_SOURCE_DIR}/lv2/manifest.ttl
          -DIN2=${CMAKE_SOURCE_DIR}/channel-plus/lv2/manifest.ttl
          -DOUT=${SUITE_LV2_DIR}/manifest.ttl
          -P "${CMAKE_SOURCE_DIR}/tools/merge_manifest.cmake"
  DEPENDS suite_control_lv2 suite_control_lv2_ui
          suite_channel_plus_lv2 suite_channel_plus_lv2_ui)

# The installed composition is what users get, so test that exact tree.
add_test(NAME bundle_layout
  COMMAND ${CMAKE_COMMAND} -DBUNDLE=${SUITE_LV2_DIR}
          -P "${CMAKE_SOURCE_DIR}/test/bundle_layout.cmake")
add_test(NAME bundle_lv2_control
  COMMAND suite_lv2_host_test "${SUITE_LV2_DIR}/suite_control.so")
add_test(NAME bundle_lv2_plus
  COMMAND plus_lv2_host_test "${SUITE_LV2_DIR}/suite_channel_plus.so")
find_program(XVFB_RUN_BUNDLE xvfb-run)
if(XVFB_RUN_BUNDLE)
  add_test(NAME bundle_ui_control
    COMMAND "${XVFB_RUN_BUNDLE}" -a -s "-screen 0 1600x1200x24"
            "$<TARGET_FILE:suite_lv2_ui_host_test>" "${SUITE_LV2_DIR}")
  add_test(NAME bundle_ui_plus
    COMMAND "${XVFB_RUN_BUNDLE}" -a -s "-screen 0 1600x1200x24"
            "$<TARGET_FILE:plus_lv2_ui_host_test>" "${SUITE_LV2_DIR}")
else()
  add_test(NAME bundle_ui_control
    COMMAND suite_lv2_ui_host_test "${SUITE_LV2_DIR}")
  add_test(NAME bundle_ui_plus
    COMMAND plus_lv2_ui_host_test "${SUITE_LV2_DIR}")
endif()

message(STATUS "Suite bundle ${PROJECT_VERSION}")
message(STATUS "  LV2: ${SUITE_LV2_DIR}")
message(STATUS "  CLAP: ${CMAKE_BINARY_DIR}/clap/SuiteControl.clap")
message(STATUS "  CLAP: ${CMAKE_BINARY_DIR}/clap/SuiteChannelPlus.clap")
