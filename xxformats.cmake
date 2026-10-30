# Format readers, extractors and algorithms, plus their runtime dependencies.
# Define XXFC_STATIC, add XXFORMATS_INCLUDE_DIR publicly and
# XXFORMATS_INCLUDE_DIRS privately, and link XXFORMATS_LIBRARIES.
# The legacy x*.c parsers are part of die_engine and are deliberately excluded.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xxfclib_streams.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xxfclib_codepages.cmake")
set(XXFORMATS_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")
# FOLLOW_SYMLINKS: a synced folder (ownCloud, OneDrive) can mark directories
# as cloud reparse points, which a recursive glob otherwise skips.
file(GLOB_RECURSE XXFORMATS_SOURCES FOLLOW_SYMLINKS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/algo/xx_*.c"
)
list(FILTER XXFORMATS_SOURCES EXCLUDE REGEX
    "[/\\\\][^/\\\\]* \\(conflicted copy [^/\\\\]*\\)\\.[^/\\\\]+$")
# This includes SHA and BZip2 scalar and CPU-dispatched kernels. Their SIMD
# instructions are isolated inside guarded functions, so no global ISA flag
# is needed for formats-only consumers or non-x86 builds.
# DIE music readers require the separate signature engine and its runtime.
list(FILTER XXFORMATS_SOURCES EXCLUDE REGEX "/src/formats/audio/die_music/")
# xx_format_datastructs.c compiles every schema family unit and per-format
# data file itself, so the glob must not compile them a second time.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xxfclib_schema_sources.cmake")
list(REMOVE_ITEM XXFORMATS_SOURCES ${XXFC_SCHEMA_FAMILY_SOURCES})
list(APPEND XXFORMATS_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/extractor/xx_extractor.c
    ${XXFC_CODEPAGE_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/installer/fead/encoder/deflate.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/installer/fead/encoder/trees.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/installer/fead/encoder/adler32.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/installer/fead/encoder/crc32.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/installer/fead/encoder/compress.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/document/pdf/xxpdf.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/document/pdf/xxpdf_decode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/document/pdf/xxpdf_extractor.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/installer/nsis/nsis_bzip2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sort/xx_sort.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sort/platforms/xx_sort_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sort/platforms/xx_sort_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sort/platforms/xx_sort_neon.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt_fp.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt_math.c
    ${CMAKE_CURRENT_LIST_DIR}/src/buf/xx_buf.c
    ${CMAKE_CURRENT_LIST_DIR}/src/json/xx_json.c
    ${CMAKE_CURRENT_LIST_DIR}/src/xml/xx_xml.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/xx_global.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_cpu.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_file.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_mem.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_memory_only.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_sub.c
    ${CMAKE_CURRENT_LIST_DIR}/src/io/xx_io_process.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/xx_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/xx_memory_rt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_neon.c
    ${CMAKE_CURRENT_LIST_DIR}/src/strings/xx_string.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_pd.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_data_raw.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_data_io.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms/xx_data_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms/xx_data_avx2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms/xx_data_neon.c
    ${CMAKE_CURRENT_LIST_DIR}/src/list/xx_list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sort/xx_sort_list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/var/xx_var.c
)
if(WIN32)
    list(APPEND XXFORMATS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/fs/platforms/xx_fs_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/io/platforms/xx_io_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/strings/platforms/xx_string_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/rt/platforms/xx_rt_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/sort/platforms/xx_sort_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_windows.c
    )
    set(XXFORMATS_LIBRARIES kernel32)
else()
    list(APPEND XXFORMATS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/fs/platforms/xx_fs_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/io/platforms/xx_io_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/strings/platforms/xx_string_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/rt/platforms/xx_rt_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/sort/platforms/xx_sort_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_posix.c
    )
    find_package(Threads REQUIRED)
    set(XXFORMATS_LIBRARIES Threads::Threads m ${CMAKE_DL_LIBS})
endif()

xxfclib_select_stream_sources(XXFORMATS_SOURCES)

# Resolve internal headers and the short algorithm header names used by readers.
set(XXFORMATS_INCLUDE_DIRS
    ${XXFORMATS_INCLUDE_DIR}
    ${XXFORMATS_INCLUDE_DIR}/xxfclib
    ${XXFORMATS_INCLUDE_DIR}/xxfclib/formats
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/data
    ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms
    ${CMAKE_CURRENT_LIST_DIR}/src/formats
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/archive/tar_common
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/bzip2
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/sha
    ${CMAKE_CURRENT_LIST_DIR}/src/algo/md5
)
# FOLLOW_SYMLINKS: a synced folder (ownCloud, OneDrive) can mark directories
# as cloud reparse points, which a recursive glob otherwise skips.
file(GLOB_RECURSE _xxformats_algo_headers FOLLOW_SYMLINKS CONFIGURE_DEPENDS
    "${XXFORMATS_INCLUDE_DIR}/xxfclib/algo/*.h")
list(FILTER _xxformats_algo_headers EXCLUDE REGEX
    "[/\\\\][^/\\\\]* \\(conflicted copy [^/\\\\]*\\)\\.[^/\\\\]+$")
foreach(_xxformats_header IN LISTS _xxformats_algo_headers)
    get_filename_component(_xxformats_dir "${_xxformats_header}" DIRECTORY)
    list(APPEND XXFORMATS_INCLUDE_DIRS "${_xxformats_dir}")
endforeach()
list(REMOVE_DUPLICATES XXFORMATS_INCLUDE_DIRS)

# Private prefix batching is used only by die_engine's literal-search engine.
set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/src/data/xx_data_raw.c
    APPEND PROPERTY COMPILE_DEFINITIONS XXFC_FORMATS_ONLY)
# The detection fallback also depends on the excluded DIE music engine.
set_property(SOURCE
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_format.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_format_catalog.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_format_names.c
    ${CMAKE_CURRENT_LIST_DIR}/src/formats/detection/xx_format_detect_device.c
    APPEND PROPERTY COMPILE_DEFINITIONS XXFC_FORMATS_ONLY)
set_property(SOURCE ${CMAKE_CURRENT_LIST_DIR}/src/formats/xx_format_extractor.c
    APPEND PROPERTY COMPILE_DEFINITIONS XXFC_FORMATS_ONLY)

if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
    set_source_files_properties(
        ${CMAKE_CURRENT_LIST_DIR}/src/data/platforms/xx_data_avx2.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_avx2.c
        ${CMAKE_CURRENT_LIST_DIR}/src/algo/adler32/platforms/xx_adler32_avx2.c
        ${CMAKE_CURRENT_LIST_DIR}/src/algo/crc/platforms/xx_crc64_avx2.c
        ${CMAKE_CURRENT_LIST_DIR}/src/algo/deflate/platforms/xx_deflate_avx2.c
        ${CMAKE_CURRENT_LIST_DIR}/src/algo/entropy/platforms/xx_entropy_avx2.c
        ${CMAKE_CURRENT_LIST_DIR}/src/algo/lzma/platforms/xx_lzma_avx2.c
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
endif()
unset(_xxformats_header)
unset(_xxformats_dir)
unset(_xxformats_algo_headers)

# SIMD flags apply only to the runtime-dispatched sorting translation units.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xxfclib_sort_simd.cmake")
xxfclib_configure_sort_simd_sources("${CMAKE_CURRENT_LIST_DIR}")

# SIMD flags apply only to the runtime-dispatched .NET #US backends.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xxfclib_dotnet_simd.cmake")
xxfclib_configure_dotnet_simd_sources("${CMAKE_CURRENT_LIST_DIR}")

# ARM feature flags apply only to runtime-dispatched algorithm backends.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xxfclib_arm_accel.cmake")
xxfclib_configure_arm_accel_sources("${CMAKE_CURRENT_LIST_DIR}")
