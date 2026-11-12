# lzop readers and the native lzop encoder.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(lzop
    FORMATS LZOP
    CODECS adler32 crc lzo lzop
    SORT
    ENGINE native_filter_formats.c
    ENGINE_DEFINES XFU_NATIVE_FILTER_WRITERS=1
    SOURCES
        src/formats/archive/lzop/xx_lzop.c
        src/formats/archive/lzop/xx_lzop_extractor.c
        src/algo/lzo/xx_lzo.c
        src/algo/lzop/xx_lzop.c
        src/algo/adler32/xx_adler32.c
        src/algo/adler32/platforms/xx_adler32_arm_dotprod.c
        src/algo/adler32/platforms/xx_adler32_avx2.c
        src/algo/adler32/platforms/xx_adler32_neon.c
        src/algo/adler32/platforms/xx_adler32_sse2.c)
