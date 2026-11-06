# Shared archive console frontend, with configurable branding and format scope.
include_guard(GLOBAL)

if(TARGET xxfclib::archive_console)
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_xfileunpacker.cmake")
get_filename_component(_xxfc_console_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(xxfclib_archive_console STATIC
    "${_xxfc_console_root}/src/console/xx_archive_console.c"
    "${_xxfc_console_root}/include/xxfclib/console/xx_archive_console.h")
add_library(xxfclib::archive_console ALIAS xxfclib_archive_console)
target_link_libraries(xxfclib_archive_console PUBLIC xxfclib::xfileunpacker_core)
target_compile_features(xxfclib_archive_console PUBLIC c_std_11)
set_target_properties(xxfclib_archive_console PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(xxfclib_archive_console PRIVATE /W4 /utf-8)
    target_compile_definitions(xxfclib_archive_console PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(xxfclib_archive_console PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_compile_definitions(xxfclib_archive_console PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(xxfclib_archive_console PRIVATE shell32)
else()
    target_compile_definitions(xxfclib_archive_console PRIVATE _POSIX_C_SOURCE=200809L)
endif()
unset(_xxfc_console_root)
