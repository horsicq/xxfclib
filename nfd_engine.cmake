# Copyright (c) 2017-2026 hors<horsicq@gmail.com>
# SPDX-License-Identifier: MIT
#
# Focused C closure for the SpecAbstract-derived NFD engine. Ordinary builds
# consume the committed generated tables and never require Python or Qt.

# The common scanner/readers/runtime closure is currently shared as a source
# manifest with the proven DiE focused archive. Remove every concrete DiE and
# script-VM object, and remove the generic schema catalog. Static archive
# extraction guarantees that a consumer linking both concrete engines gets
# only one definition of each common symbol.
if(NOT DEFINED XXFC_DIE_ENGINE_SOURCES)
    include("${CMAKE_CURRENT_LIST_DIR}/die_engine.cmake")
endif()

set(XXFC_NFD_ENGINE_SOURCES ${XXFC_DIE_ENGINE_SOURCES})
list(FILTER XXFC_NFD_ENGINE_SOURCES EXCLUDE REGEX "/src/die_engine/")
list(FILTER XXFC_NFD_ENGINE_SOURCES EXCLUDE REGEX "/src/js/")
list(FILTER XXFC_NFD_ENGINE_SOURCES EXCLUDE REGEX "/src/regex/xx_regex_(die|js)")
list(FILTER XXFC_NFD_ENGINE_SOURCES EXCLUDE REGEX "/src/sort/xx_sort_scan\\.c$")
list(FILTER XXFC_NFD_ENGINE_SOURCES EXCLUDE REGEX "/src/formats/xx_format_datastructs\\.c$")
list(APPEND XXFC_NFD_ENGINE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/nfd_engine/xx_nfd_engine_engine.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nfd_engine/xx_nfd_engine_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nfd_engine/xx_nfd_engine_names.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nfd_engine/xx_nfd_engine_binary.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nfd_engine/xx_nfd_engine_text.c)
list(REMOVE_DUPLICATES XXFC_NFD_ENGINE_SOURCES)
