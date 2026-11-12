# Unix compress(1) readers and the native LZW encoder.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(unixcompress
    FORMATS UNIX_COMPRESS
    CODECS compress crc
    ENGINE native_filter_formats.c
    ENGINE_DEFINES XFU_NATIVE_FILTER_WRITERS=1
    SOURCES
        src/formats/archive/unixcompress/xx_unixcompress.c
        src/formats/archive/unixcompress/xx_unixcompress_extractor.c
        src/algo/compress/xx_compress.c)
