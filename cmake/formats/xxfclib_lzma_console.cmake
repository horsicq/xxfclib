# LZMA-Alone readers and writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(lzma
    FORMATS LZMA
    CODECS crc lzma lzma_alone sha
    SOURCES
        src/formats/archive/lzma/xx_lzma.c
        src/formats/archive/lzma/xx_lzma_extractor.c
        src/formats/archive/lzma/xx_lzma_writer.c
        src/algo/lzma_alone/xx_lzma_alone.c)
