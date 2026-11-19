# Select optional format stream implementations before creating a target.
# Keep the six switches independent: turning one off omits its translation
# units, while the small capability registry stays available to all readers.
include_guard(GLOBAL)

foreach(_xxfc_stream DATASTRUCTS IMPORT EXPORT RESOURCES METADATA SYMBOLS)
    option(XXFC_ENABLE_STREAM_${_xxfc_stream}
        "Compile the ${_xxfc_stream} format stream capability" ON)
endforeach()
unset(_xxfc_stream)

# Every optional native provider has a core reader with the same stem. The
# explicit list also defines availability for bounded emulator/DIE profiles.
# DataStructs has no C provider at all: every file type is served by a script
# in src/datastructs (which needs the JS engine in the profile). A profile
# without the JS engine has no DataStructs.
set(_XXFC_STREAM_NATIVE_READERS
    7zip ar bz2 gz rar tar tar_bz2 tar_gz tar_lz4 tar_xz tar_zstd xz zip
    dotnet elf le lx macho ne omf pe pef vxworks xtac xorarchive)

function(xxfclib_select_stream_sources source_list)
    set(_selected)
    set(_core_sources)
    foreach(_source IN LISTS ${source_list})
        string(REPLACE "\\" "/" _source "${_source}")
        # These former combined files are replaced by independent providers.
        if(_source MATCHES "/xx_pe_stream_(symbols|coff|metadata)\\.c$" OR
           _source MATCHES "^src/formats/executable/pe/xx_pe_stream_(symbols|coff|metadata)\\.c$")
            continue()
        endif()
        if(IS_ABSOLUTE "${_source}")
            set(_absolute "${_source}")
        else()
            get_filename_component(_absolute "${_source}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        get_filename_component(_name "${_source}" NAME)
        # A reader source named *_data.c that only sets up data-struct callbacks
        # belongs to the optional data stream, unlike core readers whose actual
        # format name happens to end in data.
        if(NOT XXFC_ENABLE_STREAM_DATASTRUCTS AND _absolute MATCHES "/src/formats/")
            if(_name MATCHES "_data\\.c$")
                file(STRINGS "${_absolute}" _data_provider_markers
                    REGEX "setup_data_struct_callbacks")
                if(_data_provider_markers)
                    continue()
                endif()
            endif()
        endif()
        # Resource scanning consumes StreamResources, but is a scan component
        # rather than a native format sidecar. Keep this dependency explicit.
        if(_absolute MATCHES "/src/scan/xx_scan_resources\\.c$" AND
           NOT XXFC_ENABLE_STREAM_RESOURCES)
            continue()
        endif()
        set(_optional FALSE)
        if(_absolute MATCHES "/src/formats/")
            foreach(_cap datastructs import export resources metadata symbols)
                if(_name MATCHES "^xx_(.+)_${_cap}\\.c$")
                    set(_reader_name "${CMAKE_MATCH_1}")
                    string(REGEX REPLACE "_${_cap}\\.c$" ".c" _reader "${_absolute}")
                    # Match registered native readers and the known shared
                    # modules only. Suffixes in unrelated subsystems, or core
                    # names such as xx_dotnet_metadata.c, are not capabilities.
                    if((_reader_name IN_LIST _XXFC_STREAM_NATIVE_READERS AND
                        EXISTS "${_reader}") OR
                       _reader_name STREQUAL "stream" OR
                       (_cap STREQUAL "datastructs" AND
                        _reader_name STREQUAL "format"))
                        set(_optional TRUE)
                        string(TOUPPER "${_cap}" _flag)
                        if(XXFC_ENABLE_STREAM_${_flag})
                            list(APPEND _selected "${_absolute}")
                        endif()
                        break()
                    endif()
                endif()
            endforeach()
        endif()
        if(NOT _optional)
            if(_name STREQUAL "xx_pe_stream_common.c" AND
               NOT XXFC_ENABLE_STREAM_SYMBOLS AND
               NOT XXFC_ENABLE_STREAM_RESOURCES AND
               NOT XXFC_ENABLE_STREAM_METADATA)
                continue()
            endif()
            list(APPEND _selected "${_absolute}")
            list(APPEND _core_sources "${_absolute}")
        endif()
    endforeach()
    # Explicit profiles list core readers. Discover only their sibling
    # capabilities, so focused profiles never gain unrelated format readers.
    foreach(_source IN LISTS _core_sources)
        get_filename_component(_stem "${_source}" NAME_WE)
        if(_source MATCHES "/src/formats/" AND _stem MATCHES "^xx_(.+)$")
            set(_reader_name "${CMAKE_MATCH_1}")
            if(_reader_name IN_LIST _XXFC_STREAM_NATIVE_READERS OR
               _reader_name STREQUAL "format")
                foreach(_cap datastructs import export resources metadata symbols)
                    string(TOUPPER "${_cap}" _flag)
                    string(REGEX REPLACE "\\.c$" "_${_cap}.c" _peer "${_source}")
                    if(XXFC_ENABLE_STREAM_${_flag} AND EXISTS "${_peer}")
                        list(APPEND _selected "${_peer}")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()
    set(_root "${_XXFC_STREAM_SOURCE_ROOT}")
    # CreateInstall, HCI Shadow, SFX LHA and RAR extraction use the private bounded PE walker
    # even when the public StreamResources capability is disabled.
    foreach(_source IN LISTS _selected)
        if(_source MATCHES "/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor\\.c$" OR
           _source MATCHES "/sfx_hci_instalit/xx_sfx_hci_instalit\\.c$" OR
           _source MATCHES "/sfx_lha/xx_sfx_lha\\.c$" OR
           _source MATCHES "/rar/xx_rar\\.c$" OR
           _source MATCHES "/sfx_zipcentral/xx_sfx_zipcentral\\.c$")
            list(APPEND _selected "${_root}/src/formats/executable/pe/xx_pe_stream_common.c")
            break()
        endif()
    endforeach()
    foreach(_cap datastructs import export resources metadata symbols)
        string(TOUPPER "${_cap}" _flag)
        if(XXFC_ENABLE_STREAM_${_flag})
            list(APPEND _selected "${_root}/src/formats/xx_stream_${_cap}.c")
        endif()
    endforeach()
    if(XXFC_ENABLE_STREAM_DATASTRUCTS)
        # Data-structure descriptions expose the public format type name.
        # The pure name catalog adds no concrete reader dependencies.
        list(APPEND _selected "${_root}/src/formats/xx_format_names.c")
    endif()
    # DataStructs scripts (src/datastructs/*.xxds) run in the JS engine, so the
    # script host joins exactly the profiles that already carry it.
    if(XXFC_ENABLE_STREAM_DATASTRUCTS)
        foreach(_source IN LISTS _selected)
            if(_source MATCHES "(^|/)src/js/xx_js_engine\\.c$")
                list(APPEND _selected
                    "${_root}/src/datastructs/xx_xxds.c"
                    "${_root}/src/datastructs/xx_xxds_fields.c"
                    "${_root}/src/datastructs/xx_xxds_readers.c")
                break()
            endif()
        endforeach()
    endif()
    list(APPEND _selected "${_root}/src/formats/xx_format_streams.c")
    list(REMOVE_DUPLICATES _selected)
    set(${source_list} "${_selected}" PARENT_SCOPE)
endfunction()

get_filename_component(_XXFC_STREAM_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(xxfclib_configure_streams target)
    foreach(_cap DATASTRUCTS IMPORT EXPORT RESOURCES METADATA SYMBOLS)
        if(XXFC_ENABLE_STREAM_${_cap})
            set(_value 1)
        else()
            set(_value 0)
        endif()
        target_compile_definitions(${target} PUBLIC
            XXFC_ENABLE_STREAM_${_cap}=${_value})
    endforeach()
    get_target_property(_sources ${target} SOURCES)
    foreach(_reader IN LISTS _XXFC_STREAM_NATIVE_READERS)
        set(_value 0)
        foreach(_source IN LISTS _sources)
            if(_source MATCHES "(^|/)xx_${_reader}\\.c$")
                set(_value 1)
                break()
            endif()
        endforeach()
        string(TOUPPER "${_reader}" _upper)
        target_compile_definitions(${target} PRIVATE
            XXFC_STREAM_HAS_${_upper}=${_value})
    endforeach()
    set(_value 0)
    foreach(_source IN LISTS _sources)
        if(_source MATCHES "(^|/)xx_xxds\\.c$")
            set(_value 1)
            break()
        endif()
    endforeach()
    target_compile_definitions(${target} PRIVATE XXFC_STREAM_HAS_XXDS=${_value})
endfunction()

# A project reusing an already-created xxfclib target must not silently run
# without a stream it requires. Older imported libraries without feature
# declarations retain their existing compatibility behavior.
function(xxfclib_require_streams target)
    get_target_property(_definitions ${target} INTERFACE_COMPILE_DEFINITIONS)
    foreach(_cap IN LISTS ARGN)
        list(FIND _definitions "XXFC_ENABLE_STREAM_${_cap}=0" _disabled)
        if(NOT _disabled EQUAL -1)
            message(FATAL_ERROR
                "${CMAKE_CURRENT_SOURCE_DIR} requires xxfclib stream ${_cap}, "
                "but the existing ${target} target disables it. Enable "
                "XXFC_ENABLE_STREAM_${_cap} when configuring that target.")
        endif()
    endforeach()
endfunction()
