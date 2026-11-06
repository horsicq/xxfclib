# bzip2 readers and writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_console.cmake")
xxfclib_add_family_console(bz2
    FORMATS BZ2
    CODECS bzip2 crc
    SOURCES
        src/formats/archive/bz2/xx_bz2.c
        src/formats/archive/bz2/xx_bz2_extractor.c)
