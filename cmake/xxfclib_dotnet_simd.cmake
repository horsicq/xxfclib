# Keep instruction-set flags local to the runtime-dispatched #US backends.
# Call from the directory that owns the consumer target, including focused
# consumers that compile a source subset instead of adding xxfclib itself.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_target_arch.cmake")

function(xxfclib_configure_dotnet_simd_sources source_dir)
    get_filename_component(_xxfc_dotnet_root "${source_dir}" ABSOLUTE)
    get_property(_xxfc_dotnet_configured DIRECTORY PROPERTY XXFC_DOTNET_SIMD_ROOTS)
    if(_xxfc_dotnet_root IN_LIST _xxfc_dotnet_configured)
        return()
    endif()
    set_property(DIRECTORY APPEND PROPERTY XXFC_DOTNET_SIMD_ROOTS "${_xxfc_dotnet_root}")

    set_source_files_properties(
        "${_xxfc_dotnet_root}/src/formats/executable/dotnet/platforms/xx_dotnet_us_simd.h"
        PROPERTIES HEADER_FILE_ONLY TRUE)

    if(MSVC)
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86)$")
            set_property(SOURCE "${_xxfc_dotnet_root}/src/formats/executable/dotnet/platforms/xx_dotnet_us_sse2.c"
                APPEND PROPERTY COMPILE_OPTIONS /arch:SSE2)
        endif()
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64|amd64|X86|x86)$")
            set_property(SOURCE "${_xxfc_dotnet_root}/src/formats/executable/dotnet/platforms/xx_dotnet_us_avx2.c"
                APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # The target decides, not the host (see xxfclib_target_arch.cmake):
        # a universal macOS build gives the flags to its x86_64 compilation
        # only.
        xxfclib_target_isa_options(x86 _xxfc_dotnet_sse2 -msse2)
        xxfclib_target_isa_options(x86 _xxfc_dotnet_avx2 -mavx2)
        if(_xxfc_dotnet_sse2)
            set_property(SOURCE "${_xxfc_dotnet_root}/src/formats/executable/dotnet/platforms/xx_dotnet_us_sse2.c"
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_dotnet_sse2})
            set_property(SOURCE "${_xxfc_dotnet_root}/src/formats/executable/dotnet/platforms/xx_dotnet_us_avx2.c"
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_dotnet_avx2})
        endif()
    endif()
endfunction()
