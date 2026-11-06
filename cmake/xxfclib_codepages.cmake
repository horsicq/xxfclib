# The East Asian double-byte tables are about 490 KiB together, so a
# size-sensitive consumer can leave each language out (cdie does). Without
# them xx_codepage_by_number() and xx_codepage_find() return NULL for those
# pages, which the readers that ask for them (CAB and Smart Install Maker
# names) treat as undecodable. The shared DBCS decoder and the Shift-JIS byte
# classifiers stay in every build.
option(XXFC_CODEPAGE_JAPANESE "Include the Japanese Shift-JIS codepage (CP932)" ON)
option(XXFC_CODEPAGE_CHINESE "Include the Chinese GBK (CP936) and Big5 (CP950) codepages" ON)
option(XXFC_CODEPAGE_KOREAN "Include the Korean Unified Hangul codepage (CP949)" ON)
# Every build needs the registry, the shared decoders, the Unicode encodings
# and CP437 (ZIP names without the UTF-8 flag). XXFC_CODEPAGE_ALL adds the
# other single-byte pages and, subject to the three options above, the East
# Asian ones. Without it xx_codepage.c is compiled with
# XXFC_CODEPAGE_CP437_ONLY, so xx_codepage_by_number() and xx_codepage_find()
# know only CP437 and the Unicode encodings. The consoles of
# XCArchiveCollection turn it off. A build that does not go through this file
# keeps every page.
option(XXFC_CODEPAGE_ALL "Include every codepage table; OFF keeps CP437 and the Unicode encodings only" ON)

set(XXFC_CODEPAGE_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_dbcs.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_unicode.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp437.c"
)

set(_xxfc_codepage_definitions "")
if(XXFC_CODEPAGE_ALL)
    list(APPEND XXFC_CODEPAGE_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_petscii_upper.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_petscii_lower.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_riscos.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp037.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_ascii.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_1.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_2.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_3.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_4.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_5.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_6.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_7.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_8.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_9.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_10.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_11.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_13.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_14.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_15.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_iso8859_16.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1250.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1251.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1252.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1253.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1254.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1255.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1256.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1257.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp1258.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp850.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp852.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp855.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp857.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp858.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp860.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp861.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp862.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp863.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp864.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp865.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp866.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp869.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_tis_620.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp874.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_mac_roman.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_hp_roman8.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_koi8_r.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_koi8_u.c"
    )
    if(XXFC_CODEPAGE_JAPANESE)
        list(APPEND XXFC_CODEPAGE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp932.c")
        list(APPEND _xxfc_codepage_definitions XXFC_CODEPAGE_JAPANESE)
    endif()
    if(XXFC_CODEPAGE_CHINESE)
        list(APPEND XXFC_CODEPAGE_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp936.c"
            "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp950.c")
        list(APPEND _xxfc_codepage_definitions XXFC_CODEPAGE_CHINESE)
    endif()
    if(XXFC_CODEPAGE_KOREAN)
        list(APPEND XXFC_CODEPAGE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage_cp949.c")
        list(APPEND _xxfc_codepage_definitions XXFC_CODEPAGE_KOREAN)
    endif()
else()
    list(APPEND _xxfc_codepage_definitions XXFC_CODEPAGE_CP437_ONLY)
endif()

# Only the registry names the optional pages. A source property applies to the
# targets of the directory that included this file, which is where every
# consumer of XXFC_CODEPAGE_SOURCES (xxfclib, die_engine, the emulator
# profiles, xxanalyze) creates its library, so none needs its own definitions.
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../src/codepages/xx_codepage.c"
    PROPERTIES COMPILE_DEFINITIONS "${_xxfc_codepage_definitions}")
unset(_xxfc_codepage_definitions)
