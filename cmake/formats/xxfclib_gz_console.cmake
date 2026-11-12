# gzip readers and writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(gz
    FORMATS GZ
    CODECS crc deflate
    SOURCES
        src/formats/archive/gz/xx_gz.c
        src/formats/archive/gz/xx_gz_extractor.c)
