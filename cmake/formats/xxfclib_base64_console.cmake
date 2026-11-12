# Base64 readers and the native Base64 writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../xxfclib_family_console.cmake")
xxfclib_add_family_console(base64
    FORMATS BASE64
    CODECS crc
    ENGINE native_filter_formats.c
    ENGINE_DEFINES XFU_NATIVE_FILTER_WRITERS=1
    SOURCES
        src/formats/other/base64/xx_base64.c
        src/formats/other/base64/xx_base64_extractor.c)
