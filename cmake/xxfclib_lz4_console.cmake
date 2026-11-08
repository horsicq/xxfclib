# LZ4 frame readers and the native LZ4 encoder.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_console.cmake")
xxfclib_add_family_console(lz4
    FORMATS LZ4
    CODECS crc lz4
    ENGINE native_filter_formats.c
    ENGINE_DEFINES XFU_NATIVE_FILTER_WRITERS=1
    SOURCES
        src/formats/archive/lz4/xx_lz4.c
        src/formats/archive/lz4/xx_lz4_extractor.c
        src/algo/lz4/xx_lz4.c)
