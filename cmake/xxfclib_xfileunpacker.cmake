# Optional CXFileUnpacker engine component.
#
# The application frontends include this module after adding xxfclib.  Keeping
# the archive engine here gives the console, GUI and TUI one local xxfclib
# implementation without package-manager codecs or runtime helper programs.
include_guard(GLOBAL)

if(TARGET xxfclib::xfileunpacker_core)
    return()
endif()
if(NOT TARGET xxfclib::xxfclib)
    message(FATAL_ERROR
        "xxfclib_xfileunpacker.cmake must be included after xxfclib::xxfclib exists")
endif()

get_filename_component(_xxfc_xfu_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_xxfc_xfu_source_dir "${_xxfc_xfu_root}/src/xfileunpacker")
set(_xxfc_xfu_include_dir "${_xxfc_xfu_source_dir}/include")
set(_xxfc_xfu_registry_dir "${_xxfc_xfu_source_dir}")

option(XFILEUNPACKER_BUILD_FREEARC_SFX
    "Embed the source-built Windows XFU FreeArc SFX stub" OFF)

if(NOT WIN32 OR NOT XFILEUNPACKER_BUILD_FREEARC_SFX)
    add_library(xxfclib_xfu_freearc_sfx_stub INTERFACE)
else()
    add_executable(xxfclib_xfu_freearc_sfx_image
        "${_xxfc_xfu_source_dir}/freearc_sfx_stub.c")
    target_link_libraries(xxfclib_xfu_freearc_sfx_image
        PRIVATE xxfclib::xxfclib bcrypt)
    target_compile_features(xxfclib_xfu_freearc_sfx_image PRIVATE c_std_11)
    target_compile_definitions(xxfclib_xfu_freearc_sfx_image PRIVATE
        UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_include_directories(xxfclib_xfu_freearc_sfx_image PRIVATE
        "${_xxfc_xfu_source_dir}")
    set_target_properties(xxfclib_xfu_freearc_sfx_image PROPERTIES
        OUTPUT_NAME XfuFreeArc
        SUFFIX .sfx
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/freearc-sfx-stub")
    if(MSVC)
        target_compile_options(xxfclib_xfu_freearc_sfx_image PRIVATE /W4 /utf-8)
    elseif(MINGW)
        target_link_options(xxfclib_xfu_freearc_sfx_image PRIVATE -municode)
    endif()

    set(_xxfc_xfu_embedded_sfx
        "${CMAKE_CURRENT_BINARY_DIR}/freearc-sfx-stub/$<CONFIG>/freearc_sfx_data.c")
    add_custom_command(OUTPUT "${_xxfc_xfu_embedded_sfx}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=$<TARGET_FILE:xxfclib_xfu_freearc_sfx_image>"
            "-DOUTPUT=${_xxfc_xfu_embedded_sfx}"
            -P "${CMAKE_CURRENT_LIST_DIR}/freearc_sfx_embed.cmake"
        DEPENDS
            xxfclib_xfu_freearc_sfx_image
            "${CMAKE_CURRENT_LIST_DIR}/freearc_sfx_embed.cmake"
        VERBATIM)
    add_library(xxfclib_xfu_freearc_sfx_stub STATIC
        "${_xxfc_xfu_embedded_sfx}")
    target_compile_definitions(xxfclib_xfu_freearc_sfx_stub
        INTERFACE XFU_HAVE_FREEARC_SFX=1)
endif()
add_library(xxfclib::xfu_freearc_sfx_stub ALIAS
    xxfclib_xfu_freearc_sfx_stub)

set(_xxfc_xfu_engine_sources
    "${_xxfc_xfu_source_dir}/core.c"
    "${_xxfc_xfu_source_dir}/metadata.c"
    "${_xxfc_xfu_source_dir}/native_reader_backend.c"
    "${_xxfc_xfu_source_dir}/native_writer_backend.c"
    "${_xxfc_xfu_source_dir}/native_tar_writer.c"
    "${_xxfc_xfu_source_dir}/native_filter_formats.c"
    "${_xxfc_xfu_source_dir}/native_extra_formats.c"
    "${_xxfc_xfu_source_dir}/xxfclib_only_backends.c"
    "${_xxfc_xfu_source_dir}/pea_backend.c"
    "${_xxfc_xfu_source_dir}/freearc_backend.c"
    "${_xxfc_xfu_source_dir}/freearc_sfx_backend.c"
    "${_xxfc_xfu_source_dir}/split_backend.c"
    "${_xxfc_xfu_source_dir}/sfx_backend.c"
    "${_xxfc_xfu_source_dir}/information.c"
    "${_xxfc_xfu_source_dir}/supported_types.c"
    "${_xxfc_xfu_registry_dir}/xxfc_readers.c")

set(_xxfc_xfu_public_headers
    "${_xxfc_xfu_include_dir}/core.h"
    "${_xxfc_xfu_include_dir}/metadata.h"
    "${_xxfc_xfu_include_dir}/native_reader_backend.h"
    "${_xxfc_xfu_include_dir}/native_writer_backend.h"
    "${_xxfc_xfu_include_dir}/native_tar_writer.h"
    "${_xxfc_xfu_include_dir}/native_filter_formats.h"
    "${_xxfc_xfu_include_dir}/native_extra_formats.h"
    "${_xxfc_xfu_include_dir}/peazip_backend.h"
    "${_xxfc_xfu_include_dir}/pea_backend.h"
    "${_xxfc_xfu_include_dir}/freearc_backend.h"
    "${_xxfc_xfu_include_dir}/freearc_sfx_backend.h"
    "${_xxfc_xfu_include_dir}/split_backend.h"
    "${_xxfc_xfu_include_dir}/sfx_backend.h"
    "${_xxfc_xfu_include_dir}/upx_backend.h"
    "${_xxfc_xfu_include_dir}/information.h"
    "${_xxfc_xfu_include_dir}/supported_types.h")

add_library(xxfclib_xfileunpacker_core STATIC
    ${_xxfc_xfu_engine_sources}
    ${_xxfc_xfu_public_headers}
    "${_xxfc_xfu_source_dir}/freearc_sfx_locator.h"
    "${_xxfc_xfu_source_dir}/freearc_sfx_names.h")
add_library(xxfclib::xfileunpacker_core ALIAS
    xxfclib_xfileunpacker_core)

target_compile_features(xxfclib_xfileunpacker_core PUBLIC c_std_11)
target_compile_definitions(xxfclib_xfileunpacker_core PRIVATE
    XFU_XXFCLIB_ONLY=1
    XXFC_NO_EXTERNAL_HELPERS=1)
target_include_directories(xxfclib_xfileunpacker_core
    PUBLIC "${_xxfc_xfu_include_dir}"
    PRIVATE
        "${_xxfc_xfu_source_dir}"
        "${_xxfc_xfu_registry_dir}")
target_link_libraries(xxfclib_xfileunpacker_core
    PUBLIC xxfclib::xxfclib
    PRIVATE xxfclib::xfu_freearc_sfx_stub)
set_target_properties(xxfclib_xfileunpacker_core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(xxfclib_xfileunpacker_core PRIVATE /W4 /utf-8)
    target_compile_definitions(xxfclib_xfileunpacker_core PRIVATE
        _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(xxfclib_xfileunpacker_core PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_compile_definitions(xxfclib_xfileunpacker_core PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_compile_definitions(xxfclib_xfileunpacker_core PRIVATE
        _POSIX_C_SOURCE=200809L)
endif()

list(LENGTH _xxfc_xfu_engine_sources _xxfc_xfu_engine_source_count)
message(STATUS
    "xxfclib XFileUnpacker engine: ${_xxfc_xfu_engine_source_count} C sources; external helpers disabled")

unset(_xxfc_xfu_embedded_sfx)
unset(_xxfc_xfu_engine_source_count)
unset(_xxfc_xfu_engine_sources)
unset(_xxfc_xfu_public_headers)
unset(_xxfc_xfu_registry_dir)
unset(_xxfc_xfu_include_dir)
unset(_xxfc_xfu_source_dir)
unset(_xxfc_xfu_root)
