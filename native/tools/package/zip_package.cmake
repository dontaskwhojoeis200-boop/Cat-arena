# N6.5: zips a game folder so that the archive holds one top-level folder of the same name.
#   cmake -DPACKAGE_DIR=<.../CAT_ARENA_FERAL_CLAW_N6_WINDOWS> -DZIP=<.../CAT_ARENA_FERAL_CLAW_N6_WINDOWS.zip> -P zip_package.cmake
get_filename_component(PACKAGE_DIR "${PACKAGE_DIR}" ABSOLUTE)
get_filename_component(ZIP "${ZIP}" ABSOLUTE)
get_filename_component(parent "${PACKAGE_DIR}" DIRECTORY)
get_filename_component(name "${PACKAGE_DIR}" NAME)
file(REMOVE "${ZIP}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP}" --format=zip "${name}" WORKING_DIRECTORY "${parent}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS "${ZIP}")
  message(FATAL_ERROR "Could not write ${ZIP}")
endif()
file(SIZE "${ZIP}" size)
file(SHA256 "${ZIP}" sha)
message("Wrote ${ZIP}: ${size} bytes, SHA-256 ${sha}")
