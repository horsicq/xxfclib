# Zstandard readers and writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_console.cmake")
xxfclib_add_family_console(zstd
    FORMATS ZSTD
    CODECS crc zstd
    SOURCES
        src/formats/archive/zstd/xx_zstd.c
        src/formats/archive/zstd/xx_zstd_extractor.c)
