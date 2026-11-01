# The instruction sets the C compiler targets, for per-source ISA flags.
#
# CMAKE_SYSTEM_PROCESSOR is the build machine's processor whenever CMake is
# not cross-compiling. On macOS CMAKE_OSX_ARCHITECTURES picks the target
# without changing it: an x86_64 build on an Apple Silicon Mac (or an arm64
# build on an Intel Mac) still reports the host, and a universal build
# (arm64;x86_64) compiles every translation unit once per architecture, so a
# single-architecture flag such as -mavx2 or -march=armv8-a+crc breaks the
# other slice. Per-source ISA flags therefore follow the target list below,
# and a universal build hands each flag to its own architecture only, with
# clang's -Xarch_<arch>.
include_guard(GLOBAL)

# Canonical names: x86_64, x86, arm64 and arm64ec (never arm64). Anything
# else comes back lower-cased.
function(xxfclib_normalize_architecture name out_var)
    string(TOLOWER "${name}" _xxfc_arch)
    if(_xxfc_arch MATCHES "^(x86_64h?|x64|amd64|em64t)$")
        set(_xxfc_arch "x86_64")
    elseif(_xxfc_arch MATCHES "^(i[3-6]86|x86|win32)$")
        set(_xxfc_arch "x86")
    elseif(_xxfc_arch MATCHES "^(arm64e?|aarch64)$")
        set(_xxfc_arch "arm64")
    endif()
    set(${out_var} "${_xxfc_arch}" PARENT_SCOPE)
endfunction()

# xxfclib_target_architectures(<names_var> [<spellings_var>])
#
# <names_var> receives the canonical names of the target architectures and
# <spellings_var> the same list as the toolchain spells it (the -arch value
# -Xarch_ takes on macOS, e.g. arm64e). The first source that is set wins:
#   1. CMAKE_OSX_ARCHITECTURES on Apple platforms: exactly what CMake passes
#      as -arch, read on every configure, so a build tree switched to another
#      architecture never keeps the compiler id cached by its first configure.
#      An Xcode build setting such as $(ARCHS_STANDARD) leaves the choice to
#      Xcode, possibly a universal build, so it counts as arm64 and x86_64:
#      each flag then reaches only its own architecture through -Xarch_;
#   2. CMAKE_C_COMPILER_ARCHITECTURE_ID (MSVC; GNU and Clang since CMake 4.1);
#   3. CMAKE_GENERATOR_PLATFORM (Visual Studio generators);
#   4. the CMAKE_C_COMPILER_TARGET triple;
#   5. CMAKE_SYSTEM_PROCESSOR.
# Variables that may be undefined are only ever tested quoted: an unquoted
# name that is not a variable is compared as text (CMAKE_C_COMPILER_TARGET
# MATCHES "^([^-]+)" matches the name itself on a native build).
function(xxfclib_target_architectures names_var)
    set(_xxfc_raw)
    if(APPLE AND "${CMAKE_OSX_ARCHITECTURES}" MATCHES "\\$")
        set(_xxfc_raw arm64 x86_64)
    elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(_xxfc_raw ${CMAKE_OSX_ARCHITECTURES})
    elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID)
        set(_xxfc_raw ${CMAKE_C_COMPILER_ARCHITECTURE_ID})
    elseif(CMAKE_GENERATOR_PLATFORM)
        set(_xxfc_raw "${CMAKE_GENERATOR_PLATFORM}")
    elseif(NOT "${CMAKE_C_COMPILER_TARGET}" STREQUAL "")
        string(REGEX MATCH "^[^-]+" _xxfc_raw "${CMAKE_C_COMPILER_TARGET}")
    elseif(CMAKE_SYSTEM_PROCESSOR)
        set(_xxfc_raw "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(_xxfc_raw)
        list(REMOVE_DUPLICATES _xxfc_raw)
    endif()

    set(_xxfc_names)
    foreach(_xxfc_spelling IN LISTS _xxfc_raw)
        xxfclib_normalize_architecture("${_xxfc_spelling}" _xxfc_name)
        list(APPEND _xxfc_names "${_xxfc_name}")
    endforeach()
    set(${names_var} "${_xxfc_names}" PARENT_SCOPE)
    if(ARGC GREATER 1)
        set(${ARGV1} "${_xxfc_raw}" PARENT_SCOPE)
    endif()
endfunction()

# xxfclib_target_isa_options(<x86|arm64> <out_var> <option>...)
#
# Sets <out_var> to the compile options that give <option>... to the target
# architectures of one family (x86 covers 32- and 64-bit x86; arm64 never
# includes ARM64EC):
#   - the options unchanged when the build has one target architecture and it
#     is in the family;
#   - "-Xarch_<arch>;<option>" for each option and architecture of the
#     family when the build has several (a universal macOS build);
#   - nothing when no target architecture is in the family.
# The result is meant for the COMPILE_OPTIONS of a source file. CMake neither
# de-duplicates nor SHELL:-expands source-level options (checked with CMake
# 4.2: a "SHELL:..." item reaches the compiler verbatim), so the -Xarch_<arch>
# words stay separate list items, each followed by its option.
function(xxfclib_target_isa_options family out_var)
    if(family STREQUAL "x86")
        set(_xxfc_family x86 x86_64)
    elseif(family STREQUAL "arm64")
        set(_xxfc_family arm64)
    else()
        message(FATAL_ERROR "xxfclib_target_isa_options: unknown family '${family}'")
    endif()

    xxfclib_target_architectures(_xxfc_names _xxfc_spellings)
    list(LENGTH _xxfc_names _xxfc_count)
    set(_xxfc_options)
    set(_xxfc_index 0)
    foreach(_xxfc_name IN LISTS _xxfc_names)
        list(GET _xxfc_spellings ${_xxfc_index} _xxfc_spelling)
        math(EXPR _xxfc_index "${_xxfc_index} + 1")
        if(NOT _xxfc_name IN_LIST _xxfc_family)
            continue()
        endif()
        if(_xxfc_count EQUAL 1)
            list(APPEND _xxfc_options ${ARGN})
        else()
            foreach(_xxfc_option IN LISTS ARGN)
                list(APPEND _xxfc_options "-Xarch_${_xxfc_spelling}" "${_xxfc_option}")
            endforeach()
        endif()
    endforeach()
    set(${out_var} "${_xxfc_options}" PARENT_SCOPE)
endfunction()
