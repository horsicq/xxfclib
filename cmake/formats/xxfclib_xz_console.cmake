# XZ readers and writer; the BCJ filters share xx_7zip_branch.c.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(xz
    FORMATS XZ
    CODECS crc lzma sha
    SOURCES
        src/formats/archive/xz/xx_xz.c
        src/formats/archive/xz/xx_xz_extractor.c
        src/formats/archive/xz/xx_xz_writer.c
        src/formats/archive/7zip/xx_7zip_branch.c)
