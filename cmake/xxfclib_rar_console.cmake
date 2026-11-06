# RAR readers (RAR 1.4 to RAR 5) with their codecs; the PE reader locates SFX archives.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_console.cmake")
xxfclib_add_family_console(rar
    FORMATS RAR
    CODECS aes blake2 crc ppmd7 rar sha
    SORT
    SOURCES
        src/formats/archive/rar/xx_rar.c
        src/formats/archive/rar/xx_rar_extractor.c
        src/formats/executable/pe/xx_pe.c
        src/formats/executable/pe/xx_pe_stream_common.c
        src/algo/blake2/xx_blake2.c
        src/algo/rar/xx_rarx.c
        src/algo/rar/xx_rarx15.c
        src/algo/rar/xx_rarx20.c
        src/algo/rar/xx_rarx29.c
        src/algo/rar/xx_rarx50.c
        src/algo/ppmd7/xx_ppmd7_common.c
        src/algo/ppmd7/xx_ppmd7_dec.c)
