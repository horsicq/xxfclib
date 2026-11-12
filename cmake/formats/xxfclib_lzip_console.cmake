# lzip readers and the native lzip encoder.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(lzip
    FORMATS LZIP
    CODECS crc lzip lzma sha
    ENGINE native_filter_formats.c
    ENGINE_DEFINES XFU_NATIVE_FILTER_WRITERS=1
    SOURCES
        src/formats/archive/lzip/xx_lzip.c
        src/formats/archive/lzip/xx_lzip_extractor.c
        src/algo/lzip/xx_lzip.c)
