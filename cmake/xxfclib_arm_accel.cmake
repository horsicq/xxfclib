# Keep optional ARM instruction-set flags local to runtime-dispatched backends.
# The rest of the library remains compatible with the baseline ARM64 ISA.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_target_arch.cmake")

function(xxfclib_configure_arm_accel_sources source_dir)
    get_filename_component(_xxfc_arm_root "${source_dir}" ABSOLUTE)
    get_property(_xxfc_arm_configured DIRECTORY PROPERTY XXFC_ARM_ACCEL_ROOTS)
    if(_xxfc_arm_root IN_LIST _xxfc_arm_configured)
        return()
    endif()
    set_property(DIRECTORY APPEND PROPERTY XXFC_ARM_ACCEL_ROOTS "${_xxfc_arm_root}")

    # The target, never the host: a native ARM64 Windows host can generate x64
    # Visual Studio projects, and on macOS CMAKE_SYSTEM_PROCESSOR names the
    # build machine even when CMAKE_OSX_ARCHITECTURES selects x86_64. A
    # universal macOS build also compiles these sources for x86_64 (empty
    # there), so its flags go to the arm64 compilation only. ARM64EC is not
    # ARM64 (see xxfclib_target_arch.cmake).
    xxfclib_target_architectures(_xxfc_arm_archs)
    if(NOT "arm64" IN_LIST _xxfc_arm_archs)
        return()
    endif()

    set(_xxfc_arm_crc
        "${_xxfc_arm_root}/src/algo/crc/platforms/xx_crc32_arm.c")
    set(_xxfc_arm_pmull "${_xxfc_arm_root}/src/algo/crc/platforms/xx_crc32_arm_pmull.c")
    set(_xxfc_arm_dotprod
        "${_xxfc_arm_root}/src/algo/adler32/platforms/xx_adler32_arm_dotprod.c")
    set(_xxfc_arm_crypto
        "${_xxfc_arm_root}/src/algo/aes/platforms/xx_aes_arm_crypto.c"
        "${_xxfc_arm_root}/src/algo/sha/platforms/xx_sha_arm_crypto.c")

    if(MSVC AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        # These levels were compile-tested with MSVC 19.44 for ARM64. Each
        # source is reached only after its matching runtime feature check.
        set_property(SOURCE ${_xxfc_arm_crc}
            APPEND PROPERTY COMPILE_OPTIONS /arch:armv8.1)
        set_property(SOURCE ${_xxfc_arm_dotprod} ${_xxfc_arm_crypto} ${_xxfc_arm_pmull}
            APPEND PROPERTY COMPILE_OPTIONS /arch:armv8.2)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set_property(SOURCE ${_xxfc_arm_pmull}
                APPEND PROPERTY COMPILE_OPTIONS /clang:-march=armv8-a+crc+crypto)
            set_property(SOURCE ${_xxfc_arm_crc}
                APPEND PROPERTY COMPILE_OPTIONS /clang:-march=armv8-a+crc)
            set_property(SOURCE ${_xxfc_arm_dotprod}
                APPEND PROPERTY COMPILE_OPTIONS /clang:-march=armv8.2-a+dotprod)
            set_property(SOURCE ${_xxfc_arm_crypto}
                APPEND PROPERTY COMPILE_OPTIONS /clang:-march=armv8-a+crypto)
        else()
            xxfclib_target_isa_options(arm64 _xxfc_arm_option -march=armv8-a+crc+crypto)
            set_property(SOURCE ${_xxfc_arm_pmull}
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_arm_option})
            xxfclib_target_isa_options(arm64 _xxfc_arm_option -march=armv8-a+crc)
            set_property(SOURCE ${_xxfc_arm_crc}
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_arm_option})
            xxfclib_target_isa_options(arm64 _xxfc_arm_option -march=armv8.2-a+dotprod)
            set_property(SOURCE ${_xxfc_arm_dotprod}
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_arm_option})
            xxfclib_target_isa_options(arm64 _xxfc_arm_option -march=armv8-a+crypto)
            set_property(SOURCE ${_xxfc_arm_crypto}
                APPEND PROPERTY COMPILE_OPTIONS ${_xxfc_arm_option})
        endif()
    endif()
endfunction()
