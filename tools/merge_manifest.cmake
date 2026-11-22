# Concatenate the per-plug-in manifests into the bundle's single manifest.
# Turtle allows a prefix to be re-declared, and both files use the same
# prefixes, so a plain concatenation is a valid bundle manifest.
if(NOT DEFINED IN1 OR NOT DEFINED IN2 OR NOT DEFINED OUT)
  message(FATAL_ERROR "merge_manifest.cmake needs IN1, IN2 and OUT")
endif()
file(READ "${IN1}" first)
file(READ "${IN2}" second)
file(WRITE "${OUT}" "${first}\n${second}")
