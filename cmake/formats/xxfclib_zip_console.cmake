# ZIP-only variant of the shared archive console and unpacker engine.
# This target has no dependency on the general xxfclib formats library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_zip.cmake")
get_filename_component(_xxfc_zip_console_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_xxfc_zip_engine_dir "${_xxfc_zip_console_root}/src/xfileunpacker")

add_library(xxfclib_xfileunpacker_zip_core STATIC
    "${_xxfc_zip_engine_dir}/core.c"
    "${_xxfc_zip_engine_dir}/metadata.c"
    "${_xxfc_zip_engine_dir}/native_writer_backend.c"
    "${_xxfc_zip_engine_dir}/xxfc_readers.c")
add_library(xxfclib::xfileunpacker_zip_core ALIAS xxfclib_xfileunpacker_zip_core)
target_link_libraries(xxfclib_xfileunpacker_zip_core PUBLIC xxfclib::zip)
target_include_directories(xxfclib_xfileunpacker_zip_core
    PUBLIC "${_xxfc_zip_engine_dir}/include"
    PRIVATE "${_xxfc_zip_engine_dir}")
target_compile_definitions(xxfclib_xfileunpacker_zip_core
    PRIVATE XFU_XXFCLIB_ONLY=1 XXFC_NO_EXTERNAL_HELPERS=1)

add_library(xxfclib_archive_console_zip STATIC
    "${_xxfc_zip_console_root}/src/console/xx_archive_console.c"
    "${_xxfc_zip_console_root}/include/xxfclib/console/xx_archive_console.h")
add_library(xxfclib::archive_console_zip ALIAS xxfclib_archive_console_zip)
target_link_libraries(xxfclib_archive_console_zip PUBLIC xxfclib::xfileunpacker_zip_core)
if(WIN32)
    target_link_libraries(xxfclib_archive_console_zip PRIVATE shell32)
endif()

foreach(_target xxfclib_xfileunpacker_zip_core xxfclib_archive_console_zip)
    target_compile_features(${_target} PUBLIC c_std_11)
    set_target_properties(${_target} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /W4 /utf-8 /Gy /Gw)
        target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${_target} PRIVATE
            -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections)
    endif()
    if(WIN32)
        target_compile_definitions(${_target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    else()
        target_compile_definitions(${_target} PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
endforeach()
unset(_target)
unset(_xxfc_zip_engine_dir)
unset(_xxfc_zip_console_root)
