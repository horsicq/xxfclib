# Microsoft Cabinet readers with the MSZIP, LZX and Quantum codecs.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_console.cmake")
xxfclib_add_family_console(cab
    FORMATS CAB
    CODECS crc deflate lzx quantum
    SOURCES
        src/formats/archive/cab/xx_cab.c
        src/formats/archive/cab/xx_cab_extractor.c
        src/algo/lzx/xx_lzx.c
        src/algo/quantum/xx_quantum.c)
