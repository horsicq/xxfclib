# ZIP/ZIP64 readers and writers plus their native codec/runtime closure.
# This component can coexist with the complete formats target. It must never
# link that target: doing so would retain the unrelated reader registries.
include_guard(GLOBAL)
if(TARGET xxfclib::zip)
    return()
endif()

get_filename_component(_xxfc_zip_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_codepages.cmake")

# Method 95 embeds an XZ stream. Its BCJ filters share xx_7zip_branch.c;
# neither the XZ nor the 7-Zip archive reader is registered by this target.
set(_xxfc_zip_sources
    src/algo/aes/platforms/xx_aes_arm_crypto.c
    src/algo/aes/xx_aes.c
    src/algo/aes_winzip/xx_aes_winzip.c
    src/algo/bzip2/xx_bzip2.c
    src/algo/bzip2/xx_bzip2_bwt.c
    src/algo/bzip2/xx_bzip2_dec.c
    src/algo/bzip2/xx_bzip2_enc.c
    src/algo/bzip2/xx_bzip2_huffman.c
    src/algo/bzip2/xx_bzip2_mtf.c
    src/algo/cmpsc/xx_cmpsc.c
    src/algo/cmpsc/xx_cmpsc_enc.c
    src/algo/crc/platforms/xx_crc32_arm.c
    src/algo/crc/platforms/xx_crc32_arm_pmull.c
    src/algo/crc/platforms/xx_crc32c_sse42.c
    src/algo/crc/platforms/xx_crc64_avx2.c
    src/algo/crc/platforms/xx_crc64_sse2.c
    src/algo/crc/platforms/xx_crc64_vpclmul.c
    src/algo/crc/xx_crc.c
    src/algo/crc/xx_crc16.c
    src/algo/crc/xx_crc32.c
    src/algo/crc/xx_crc64.c
    src/algo/crc/xx_crc8.c
    src/algo/dcl/xx_dcl.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
    src/algo/deflate/platforms/xx_deflate_neon.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/xx_deflate.c
    src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/entropy/platforms/xx_entropy_avx2.c
    src/algo/entropy/platforms/xx_entropy_neon.c
    src/algo/entropy/platforms/xx_entropy_sse2.c
    src/algo/entropy/xx_entropy.c
    src/algo/implode/xx_implode.c
    src/algo/lzma/platforms/xx_lzma_avx2.c
    src/algo/lzma/platforms/xx_lzma_neon.c
    src/algo/lzma/platforms/xx_lzma_sse2.c
    src/algo/lzma/xx_lzma.c
    src/algo/lzma/xx_lzma2_filters.c
    src/algo/lzma/xx_lzma_dec.c
    src/algo/lzma/xx_lzma_enc.c
    src/algo/lzma/xx_lzma_stream.c
    src/algo/packmp3/xx_packmp3.c
    src/algo/ppmd8/xx_ppmd8.c
    src/algo/ppmd8/xx_ppmd8_common.c
    src/algo/ppmd8/xx_ppmd8_dec.c
    src/algo/ppmd8/xx_ppmd8_enc.c
    src/algo/reduce/xx_reduce.c
    src/algo/sha/platforms/xx_sha_arm_crypto.c
    src/algo/sha/platforms/xx_sha_neon.c
    src/algo/sha/xx_sha.c
    src/algo/sha/xx_sha_simd.c
    src/algo/shrink/xx_shrink.c
    src/algo/store/xx_store.c
    src/algo/wavpack/xx_wavpack.c
    src/algo/wavpack/xx_wavpack_dec.c
    src/algo/winzipjpeg/xx_winzipjpeg.c
    src/algo/zipcrypto/xx_zipcrypto.c
    src/algo/zstd/xx_zstd.c
    src/algo/zstd/xx_zstd_dec.c
    src/buf/xx_buf.c
    src/data/platforms/xx_data_avx2.c
    src/data/platforms/xx_data_neon.c
    src/data/platforms/xx_data_sse2.c
    src/data/xx_data_io.c
    src/data/xx_data_raw.c
    src/data/xx_pd.c
    src/formats/archive/7zip/xx_7zip_branch.c
    src/formats/archive/xz/xx_xz.c
    src/formats/archive/xz/xx_xz_writer.c
    src/formats/archive/zip/xx_zip.c
    src/formats/archive/zip/xx_zip_extractor.c
    src/formats/archive/zip/xx_zip_inspect.c
    src/formats/archive/zip/xx_zip_legacy_encoder.c
    src/formats/detection/xx_format_detect_device.c
    src/formats/xx_format.c
    src/formats/xx_format_base.c
    src/formats/xx_format_catalog.c
    src/formats/xx_format_detect_fast.c
    src/formats/xx_format_extractor.c
    src/formats/xx_format_fast_detect_rules.c
    src/formats/xx_format_handle_types.c
    src/formats/xx_format_names.c
    src/formats/xx_format_streams.c
    src/formats/xx_format_unpack_handles.c
    src/formats/xx_memory_map.c
    src/global/platforms/xx_global_cpu.c
    src/global/xx_global.c
    src/io/xx_io.c
    src/io/xx_io_buffer.c
    src/io/xx_io_file.c
    src/io/xx_io_mem.c
    src/io/xx_io_memory_only.c
    src/io/xx_io_sub.c
    src/json/xx_json.c
    src/list/xx_list.c
    src/memory/platforms/xx_memory_avx2.c
    src/memory/platforms/xx_memory_neon.c
    src/memory/platforms/xx_memory_sse2.c
    src/memory/xx_memory.c
    src/memory/xx_memory_rt.c
    src/rt/xx_rt.c
    src/rt/xx_rt_fp.c
    src/rt/xx_rt_math.c
    src/strings/xx_string.c
    src/terminal/xx_terminal.c
    src/terminal/xx_terminal_color.c
    src/var/xx_var.c
    src/xml/xx_xml.c
)

if(WIN32)
    list(APPEND _xxfc_zip_sources
        src/global/platforms/xx_global_windows.c
        src/io/platforms/xx_io_windows.c
        src/memory/platforms/xx_memory_windows.c
        src/rt/platforms/xx_rt_windows.c
        src/strings/platforms/xx_string_windows.c
        src/terminal/platforms/xx_terminal_windows.c
    )
else()
    list(APPEND _xxfc_zip_sources
        src/global/platforms/xx_global_posix.c
        src/io/platforms/xx_io_posix.c
        src/memory/platforms/xx_memory_posix.c
        src/rt/platforms/xx_rt_posix.c
        src/strings/platforms/xx_string_posix.c
        src/terminal/platforms/xx_terminal_posix.c
    )
endif()
list(TRANSFORM _xxfc_zip_sources PREPEND "${_xxfc_zip_root}/")
list(APPEND _xxfc_zip_sources ${XXFC_CODEPAGE_SOURCES})

add_library(xxfclib_zip STATIC EXCLUDE_FROM_ALL ${_xxfc_zip_sources})
add_library(xxfclib::zip ALIAS xxfclib_zip)
target_compile_features(xxfclib_zip PUBLIC c_std_11)
target_compile_definitions(xxfclib_zip
    PUBLIC XXFC_STATIC XXFC_FORMATS_ONLY=1 XFU_SUPPORT_FORMAT_ZIP=1
        XXFC_ENABLE_STREAM_DATASTRUCTS=0
        XXFC_ENABLE_STREAM_IMPORT=0
        XXFC_ENABLE_STREAM_EXPORT=0
        XXFC_ENABLE_STREAM_RESOURCES=0
        XXFC_ENABLE_STREAM_METADATA=0
        XXFC_ENABLE_STREAM_SYMBOLS=0
    PRIVATE XXFC_NO_EXTERNAL_HELPERS=1)
target_include_directories(xxfclib_zip
    PUBLIC "${_xxfc_zip_root}/include"
    PRIVATE
        "${_xxfc_zip_root}/include/xxfclib"
        "${_xxfc_zip_root}/include/xxfclib/formats"
        "${_xxfc_zip_root}/src"
        "${_xxfc_zip_root}/src/data"
        "${_xxfc_zip_root}/src/data/platforms"
        "${_xxfc_zip_root}/src/formats"
        "${_xxfc_zip_root}/src/algo/bzip2"
        "${_xxfc_zip_root}/src/algo/sha")

# Several codec headers use their established short include names. Restrict
# those paths to the algorithms selected above instead of globbing all codecs.
foreach(_xxfc_zip_codec
    aes aes_winzip bzip2 cmpsc crc dcl deflate entropy implode lzma
    packmp3 ppmd8 reduce sha shrink store wavpack winzipjpeg zipcrypto zstd)
    target_include_directories(xxfclib_zip PRIVATE
        "${_xxfc_zip_root}/include/xxfclib/algo/${_xxfc_zip_codec}")
endforeach()

set_target_properties(xxfclib_zip PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_link_libraries(xxfclib_zip PUBLIC kernel32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(xxfclib_zip PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(xxfclib_zip PRIVATE /W4 /utf-8 /Gy /Gw)
    target_compile_definitions(xxfclib_zip PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(xxfclib_zip INTERFACE /OPT:REF /OPT:ICF)
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|AMD64)$")
        enable_language(ASM_MASM)
        target_sources(xxfclib_zip PRIVATE
            "${_xxfc_zip_root}/src/algo/lzma/asm/lzma_windows_x64.asm"
            "${_xxfc_zip_root}/src/algo/bzip2/asm/bzip2_windows_x64.asm")
    elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86)$")
        enable_language(ASM_MASM)
        target_sources(xxfclib_zip PRIVATE
            "${_xxfc_zip_root}/src/algo/bzip2/asm/bzip2_windows_x86.asm")
    endif()
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
        foreach(_xxfc_zip_avx_source
            src/data/platforms/xx_data_avx2.c
            src/memory/platforms/xx_memory_avx2.c
            src/algo/crc/platforms/xx_crc64_avx2.c
            src/algo/deflate/platforms/xx_deflate_avx2.c
            src/algo/entropy/platforms/xx_entropy_avx2.c
            src/algo/lzma/platforms/xx_lzma_avx2.c)
            set_property(SOURCE "${_xxfc_zip_root}/${_xxfc_zip_avx_source}"
                APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
        endforeach()
    endif()
else()
    target_compile_options(xxfclib_zip PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(xxfclib_zip PRIVATE -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(xxfclib_zip INTERFACE "LINKER:-dead_strip")
        elseif(UNIX OR MINGW)
            target_link_options(xxfclib_zip INTERFACE "LINKER:--gc-sections")
        endif()
    endif()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_arm_accel.cmake")
xxfclib_configure_arm_accel_sources("${_xxfc_zip_root}")

list(LENGTH _xxfc_zip_sources _xxfc_zip_source_count)
message(STATUS "xxfclib ZIP component: ${_xxfc_zip_source_count} C sources; ZIP/ZIP64 only")
unset(_xxfc_zip_sources)
unset(_xxfc_zip_source_count)
unset(_xxfc_zip_codec)
unset(_xxfc_zip_avx_source)
unset(_xxfc_zip_root)
