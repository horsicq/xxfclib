# One archive family's readers and writers, its unpacker core and the shared
# console, next to the ZIP, TAR and AR components. The family targets never
# link the complete formats library. Every family library is one static
# archive made of:
#   - the shared runtime (the ZIP component's sources without archive formats,
#     codecs other than store/entropy, and the format registries), compiled
#     once as an object library;
#   - the source directories the family names, each compiled once as an
#     object library (cmake/xxfclib_family_sources.cmake lists them);
#   - the format registries and the flag-dependent readers the family names,
#     compiled for this family with its XFU_SUPPORT_FORMAT_* flag.
# The linker takes from the archive only the objects a tool references.
#
#   xxfclib_add_family_console(<name>
#       FORMATS <family>...     XFU_SUPPORT_FORMAT_<family>=1, PUBLIC
#       SOURCES <path>...       root-relative readers, writers and codecs: the
#                               whole directory of each is linked; a directory
#                               outside the table is compiled for this family
#       [CODECS <codec>...]     src/algo/<codec>/ is linked;
#                               include/xxfclib/algo/<codec> and
#                               src/algo/<codec> become include directories
#       [SORT]                  the family reads with src/sort
#       [ENGINE <file>...]      extra src/xfileunpacker sources
#       [ENGINE_DEFINES <d>...] PRIVATE definitions of the unpacker core
#       [INCLUDE_DIRS <dir>...] extra root-relative PRIVATE include directories)
#
# Exports xxfclib::<name>, xxfclib::xfileunpacker_<name>_core and
# xxfclib::archive_console_<name>. The property XXFCLIB_FAMILY_SOURCE_LIST of
# xxfclib::<name> lists every C source behind the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_zip.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_family_sources.cmake")
if(MSVC)
    enable_language(ASM_MASM)
endif()

# Compile settings of every family object: the ZIP component's options and
# definitions without its format flag, and the library's own private include
# directories, so that any format or codec unit compiles as in the library.
function(_xxfclib_family_configure target)
    get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    foreach(_property INCLUDE_DIRECTORIES COMPILE_OPTIONS)
        get_target_property(_value xxfclib_zip "${_property}")
        if(_value)
            set_property(TARGET "${target}" PROPERTY "${_property}" "${_value}")
        endif()
    endforeach()
    set(_includes)
    foreach(_dir IN LISTS XXFCLIB_FAMILY_INCLUDE_DIRECTORIES)
        list(APPEND _includes "${_root}/${_dir}")
    endforeach()
    target_include_directories("${target}" PRIVATE ${_includes})
    get_target_property(_definitions xxfclib_zip COMPILE_DEFINITIONS)
    list(FILTER _definitions EXCLUDE REGEX "^XFU_SUPPORT_FORMAT_")
    target_compile_definitions("${target}" PRIVATE ${_definitions})
    target_compile_features("${target}" PUBLIC c_std_11)
    set_target_properties("${target}" PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_definitions("${target}" PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()

# The ZIP component configures the ISA flags of its own dispatched sources;
# sort and adler32 are outside it.
function(_xxfclib_family_isa_sources sources)
    set(_avx2_sources ${sources})
    list(FILTER _avx2_sources INCLUDE REGEX "/src/(sort|algo/adler32)/platforms/xx_[a-z0-9_]*_avx2\\.c$")
    if(NOT _avx2_sources)
        return()
    endif()
    if(MSVC)
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
            set_property(SOURCE ${_avx2_sources} APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        xxfclib_target_isa_options(x86 _isa -mavx2)
        set_property(SOURCE ${_avx2_sources} APPEND PROPERTY COMPILE_OPTIONS ${_isa})
    endif()
endfunction()

# The shared runtime, compiled once: xxfclib_family_runtime.
function(_xxfclib_family_runtime objects sources)
    if(NOT TARGET xxfclib_family_runtime)
        get_target_property(_base xxfclib_zip SOURCES)
        set(_selected)
        foreach(_source IN LISTS _base)
            if(_source MATCHES "/src/formats/archive/")
                continue()
            endif()
            if(_source MATCHES "/src/algo/" AND NOT _source MATCHES "/src/algo/(store|entropy)/")
                continue()
            endif()
            set(_flagged FALSE)
            foreach(_flag IN LISTS XXFCLIB_FAMILY_FLAG_SOURCES)
                if(_source MATCHES "/${_flag}$")
                    set(_flagged TRUE)
                    break()
                endif()
            endforeach()
            if(NOT _flagged)
                list(APPEND _selected "${_source}")
            endif()
        endforeach()
        add_library(xxfclib_family_runtime OBJECT EXCLUDE_FROM_ALL ${_selected})
        _xxfclib_family_configure(xxfclib_family_runtime)
        set_property(TARGET xxfclib_family_runtime PROPERTY XXFCLIB_FAMILY_SOURCE_LIST "${_selected}")
    endif()
    get_target_property(_selected xxfclib_family_runtime XXFCLIB_FAMILY_SOURCE_LIST)
    set(${objects} "$<TARGET_OBJECTS:xxfclib_family_runtime>" PARENT_SCOPE)
    set(${sources} "${_selected}" PARENT_SCOPE)
endfunction()

# The object library of one source directory of the table, created on first
# use. <objects> receives $<TARGET_OBJECTS:...>, empty when the directory is
# not in the table; <sources> its absolute sources. Platform halves follow the
# target system; MSVC x64 adds the LZMA and bzip2 assembly as the ZIP
# component does.
function(_xxfclib_family_directory_objects directory objects sources)
    get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" _mangled "${directory}")
    set(_files ${XXFCLIB_FAMILY_DIRECTORY_${_mangled}})
    set(${objects} "" PARENT_SCOPE)
    set(${sources} "" PARENT_SCOPE)
    if(NOT _files)
        return()
    endif()
    set(_selected)
    foreach(_file IN LISTS _files)
        if(_file MATCHES "_posix\\.c$")
            if(WIN32)
                string(REGEX REPLACE "_posix\\.c$" "_windows.c" _file "${_file}")
                if(NOT EXISTS "${_root}/${_file}")
                    continue()
                endif()
            endif()
        elseif(_file MATCHES "_windows\\.c$" AND NOT WIN32)
            continue()
        endif()
        list(APPEND _selected "${_root}/${_file}")
    endforeach()
    list(REMOVE_DUPLICATES _selected)
    set(_target "xxfclib_family_objects_${_mangled}")
    if(NOT TARGET "${_target}")
        add_library("${_target}" OBJECT EXCLUDE_FROM_ALL ${_selected})
        _xxfclib_family_configure("${_target}")
        _xxfclib_family_isa_sources("${_selected}")
        if(MSVC)
            if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|AMD64)$")
                if(directory STREQUAL "src/algo/lzma")
                    target_sources("${_target}" PRIVATE "${_root}/src/algo/lzma/asm/lzma_windows_x64.asm")
                elseif(directory STREQUAL "src/algo/bzip2")
                    target_sources("${_target}" PRIVATE "${_root}/src/algo/bzip2/asm/bzip2_windows_x64.asm")
                endif()
            elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86)$")
                if(directory STREQUAL "src/algo/bzip2")
                    target_sources("${_target}" PRIVATE "${_root}/src/algo/bzip2/asm/bzip2_windows_x86.asm")
                endif()
            endif()
        endif()
        set_property(TARGET "${_target}" PROPERTY XXFCLIB_FAMILY_SOURCE_LIST "${_selected}")
    endif()
    get_target_property(_selected "${_target}" XXFCLIB_FAMILY_SOURCE_LIST)
    set(${objects} "$<TARGET_OBJECTS:${_target}>" PARENT_SCOPE)
    set(${sources} "${_selected}" PARENT_SCOPE)
endfunction()

function(xxfclib_add_family_console name)
    if(TARGET "xxfclib_${name}")
        return()
    endif()
    cmake_parse_arguments(_family "SORT" ""
        "FORMATS;SOURCES;CODECS;ENGINE;ENGINE_DEFINES;INCLUDE_DIRS" ${ARGN})
    if(_family_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "xxfclib_add_family_console(${name}): unknown arguments "
            "${_family_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT _family_FORMATS OR NOT _family_SOURCES)
        message(FATAL_ERROR "xxfclib_add_family_console(${name}): FORMATS and SOURCES are required")
    endif()
    get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(_library "xxfclib_${name}")
    set(_core "xxfclib_xfileunpacker_${name}_core")
    set(_console "xxfclib_archive_console_${name}")

    # Compiled for this family: the format registries, and the flag-dependent
    # readers among the named sources.
    set(_compiled)
    foreach(_flag IN LISTS XXFCLIB_FAMILY_FLAG_SOURCES)
        if(NOT _flag MATCHES "^src/formats/archive/")
            list(APPEND _compiled "${_root}/${_flag}")
        endif()
    endforeach()
    set(_directories)
    foreach(_source IN LISTS _family_SOURCES)
        if(NOT EXISTS "${_root}/${_source}")
            message(FATAL_ERROR "xxfclib_add_family_console(${name}): no source ${_source}")
        endif()
        if(_source IN_LIST XXFCLIB_FAMILY_FLAG_SOURCES)
            list(APPEND _compiled "${_root}/${_source}")
        else()
            get_filename_component(_directory "${_source}" DIRECTORY)
            list(APPEND _directories "${_directory}")
        endif()
    endforeach()
    foreach(_codec IN LISTS _family_CODECS)
        list(APPEND _directories "src/algo/${_codec}" "src/algo/${_codec}/platforms")
    endforeach()
    if(_family_SORT)
        list(APPEND _directories "src/sort" "src/sort/platforms")
    endif()
    list(REMOVE_DUPLICATES _directories)

    # Linked once compiled: the runtime and the named directories.
    _xxfclib_family_runtime(_objects _listed)
    foreach(_directory IN LISTS _directories)
        _xxfclib_family_directory_objects("${_directory}" _directory_objects _directory_sources)
        if(_directory_objects)
            list(APPEND _objects "${_directory_objects}")
            list(APPEND _listed ${_directory_sources})
        elseif(_directory MATCHES "^src/(sort|algo)")
            file(GLOB _units "${_root}/${_directory}/*.c")
            if(_units)
                message(FATAL_ERROR "xxfclib_add_family_console(${name}): ${_directory} is not "
                    "in cmake/xxfclib_family_sources.cmake; regenerate it")
            endif()
        else()
            # Outside the table (the ISO 9660 filesystem reader, for example):
            # the named sources of that directory are compiled for this family.
            foreach(_source IN LISTS _family_SOURCES)
                get_filename_component(_source_directory "${_source}" DIRECTORY)
                if(_source_directory STREQUAL _directory)
                    list(APPEND _compiled "${_root}/${_source}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _compiled)

    add_library("${_library}" STATIC EXCLUDE_FROM_ALL ${_compiled} ${_objects})
    add_library("xxfclib::${name}" ALIAS "${_library}")
    _xxfclib_family_configure("${_library}")
    _xxfclib_family_isa_sources("${_compiled}")
    foreach(_property INTERFACE_INCLUDE_DIRECTORIES INTERFACE_LINK_OPTIONS)
        get_target_property(_value xxfclib_zip "${_property}")
        if(_value)
            set_property(TARGET "${_library}" PROPERTY "${_property}" "${_value}")
        endif()
    endforeach()
    get_target_property(_definitions xxfclib_zip INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER _definitions EXCLUDE REGEX "^XFU_SUPPORT_FORMAT_")
    set(_flags)
    foreach(_format IN LISTS _family_FORMATS)
        list(APPEND _flags "XFU_SUPPORT_FORMAT_${_format}=1")
    endforeach()
    target_compile_definitions("${_library}" PUBLIC ${_definitions} ${_flags})
    get_target_property(_libraries xxfclib_zip LINK_LIBRARIES)
    target_link_libraries("${_library}" PUBLIC ${_libraries})
    foreach(_codec IN LISTS _family_CODECS)
        target_include_directories("${_library}" PRIVATE
            "${_root}/include/xxfclib/algo/${_codec}" "${_root}/src/algo/${_codec}")
    endforeach()
    foreach(_dir IN LISTS _family_INCLUDE_DIRS)
        target_include_directories("${_library}" PRIVATE "${_root}/${_dir}")
    endforeach()
    set_property(TARGET "${_library}" PROPERTY XXFCLIB_FAMILY_SOURCE_LIST "${_listed};${_compiled}")
    xxfclib_configure_arm_accel_sources("${_root}")

    set(_engine "${_root}/src/xfileunpacker")
    set(_engine_sources "${_engine}/core.c" "${_engine}/metadata.c"
        "${_engine}/xxfc_readers.c" "${_engine}/native_writer_backend.c")
    foreach(_file IN LISTS _family_ENGINE)
        list(APPEND _engine_sources "${_engine}/${_file}")
    endforeach()
    add_library("${_core}" STATIC ${_engine_sources})
    add_library("xxfclib::xfileunpacker_${name}_core" ALIAS "${_core}")
    target_link_libraries("${_core}" PUBLIC "xxfclib::${name}")
    target_include_directories("${_core}" PUBLIC "${_engine}/include" PRIVATE "${_engine}")
    target_compile_definitions("${_core}" PRIVATE
        XFU_XXFCLIB_ONLY=1 XXFC_NO_EXTERNAL_HELPERS=1 ${_family_ENGINE_DEFINES})

    add_library("${_console}" STATIC "${_root}/src/console/xx_archive_console.c")
    add_library("xxfclib::archive_console_${name}" ALIAS "${_console}")
    target_link_libraries("${_console}" PUBLIC "xxfclib::xfileunpacker_${name}_core")
    if(WIN32)
        target_link_libraries("${_console}" PRIVATE shell32)
    endif()
    foreach(_target "${_core}" "${_console}")
        target_compile_features("${_target}" PUBLIC c_std_11)
        set_target_properties("${_target}" PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES
            C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
        if(MSVC)
            target_compile_options("${_target}" PRIVATE /W4 /utf-8 /Gy /Gw)
            target_compile_definitions("${_target}" PRIVATE _CRT_SECURE_NO_WARNINGS)
        else()
            target_compile_options("${_target}" PRIVATE -Wall -Wextra -Wpedantic
                -ffunction-sections -fdata-sections)
        endif()
        if(WIN32)
            target_compile_definitions("${_target}" PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        else()
            target_compile_definitions("${_target}" PRIVATE _POSIX_C_SOURCE=200809L)
            if(APPLE)
                target_compile_definitions("${_target}" PRIVATE _DARWIN_C_SOURCE)
            endif()
        endif()
    endforeach()
    list(LENGTH _compiled _compiled_count)
    list(LENGTH _listed _shared_count)
    message(STATUS "xxfclib ${name} component: ${_compiled_count} own and ${_shared_count} shared C sources; ${_family_FORMATS} only")
endfunction()
