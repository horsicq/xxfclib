# Native archive presets sharing the ZIP component's codec/runtime baseline.
# xxfclib_add_archive_profile(target DEFAULT|SCANENGINEMIN|ZIP|TAR|AR|ALL) exports a
# console target, plus target_core and target_formats for DEFAULT/SCANENGINEMIN.
include_guard(GLOBAL)

function(xxfclib_add_archive_profile target profile)
    if(TARGET "${target}")
        message(FATAL_ERROR "Archive profile target '${target}' already exists")
    endif()
    if(NOT profile MATCHES "^(ALL|DEFAULT|SCANENGINEMIN|ZIP|TAR|AR)$")
        message(FATAL_ERROR "Unknown archive format profile '${profile}'")
    endif()
    set(_module_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    get_filename_component(_root "${_module_dir}/.." ABSOLUTE)
    if(profile STREQUAL "ALL")
        include("${_module_dir}/xxfclib_console.cmake")
        add_library("${target}" INTERFACE)
        target_compile_definitions("${target}" INTERFACE XFU_SUPPORT_FORMAT_ALL=1)
        target_link_libraries("${target}" INTERFACE xxfclib::archive_console)
        return()
    elseif(profile STREQUAL "AR")
        include("${_module_dir}/xxfclib_ar_console.cmake")
        add_library("${target}" INTERFACE)
        target_link_libraries("${target}" INTERFACE xxfclib::archive_console_ar)
        return()
    elseif(profile STREQUAL "TAR")
        include("${_module_dir}/xxfclib_tar_console.cmake")
        add_library("${target}" INTERFACE)
        target_link_libraries("${target}" INTERFACE xxfclib::archive_console_tar)
        return()
    elseif(profile STREQUAL "ZIP")
        include("${_module_dir}/xxfclib_zip_console.cmake")
        add_library("${target}" INTERFACE)
        target_link_libraries("${target}" INTERFACE xxfclib::archive_console_zip)
        return()
    endif()

    include("${_module_dir}/xxfclib_zip.cmake")
    # These native units are the closure of the selected archive readers.
    # RAR needs the PE resource reader; 7z also needs Brotli/LZ4/LZ5/Lizard
    # codecs. These dependencies do not register their own archive formats.
    set(_extra_sources
        src/algo/adler32/xx_adler32.c
        src/algo/adler32/platforms/xx_adler32_arm_dotprod.c
        src/algo/adler32/platforms/xx_adler32_avx2.c
        src/algo/adler32/platforms/xx_adler32_neon.c
        src/algo/adler32/platforms/xx_adler32_sse2.c
        src/algo/blake2/xx_blake2.c
        src/algo/brotli/xx_brotli.c
        src/algo/brotli/xx_brotli_data.c
        src/algo/deflate/xx_zlib_stream.c
        src/algo/lizard/xx_lizard.c
        src/algo/lz4/xx_lz4.c
        src/algo/lz5/xx_lz5.c
        src/algo/lzx/xx_lzx.c
        src/algo/ppmd7/xx_ppmd7_common.c
        src/algo/ppmd7/xx_ppmd7_dec.c
        src/algo/ppmd7/xx_ppmd7_enc.c
        src/algo/quantum/xx_quantum.c
        src/algo/rar/xx_rarx.c
        src/algo/rar/xx_rarx15.c
        src/algo/rar/xx_rarx20.c
        src/algo/rar/xx_rarx29.c
        src/algo/rar/xx_rarx50.c
        src/formats/archive/7zip/xx_7zip.c
        src/formats/archive/7zip/xx_7zip_extractor.c
        src/formats/archive/7zip/xx_7zip_stream.c
        src/formats/archive/7zip/xx_7zip_writer.c
        src/formats/archive/cab/xx_cab.c
        src/formats/archive/cab/xx_cab_extractor.c
        src/formats/archive/rar/xx_rar.c
        src/formats/archive/rar/xx_rar_extractor.c
        src/formats/executable/pe/xx_pe.c
        src/formats/executable/pe/xx_pe_stream_common.c
        src/formats/filesystem/iso9660/xx_iso9660.c
        src/formats/filesystem/iso9660/xx_iso9660_extractor.c
        src/formats/filesystem/iso9660/xx_iso_zisofs_native.c
        src/sort/platforms/xx_sort_avx2.c
        src/sort/platforms/xx_sort_neon.c
        src/sort/platforms/xx_sort_sse2.c
        src/sort/xx_sort.c)
    if(WIN32)
        list(APPEND _extra_sources src/sort/platforms/xx_sort_windows.c)
    else()
        list(APPEND _extra_sources src/sort/platforms/xx_sort_posix.c)
    endif()
    if(profile STREQUAL "DEFAULT")
        list(APPEND _extra_sources
            src/formats/archive/ar/xx_ar.c
            src/formats/archive/ar/xx_ar_extractor.c
            src/formats/archive/bz2/xx_bz2.c
            src/formats/archive/bz2/xx_bz2_extractor.c
            src/formats/archive/cpio/xx_cpio.c
            src/formats/archive/cpio/xx_cpio_extractor.c
            src/formats/archive/gz/xx_gz.c
            src/formats/archive/gz/xx_gz_extractor.c
            src/formats/archive/lz4/xx_lz4.c
            src/formats/archive/lz4/xx_lz4_extractor.c
            src/formats/archive/tar/xx_tar.c
            src/formats/archive/tar/xx_tar_extractor.c
            src/formats/archive/tar_bz2/xx_tar_bz2.c
            src/formats/archive/tar_bz2/xx_tar_bz2_extractor.c
            src/formats/archive/tar_common/xx_tar_common.c
            src/formats/archive/tar_gz/xx_tar_gz.c
            src/formats/archive/tar_gz/xx_tar_gz_extractor.c
            src/formats/archive/tar_lz4/xx_tar_lz4.c
            src/formats/archive/tar_lz4/xx_tar_lz4_extractor.c
            src/formats/archive/tar_xz/xx_tar_xz.c
            src/formats/archive/tar_xz/xx_tar_xz_extractor.c
            src/formats/archive/tar_zstd/xx_tar_zstd.c
            src/formats/archive/tar_zstd/xx_tar_zstd_extractor.c
            src/formats/archive/zstd/xx_zstd.c
            src/formats/archive/zstd/xx_zstd_extractor.c
            src/formats/archive/xz/xx_xz_extractor.c)
    endif()
    list(TRANSFORM _extra_sources PREPEND "${_root}/")
    get_target_property(_base_sources xxfclib_zip SOURCES)
    add_library("${target}_formats" STATIC ${_base_sources} ${_extra_sources})
    foreach(_property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES
            COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
        get_target_property(_value xxfclib_zip "${_property}")
        if(_value)
            set_property(TARGET "${target}_formats" PROPERTY "${_property}" "${_value}")
        endif()
    endforeach()
    get_target_property(_definitions xxfclib_zip INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER _definitions EXCLUDE REGEX "^XFU_SUPPORT_FORMAT_")
    target_compile_definitions("${target}_formats" PUBLIC
        ${_definitions} "XFU_SUPPORT_FORMAT_${profile}=1"
        PRIVATE XXFC_NO_EXTERNAL_HELPERS=1)
    get_target_property(_libraries xxfclib_zip LINK_LIBRARIES)
    target_link_libraries("${target}_formats" PUBLIC ${_libraries})
    if(profile STREQUAL "DEFAULT")
        target_include_directories("${target}_formats" PRIVATE
            "${_root}/src/formats/archive/tar_common")
    endif()
    foreach(_codec blake2 brotli lizard lz4 lz5 lzx ppmd7 quantum rar)
        target_include_directories("${target}_formats" PRIVATE
            "${_root}/include/xxfclib/algo/${_codec}" "${_root}/src/algo/${_codec}")
    endforeach()
    if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
        set_property(SOURCE "${_root}/src/sort/platforms/xx_sort_avx2.c"
            "${_root}/src/algo/adler32/platforms/xx_adler32_avx2.c"
            APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        xxfclib_target_isa_options(x86 _sort_isa -mavx2)
        set_property(SOURCE "${_root}/src/sort/platforms/xx_sort_avx2.c"
            "${_root}/src/algo/adler32/platforms/xx_adler32_avx2.c"
            APPEND PROPERTY COMPILE_OPTIONS ${_sort_isa})
    endif()

    set(_engine "${_root}/src/xfileunpacker")
    add_library("${target}_core" STATIC
        "${_engine}/core.c" "${_engine}/metadata.c"
        "${_engine}/native_writer_backend.c" "${_engine}/xxfc_readers.c")
    if(profile STREQUAL "DEFAULT")
        target_sources("${target}_core" PRIVATE "${_engine}/native_tar_writer.c")
    endif()
    target_include_directories("${target}_core" PUBLIC "${_engine}/include"
        PRIVATE "${_engine}")
    target_link_libraries("${target}_core" PUBLIC "${target}_formats")
    target_compile_definitions("${target}_core" PRIVATE
        XFU_XXFCLIB_ONLY=1 XXFC_NO_EXTERNAL_HELPERS=1)

    add_library("${target}" STATIC "${_root}/src/console/xx_archive_console.c")
    target_link_libraries("${target}" PUBLIC "${target}_core")
    if(WIN32)
        target_link_libraries("${target}" PRIVATE shell32)
    endif()
    foreach(_component "${target}" "${target}_core" "${target}_formats")
        target_compile_features("${_component}" PUBLIC c_std_11)
        set_target_properties("${_component}" PROPERTIES C_STANDARD 11
            C_STANDARD_REQUIRED YES C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
        if(MSVC)
            target_compile_options("${_component}" PRIVATE /W4 /utf-8 /Gy /Gw)
            target_compile_definitions("${_component}" PRIVATE _CRT_SECURE_NO_WARNINGS)
        else()
            target_compile_options("${_component}" PRIVATE
                -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections)
        endif()
        if(WIN32)
            target_compile_definitions("${_component}" PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        else()
            target_compile_definitions("${_component}" PRIVATE _POSIX_C_SOURCE=200809L)
            if(APPLE)
                # sys/sysctl.h (xx_global_cpu.c) needs Darwin's BSD typedefs.
                target_compile_definitions("${_component}" PRIVATE _DARWIN_C_SOURCE)
            endif()
        endif()
    endforeach()
endfunction()
