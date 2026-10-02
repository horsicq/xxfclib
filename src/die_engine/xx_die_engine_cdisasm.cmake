# Build the x86 decoder beside xxfclib in both the local _mylibs layout and
# the published dep/ submodule layout. The console must remain self-contained.
if(NOT TARGET cdisasm::cdisasm)
    set(XXFC_CDISASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cdisasm"
        CACHE PATH "Location of the cdisasm source tree")
    if(NOT EXISTS "${XXFC_CDISASM_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "xxfclib needs cdisasm for DIE x86 decoding. Looked in "
            "'${XXFC_CDISASM_DIR}'. Set -DXXFC_CDISASM_DIR=<path>.")
    endif()

    # Scope cdisasm's options to its own subdirectory. Its development tests,
    # ARM decoder and examples are not part of xxfclib's embedded decoder.
    function(_xxfc_add_cdisasm source_dir)
        set(BUILD_TESTING OFF)
        set(CDISASM_BUILD_SHARED_LIBS OFF)
        set(CDISASM_BUILD_EXAMPLES OFF)
        set(CDISASM_BUILD_FUZZERS OFF)
        set(CDISASM_VERIFY_GENERATED OFF)
        set(USE_ARCH_X86 ON)
        set(USE_ARCH_ARM OFF)
        set(USE_DISASM_FORMAT ON)
        set(USE_EXTRA_OPCODES ON)
        add_subdirectory("${source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/cdisasm")
    endfunction()
    _xxfc_add_cdisasm("${XXFC_CDISASM_DIR}")
endif()

get_target_property(_xxfc_cdisasm_type cdisasm::cdisasm TYPE)
if(NOT _xxfc_cdisasm_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "DIE x86 decoding requires a static cdisasm target")
endif()
unset(_xxfc_cdisasm_type)

get_target_property(_xxfc_cdisasm_target cdisasm::cdisasm ALIASED_TARGET)
if(_xxfc_cdisasm_target)
    get_target_property(_xxfc_cdisasm_imported ${_xxfc_cdisasm_target} IMPORTED)
    if(NOT _xxfc_cdisasm_imported)
        # The static archive is also linked into xxfclib's shared library.
        set_target_properties(${_xxfc_cdisasm_target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON)

        if(MSVC AND XXFC_MSVC_X64)
            # Match xxfclib's CRT-free static/shared configurations. The
            # consumer supplies compiler support routines and the entry point.
            target_compile_options(${_xxfc_cdisasm_target} PRIVATE /GS- /GL- /Zl)
            set_property(TARGET ${_xxfc_cdisasm_target} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded")
            if(POLICY CMP0184)
                set_property(TARGET ${_xxfc_cdisasm_target} PROPERTY
                    MSVC_RUNTIME_CHECKS "")
            else()
                target_compile_options(${_xxfc_cdisasm_target} PRIVATE /RTC-)
            endif()
        endif()
    endif()
    unset(_xxfc_cdisasm_imported)
endif()
unset(_xxfc_cdisasm_target)
