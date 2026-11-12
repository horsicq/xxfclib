# 7-Zip readers and writer with the codecs 7-Zip archives use.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(7zip
    FORMATS 7ZIP
    CODECS aes brotli bzip2 crc deflate lizard lz4 lz5 lzma ppmd7 sha zstd
    SOURCES
        src/formats/archive/7zip/xx_7zip.c
        src/formats/archive/7zip/xx_7zip_branch.c
        src/formats/archive/7zip/xx_7zip_extractor.c
        src/formats/archive/7zip/xx_7zip_stream.c
        src/formats/archive/7zip/xx_7zip_writer.c
        src/algo/brotli/xx_brotli.c
        src/algo/brotli/xx_brotli_data.c
        src/algo/lizard/xx_lizard.c
        src/algo/lz4/xx_lz4.c
        src/algo/lz5/xx_lz5.c
        src/algo/ppmd7/xx_ppmd7_common.c
        src/algo/ppmd7/xx_ppmd7_dec.c
        src/algo/ppmd7/xx_ppmd7_enc.c)
