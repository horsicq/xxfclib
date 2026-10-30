# Keep the full library and formats-only consumers on the same native reader
# collection. DIE music retains its dedicated source configuration above.
file(GLOB_RECURSE XXFCLIB_NATIVE_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/xx_*.c")
list(FILTER XXFCLIB_NATIVE_FORMAT_SOURCES EXCLUDE REGEX
    "[/\\\\][^/\\\\]* \\(conflicted copy [^/\\\\]*\\)\\.[^/\\\\]+$")
list(FILTER XXFCLIB_NATIVE_FORMAT_SOURCES EXCLUDE REGEX "/die_music/")
# xx_format_datastructs.c compiles every schema family unit and per-format
# data file itself, so the glob must not compile them a second time.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xxfclib_schema_sources.cmake")
list(REMOVE_ITEM XXFCLIB_NATIVE_FORMAT_SOURCES ${XXFC_SCHEMA_FAMILY_SOURCES})
list(APPEND XXFCLIB_SOURCES ${XXFCLIB_NATIVE_FORMAT_SOURCES}
    "${CMAKE_CURRENT_LIST_DIR}/installer/nsis/nsis_bzip2.c")
# Normalize existing relative entries before removing duplicate source paths.
set(_xxfc_normalized_sources)
foreach(_xxfc_source IN LISTS XXFCLIB_SOURCES)
    get_filename_component(_xxfc_source_absolute "${_xxfc_source}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    list(APPEND _xxfc_normalized_sources "${_xxfc_source_absolute}")
endforeach()
list(REMOVE_DUPLICATES _xxfc_normalized_sources)
set(XXFCLIB_SOURCES ${_xxfc_normalized_sources})
file(GLOB_RECURSE XXFCLIB_NATIVE_FORMAT_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/xxfclib/formats/*.h"
    "${PROJECT_SOURCE_DIR}/include/xxfclib/formats/*.inc")
list(FILTER XXFCLIB_NATIVE_FORMAT_HEADERS EXCLUDE REGEX
    "[/\\\\][^/\\\\]* \\(conflicted copy [^/\\\\]*\\)\\.[^/\\\\]+$")
list(APPEND XXFCLIB_PUBLIC_HEADERS ${XXFCLIB_NATIVE_FORMAT_HEADERS})
unset(_xxfc_source)
unset(_xxfc_source_absolute)
unset(_xxfc_normalized_sources)
