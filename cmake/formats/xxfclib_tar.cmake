# TAR and compressed TAR, without the complete archive registry.
include_guard(GLOBAL)
if(TARGET xxfclib::tar)
    return()
endif()
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_zip.cmake")
get_filename_component(_xxfc_tar_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Share the runtime build settings, but retain only codecs needed by TAR's
# filters. In particular, this target never links the ZIP library.
get_target_property(_xxfc_tar_sources xxfclib_zip SOURCES)
list(FILTER _xxfc_tar_sources EXCLUDE REGEX "/src/formats/archive/zip/")
list(FILTER _xxfc_tar_sources EXCLUDE REGEX "/src/algo/(aes|aes_winzip|cmpsc|dcl|implode|packmp3|ppmd8|reduce|shrink|wavpack|winzipjpeg|zipcrypto)/")
set(_xxfc_tar_extra
    src/algo/adler32/xx_adler32.c
    src/algo/adler32/platforms/xx_adler32_arm_dotprod.c
    src/algo/adler32/platforms/xx_adler32_avx2.c
    src/algo/adler32/platforms/xx_adler32_neon.c
    src/algo/adler32/platforms/xx_adler32_sse2.c
    src/algo/compress/xx_compress.c
    src/algo/lz4/xx_lz4.c
    src/algo/lzip/xx_lzip.c
    src/algo/lzma_alone/xx_lzma_alone.c
    src/algo/lzo/xx_lzo.c
    src/algo/lzop/xx_lzop.c
    src/formats/archive/bz2/xx_bz2.c
    src/formats/archive/bz2/xx_bz2_extractor.c
    src/formats/archive/gz/xx_gz.c
    src/formats/archive/gz/xx_gz_extractor.c
    src/formats/archive/lz4/xx_lz4.c
    src/formats/archive/lz4/xx_lz4_extractor.c
    src/formats/archive/lzip/xx_lzip.c
    src/formats/archive/lzip/xx_lzip_extractor.c
    src/formats/archive/lzma/xx_lzma.c
    src/formats/archive/lzma/xx_lzma_extractor.c
    src/formats/archive/lzma/xx_lzma_writer.c
    src/formats/archive/lzop/xx_lzop.c
    src/formats/archive/lzop/xx_lzop_extractor.c
    src/formats/archive/tar/xx_tar.c
    src/formats/archive/tar/xx_tar_extractor.c
    src/formats/archive/tar_bz2/xx_tar_bz2.c
    src/formats/archive/tar_bz2/xx_tar_bz2_extractor.c
    src/formats/archive/tar_common/xx_tar_common.c
    src/formats/archive/tar_compress/xx_tar_compress.c
    src/formats/archive/tar_compress/xx_tar_compress_extractor.c
    src/formats/archive/tar_gz/xx_tar_gz.c
    src/formats/archive/tar_gz/xx_tar_gz_extractor.c
    src/formats/archive/tar_lz4/xx_tar_lz4.c
    src/formats/archive/tar_lz4/xx_tar_lz4_extractor.c
    src/formats/archive/tar_lzip/xx_tar_lzip.c
    src/formats/archive/tar_lzip/xx_tar_lzip_extractor.c
    src/formats/archive/tar_lzma/xx_tar_lzma.c
    src/formats/archive/tar_lzma/xx_tar_lzma_extractor.c
    src/formats/archive/tar_lzop/xx_tar_lzop.c
    src/formats/archive/tar_lzop/xx_tar_lzop_extractor.c
    src/formats/archive/tar_xz/xx_tar_xz.c
    src/formats/archive/tar_xz/xx_tar_xz_extractor.c
    src/formats/archive/tar_zstd/xx_tar_zstd.c
    src/formats/archive/tar_zstd/xx_tar_zstd_extractor.c
    src/formats/archive/unixcompress/xx_unixcompress.c
    src/formats/archive/unixcompress/xx_unixcompress_extractor.c
    src/formats/archive/uue/xx_uue.c
    src/formats/archive/uue/xx_uue_extractor.c
    src/formats/archive/xz/xx_xz_extractor.c
    src/formats/archive/zstd/xx_zstd.c
    src/formats/archive/zstd/xx_zstd_extractor.c
    src/formats/other/base64/xx_base64.c
    src/formats/other/base64/xx_base64_extractor.c
    src/sort/platforms/xx_sort_avx2.c
    src/sort/platforms/xx_sort_neon.c
    src/sort/platforms/xx_sort_sse2.c
    src/sort/xx_sort.c)
if(WIN32)
    list(APPEND _xxfc_tar_extra src/sort/platforms/xx_sort_windows.c)
else()
    list(APPEND _xxfc_tar_extra src/sort/platforms/xx_sort_posix.c)
endif()
list(TRANSFORM _xxfc_tar_extra PREPEND "${_xxfc_tar_root}/")
add_library(xxfclib_tar STATIC EXCLUDE_FROM_ALL ${_xxfc_tar_sources} ${_xxfc_tar_extra})
add_library(xxfclib::tar ALIAS xxfclib_tar)
foreach(_property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES
        COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
    get_target_property(_value xxfclib_zip "${_property}")
    if(_value)
        set_property(TARGET xxfclib_tar PROPERTY "${_property}" "${_value}")
    endif()
endforeach()
get_target_property(_definitions xxfclib_zip INTERFACE_COMPILE_DEFINITIONS)
list(FILTER _definitions EXCLUDE REGEX "^XFU_SUPPORT_FORMAT_")
target_compile_definitions(xxfclib_tar PUBLIC ${_definitions}
    XFU_SUPPORT_FORMAT_TAR=1 XFU_SUPPORT_FORMAT_GZ=1 XFU_SUPPORT_FORMAT_BZ2=1
    XFU_SUPPORT_FORMAT_XZ=1 XFU_SUPPORT_FORMAT_LZMA=1 XFU_SUPPORT_FORMAT_LZIP=1
    XFU_SUPPORT_FORMAT_LZ4=1 XFU_SUPPORT_FORMAT_LZOP=1 XFU_SUPPORT_FORMAT_ZSTD=1
    XFU_SUPPORT_FORMAT_UNIX_COMPRESS=1 XFU_SUPPORT_FORMAT_UUE=1 XFU_SUPPORT_FORMAT_BASE64=1
    PRIVATE XXFC_NO_EXTERNAL_HELPERS=1)
get_target_property(_libraries xxfclib_zip LINK_LIBRARIES)
target_link_libraries(xxfclib_tar PUBLIC ${_libraries})
target_include_directories(xxfclib_tar PRIVATE "${_xxfc_tar_root}/src/formats/archive/tar_common")
foreach(_codec adler32 compress lz4 lzip lzma_alone lzo lzop)
    target_include_directories(xxfclib_tar PRIVATE
        "${_xxfc_tar_root}/include/xxfclib/algo/${_codec}")
endforeach()
target_compile_features(xxfclib_tar PUBLIC c_std_11)
set_target_properties(xxfclib_tar PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(xxfclib_tar PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
        set_property(SOURCE "${_xxfc_tar_root}/src/sort/platforms/xx_sort_avx2.c"
            "${_xxfc_tar_root}/src/algo/adler32/platforms/xx_adler32_avx2.c"
            APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    xxfclib_target_isa_options(x86 _sort_isa -mavx2)
    set_property(SOURCE "${_xxfc_tar_root}/src/sort/platforms/xx_sort_avx2.c"
        "${_xxfc_tar_root}/src/algo/adler32/platforms/xx_adler32_avx2.c"
        APPEND PROPERTY COMPILE_OPTIONS ${_sort_isa})
endif()
unset(_xxfc_tar_extra)
unset(_xxfc_tar_sources)
unset(_xxfc_tar_root)
