# A reader that became a family of its own keeps its old generated
# xx_<name>_reader_adapters.c beside the new xx_<name>_extractor.c; both
# define the same abstract extractor symbols (LNK2005 on the first link that
# loads both). Keep only the adapters the generator still lists.
# Usage: xxfc_keep_listed_reader_adapters(<source list variable>)
include("${CMAKE_CURRENT_LIST_DIR}/xx_reader_adapters_sources.cmake")
macro(xxfc_keep_listed_reader_adapters _xxfc_sources)
    list(FILTER ${_xxfc_sources} EXCLUDE REGEX "_reader_adapters\.c$")
    foreach(_xxfc_adapter IN LISTS XXFC_READER_ADAPTER_SOURCES)
        get_filename_component(_xxfc_adapter "${_xxfc_adapter}" ABSOLUTE)
        list(APPEND ${_xxfc_sources} "${_xxfc_adapter}")
    endforeach()
    unset(_xxfc_adapter)
endmacro()
