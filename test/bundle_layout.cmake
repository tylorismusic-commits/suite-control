# Checks the composed distribution bundle, not the per-plug-in build folders.
if(NOT DEFINED BUNDLE)
  message(FATAL_ERROR "BUNDLE not set")
endif()
set(required
  manifest.ttl
  suite_control.so suite_control.ttl suite_control_ui.so
  suite_channel_plus.so suite_channel_plus.ttl suite_channel_plus_ui.so
  presets.ttl Jost-ExtraBold.ttf OFL.txt)
foreach(file ${required})
  if(NOT EXISTS "${BUNDLE}/${file}")
    message(FATAL_ERROR "missing ${file} in ${BUNDLE}")
  endif()
endforeach()

file(READ "${BUNDLE}/manifest.ttl" manifest)
foreach(uri
    "https://github.com/tylorismusic-commits/suite-control"
    "https://github.com/tylorismusic-commits/suite-channel-plus")
  string(FIND "${manifest}" "${uri}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "manifest does not declare ${uri}")
  endif()
endforeach()
string(REGEX MATCHALL "ui:binary" uis "${manifest}")
list(LENGTH uis uiCount)
if(NOT uiCount EQUAL 2)
  message(FATAL_ERROR "expected 2 UIs in the bundle manifest, found ${uiCount}")
endif()
string(FIND "${manifest}" "suite-channel#ui" stale)
if(NOT stale EQUAL -1)
  message(FATAL_ERROR "manifest still points at the old Suite Channel UI URI")
endif()
message(STATUS "bundle layout ok: ${BUNDLE}")
