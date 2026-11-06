# uuencode readers and the native uuencode writer.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_console.cmake")
xxfclib_add_family_console(uue
    FORMATS UUE
    ENGINE native_filter_formats.c
    ENGINE_DEFINES XFU_NATIVE_FILTER_WRITERS=1
    SOURCES
        src/formats/archive/uue/xx_uue.c
        src/formats/archive/uue/xx_uue_extractor.c)
