# TAR-specific variant of the shared console and native unpacker.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_tar.cmake")
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_engine "${_root}/src/xfileunpacker")
add_library(xxfclib_xfileunpacker_tar_core STATIC
    "${_engine}/core.c" "${_engine}/metadata.c" "${_engine}/xxfc_readers.c"
    "${_engine}/native_writer_backend.c" "${_engine}/native_tar_writer.c"
    "${_engine}/native_filter_formats.c" "${_engine}/native_reader_backend.c")
add_library(xxfclib::xfileunpacker_tar_core ALIAS xxfclib_xfileunpacker_tar_core)
target_link_libraries(xxfclib_xfileunpacker_tar_core PUBLIC xxfclib::tar)
target_include_directories(xxfclib_xfileunpacker_tar_core PUBLIC "${_engine}/include"
    PRIVATE "${_engine}")
target_compile_definitions(xxfclib_xfileunpacker_tar_core PRIVATE
    XFU_XXFCLIB_ONLY=1 XXFC_NO_EXTERNAL_HELPERS=1)
add_library(xxfclib_archive_console_tar STATIC "${_root}/src/console/xx_archive_console.c")
add_library(xxfclib::archive_console_tar ALIAS xxfclib_archive_console_tar)
target_link_libraries(xxfclib_archive_console_tar PUBLIC xxfclib::xfileunpacker_tar_core)
if(WIN32)
    target_link_libraries(xxfclib_archive_console_tar PRIVATE shell32)
endif()
foreach(_target xxfclib_xfileunpacker_tar_core xxfclib_archive_console_tar)
    target_compile_features(${_target} PUBLIC c_std_11)
    set_target_properties(${_target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /W4 /utf-8 /Gy /Gw)
        target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic
            -ffunction-sections -fdata-sections)
    endif()
    if(WIN32)
        target_compile_definitions(${_target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    else()
        target_compile_definitions(${_target} PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
endforeach()
unset(_target)
unset(_engine)
unset(_root)
