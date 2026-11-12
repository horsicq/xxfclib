# CPIO readers and writer, including compressed CPIO.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(cpio
    FORMATS CPIO
    CODECS bzip2 crc deflate lzma sha
    SORT
    SOURCES
        src/formats/archive/cpio/xx_cpio.c
        src/formats/archive/cpio/xx_cpio_extractor.c
        src/formats/archive/gz/xx_gz.c
        src/formats/archive/gz/xx_gz_extractor.c
        src/formats/archive/xz/xx_xz.c
        src/formats/archive/xz/xx_xz_extractor.c
        src/formats/archive/7zip/xx_7zip_branch.c)
