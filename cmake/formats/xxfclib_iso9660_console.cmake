# ISO 9660 readers (with zisofs) and the native ISO 9660 writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(iso9660
    FORMATS ISO9660
    CODECS adler32 crc deflate sha
    ENGINE native_extra_formats.c
    ENGINE_DEFINES XFU_NATIVE_EXTRA_WRITERS=1
    SOURCES
        src/formats/filesystem/iso9660/xx_iso9660.c
        src/formats/filesystem/iso9660/xx_iso9660_extractor.c
        src/formats/filesystem/iso9660/xx_iso_zisofs_native.c
        src/algo/deflate/xx_zlib_stream.c
        src/algo/adler32/xx_adler32.c
        src/algo/adler32/platforms/xx_adler32_arm_dotprod.c
        src/algo/adler32/platforms/xx_adler32_avx2.c
        src/algo/adler32/platforms/xx_adler32_neon.c
        src/algo/adler32/platforms/xx_adler32_sse2.c)
