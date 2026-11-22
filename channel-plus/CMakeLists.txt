cmake_minimum_required(VERSION 3.16)
project(suite_channel_plus VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(GNUInstallDirs)

get_filename_component(SUITE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(HEARTH_DSP_DIR "${SUITE_ROOT}/third_party/hearth-dsp"
    CACHE PATH "Path to the hearth-dsp checkout")
if(NOT EXISTS "${HEARTH_DSP_DIR}/include/hdsp/hdsp.hpp")
  message(FATAL_ERROR "hearth-dsp not found at ${HEARTH_DSP_DIR}")
endif()

set(DUSK_EQ_DIR "${SUITE_ROOT}/third_party/dusk-4k"
    CACHE PATH "Path to the vendored Dusk 4K EQ core")
if(NOT EXISTS "${DUSK_EQ_DIR}/FourKEQDSP.hpp")
  message(FATAL_ERROR "Dusk 4K EQ core not found at ${DUSK_EQ_DIR}")
endif()

include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${HEARTH_DSP_DIR}/include"
  "${DUSK_EQ_DIR}")

# Plug-ins and UIs from different bundles share a host process.  With default
# visibility the dynamic linker can bind suite::Panel/kParamSpecs/Shared to a
# different plug-in's copy (REAPER dlopens with global scope), so hide
# everything except the entry points, which the export macros mark.
if(NOT COMMAND suite_hide_symbols)
  function(suite_hide_symbols target)
    set_target_properties(${target} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES)
  endfunction()
endif()

enable_testing()

set(CLAP_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/clap"
    CACHE PATH "Directory where CLAP bundles are installed")
set(LV2_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/lv2"
    CACHE PATH "Directory where the LV2 bundle is installed")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
# The editor hosts are X11 today.  macOS builds the portable subset (DSP,
# panel, tests, tools) until a Cocoa view lands, so X11 stays optional.
option(SUITE_NO_X11 "Skip X11 editor targets (portable subset)" OFF)
if(APPLE OR SUITE_NO_X11)
  set(SUITE_X11 OFF)
else()
  pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
  set(SUITE_X11 ON)
endif()

set(SUITE_UI_SOURCES src/ui/panel.cpp)
set(SUITE_DSP_SOURCES ${SUITE_ROOT}/third_party/dusk-4k/FourKEQDSP.cpp)

# ---------------------------------------------------------------------------
# CLAP plugin: one cairo editor (X11 on Linux, Cocoa on macOS).
# ---------------------------------------------------------------------------
set(SUITE_RESOURCE_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL.txt")

if(APPLE)
enable_language(OBJCXX)
add_library(suite_channel_plus MODULE
  src/clap/suite_channel_plus_clap.cpp
  ${SUITE_UI_SOURCES}
  ${SUITE_DSP_SOURCES}
  src/ui/cocoa_view.mm)
target_include_directories(suite_channel_plus PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${SUITE_ROOT}/third_party/clap/include"
  "${HEARTH_DSP_DIR}/include"
  "${DUSK_EQ_DIR}"
  ${CAIRO_INCLUDE_DIRS})
target_compile_definitions(suite_channel_plus PRIVATE SUITE_MACOS_COCOA=1)
target_compile_options(suite_channel_plus PRIVATE -fobjc-arc)
target_link_libraries(suite_channel_plus PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE
  "-framework Cocoa" "-framework CoreGraphics" "-framework QuartzCore"
  Threads::Threads ${CMAKE_DL_LIBS})
target_compile_features(suite_channel_plus PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(suite_channel_plus PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()
set_target_properties(suite_channel_plus PROPERTIES
  PREFIX ""
  OUTPUT_NAME "SuiteChannelPlus"
  SUFFIX ".dylib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/macos")

set(SUITE_CLAP_BUNDLE "${CMAKE_BINARY_DIR}/clap/SuiteChannelPlus.clap")
find_program(DYLIBBUNDLER dylibbundler)
add_custom_target(suite_channel_plus_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${SUITE_CLAP_BUNDLE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${SUITE_CLAP_BUNDLE}/Contents/MacOS"
  COMMAND ${CMAKE_COMMAND} -E copy
          "$<TARGET_FILE:suite_channel_plus>"
          "${SUITE_CLAP_BUNDLE}/Contents/MacOS/SuiteChannelPlus"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist"
          "${SUITE_CLAP_BUNDLE}/Contents/Info.plist"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${SUITE_CLAP_BUNDLE}/Contents/Resources/SuiteChannelPlus.resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SUITE_RESOURCE_FILES}
          "${SUITE_CLAP_BUNDLE}/Contents/Resources/SuiteChannelPlus.resources/"
  DEPENDS suite_channel_plus ${SUITE_RESOURCE_FILES})
if(DYLIBBUNDLER)
  add_custom_command(TARGET suite_channel_plus_bundle POST_BUILD
    COMMAND "${DYLIBBUNDLER}" -od -b
            -x "${SUITE_CLAP_BUNDLE}/Contents/MacOS/SuiteChannelPlus"
            -d "${SUITE_CLAP_BUNDLE}/Contents/Frameworks"
            -p "@loader_path/../Frameworks")
endif()
# ---------------------------------------------------------------------------
# VST3 and AUv2 projections of the same CLAP (clap-wrapper)
#
# Both wrappers load the .clap we just built from their own Contents/PlugIns,
# so the bundles stay self-contained: no copy of the plug-in has to be
# installed for the wrapper to work.
# ---------------------------------------------------------------------------
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON)
add_subdirectory(${SUITE_ROOT}/third_party/clap-wrapper EXCLUDE_FROM_ALL)

set(SUITE_VST3_BUNDLE "${CMAKE_BINARY_DIR}/vst3/SuiteChannelPlus.vst3")
set(SUITE_AUV2_BUNDLE "${CMAKE_BINARY_DIR}/au/SuiteChannelPlus.component")

add_library(suite_channel_plus_vst3 MODULE)
add_dependencies(suite_channel_plus_vst3 suite_channel_plus_bundle)
target_add_vst3_wrapper(TARGET suite_channel_plus_vst3
  OUTPUT_NAME "SuiteChannelPlus"
  BUNDLE_IDENTIFIER "com.tylorismusic.suitechannel"
  BUNDLE_VERSION "${PROJECT_VERSION}"
  ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vst3"
  MACOS_EMBEDDED_CLAP_LOCATION "${SUITE_CLAP_BUNDLE}")

add_library(suite_channel_plus_auv2 MODULE)
add_dependencies(suite_channel_plus_auv2 suite_channel_plus_bundle)
target_add_auv2_wrapper(TARGET suite_channel_plus_auv2
  OUTPUT_NAME "SuiteChannelPlus"
  BUNDLE_IDENTIFIER "com.tylorismusic.suitechannel"
  BUNDLE_VERSION "${PROJECT_VERSION}"
  MANUFACTURER_NAME "tylorismusic"
  MANUFACTURER_CODE "Tylr"
  SUBTYPE_CODE "ScCh"
  INSTRUMENT_TYPE "aufx"
  MACOS_EMBEDDED_CLAP_LOCATION "${SUITE_CLAP_BUNDLE}")
# The AUv2 build helper loads the CLAP bundle to read its metadata, and
# clap-wrapper only orders it after the CLAP library -- not after our bundle
# target, whose POST_BUILD runs dylibbundler over the same bundle.  Without
# this the helper can dlopen a bundle that is being rewritten (SIGSEGV).
add_dependencies(suite_channel_plus_auv2-build-helper suite_channel_plus_bundle)
set_target_properties(suite_channel_plus_auv2 PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/au")

# The wrapper generates Contents/Info.plist with its AudioComponents entry at
# build time and copies it PRE_BUILD, but CMake writes its own default bundle
# plist when the wrapper links -- and only then.  A build where the CLAP
# changed but the wrapper did not relink kept whatever plist happened to be
# there, which is how a component ends up registered nowhere.  An always-run
# target installs the generated plist after every build; the signing targets
# depend on it, so the signature covers it.
add_custom_target(suite_channel_plus_auv2_plist ALL
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_BINARY_DIR}/suite_channel_plus_auv2-build-helper-output/auv2_Info.plist"
          "$<TARGET_BUNDLE_DIR:suite_channel_plus_auv2>/Contents/Info.plist"
  COMMENT "Installing the generated AUv2 Info.plist"
  DEPENDS suite_channel_plus_auv2)

# Embedding the CLAP happens PRE_BUILD, which invalidates any signature the
# wrapper had, so re-sign the finished bundles.
find_program(CODESIGN codesign)
if(CODESIGN)
  add_custom_target(suite_channel_plus_vst3_sign ALL
    COMMAND "${CODESIGN}" --force --deep --sign - "${SUITE_VST3_BUNDLE}"
    COMMENT "Ad-hoc signing ${SUITE_VST3_BUNDLE}"
    DEPENDS suite_channel_plus_vst3
    VERBATIM)
  add_custom_target(suite_channel_plus_auv2_sign ALL
    COMMAND "${CODESIGN}" --force --deep --sign - "${SUITE_AUV2_BUNDLE}"
    COMMENT "Ad-hoc signing ${SUITE_AUV2_BUNDLE}"
    DEPENDS suite_channel_plus_auv2
    VERBATIM)
  add_dependencies(suite_channel_plus_auv2_sign suite_channel_plus_auv2_plist)
endif()

install(DIRECTORY "${SUITE_CLAP_BUNDLE}" DESTINATION "${CLAP_DIR}")
elseif(SUITE_X11)
add_library(suite_channel_plus MODULE
  src/clap/suite_channel_plus_clap.cpp
  ${SUITE_UI_SOURCES}
  ${SUITE_DSP_SOURCES})
target_include_directories(suite_channel_plus PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${SUITE_ROOT}/third_party/clap/include"
  "${HEARTH_DSP_DIR}/include"
  "${DUSK_EQ_DIR}")
target_link_libraries(suite_channel_plus PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11
  Threads::Threads ${CMAKE_DL_LIBS})
target_compile_features(suite_channel_plus PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(suite_channel_plus PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()
set_target_properties(suite_channel_plus PROPERTIES
  PREFIX ""
  OUTPUT_NAME "SuiteChannelPlus"
  SUFFIX ".clap"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap")

add_custom_target(suite_channel_plus_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clap/SuiteChannelPlus.resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SUITE_RESOURCE_FILES}
          "${CMAKE_BINARY_DIR}/clap/SuiteChannelPlus.resources/"
  DEPENDS suite_channel_plus ${SUITE_RESOURCE_FILES})

install(TARGETS suite_channel_plus LIBRARY DESTINATION "${CLAP_DIR}")
install(FILES ${SUITE_RESOURCE_FILES}
        DESTINATION "${CLAP_DIR}/SuiteChannelPlus.resources")
endif()  # APPLE / SUITE_X11

# ---------------------------------------------------------------------------
# LV2 bundle
# ---------------------------------------------------------------------------
set(LV2_BUNDLE_DIR "${CMAKE_BINARY_DIR}/lv2/suite-channel-plus.lv2")

add_library(suite_channel_plus_lv2 MODULE
  src/suite_channel_plus_lv2.cpp
  ${SUITE_DSP_SOURCES})
target_include_directories(suite_channel_plus_lv2 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${HEARTH_DSP_DIR}/include"
  "${DUSK_EQ_DIR}")
target_link_libraries(suite_channel_plus_lv2 PRIVATE PkgConfig::LV2)
target_compile_features(suite_channel_plus_lv2 PRIVATE cxx_std_17)
set_target_properties(suite_channel_plus_lv2 PROPERTIES
  PREFIX ""
  OUTPUT_NAME "suite_channel_plus"
  LIBRARY_OUTPUT_DIRECTORY "${LV2_BUNDLE_DIR}")

if(SUITE_X11)
add_library(suite_channel_plus_lv2_ui MODULE
  src/ui/lv2_ui.cpp
  ${SUITE_UI_SOURCES})
target_include_directories(suite_channel_plus_lv2_ui PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${HEARTH_DSP_DIR}/include"
  "${DUSK_EQ_DIR}")
target_link_libraries(suite_channel_plus_lv2_ui PRIVATE
  PkgConfig::LV2 PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11)
target_compile_features(suite_channel_plus_lv2_ui PRIVATE cxx_std_17)
set_target_properties(suite_channel_plus_lv2_ui PROPERTIES
  PREFIX ""
  OUTPUT_NAME "suite_channel_plus_ui"
  LIBRARY_OUTPUT_DIRECTORY "${LV2_BUNDLE_DIR}")

add_custom_target(suite_channel_plus_lv2_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LV2_BUNDLE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/lv2/manifest.ttl"
          "${CMAKE_CURRENT_SOURCE_DIR}/lv2/suite_channel_plus.ttl"
          "${CMAKE_CURRENT_SOURCE_DIR}/lv2/presets.ttl"
          "${LV2_BUNDLE_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SUITE_RESOURCE_FILES}
          "${LV2_BUNDLE_DIR}/"
  DEPENDS suite_channel_plus_lv2 suite_channel_plus_lv2_ui
          "${CMAKE_CURRENT_SOURCE_DIR}/lv2/manifest.ttl"
          "${CMAKE_CURRENT_SOURCE_DIR}/lv2/suite_channel_plus.ttl"
          "${CMAKE_CURRENT_SOURCE_DIR}/lv2/presets.ttl"
          ${SUITE_RESOURCE_FILES})
endif()  # SUITE_X11

install(TARGETS suite_channel_plus_lv2 LIBRARY DESTINATION "${LV2_DIR}/suite-channel.lv2")
if(SUITE_X11)
  install(TARGETS suite_channel_plus_lv2_ui
          LIBRARY DESTINATION "${LV2_DIR}/suite-channel.lv2")
endif()
install(FILES
          lv2/manifest.ttl
          lv2/suite_channel_plus.ttl
          lv2/presets.ttl
        ${SUITE_RESOURCE_FILES}
        DESTINATION "${LV2_DIR}/suite-channel.lv2")

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
add_executable(suite_ttl_gen tools/gen_ttl.cpp)
target_include_directories(suite_ttl_gen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(suite_ttl_gen PRIVATE cxx_std_17)
add_test(NAME suite_ttl
  COMMAND suite_ttl_gen --check --root "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(suite_dsp_test test/test_dsp.cpp ${SUITE_DSP_SOURCES})
target_include_directories(suite_dsp_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${HEARTH_DSP_DIR}/include"
  "${DUSK_EQ_DIR}")
target_compile_features(suite_dsp_test PRIVATE cxx_std_17)
add_test(NAME suite_dsp COMMAND suite_dsp_test)

add_executable(suite_peer_test test/peer_test.cpp)
target_include_directories(suite_peer_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(suite_peer_test PRIVATE cxx_std_17)
add_test(NAME suite_peer COMMAND suite_peer_test)

add_executable(suite_panel_test test/test_panel.cpp ${SUITE_UI_SOURCES})
target_include_directories(suite_panel_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(suite_panel_test PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(suite_panel_test PRIVATE cxx_std_17)
add_test(NAME suite_panel COMMAND suite_panel_test)

add_executable(suite_ui_preview test/ui_preview.cpp ${SUITE_UI_SOURCES})
target_include_directories(suite_ui_preview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(suite_ui_preview PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(suite_ui_preview PRIVATE cxx_std_17)

if(SUITE_X11)
add_executable(suite_clap_host_test test/clap_host_test.cpp)
target_include_directories(suite_clap_host_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src" "${SUITE_ROOT}/third_party/clap/include")
target_link_libraries(suite_clap_host_test PRIVATE
  PkgConfig::X11 ${CMAKE_DL_LIBS})
target_compile_features(suite_clap_host_test PRIVATE cxx_std_17)
add_dependencies(suite_clap_host_test suite_channel_plus_bundle)
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
  add_test(NAME suite_clap_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1200x24"
      "$<TARGET_FILE:suite_clap_host_test>" "${CMAKE_BINARY_DIR}/clap/SuiteChannelPlus.clap")
else()
  add_test(NAME suite_clap_host
    COMMAND suite_clap_host_test "${CMAKE_BINARY_DIR}/clap/SuiteChannelPlus.clap")
endif()

add_executable(plus_lv2_host_test test/lv2_host_test.cpp)
target_include_directories(plus_lv2_host_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(plus_lv2_host_test PRIVATE
  PkgConfig::LV2 ${CMAKE_DL_LIBS})
target_compile_features(plus_lv2_host_test PRIVATE cxx_std_17)
add_dependencies(plus_lv2_host_test suite_channel_plus_lv2_bundle)
add_test(NAME plus_lv2_host
  COMMAND plus_lv2_host_test "${CMAKE_BINARY_DIR}/lv2/suite-channel-plus.lv2/suite_channel_plus.so")

add_executable(plus_lv2_ui_host_test test/lv2_ui_host_test.cpp)
target_include_directories(plus_lv2_ui_host_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(plus_lv2_ui_host_test PRIVATE
  PkgConfig::X11 ${CMAKE_DL_LIBS})
target_compile_features(plus_lv2_ui_host_test PRIVATE cxx_std_17)
add_dependencies(plus_lv2_ui_host_test suite_channel_plus_lv2_bundle)
if(XVFB_RUN)
  add_test(NAME plus_lv2_ui_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1200x24"
      "$<TARGET_FILE:plus_lv2_ui_host_test>" "${CMAKE_BINARY_DIR}/lv2/suite-channel-plus.lv2")
else()
  add_test(NAME plus_lv2_ui_host
    COMMAND plus_lv2_ui_host_test "${CMAKE_BINARY_DIR}/lv2/suite-channel-plus.lv2")
endif()
endif()  # SUITE_X11

# ---------------------------------------------------------------------------
# Distribution
# ---------------------------------------------------------------------------
if(SUITE_X11)
add_custom_target(dist
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
          "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
          "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
          "${CMAKE_BINARY_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E tar czf
          "dist/suite-channel-${PROJECT_VERSION}-linux-x86_64.tar.gz"
          README.md LICENSE THIRD_PARTY_NOTICES.md
          lv2/suite-channel-plus.lv2 clap/SuiteChannelPlus.clap clap/SuiteChannelPlus.resources
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  DEPENDS suite_channel_plus_lv2_bundle suite_channel_plus_bundle
  COMMENT "Packing suite-channel-${PROJECT_VERSION}")
endif()  # SUITE_X11

if(TARGET suite_channel_plus)
  suite_hide_symbols(suite_channel_plus)
endif()

if(TARGET suite_channel_plus_lv2)
  suite_hide_symbols(suite_channel_plus_lv2)
endif()

if(TARGET suite_channel_plus_lv2_ui)
  suite_hide_symbols(suite_channel_plus_lv2_ui)
endif()
