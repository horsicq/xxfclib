# Keep instruction-set flags local to the dispatched sorting backends.
# Call from the directory that owns the consumer's target, including focused
# consumers that compile a source subset instead of adding xxfclib itself.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_target_arch.cmake")

function(xxfclib_configure_sort_simd_sources source_dir)
    get_filename_component(_xxfc_sort_root "${source_dir}" ABSOLUTE)
    get_property(_xxfc_sort_configured DIRECTORY PROPERTY XXFC_SORT_SIMD_ROOTS)
    if(_xxfc_sort_root IN_LIST _xxfc_sort_configured)
        return()
    endif()
    set_property(DIRECTORY APPEND PROPERTY XXFC_SORT_SIMD_ROOTS "${_xxfc_sort_root}")

    set_source_files_properties(
        "${_xxfc_sort_root}/src/sort/xx_sort_internal.h"
        "${_xxfc_sort_root}/src/sort/xx_sort_algorithms.inc"
        PROPERTIES HEADER_FILE_ONLY TRUE)

    if(MSVC)
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|X86|x86)$")
            set_property(SOURCE "${_xxfc_sort_root}/src/sort/platforms/xx_sort_avx2.c"
                APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # SSE2 is not an architectural baseline for 32-bit GNU/Clang targets.
        # The target decides, not the host: an x86_64 build on an Apple
        # Silicon Mac needs the flags, an arm64 build must not get them, and
        # a universal build gives them to its x86_64 compilation only.
        xxfclib_target_isa_options(x86 _xxfc_sort_sse2 -msse2)
        xxfclib_target_isa_options(x86 _xxfc_sort_avx2 -mavx2)
        if(_xxfc_sort_sse2)
            set_property(SOURCE "${_xxfc_sort_root}/src/sort/platforms/xx_sort_sse2.c"
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_sort_sse2})
            set_property(SOURCE "${_xxfc_sort_root}/src/sort/platforms/xx_sort_avx2.c"
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_sort_avx2})
        endif()
    endif()
endfunction()
