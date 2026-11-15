# Common hosted, static C11 infrastructure for emulator format profiles. This file is selected
# before the full source catalog, registry and DIE engine are evaluated.
# Keep this explicit manifest in sync with the selected objects' link closure.
include(GNUInstallDirs)

if(BUILD_SHARED_LIBS OR XXFC_STATIC_NO_CRT OR XXFC_BUILD_BOTH)
    message(FATAL_ERROR "The ${XXFC_BUILD_PROFILE} profile supports one hosted static library; use full for CRT-free or dual builds")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_codepages.cmake")
set(XXFC_EMUL_CORE_SOURCES
    ${XXFC_CODEPAGE_SOURCES}
    src/rt/xx_rt.c
    src/sort/xx_sort.c
    src/sort/platforms/xx_sort_sse2.c
    src/sort/platforms/xx_sort_avx2.c
    src/sort/platforms/xx_sort_neon.c
    src/rt/xx_rt_fp.c
    src/rt/xx_rt_math.c
    src/buf/xx_buf.c
    src/json/xx_json.c
    src/global/xx_global.c
    src/global/platforms/xx_global_cpu.c
    src/io/xx_io.c
    src/translation/xx_translation.c
    src/translation/xx_translation_io.c
    src/history/xx_history.c
    src/io/xx_io_buffer.c
    src/io/xx_io_file.c
    src/io/xx_io_mem.c
    src/io/xx_io_memory_only.c
    src/io/xx_io_sub.c
    src/memory/xx_memory.c
    src/memory/xx_memory_rt.c
    src/memory/platforms/xx_memory_sse2.c
    src/memory/platforms/xx_memory_avx2.c
    src/memory/platforms/xx_memory_neon.c
    src/strings/xx_string.c
    src/strings/xx_string_casefold.c
    src/algo/md5/xx_md5.c
    src/algo/tlsh/xx_tlsh.c
    src/list/xx_list.c
    src/sort/xx_sort_list.c
    src/var/xx_var.c
    src/data/xx_pd.c
    src/data/xx_data_raw.c
    src/data/xx_data_io.c
    src/data/platforms/xx_data_sse2.c
    src/data/platforms/xx_data_avx2.c
    src/data/platforms/xx_data_neon.c
    src/algo/crc/xx_crc.c
    src/algo/crc/xx_crc8.c
    src/algo/crc/xx_crc16.c
    src/algo/crc/xx_crc32.c
    src/algo/crc/platforms/xx_crc32_arm_pmull.c
    src/algo/crc/platforms/xx_crc32_arm.c
    src/algo/crc/platforms/xx_crc32c_sse42.c
    src/algo/crc/xx_crc64.c
    src/algo/crc/platforms/xx_crc64_sse2.c
    src/algo/crc/platforms/xx_crc64_avx2.c
    src/algo/crc/platforms/xx_crc64_vpclmul.c
    src/formats/xx_format_base.c
    src/formats/xx_format_datastructs.c
    src/formats/xx_format_handle_types.c
    src/formats/xx_format_streams.c
    src/formats/xx_memory_map.c
)

if(WIN32)
    list(APPEND XXFC_EMUL_CORE_SOURCES
        src/io/platforms/xx_io_windows.c
        src/memory/platforms/xx_memory_windows.c
        src/strings/platforms/xx_string_windows.c
        src/rt/platforms/xx_rt_windows.c
        src/sort/platforms/xx_sort_windows.c
        src/global/platforms/xx_global_windows.c)
else()
    list(APPEND XXFC_EMUL_CORE_SOURCES
        src/io/platforms/xx_io_posix.c
        src/memory/platforms/xx_memory_posix.c
        src/strings/platforms/xx_string_posix.c
        src/rt/platforms/xx_rt_posix.c
        src/sort/platforms/xx_sort_posix.c
        src/global/platforms/xx_global_posix.c)
endif()

# The native CPU core also references its executable image loaders. Keep their
# bounded reader closure available to focused emulator consumers without the
# full format registry, inspectors, extractors or unrelated codecs.
set(XXFC_EMUL_NATIVE_FORMAT_SOURCES
    src/formats/executable/com/xx_com.c
    src/formats/executable/msdos/xx_msdos.c
    src/formats/executable/pe/xx_pe.c src/formats/executable/pe/xx_pe_datastructs.c
    src/formats/executable/pe/xx_pe_stream_common.c src/formats/executable/pe/xx_pe_export.c
    src/formats/executable/pe/xx_pe_metadata.c src/formats/executable/pe/xx_pe_import.c
    src/formats/executable/pe/xx_pe_yx.c    src/formats/executable/pe/xx_pe_yx_authenticode.c
    src/formats/executable/elf/xx_elf.c src/formats/executable/elf/xx_elf_symbols.c src/formats/executable/elf/xx_elf_resources.c src/formats/executable/elf/xx_elf_datastructs.c
    src/formats/executable/elf/xx_elf_yx.c
    src/formats/executable/macho/xx_macho.c src/formats/executable/macho/xx_macho_datastructs.c)
set(XXFC_EMUL_SOURCES ${XXFC_EMUL_CORE_SOURCES}
    ${XXFC_EMUL_FORMAT_SOURCES} ${XXFC_EMUL_NATIVE_FORMAT_SOURCES})
list(REMOVE_DUPLICATES XXFC_EMUL_SOURCES)
xxfclib_select_stream_sources(XXFC_EMUL_SOURCES)
add_library(xxfclib STATIC ${XXFC_EMUL_SOURCES})
add_library(xxfclib::xxfclib ALIAS xxfclib)
xxfclib_configure_streams(xxfclib)
target_compile_features(xxfclib PUBLIC c_std_11)
target_compile_definitions(xxfclib PUBLIC XXFC_STATIC PRIVATE ${XXFC_EMUL_DEFINITIONS})
target_include_directories(xxfclib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(xxfclib PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(xxfclib PRIVATE /W4)
    target_compile_definitions(xxfclib PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(xxfclib PUBLIC kernel32)
else()
    target_compile_options(xxfclib PRIVATE -Wall -Wextra -Wpedantic)
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(xxfclib PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
    endif()
endif()

list(LENGTH XXFC_EMUL_SOURCES _xxfc_apk_source_count)
message(STATUS "xxfclib ${XXFC_BUILD_PROFILE} profile: ${_xxfc_apk_source_count} C sources")

install(TARGETS xxfclib EXPORT xxfclibTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT xxfclibTargets FILE xxfclibTargets.cmake NAMESPACE xxfclib::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xxfclib)
include(CMakePackageConfigHelpers)
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/xxfclib_apk_emul_config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/xxfclibConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xxfclib)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/xxfclibConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/xxfclibConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xxfclibConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xxfclib)

# SIMD flags apply only to the runtime-dispatched sorting translation units.
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_sort_simd.cmake")
xxfclib_configure_sort_simd_sources("${CMAKE_CURRENT_SOURCE_DIR}")

# SIMD flags apply only to the runtime-dispatched .NET #US backends.
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_dotnet_simd.cmake")
xxfclib_configure_dotnet_simd_sources("${CMAKE_CURRENT_SOURCE_DIR}")

# ARM feature flags apply only to runtime-dispatched algorithm backends.
include("${CMAKE_CURRENT_LIST_DIR}/xxfclib_arm_accel.cmake")
xxfclib_configure_arm_accel_sources("${CMAKE_CURRENT_SOURCE_DIR}")
