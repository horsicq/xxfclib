cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0184)
    cmake_policy(SET CMP0184 NEW)
endif()

project(xxfclib
    VERSION 0.1.0
    DESCRIPTION "C library template for handling data"
    LANGUAGES C
)

# The private include path lists every format directory. With Ninja, pass
# compiler options in response files so that command lines stay under the
# 32 KiB Windows limit however deep the checkout is. MSBuild already does.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_streams.cmake")

option(XXFC_BUILD_TRANSLATION_TESTS "Build standalone PO translation regression tests" OFF)
if(XXFC_BUILD_TRANSLATION_TESTS)
    enable_testing()
    add_subdirectory(tests/translation)
endif()

set(XXFC_BUILD_PROFILE "full" CACHE STRING "xxfclib source profile: full, formats, apk_emul, dotnet_emul or bytecode")
set_property(CACHE XXFC_BUILD_PROFILE PROPERTY STRINGS full formats apk_emul dotnet_emul bytecode)
if(XXFC_BUILD_PROFILE STREQUAL "formats")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_formats.cmake")
    return()
elseif(XXFC_BUILD_PROFILE STREQUAL "apk_emul")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_apk_emul.cmake")
    return()
elseif(XXFC_BUILD_PROFILE STREQUAL "dotnet_emul")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_dotnet_emul.cmake")
    return()
elseif(XXFC_BUILD_PROFILE STREQUAL "bytecode")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_bytecode.cmake")
    return()
elseif(NOT XXFC_BUILD_PROFILE STREQUAL "full")
    message(FATAL_ERROR "Unknown XXFC_BUILD_PROFILE '${XXFC_BUILD_PROFILE}'; choose full, formats, apk_emul, dotnet_emul or bytecode")
endif()

# C Standard Configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

# Library build options
option(BUILD_SHARED_LIBS "Build xxfclib as a shared/dynamic library instead of static" OFF)
option(XXFC_BUILD_BOTH "Build both static and shared library targets" OFF)
option(XXFC_BUILD_TESTS "Build test suite to verify linkage and functionality" OFF)
option(XXFC_BUILD_STRINGS_TESTS "Build focused streaming strings tests and billion-record benchmark" OFF)
option(XXFC_BUILD_EXTRACTOR_TESTS "Build bounded format-carving and extraction tests" OFF)
option(XXFC_BUILD_THREAD_TESTS "Build the focused threading and thread pool tests" OFF)
option(XXFC_BUILD_DISASM_TESTS "Build the focused DIE disassembly regression test" OFF)
option(XXFC_BUILD_SCAN_TESTS "Build the focused scan and subdevice regression tests" OFF)
option(XXFC_BUILD_ZIP_SUPPORT_TESTS "Build the focused ZIP method regression tests" OFF)
option(XXFC_BUILD_PDF_TESTS "Build the focused native PDF reader and extractor regression tests" OFF)
option(XXFC_BUILD_PYC_TESTS "Build the focused native Python bytecode reader and extractor regression tests" OFF)
option(XXFC_BUILD_FORMAT_EXPANSION_TESTS "Build additional reader registration and extraction tests" OFF)
option(XXFC_BUILD_RADARE2_PRIVATE_TESTS "Build private Radare2-derived parser and format tests" OFF)
option(XXFC_BUILD_SAMPLES "Build xxfclib sample programs" OFF)

# Opt-in freestanding STATIC archive (MSVC x64 and ARM64).
#
# The default static archive is hosted: its objects carry /DEFAULTLIB:MSVCRT
# and reference __chkstk and __security_check_cookie, so a consumer that links
# with /NODEFAULTLIB gets unresolved externals. With this ON the static objects
# are compiled /GS- /GL- /Zl. The x64 archive carries its clean-room stack
# probe. On ARM64, link the separate xxfclib::no_crt_runtime helper target as
# well; keeping the atomic fallbacks outside the archive lets the same archive
# remain usable by hosted targets in a mixed build.
#
# XXFC_NO_CRT is deliberately NOT defined here: that macro makes
# src/platforms/xx_entry_windows.c define memset/memcpy/memmove/_fltused, and
# a CRT-free EXE consumer (cdie) already defines those in its own entry object.
# Defining both is LNK2005. The consumer owns the compiler-support routines;
# this option only removes the archive's CRT dependency.
option(XXFC_STATIC_NO_CRT "Build the static archive so it can be linked with /NODEFAULTLIB (MSVC x64 or ARM64)" OFF)
option(XXFC_SHARED_NO_CRT "Build the MSVC x64 shared library without the C runtime" ON)
option(XXFC_BUILD_DIE_ENGINE "Also build die_engine, a static library holding only the DIE signature engine" ON)
option(XXFC_DIE_ENGINE_TAR_ONLY_WRITING "Keep only TAR archive creation in the focused DIE engine" OFF)

set(XXFC_MSVC_X64 FALSE)
set(XXFC_MSVC_ARM64 FALSE)
set(XXFC_MSVC_STATIC_NO_CRT_ARCH FALSE)
if(MSVC)
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        set(XXFC_MSVC_X64 TRUE)
        set(XXFC_MSVC_STATIC_NO_CRT_ARCH TRUE)
    elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
        set(XXFC_MSVC_ARM64 TRUE)
        set(XXFC_MSVC_STATIC_NO_CRT_ARCH TRUE)
    endif()
endif()

if(XXFC_STATIC_NO_CRT AND MSVC AND NOT XXFC_MSVC_STATIC_NO_CRT_ARCH)
    message(FATAL_ERROR
        "XXFC_STATIC_NO_CRT is supported only for MSVC x64 and ARM64 targets")
endif()

# cdisasm is one static target shared by every xxfclib consumer. Compile it
# freestanding only when at least one target that consumes it is CRT-free.
set(XXFC_HAS_STATIC_TARGET FALSE)
if(NOT BUILD_SHARED_LIBS OR XXFC_BUILD_BOTH OR XXFC_BUILD_DIE_ENGINE)
    set(XXFC_HAS_STATIC_TARGET TRUE)
endif()

set(XXFC_MSVC_CRT_FREE_CDISASM FALSE)
if((XXFC_MSVC_X64 AND XXFC_SHARED_NO_CRT AND
    (BUILD_SHARED_LIBS OR XXFC_BUILD_BOTH)) OR
   (XXFC_MSVC_STATIC_NO_CRT_ARCH AND XXFC_STATIC_NO_CRT AND
    XXFC_HAS_STATIC_TARGET))
    set(XXFC_MSVC_CRT_FREE_CDISASM TRUE)
endif()

# Older CMake versions and previously configured build trees can carry /RTC1
# in the configuration flags, which introduces a CRT dependency. Strip those
# flags whenever cdisasm must be freestanding; with CMP0184 NEW, the per-target
# property below prevents them from being added again.
if(XXFC_MSVC_CRT_FREE_CDISASM)
    foreach(_xxfc_flags CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "[/-](RTC[1csu]|GZ)" ""
            ${_xxfc_flags} "${${_xxfc_flags}}")
    endforeach()
endif()

# CRT-free Windows targets supply compiler helpers in assembly. Keep the
# assemblers out of configurations where the normal compiler runtime owns
# those symbols.
if(MSVC AND (XXFC_MSVC_X64 OR CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4))
    enable_language(ASM_MASM)
elseif(XXFC_MSVC_ARM64 AND XXFC_STATIC_NO_CRT AND XXFC_HAS_STATIC_TARGET)
    if(CMAKE_VERSION VERSION_LESS 3.26)
        message(FATAL_ERROR
            "XXFC_STATIC_NO_CRT on MSVC ARM64 needs CMake 3.26 or newer "
            "for the ASM_MARMASM language")
    endif()
    enable_language(ASM_MARMASM)
endif()

# ARM64's CRT groups the whole interlocked family into one object. Keeping our
# fallback subset inside die_engine would therefore collide when that archive
# is also used by a hosted target. CRT-free consumers link this helper target
# explicitly; hosted targets leave the compiler helpers to their runtime.
if(XXFC_MSVC_ARM64 AND XXFC_STATIC_NO_CRT AND XXFC_HAS_STATIC_TARGET)
    add_library(xxfclib_no_crt_runtime STATIC
        src/platforms/xx_chkstk_arm64.asm)
    add_library(xxfclib::no_crt_runtime ALIAS xxfclib_no_crt_runtime)
endif()

# Source files
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_codepages.cmake")
option(XXFC_BUILD_CODEPAGE_TESTS "Build portable codepage callback tests" OFF)
if(XXFC_BUILD_CODEPAGE_TESTS)
    enable_testing()
    add_executable(xxfc_codepage_tests tests/test_codepages.c ${XXFC_CODEPAGE_SOURCES})
    target_include_directories(xxfc_codepage_tests PRIVATE include)
    target_compile_definitions(xxfc_codepage_tests PRIVATE XXFC_STATIC)
    add_test(NAME xxfc_codepages COMMAND xxfc_codepage_tests)
endif()
set(XXFCLIB_PUBLIC_HEADERS
    include/xxfclib/translation/xx_translation.h
    include/xxfclib/history/xx_history.h
    include/xxfclib/process/xx_process_structs.h
    include/xxfclib/codepages/xx_codepage.h
    include/xxfclib/formats/executable/pyc/xx_pyc.h
    include/xxfclib/formats/document/pdf/xxpdf.h
    include/xxfclib/formats/installer/mozilla_mar/xx_mozilla_mar.h
    include/xxfclib/formats/game/westwood_pak/xx_westwood_pak.h
    include/xxfclib/formats/filesystem/fatx/xx_fatx.h
    include/xxfclib/formats/audio/soundfont2/xx_soundfont2.h
    include/xxfclib/formats/video/ivf/xx_ivf.h
    include/xxfclib/formats/image/windows_ani/xx_windows_ani.h
    include/xxfclib/formats/audio/interplay_acm/xx_interplay_acm.h
    include/xxfclib/formats/audio/cri_ahx/xx_cri_ahx.h
    include/xxfclib/formats/video/adobe_director_cxt/xx_adobe_director_cxt.h
    include/xxfclib/formats/audio/olympus_dss/xx_olympus_dss.h
    include/xxfclib/formats/audio/ea_exa/xx_ea_exa.h
    include/xxfclib/formats/game/ckp/xx_ckp.h
    include/xxfclib/formats/game/edp/xx_edp.h
    include/xxfclib/formats/game/parsec_rib/xx_parsec_rib.h
    include/xxfclib/formats/game/parsec_archive/xx_parsec_archive.h
    include/xxfclib/formats/audio/parsec_pmm/xx_parsec_pmm.h
    include/xxfclib/formats/game/ptero_bigf/xx_ptero_bigf.h
    include/xxfclib/formats/game/rvz/xx_rvz.h
    include/xxfclib/formats/audio/audio_nitro_strm/xx_audio_nitro_strm.h
    include/xxfclib/formats/audio/audio_wwise_wem/xx_audio_wwise_wem.h
    include/xxfclib/formats/game/audio_scumm_sou/xx_audio_scumm_sou.h
    include/xxfclib/formats/audio/audio_riff_ima/xx_audio_riff_ima.h
    include/xxfclib/formats/audio/hmi_midi/xx_hmi_midi.h
    include/xxfclib/formats/audio/ensoniq_paf/xx_ensoniq_paf.h
    include/xxfclib/formats/audio/abylight_strm/xx_abylight_strm.h
    include/xxfclib/formats/audio/lego_alp/xx_lego_alp.h
    include/xxfclib/formats/audio/audio_pvf/xx_audio_pvf.h
    include/xxfclib/formats/audio/audio_rifx_wave/xx_audio_rifx_wave.h
    include/xxfclib/formats/audio/audio_shockwave_swa/xx_audio_shockwave_swa.h
    include/xxfclib/formats/audio/die_music/xx_die_music.h
    include/xxfclib/formats/audio/die_music/xx_die_music_abstract_extractor_decls.inc
    include/xxfclib/formats/audio/die_music/xx_die_music_enums.inc
    include/xxfclib/formats/filesystem/acorn_adfs/xx_acorn_adfs.h
    include/xxfclib/formats/filesystem/aaruformat/xx_aaruformat.h
    include/xxfclib/formats/game/gdi/xx_gdi.h
    include/xxfclib/formats/filesystem/mds/xx_mds.h
    include/xxfclib/formats/filesystem/cbm_d64/xx_cbm_d64.h
    include/xxfclib/formats/filesystem/cbm_d71/xx_cbm_d71.h
    include/xxfclib/formats/filesystem/cbm_d81/xx_cbm_d81.h
    include/xxfclib/formats/filesystem/cbm_d8x/xx_cbm_d8x.h
    include/xxfclib/formats/filesystem/cbm_d67/xx_cbm_d67.h
    include/xxfclib/formats/filesystem/cbm_d90/xx_cbm_d90.h
    include/xxfclib/formats/filesystem/libdsk_extra/xx_libdsk_extra.h
    include/xxfclib/formats/filesystem/simh_disk/xx_simh_disk.h
    include/xxfclib/formats/filesystem/snatchit_cp2/xx_snatchit_cp2.h
    include/xxfclib/formats/filesystem/northstar_nsi/xx_northstar_nsi.h
    include/xxfclib/formats/filesystem/thomson_fd/xx_thomson_fd.h
    include/xxfclib/formats/filesystem/cmd_fd/xx_cmd_fd.h
    include/xxfclib/formats/filesystem/pce_pri/xx_pce_pri.h
    include/xxfclib/formats/filesystem/pce_pfi/xx_pce_pfi.h
    include/xxfclib/formats/filesystem/pce_pfdc/xx_pce_pfdc.h
    include/xxfclib/formats/filesystem/pce_pbi/xx_pce_pbi.h
    include/xxfclib/formats/filesystem/pce_pbit/xx_pce_pbit.h
    include/xxfclib/formats/filesystem/pce_tc/xx_pce_tc.h
    include/xxfclib/formats/filesystem/pce_anadisk/xx_pce_anadisk.h
    include/xxfclib/formats/filesystem/pce_xdf/xx_pce_xdf.h
    include/xxfclib/formats/filesystem/os9_rbf/xx_os9_rbf.h
    include/xxfclib/formats/filesystem/ldbs/xx_ldbs.h
    include/xxfclib/formats/filesystem/ldbst/xx_ldbst.h
    include/xxfclib/formats/filesystem/ccd/xx_ccd.h
    include/xxfclib/formats/filesystem/cdrdao_toc/xx_cdrdao_toc.h
    include/xxfclib/formats/filesystem/diskcopy42/xx_diskcopy42.h
    include/xxfclib/formats/filesystem/acorn_dfs/xx_acorn_dfs.h
    include/xxfclib/formats/filesystem/fdcopy_cfi/xx_fdcopy_cfi.h
    include/xxfclib/formats/filesystem/atari_dos2/xx_atari_dos2.h
    include/xxfclib/formats/filesystem/apridisk/xx_apridisk.h
    include/xxfclib/formats/filesystem/ti99_dsk/xx_ti99_dsk.h
    include/xxfclib/formats/filesystem/apple_dos32/xx_apple_dos32.h
    include/xxfclib/formats/filesystem/apple_dos33_32/xx_apple_dos33_32.h
    include/xxfclib/formats/archive/bytekiller/xx_bytekiller.h
    include/xxfclib/formats/filesystem/partimage/xx_partimage.h
    include/xxfclib/formats/game/qlie_pack/xx_qlie_pack.h
    include/xxfclib/formats/filesystem/hfsplus/xx_hfsplus.h
    include/xxfclib/formats/filesystem/xva/xx_xva.h
    include/xxfclib/formats/filesystem/hfs/xx_hfs.h
    include/xxfclib/formats/game/nexas_pac/xx_nexas_pac.h
    include/xxfclib/formats/game/nitroplus_npa/xx_nitroplus_npa.h
    include/xxfclib/formats/filesystem/cpm/xx_cpm.h
    include/xxfclib/formats/filesystem/cpm/xx_cpm_presets.h
    include/xxfclib/formats/filesystem/ufs1/xx_ufs1.h
    include/xxfclib/formats/filesystem/apple_dos33/xx_apple_dos33.h
    include/xxfclib/formats/game/malie_lib/xx_malie_lib.h
    include/xxfclib/formats/filesystem/dmk/xx_dmk.h
    include/xxfclib/formats/filesystem/mfs/xx_mfs.h
    include/xxfclib/formats/game/catsystem_kif/xx_catsystem_kif.h
    include/xxfclib/formats/filesystem/apple_pascal/xx_apple_pascal.h
    include/xxfclib/formats/game/nitroplus_npk2/xx_nitroplus_npk2.h
    include/xxfclib/formats/filesystem/thomson_sap/xx_thomson_sap.h
    include/xxfclib/formats/filesystem/anex86_hdi/xx_anex86_hdi.h
    include/xxfclib/formats/filesystem/exfat/xx_exfat.h
    include/xxfclib/formats/game/majiro/xx_majiro.h
    include/xxfclib/formats/game/wux/xx_wux.h
    include/xxfclib/formats/filesystem/sdi/xx_sdi.h
    include/xxfclib/formats/filesystem/nhd/xx_nhd.h
    include/xxfclib/formats/filesystem/virtual98/xx_virtual98.h
    include/xxfclib/formats/archive/lpak/xx_lpak.h
    include/xxfclib/formats/archive/freeze/xx_freeze.h
    include/xxfclib/formats/archive/bzip1/xx_bzip1.h
    include/xxfclib/formats/filesystem/acorn_atom_disk/xx_acorn_atom_disk.h
    include/xxfclib/formats/filesystem/apollo_afd/xx_apollo_afd.h
    include/xxfclib/formats/archive/bga/xx_bga.h
    include/xxfclib/formats/game/bgi/xx_bgi.h
    include/xxfclib/formats/game/bgi2/xx_bgi2.h
    include/xxfclib/formats/archive/binscii/xx_binscii.h
    include/xxfclib/formats/filesystem/blindwrite_5_6_image/xx_blindwrite_5_6_image.h
    include/xxfclib/formats/filesystem/btrfs_stream/xx_btrfs_stream.h
    include/xxfclib/formats/filesystem/camputers_lynx_ldf/xx_camputers_lynx_ldf.h
    include/xxfclib/formats/game/cpk/xx_cpk.h
    include/xxfclib/formats/game/crt/xx_crt.h
    include/xxfclib/formats/firmware/d_link_alpha_encimg_v2/xx_d_link_alpha_encimg_v2.h
    include/xxfclib/formats/firmware/d_link_fpkg_cpkg/xx_d_link_fpkg_cpkg.h
    include/xxfclib/formats/filesystem/daemon_tools_mdx/xx_daemon_tools_mdx.h
    include/xxfclib/formats/filesystem/dart/xx_dart.h
    include/xxfclib/formats/filesystem/ddd/xx_ddd.h
    include/xxfclib/formats/archive/diet_compression/xx_diet_compression.h
    include/xxfclib/formats/game/dxa/xx_dxa.h
    include/xxfclib/formats/image/ea_fsh/xx_ea_fsh.h
    include/xxfclib/formats/filesystem/ewf2_ex01/xx_ewf2_ex01.h
    include/xxfclib/formats/filesystem/ewf2_lx01/xx_ewf2_lx01.h
    include/xxfclib/formats/filesystem/ewf_l01/xx_ewf_l01.h
    include/xxfclib/formats/audio/fmod_sample_bank/xx_fmod_sample_bank.h
    include/xxfclib/formats/filesystem/gbi/xx_gbi.h
    include/xxfclib/formats/game/goldsrc_bsp/xx_goldsrc_bsp.h
    include/xxfclib/formats/filesystem/hsf/xx_hsf.h
    include/xxfclib/formats/firmware/htc_nbh_rom_image/xx_htc_nbh_rom_image.h
    include/xxfclib/formats/filesystem/hxc_hfe_extended/xx_hxc_hfe_extended.h
    include/xxfclib/formats/filesystem/hxc_hfe_hddd_a2_variant/xx_hxc_hfe_hddd_a2_variant.h
    include/xxfclib/formats/filesystem/hxc_hfe_v3/xx_hxc_hfe_v3.h
    include/xxfclib/formats/document/hxs/xx_hxs.h
    include/xxfclib/formats/filesystem/jffs2_old/xx_jffs2_old.h
    include/xxfclib/formats/filesystem/jvc/xx_jvc.h
    include/xxfclib/formats/archive/kgb_archiver/xx_kgb_archiver.h
    include/xxfclib/formats/filesystem/kryoflux_stream/xx_kryoflux_stream.h
    include/xxfclib/formats/game/livemaker/xx_livemaker.h
    include/xxfclib/formats/archive/lzma86/xx_lzma86.h
    include/xxfclib/formats/game/maxis_far_archive/xx_maxis_far_archive.h
    include/xxfclib/formats/filesystem/mgt/xx_mgt.h
    include/xxfclib/formats/filesystem/minix/xx_minix.h
    include/xxfclib/formats/filesystem/moof/xx_moof.h
    include/xxfclib/formats/archive/ms_dos_backup2/xx_ms_dos_backup2.h
    include/xxfclib/formats/executable/mub/xx_mub.h
    include/xxfclib/formats/game/noa/xx_noa.h
    include/xxfclib/formats/document/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.h
    include/xxfclib/formats/filesystem/partclone_image/xx_partclone_image.h
    include/xxfclib/formats/archive/ppmd/xx_ppmd.h
    include/xxfclib/formats/archive/quoted_printable_encoded_fil/xx_quoted_printable_encoded_fil.h
    include/xxfclib/formats/image/risc_os_sprite/xx_risc_os_sprite.h
    include/xxfclib/formats/game/rpa/xx_rpa.h
    include/xxfclib/formats/game/rpg_maker_rgssad/xx_rpg_maker_rgssad.h
    include/xxfclib/formats/audio/sfark_compressed_soundfont/xx_sfark_compressed_soundfont.h
    include/xxfclib/formats/installer/sis/xx_sis.h
    include/xxfclib/formats/filesystem/spectrum_udi/xx_spectrum_udi.h
    include/xxfclib/formats/filesystem/squashfs_sqlz/xx_squashfs_sqlz.h
    include/xxfclib/formats/game/stos_memory_bank/xx_stos_memory_bank.h
    include/xxfclib/formats/archive/stuffitx/xx_stuffitx.h
    include/xxfclib/formats/filesystem/sufs/xx_sufs.h
    include/xxfclib/formats/filesystem/sunvtoc/xx_sunvtoc.h
    include/xxfclib/formats/game/telltale_ttarch/xx_telltale_ttarch.h
    include/xxfclib/formats/filesystem/ufs2/xx_ufs2.h
    include/xxfclib/formats/filesystem/uif/xx_uif.h
    include/xxfclib/formats/game/valve_gcf_cache/xx_valve_gcf_cache.h
    include/xxfclib/formats/game/valve_xzp/xx_valve_xzp.h
    include/xxfclib/formats/filesystem/vmdk_cowd_sparse/xx_vmdk_cowd_sparse.h
    include/xxfclib/formats/filesystem/vmdk_sesparse/xx_vmdk_sesparse.h
    include/xxfclib/formats/firmware/xiaomi_hdr1/xx_xiaomi_hdr1.h
    include/xxfclib/formats/firmware/xiaomi_hdr2/xx_xiaomi_hdr2.h
    include/xxfclib/formats/game/xp3/xx_xp3.h
    include/xxfclib/formats/archive/xpk_compressed_file/xx_xpk_compressed_file.h
    include/xxfclib/formats/archive/yenc_encoded_file/xx_yenc_encoded_file.h
    include/xxfclib/formats/game/ypf/xx_ypf.h

    include/xxfclib/xxfclib.h
    include/xxfclib/xxfc_defs.h
    include/xxfclib/data/xx_pd.h
    include/xxfclib/io/xx_io.h
    include/xxfclib/scan/xx_scan.h
    include/xxfclib/sig/xx_sig.h
    include/xxfclib/die_engine/die_engine.h
    include/xxfclib/die_engine/die_engine_debug.h
    include/xxfclib/regex/xx_regex.h
    include/xxfclib/js/xx_js.h
    include/xxfclib/js/xx_js_debug.h
    include/xxfclib/memory/xx_memory.h
    include/xxfclib/formats/xx_memory_map.h
    include/xxfclib/strings/xx_string.h
    include/xxfclib/strings/xx_strings_scan.h
    include/xxfclib/extractor/xx_extractor.h
    include/xxfclib/io/xx_io_snapshot.h
    include/xxfclib/data/xx_byte_transform.h
    include/xxfclib/data/xx_data.h
    include/xxfclib/formats/xx_data_signature.h
    include/xxfclib/list/xx_list.h
    include/xxfclib/threads/xx_threads.h
    include/xxfclib/threads/xx_thread_pool.h
    include/xxfclib/sort/xx_sort.h
    include/xxfclib/var/xx_var.h
    include/xxfclib/global/xx_global.h
    include/xxfclib/global/xx_settings_global.h
    include/xxfclib/terminal/xx_terminal.h
    include/xxfclib/settings/xx_settings.h
    include/xxfclib/formats/xx_executable_inspect.h
    include/xxfclib/formats/xx_format_streams.h
    include/xxfclib/formats/xx_format.h
    include/xxfclib/formats/xx_format_reader_only_abstract_extractor_decls.inc
    include/xxfclib/formats/xx_format_extractor.h
    include/xxfclib/algo/crc/xx_crc.h
    include/xxfclib/algo/md5/xx_md5.h
    include/xxfclib/algo/sha/xx_sha.h
    include/xxfclib/algo/adler32/xx_adler32.h
    include/xxfclib/algo/lzfse/xx_lzfse.h
    include/xxfclib/algo/blake2/xx_blake2.h
    include/xxfclib/algo/entropy/xx_entropy.h
    include/xxfclib/algo/kpa/xx_kpa.h
    include/xxfclib/algo/store/xx_store.h
    include/xxfclib/algo/deflate/xx_deflate.h
    include/xxfclib/algo/bzip2/xx_bzip2.h
    include/xxfclib/algo/lzma/xx_lzma.h
    include/xxfclib/algo/ppmd7/xx_ppmd7.h
    include/xxfclib/algo/ppmd8/xx_ppmd8.h
    include/xxfclib/algo/shrink/xx_shrink.h
    include/xxfclib/algo/reduce/xx_reduce.h
    include/xxfclib/algo/implode/xx_implode.h
    include/xxfclib/algo/lz4/xx_lz4.h
    include/xxfclib/algo/lz5/xx_lz5.h
    include/xxfclib/algo/lizard/xx_lizard.h
    include/xxfclib/algo/brotli/xx_brotli.h
    include/xxfclib/algo/zstd/xx_zstd.h
    include/xxfclib/algo/zipcrypto/xx_zipcrypto.h
    include/xxfclib/algo/aes/xx_aes.h
    include/xxfclib/algo/eid/xx_eid_crypto.h
    include/xxfclib/algo/ripemd256/xx_ripemd256.h
    include/xxfclib/algo/aes_winzip/xx_aes_winzip.h
    include/xxfclib/algo/cmpsc/xx_cmpsc.h
    include/xxfclib/algo/packmp3/xx_packmp3.h
    include/xxfclib/algo/wavpack/xx_wavpack.h
    include/xxfclib/algo/winzipjpeg/xx_winzipjpeg.h
    include/xxfclib/algo/ain/xx_ain.h
    include/xxfclib/algo/ampk/xx_ampk.h
    include/xxfclib/algo/arcfs/xx_arcfs_lzw.h
    include/xxfclib/algo/arcv2/xx_arcv2_lzhuf.h
    include/xxfclib/algo/arcv4/xx_arcv4.h
    include/xxfclib/algo/aldus/xx_aldus_lzw.h
    include/xxfclib/algo/aldus/xx_aldus_lzsh.h
    include/xxfclib/algo/dcl/xx_dcl.h
    include/xxfclib/algo/claylz/xx_claylz.h
    include/xxfclib/algo/corelltec/xx_corelltec.h
    include/xxfclib/algo/genius/xx_genius.h
    include/xxfclib/algo/hap/xx_hap.h
    include/xxfclib/algo/imp/xx_imp.h
    include/xxfclib/algo/irwinpac/xx_irwinpac.h
    include/xxfclib/algo/ivt/xx_ivt.h
    include/xxfclib/algo/kolibrikpack/xx_kolibrikpack.h
    include/xxfclib/algo/lim/xx_lim.h
    include/xxfclib/algo/npack/xx_npack.h
    include/xxfclib/algo/pakleo/xx_pakleo.h
    include/xxfclib/algo/pkt/xx_pkt.h
    include/xxfclib/algo/qda/xx_qda.h
    include/xxfclib/algo/rid/xx_rid.h
    include/xxfclib/algo/rompaq/xx_rompaq.h
    include/xxfclib/algo/tivoli/xx_tivoli.h
    include/xxfclib/algo/ulead/xx_ulead.h
    include/xxfclib/algo/xeditpack/xx_xeditpack.h
    include/xxfclib/algo/zcmp/xx_zcmp.h
    include/xxfclib/algo/zie/xx_zie.h
    include/xxfclib/algo/asymetrix/xx_asymetrix.h
    include/xxfclib/algo/c64wraptor/xx_c64wraptor.h
    include/xxfclib/algo/charc/xx_charc.h
    include/xxfclib/algo/coktellz/xx_coktellz.h
    include/xxfclib/algo/fpak/xx_fpak.h
    include/xxfclib/algo/gamos/xx_gamos.h
    include/xxfclib/algo/gtu/xx_gtu.h
    include/xxfclib/algo/lofi/xx_lofi.h
    include/xxfclib/algo/mi10/xx_mi10.h
    include/xxfclib/algo/stylus/xx_stylus.h
    include/xxfclib/algo/chieflz/xx_chieflz.h
    include/xxfclib/algo/cmp/xx_cmp.h
    include/xxfclib/algo/compactpro/xx_compactpro.h
    include/xxfclib/algo/diskdoubler/xx_diskdoubler.h
    include/xxfclib/algo/ea/xx_ea.h
    include/xxfclib/algo/flslz/xx_flslz.h
    include/xxfclib/algo/ha/xx_ha.h
    include/xxfclib/algo/hzl/xx_hzl.h
    include/xxfclib/algo/lzhuf/xx_lzhuf.h
    include/xxfclib/algo/lzw15v/xx_lzw15v.h
    include/xxfclib/algo/lzwvariants/xx_lzwvariants.h
    include/xxfclib/algo/netwarepack/xx_netwarepack.h
    include/xxfclib/algo/pcommos2/xx_pcommos2.h
    include/xxfclib/algo/qnxbase/xx_qnxbase.h
    include/xxfclib/algo/rtpatch/xx_rtpatch.h
    include/xxfclib/algo/sclsectors/xx_sclsectors.h
    include/xxfclib/algo/sqx/xx_sqx.h
    include/xxfclib/algo/sqz/xx_sqz.h
    include/xxfclib/algo/terse/xx_terse.h
    include/xxfclib/algo/ti99arc/xx_ti99arc.h
    include/xxfclib/algo/vmarc/xx_vmarc.h
    include/xxfclib/algo/wintersoft/xx_wintersoft.h
    include/xxfclib/algo/wpk/xx_wpk.h
    include/xxfclib/algo/zoo/xx_zoo.h
    include/xxfclib/algo/rar/xx_rarx.h
    include/xxfclib/algo/arj/xx_arj_dec.h
    include/xxfclib/algo/unixpack/xx_unixpack.h
    include/xxfclib/algo/compress/xx_compress.h
    include/xxfclib/algo/mscompress/xx_mscompress.h
    include/xxfclib/algo/ash0/xx_ash0.h
    include/xxfclib/algo/wiilz77/xx_wiilz77.h
    include/xxfclib/algo/lzv1/xx_lzv1.h
    include/xxfclib/algo/oraclesqueeze/xx_oraclesqueeze.h
    include/xxfclib/algo/softronics/xx_softronics.h
    include/xxfclib/algo/gashuff/xx_gashuff.h
    include/xxfclib/algo/huf/xx_huf.h
    include/xxfclib/algo/lzdiet/xx_lzdiet.h
    include/xxfclib/algo/lzpis2/xx_lzpis2.h
    include/xxfclib/algo/lzip/xx_lzip.h
    include/xxfclib/algo/lzma_alone/xx_lzma_alone.h
    include/xxfclib/algo/lzo/xx_lzo.h
    include/xxfclib/algo/lzop/xx_lzop.h
    include/xxfclib/algo/tarx/xx_tarx.h
    include/xxfclib/algo/tarx/xx_tarx2.h
    include/xxfclib/algo/lzh/xx_lzh.h
    include/xxfclib/algo/lzx/xx_lzx.h
    include/xxfclib/algo/quantum/xx_quantum.h
    include/xxfclib/formats/archive/zip/xx_zip.h
    include/xxfclib/formats/executable/jar/xx_jar.h
    include/xxfclib/formats/installer/apk/xx_apk.h
    include/xxfclib/formats/installer/ipa/xx_ipa.h
    include/xxfclib/formats/archive/7zip/xx_7zip.h
    include/xxfclib/formats/archive/rar/xx_rar.h
    include/xxfclib/formats/archive/ar/xx_ar.h
    include/xxfclib/formats/archive/bz2/xx_bz2.h
    include/xxfclib/formats/archive/gz/xx_gz.h
    include/xxfclib/formats/archive/xz/xx_xz.h
    include/xxfclib/formats/archive/tar/xx_tar.h
    include/xxfclib/formats/archive/tar_gz/xx_tar_gz.h
    include/xxfclib/formats/installer/npm/xx_npm.h
    include/xxfclib/formats/archive/tar_bz2/xx_tar_bz2.h
    include/xxfclib/formats/archive/tar_xz/xx_tar_xz.h
    include/xxfclib/formats/archive/tar_lz4/xx_tar_lz4.h
    include/xxfclib/formats/archive/lz4/xx_lz4.h
    include/xxfclib/formats/archive/lz5/xx_lz5.h
    include/xxfclib/formats/archive/lizard/xx_lizard.h
    include/xxfclib/formats/archive/brotli/xx_brotli.h
    include/xxfclib/formats/archive/unixpack/xx_unixpack.h
    include/xxfclib/formats/archive/zlib/xx_zlib.h
    include/xxfclib/formats/archive/unixcompress/xx_unixcompress.h
    include/xxfclib/formats/other/gitobject/xx_gitobject.h
    include/xxfclib/formats/archive/mscompress/xx_mscompress.h
    include/xxfclib/formats/archive/ash0/xx_ash0.h
    include/xxfclib/formats/archive/wiilz77/xx_wiilz77.h
    include/xxfclib/formats/archive/lzv1/xx_lzv1.h
    include/xxfclib/formats/archive/oraclesqueeze/xx_oraclesqueeze.h
    include/xxfclib/formats/archive/softronics/xx_softronics.h
    include/xxfclib/formats/archive/logitechcompress/xx_logitechcompress.h
    include/xxfclib/formats/archive/dmapacked/xx_dmapacked.h
    include/xxfclib/formats/archive/gashuff/xx_gashuff.h
    include/xxfclib/formats/archive/huf/xx_huf.h
    include/xxfclib/formats/archive/lzdiet/xx_lzdiet.h
    include/xxfclib/formats/archive/lzpis2/xx_lzpis2.h
    include/xxfclib/formats/archive/tar_zstd/xx_tar_zstd.h
    include/xxfclib/formats/archive/zstd/xx_zstd.h
    include/xxfclib/formats/archive/cpio/xx_cpio.h
    include/xxfclib/formats/other/mtree/xx_mtree.h
    include/xxfclib/formats/archive/tar_nextstep/xx_tar_nextstep.h
    include/xxfclib/formats/archive/tar_compress/xx_tar_compress.h
    include/xxfclib/formats/archive/tar_lzip/xx_tar_lzip.h
    include/xxfclib/formats/archive/lzip/xx_lzip.h
    include/xxfclib/formats/archive/lzma/xx_lzma.h
    include/xxfclib/formats/archive/tar_lzma/xx_tar_lzma.h
    include/xxfclib/formats/archive/tar_lzop/xx_tar_lzop.h
    include/xxfclib/formats/archive/tarx1/xx_tarx1.h
    include/xxfclib/formats/archive/tarx2/xx_tarx2.h
    include/xxfclib/formats/filesystem/iso9660/xx_iso9660.h
    include/xxfclib/formats/firmware/trx/xx_trx.h
    include/xxfclib/formats/firmware/seama/xx_seama.h
    include/xxfclib/formats/firmware/chk/xx_chk.h
    include/xxfclib/formats/firmware/packimg/xx_packimg.h
    include/xxfclib/formats/firmware/dlob/xx_dlob.h
    include/xxfclib/formats/firmware/wince/xx_wince.h
    include/xxfclib/formats/firmware/binhdr/xx_binhdr.h
    include/xxfclib/formats/firmware/rtk/xx_rtk.h
    include/xxfclib/formats/firmware/csman/xx_csman.h
    include/xxfclib/formats/firmware/vxworks/xx_vxworks.h
    include/xxfclib/formats/firmware/uefi_fv/xx_uefi_fv.h
    include/xxfclib/formats/firmware/uefi_capsule/xx_uefi_capsule.h
    include/xxfclib/formats/filesystem/qcow/xx_qcow.h
    include/xxfclib/formats/filesystem/qnx6/xx_qnx6.h
    include/xxfclib/formats/filesystem/luks/xx_luks.h
    include/xxfclib/formats/filesystem/apfs/xx_apfs.h
    include/xxfclib/formats/filesystem/btrfs/xx_btrfs.h
    include/xxfclib/formats/filesystem/logfs/xx_logfs.h
    include/xxfclib/formats/filesystem/dmg/xx_dmg.h
    include/xxfclib/formats/filesystem/dms/xx_dms.h
    include/xxfclib/formats/archive/lzop/xx_lzop.h
    include/xxfclib/formats/firmware/srec/xx_srec.h
    include/xxfclib/formats/archive/dclraw/xx_dclraw.h
    include/xxfclib/formats/archive/xpak/xx_xpak.h
    include/xxfclib/formats/database/pdb/xx_pdb.h
    include/xxfclib/formats/game/infogramesft/xx_infogramesft.h
    include/xxfclib/formats/firmware/uboot/xx_uboot.h
    include/xxfclib/formats/installer/twrx/xx_twrx.h
    include/xxfclib/formats/firmware/tplink/xx_tplink.h
    include/xxfclib/formats/game/silmarilsft/xx_silmarilsft.h
    include/xxfclib/formats/firmware/shrs/xx_shrs.h
    include/xxfclib/formats/firmware/mh01/xx_mh01.h
    include/xxfclib/formats/firmware/matter_ota/xx_matter_ota.h
    include/xxfclib/formats/archive/lz4demo/xx_lz4demo.h
    include/xxfclib/formats/archive/lingvoarc/xx_lingvoarc.h
    include/xxfclib/formats/firmware/jboot/xx_jboot.h
    include/xxfclib/formats/firmware/encrpted_img/xx_encrpted_img.h
    include/xxfclib/formats/firmware/encfw/xx_encfw.h
    include/xxfclib/formats/firmware/ecos/xx_ecos.h
    include/xxfclib/formats/firmware/dlke/xx_dlke.h
    include/xxfclib/formats/firmware/dlink_tlv/xx_dlink_tlv.h
    include/xxfclib/formats/firmware/dkbs/xx_dkbs.h
    include/xxfclib/formats/firmware/autel/xx_autel.h
    include/xxfclib/formats/firmware/arcadyan/xx_arcadyan.h
    include/xxfclib/formats/firmware/androidboot/xx_androidboot.h
    include/xxfclib/formats/archive/rawstac/xx_rawstac.h
    include/xxfclib/formats/installer/rtpatch/xx_rtpatch.h
    include/xxfclib/formats/archive/vmssaveset/xx_vmssaveset.h
    include/xxfclib/formats/archive/boo/xx_boo.h
    include/xxfclib/formats/game/wolfft/xx_wolfft.h
    include/xxfclib/formats/game/settlersft/xx_settlersft.h
    include/xxfclib/formats/game/teacy/xx_teacy.h
    include/xxfclib/formats/game/rsc/xx_rsc.h
    include/xxfclib/formats/game/res/xx_res.h
    include/xxfclib/formats/executable/winres/xx_winres_make.h
    include/xxfclib/formats/archive/bsn/xx_bsn.h
    include/xxfclib/formats/game/wintermutedcp/xx_wintermutedcp.h
    include/xxfclib/formats/game/volitionvpft/xx_volitionvpft.h
    include/xxfclib/formats/game/agis/xx_agis.h
    include/xxfclib/formats/game/hog/xx_hog.h
    include/xxfclib/formats/archive/rnca/xx_rnca.h
    include/xxfclib/formats/document/paperport/xx_paperport.h
    include/xxfclib/formats/executable/starkit/xx_starkit.h
    include/xxfclib/formats/archive/lspack10/xx_lspack10.h
    include/xxfclib/formats/archive/ixa/xx_ixa.h
    include/xxfclib/formats/archive/mlb_ft/xx_mlb_ft.h
    include/xxfclib/formats/image/fss/xx_fss.h
    include/xxfclib/formats/game/epf/xx_epf.h
    include/xxfclib/formats/archive/dfc/xx_dfc.h
    include/xxfclib/formats/archive/sfx_rsfx/xx_sfx_rsfx.h
    include/xxfclib/formats/installer/sfx_softpaq4/xx_sfx_softpaq4.h
    include/xxfclib/formats/archive/ka/xx_ka.h
    include/xxfclib/formats/filesystem/nextstep_diskimage/xx_nextstep_diskimage.h
    include/xxfclib/formats/archive/dn/xx_dn.h
    include/xxfclib/formats/archive/insa/xx_insa.h
    include/xxfclib/formats/archive/sfx_vms_dcx/xx_sfx_vms_dcx.h
    include/xxfclib/formats/installer/oberon/xx_oberon.h
    include/xxfclib/formats/game/ppd/xx_ppd.h
    include/xxfclib/formats/installer/sfx_ad01/xx_sfx_ad01.h
    include/xxfclib/formats/archive/sfx_nss/xx_sfx_nss.h
    include/xxfclib/formats/installer/solitaire_deluxe/xx_solitaire_deluxe.h
    include/xxfclib/formats/document/thebat_msb/xx_thebat_msb.h
    include/xxfclib/formats/archive/sfx_localzip/xx_sfx_localzip.h
    include/xxfclib/formats/archive/lif/xx_lif.h
    include/xxfclib/formats/installer/qip2/xx_qip2.h
    include/xxfclib/formats/filesystem/emt/xx_emt.h
    include/xxfclib/formats/archive/bagf/xx_bagf.h
    include/xxfclib/formats/archive/wrzl/xx_wrzl.h
    include/xxfclib/formats/archive/spk/xx_spk.h
    include/xxfclib/formats/archive/stac/xx_stac.h
    include/xxfclib/formats/database/dbz/xx_dbz.h
    include/xxfclib/formats/archive/squeeze2/xx_squeeze2.h
    include/xxfclib/formats/archive/sq/xx_sq.h
    include/xxfclib/formats/archive/phar/xx_phar.h
    include/xxfclib/formats/game/mpq/xx_mpq.h
    include/xxfclib/formats/filesystem/sabdu/xx_sabdu.h
    include/xxfclib/formats/filesystem/apricot/xx_apricot.h
    include/xxfclib/formats/filesystem/hdcopy/xx_hdcopy.h
    include/xxfclib/formats/filesystem/copydisk/xx_copydisk.h
    include/xxfclib/formats/filesystem/ciso/xx_ciso.h
    include/xxfclib/formats/filesystem/vmdk/xx_vmdk.h
    include/xxfclib/formats/filesystem/vhddynamic/xx_vhddynamic.h
    include/xxfclib/formats/archive/wim/xx_wim.h
    include/xxfclib/formats/installer/softpaq2/xx_softpaq2.h
    include/xxfclib/formats/archive/aiaff/xx_aiaff.h
    include/xxfclib/formats/image/gxl/xx_gxl.h
    include/xxfclib/formats/filesystem/shrinkwrap/xx_shrinkwrap.h
    include/xxfclib/formats/installer/red/xx_red.h
    include/xxfclib/formats/filesystem/diskexpress/xx_diskexpress.h
    include/xxfclib/formats/archive/cpx/xx_cpx.h
    include/xxfclib/formats/installer/smsipak/xx_smsipak.h
    include/xxfclib/formats/installer/bnd/xx_bnd.h
    include/xxfclib/formats/game/cat/xx_cat.h
    include/xxfclib/formats/filesystem/csidos/xx_csidos.h
    include/xxfclib/formats/document/binder/xx_binder.h
    include/xxfclib/formats/installer/jasc/xx_jasc.h
    include/xxfclib/formats/installer/recognita/xx_recognita.h
    include/xxfclib/formats/installer/scf/xx_scf.h
    include/xxfclib/formats/installer/bcw/xx_bcw.h
    include/xxfclib/formats/archive/bvrp/xx_bvrp.h
    include/xxfclib/formats/installer/cpoint/xx_cpoint.h
    include/xxfclib/formats/archive/ssm/xx_ssm.h
    include/xxfclib/formats/archive/mdcd/xx_mdcd.h
    include/xxfclib/formats/archive/xlas/xx_xlas.h
    include/xxfclib/formats/document/pain/xx_pain.h
    include/xxfclib/formats/filesystem/qrst/xx_qrst.h
    include/xxfclib/formats/installer/opc/xx_opc.h
    include/xxfclib/formats/document/tnef/xx_tnef.h
    include/xxfclib/formats/archive/mcc/xx_mcc.h
    include/xxfclib/formats/document/notetab/xx_notetab.h
    include/xxfclib/formats/game/stunts/xx_stunts.h
    include/xxfclib/formats/game/megatechvol/xx_megatechvol.h
    include/xxfclib/formats/video/grasp/xx_grasp.h
    include/xxfclib/formats/archive/psn/xx_psn.h
    include/xxfclib/formats/game/sinner/xx_sinner.h
    include/xxfclib/formats/game/hog2/xx_hog2.h
    include/xxfclib/formats/image/pcxlib/xx_pcxlib.h
    include/xxfclib/formats/installer/vmsdb/xx_vmsdb.h
    include/xxfclib/formats/installer/vmspcsi/xx_vmspcsi.h
    include/xxfclib/formats/installer/beospkg/xx_beospkg.h
    include/xxfclib/formats/installer/solarispkg/xx_solarispkg.h
    include/xxfclib/formats/archive/pax/xx_pax.h
    include/xxfclib/formats/executable/copyqmexe/xx_copyqmexe.h
    include/xxfclib/formats/filesystem/diskjuggler/xx_diskjuggler.h
    include/xxfclib/formats/filesystem/pmdiskcopy/xx_pmdiskcopy.h
    include/xxfclib/formats/filesystem/diskdupe/xx_diskdupe.h
    include/xxfclib/formats/filesystem/imd/xx_imd.h
    include/xxfclib/formats/filesystem/twoimg/xx_twoimg.h
    include/xxfclib/formats/filesystem/fdi/xx_fdi.h
    include/xxfclib/formats/filesystem/hfe/xx_hfe.h
    include/xxfclib/formats/filesystem/teledisk/xx_teledisk.h
    include/xxfclib/formats/filesystem/copyqm/xx_copyqm.h
    include/xxfclib/formats/installer/pcinstall/xx_pcinstall.h
    include/xxfclib/formats/installer/gksetup/xx_gksetup.h
    include/xxfclib/formats/installer/is11/xx_is11.h
    include/xxfclib/formats/installer/izpack/xx_izpack.h
    include/xxfclib/formats/archive/squeeze1/xx_squeeze1.h
    include/xxfclib/formats/filesystem/trdos/xx_trdos.h
    include/xxfclib/formats/installer/lifkd/xx_lifkd.h
    include/xxfclib/formats/installer/arcv/xx_arcv.h
    include/xxfclib/formats/archive/compaqlzh/xx_compaqlzh.h
    include/xxfclib/formats/archive/lzk00/xx_lzk00.h
    include/xxfclib/formats/archive/pma/xx_pma.h
    include/xxfclib/formats/archive/binhex/xx_binhex.h
    include/xxfclib/formats/archive/binaryii/xx_binaryii.h
    include/xxfclib/formats/archive/stuffit/xx_stuffit.h
    include/xxfclib/formats/archive/dclft/xx_dclft.h
    include/xxfclib/formats/executable/debugscr/xx_debugscr.h
    include/xxfclib/formats/game/gob/xx_gob.h
    include/xxfclib/formats/filesystem/savedskf/xx_savedskf.h
    include/xxfclib/formats/archive/edilzss/xx_edilzss.h
    include/xxfclib/formats/installer/is7inx/xx_is7inx.h
    include/xxfclib/formats/installer/is5/xx_is5.h
    include/xxfclib/formats/installer/is3/xx_is3.h
    include/xxfclib/formats/archive/psdc/xx_psdc.h
    include/xxfclib/formats/archive/unixcompact/xx_unixcompact.h
    include/xxfclib/formats/archive/sco/xx_sco.h
    include/xxfclib/formats/archive/gpfpack/xx_gpfpack.h
    include/xxfclib/formats/archive/ftcomp/xx_ftcomp.h
    include/xxfclib/formats/archive/winlink/xx_winlink.h
    include/xxfclib/formats/installer/gst/xx_gst.h
    include/xxfclib/formats/archive/finear/xx_finear.h
    include/xxfclib/formats/firmware/mwave/xx_mwave.h
    include/xxfclib/formats/archive/xorarchive/xx_xorarchive.h
    include/xxfclib/formats/game/mxs/xx_mxs.h
    include/xxfclib/formats/archive/edc/xx_edc.h
    include/xxfclib/formats/archive/mrnz/xx_mrnz.h
    include/xxfclib/formats/archive/tpwm/xx_tpwm.h
    include/xxfclib/formats/archive/cazip/xx_cazip.h
    include/xxfclib/formats/archive/ibmpack/xx_ibmpack.h
    include/xxfclib/formats/archive/rnc/xx_rnc.h
    include/xxfclib/formats/archive/shar/xx_shar.h
    include/xxfclib/formats/installer/netware2/xx_netware2.h
    include/xxfclib/formats/document/mathcad/xx_mathcad.h
    include/xxfclib/formats/document/perform/xx_perform.h
    include/xxfclib/formats/game/battleisle/xx_battleisle.h
    include/xxfclib/formats/archive/kpck/xx_kpck.h
    include/xxfclib/formats/game/beatthehouse/xx_beatthehouse.h
    include/xxfclib/formats/archive/pp20/xx_pp20.h
    include/xxfclib/formats/archive/macbinary/xx_macbinary.h
    include/xxfclib/formats/archive/applesingle/xx_applesingle.h
    include/xxfclib/formats/executable/resourcefork/xx_resourcefork.h
    include/xxfclib/formats/filesystem/cramfs/xx_cramfs.h
    include/xxfclib/formats/filesystem/jffs2/xx_jffs2.h
    include/xxfclib/formats/filesystem/yaffs/xx_yaffs.h
    include/xxfclib/formats/filesystem/ubi/xx_ubi.h
    include/xxfclib/formats/filesystem/ubifs/xx_ubifs.h
    include/xxfclib/formats/filesystem/ext/xx_ext.h
    include/xxfclib/formats/filesystem/erofs/xx_erofs.h
    include/xxfclib/formats/filesystem/fat/xx_fat.h
    include/xxfclib/formats/filesystem/mbr/xx_mbr.h
    include/xxfclib/formats/filesystem/gpt/xx_gpt.h
    include/xxfclib/formats/firmware/sparse/xx_sparse.h
    include/xxfclib/formats/firmware/uimage/xx_uimage.h
    include/xxfclib/formats/firmware/dtb/xx_dtb.h
    include/xxfclib/formats/filesystem/squashfs/xx_squashfs.h
    include/xxfclib/formats/filesystem/ntfs/xx_ntfs.h
    include/xxfclib/formats/filesystem/udf/xx_udf.h
    include/xxfclib/formats/filesystem/romfs/xx_romfs.h
    include/xxfclib/formats/archive/sqz/xx_sqz.h
    include/xxfclib/formats/installer/topspeed/xx_topspeed.h
    include/xxfclib/formats/database/tps/xx_tps.h
    include/xxfclib/formats/archive/ulead/xx_ulead.h
    include/xxfclib/formats/archive/quantum/xx_quantum.h
    include/xxfclib/formats/archive/zxzip/xx_zxzip.h
    include/xxfclib/formats/filesystem/zoom/xx_zoom.h
    include/xxfclib/formats/audio/sfpack/xx_sfpack.h
    include/xxfclib/formats/archive/claylz/xx_claylz.h
    include/xxfclib/formats/archive/c64wraptor/xx_c64wraptor.h
    include/xxfclib/formats/installer/corelltec/xx_corelltec.h
    include/xxfclib/formats/archive/pcsecure/xx_pcsecure.h
    include/xxfclib/formats/archive/rsvk/xx_rsvk.h
    include/xxfclib/formats/archive/lzw15v/xx_lzw15v.h
    include/xxfclib/formats/archive/saf/xx_saf.h
    include/xxfclib/formats/archive/sls/xx_sls.h
    include/xxfclib/formats/installer/nid/xx_nid.h
    include/xxfclib/formats/archive/gamos/xx_gamos.h
    include/xxfclib/formats/archive/panorama/xx_panorama.h
    include/xxfclib/formats/installer/fpak/xx_fpak.h
    include/xxfclib/formats/archive/ace/xx_ace.h
    include/xxfclib/formats/archive/arj/xx_arj.h
    include/xxfclib/formats/archive/ain/xx_ain.h
    include/xxfclib/formats/archive/aldus/xx_aldus.h
    include/xxfclib/formats/archive/alz/xx_alz.h
    include/xxfclib/formats/archive/ampk/xx_ampk.h
    include/xxfclib/formats/filesystem/aodos/xx_aodos.h
    include/xxfclib/formats/archive/arcfs/xx_arcfs.h
    include/xxfclib/formats/archive/pdp11ar/xx_pdp11ar.h
    include/xxfclib/formats/archive/artipack/xx_artipack.h
    include/xxfclib/formats/installer/arcv2/xx_arcv2.h
    include/xxfclib/formats/installer/arcv4/xx_arcv4.h
    include/xxfclib/formats/archive/warc/xx_warc.h
    include/xxfclib/formats/installer/aixbff/xx_aixbff.h
    include/xxfclib/formats/archive/arx/xx_arx.h
    include/xxfclib/formats/archive/cab/xx_cab.h
    include/xxfclib/formats/executable/msdos/xx_msdos.h
    include/xxfclib/formats/executable/com/xx_com.h
    include/xxfclib/formats/executable/dos16m/xx_dos16m.h
    include/xxfclib/formats/executable/atarist/xx_atarist.h
    include/xxfclib/formats/executable/amigahunk/xx_amigahunk.h
    include/xxfclib/formats/executable/pe/xx_pe_inspect.h
    include/xxfclib/formats/executable/pe/xx_pe_unwind_validate.h
    include/xxfclib/formats/executable/dotnet/xx_dotnet.h
    include/xxfclib/formats/executable/dotnet/xx_dotnet_inspect.h
    include/xxfclib/formats/executable/dotnet/xx_dotnet_reader.h
    include/xxfclib/formats/executable/pe/xx_pe.h
    include/xxfclib/formats/executable/pe/xx_pe_make.h
    include/xxfclib/formats/executable/elf/xx_elf_inspect.h
    include/xxfclib/formats/executable/elf/xx_elf.h
    include/xxfclib/formats/executable/elf/xx_elf_dwarf.h
    include/xxfclib/formats/executable/elf/xx_elf_dwarf_analysis.h
    include/xxfclib/formats/executable/dwarf/xx_dwarf_loclists.h
    include/xxfclib/formats/firmware/bios_firmware/xx_bios_firmware.h
    include/xxfclib/formats/firmware/gns1/xx_gns1.h
    include/xxfclib/formats/executable/pebble/xx_pebble.h
    include/xxfclib/formats/executable/hp_som/xx_hp_som.h
    include/xxfclib/formats/executable/qnx_lmf/xx_qnx_lmf.h
    include/xxfclib/formats/executable/nintendo_rel/xx_nintendo_rel.h
    include/xxfclib/formats/executable/xcoff/xx_xcoff.h
    include/xxfclib/formats/executable/macho/xx_macho_inspect.h
    include/xxfclib/formats/executable/macho/xx_macho.h
    include/xxfclib/formats/executable/ne/xx_ne.h
    include/xxfclib/formats/executable/le/xx_le.h
    include/xxfclib/formats/executable/lx/xx_lx.h
    include/xxfclib/formats/executable/dex/xx_dex.h
    # Readers already registered by xx_format and the public API.
    include/xxfclib/formats/installer/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.h
    include/xxfclib/formats/installer/sfx_krzip/xx_sfx_krzip.h
    include/xxfclib/formats/installer/sfx_warpin_package/xx_sfx_warpin_package.h
    include/xxfclib/formats/installer/sfx_hci_instalit/xx_sfx_hci_instalit.h
    include/xxfclib/formats/game/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.h
    include/xxfclib/formats/installer/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.h
    include/xxfclib/formats/executable/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.h
    include/xxfclib/formats/installer/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.h
    include/xxfclib/formats/archive/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.h
    include/xxfclib/formats/installer/sfx_sbx_extractor/xx_sfx_sbx_extractor.h
    include/xxfclib/formats/installer/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.h
    include/xxfclib/formats/filesystem/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.h
    include/xxfclib/formats/installer/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.h
    include/xxfclib/formats/installer/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.h
    include/xxfclib/formats/installer/wise_installation_system/xx_wise_installation_system.h
    include/xxfclib/formats/installer/eschalon_setup_epsf/xx_eschalon_setup_epsf.h
    include/xxfclib/formats/installer/gentee_installer/xx_gentee_installer.h
    include/xxfclib/formats/installer/clickteam_install_creator/xx_clickteam_install_creator.h
    include/xxfclib/formats/installer/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.h
    include/xxfclib/formats/installer/sfxstart/xx_sfxstart.h
    include/xxfclib/formats/other/modbus_tcp/xx_modbus_tcp.h
    include/xxfclib/formats/other/someip_message/xx_someip_message.h
    include/xxfclib/formats/other/dds_rtps/xx_dds_rtps.h
    include/xxfclib/formats/other/rip_message/xx_rip_message.h
    include/xxfclib/formats/other/vrrp_message/xx_vrrp_message.h
    include/xxfclib/formats/other/igmp_message/xx_igmp_message.h
    include/xxfclib/formats/other/pim_message/xx_pim_message.h
    include/xxfclib/formats/other/ldp_message/xx_ldp_message.h
    include/xxfclib/formats/other/gre_packet/xx_gre_packet.h
    include/xxfclib/formats/other/l2tp_packet/xx_l2tp_packet.h
    include/xxfclib/formats/other/lldp_message/xx_lldp_message.h
    include/xxfclib/formats/other/netflow_datagram/xx_netflow_datagram.h
    include/xxfclib/formats/other/ntlm_message/xx_ntlm_message.h
    include/xxfclib/formats/other/dcerpc_pdu/xx_dcerpc_pdu.h
    include/xxfclib/formats/other/ethereum_rlp/xx_ethereum_rlp.h
    include/xxfclib/formats/image/imagemagick_miff/xx_imagemagick_miff.h
    include/xxfclib/formats/image/avs_image/xx_avs_image.h
    include/xxfclib/formats/science/scanalytics_iplab/xx_scanalytics_iplab.h
    include/xxfclib/formats/image/mtv_image/xx_mtv_image.h
    include/xxfclib/formats/image/nokia_ota_bitmap/xx_nokia_ota_bitmap.h
    include/xxfclib/formats/image/apple_pict/xx_apple_pict.h
    include/xxfclib/formats/image/wordperfect_wpg/xx_wordperfect_wpg.h
    include/xxfclib/formats/science/nasa_vicar/xx_nasa_vicar.h
    include/xxfclib/formats/image/khoros_viff/xx_khoros_viff.h
    include/xxfclib/formats/image/imagemagick_mvg/xx_imagemagick_mvg.h
    include/xxfclib/formats/image/motif_uil/xx_motif_uil.h
    include/xxfclib/formats/science/iges_model/xx_iges_model.h
    include/xxfclib/formats/image/openusd_usda/xx_openusd_usda.h
    include/xxfclib/formats/font/ufo_glif/xx_ufo_glif.h
    include/xxfclib/formats/font/unifont_hex/xx_unifont_hex.h
    include/xxfclib/formats/audio/adlib_sop/xx_adlib_sop.h
    include/xxfclib/formats/audio/cudfm_cff/xx_cudfm_cff.h
    include/xxfclib/formats/audio/adlib_jbm/xx_adlib_jbm.h
    include/xxfclib/formats/audio/ceres_msc/xx_ceres_msc.h
    include/xxfclib/formats/audio/adlib_xsm/xx_adlib_xsm.h
    include/xxfclib/formats/audio/ken_ksm/xx_ken_ksm.h
    include/xxfclib/formats/audio/implay_music/xx_implay_music.h
    include/xxfclib/formats/audio/adlib_mtr/xx_adlib_mtr.h
    include/xxfclib/formats/audio/rdos_raw/xx_rdos_raw.h
    include/xxfclib/formats/audio/mad_tracker/xx_mad_tracker.h
    include/xxfclib/formats/science/vasp_poscar/xx_vasp_poscar.h
    include/xxfclib/formats/science/quantum_espresso_input/xx_quantum_espresso_input.h
    include/xxfclib/formats/science/cp2k_input/xx_cp2k_input.h
    include/xxfclib/formats/science/nwchem_input/xx_nwchem_input.h
    include/xxfclib/formats/science/gamess_input/xx_gamess_input.h
    include/xxfclib/formats/science/gaussian_input/xx_gaussian_input.h
    include/xxfclib/formats/science/abinit_input/xx_abinit_input.h
    include/xxfclib/formats/science/aims_geometry/xx_aims_geometry.h
    include/xxfclib/formats/science/orca_input/xx_orca_input.h
    include/xxfclib/formats/science/demon_input/xx_demon_input.h
    include/xxfclib/formats/other/ethernet_frame/xx_ethernet_frame.h
    include/xxfclib/formats/other/ip_packet/xx_ip_packet.h
    include/xxfclib/formats/other/arp_packet/xx_arp_packet.h
    include/xxfclib/formats/other/icmp_message/xx_icmp_message.h
    include/xxfclib/formats/other/sip_message/xx_sip_message.h
    include/xxfclib/formats/other/rtsp_message/xx_rtsp_message.h
    include/xxfclib/formats/other/diameter_message/xx_diameter_message.h
    include/xxfclib/formats/other/tacacs_packet/xx_tacacs_packet.h
    include/xxfclib/formats/other/gtp_message/xx_gtp_message.h
    include/xxfclib/formats/other/pfcp_message/xx_pfcp_message.h
    include/xxfclib/formats/other/pptp_message/xx_pptp_message.h
    include/xxfclib/formats/other/rsvp_message/xx_rsvp_message.h
    include/xxfclib/formats/other/age_encrypted/xx_age_encrypted.h
    include/xxfclib/formats/other/kerberos_ccache/xx_kerberos_ccache.h
    include/xxfclib/formats/other/jose_jws/xx_jose_jws.h
    include/xxfclib/formats/image/wbmp_image/xx_wbmp_image.h
    include/xxfclib/formats/image/dec_sixel/xx_dec_sixel.h
    include/xxfclib/formats/image/palm_bitmap/xx_palm_bitmap.h
    include/xxfclib/formats/image/adobe_acv/xx_adobe_acv.h
    include/xxfclib/formats/image/adobe_act/xx_adobe_act.h
    include/xxfclib/formats/image/ogre_skeleton/xx_ogre_skeleton.h
    include/xxfclib/formats/image/cal3d_skeleton/xx_cal3d_skeleton.h
    include/xxfclib/formats/image/collada_dae/xx_collada_dae.h
    include/xxfclib/formats/image/lightwave_scene/xx_lightwave_scene.h
    include/xxfclib/formats/science/dsn6_density/xx_dsn6_density.h
    include/xxfclib/formats/science/crystallography_mtz/xx_crystallography_mtz.h
    include/xxfclib/formats/science/amira_mesh/xx_amira_mesh.h
    include/xxfclib/formats/science/tetgen_mesh/xx_tetgen_mesh.h
    include/xxfclib/formats/firmware/jedec_fuse/xx_jedec_fuse.h
    include/xxfclib/formats/science/qchem_input/xx_qchem_input.h
    include/xxfclib/formats/audio/adlib_bam/xx_adlib_bam.h
    include/xxfclib/formats/audio/adlib_bmf/xx_adlib_bmf.h
    include/xxfclib/formats/audio/creative_cmf/xx_creative_cmf.h
    include/xxfclib/formats/audio/adlib_dfm/xx_adlib_dfm.h
    include/xxfclib/formats/audio/adlib_lds/xx_adlib_lds.h
    include/xxfclib/formats/audio/adlib_mkj/xx_adlib_mkj.h
    include/xxfclib/formats/audio/adlib_rol/xx_adlib_rol.h
    include/xxfclib/formats/audio/adlib_sa2/xx_adlib_sa2.h
    include/xxfclib/formats/audio/faust_fmc/xx_faust_fmc.h
    include/xxfclib/formats/audio/softstar_rix/xx_softstar_rix.h
    include/xxfclib/formats/science/genomics_bed/xx_genomics_bed.h
    include/xxfclib/formats/science/genomics_wiggle/xx_genomics_wiggle.h
    include/xxfclib/formats/science/genomics_gtf/xx_genomics_gtf.h
    include/xxfclib/formats/science/genomics_agp/xx_genomics_agp.h
    include/xxfclib/formats/science/sequencing_abif/xx_sequencing_abif.h
    include/xxfclib/formats/science/sequencing_scf/xx_sequencing_scf.h
    include/xxfclib/formats/science/genomics_sff/xx_genomics_sff.h
    include/xxfclib/formats/image/lut_spi1d/xx_lut_spi1d.h
    include/xxfclib/formats/image/lut_spi3d/xx_lut_spi3d.h
    include/xxfclib/formats/image/lut_cinespace_csp/xx_lut_cinespace_csp.h
    include/xxfclib/formats/other/ntp_message/xx_ntp_message.h
    include/xxfclib/formats/other/rtp_rtcp/xx_rtp_rtcp.h
    include/xxfclib/formats/other/bgp_messages/xx_bgp_messages.h
    include/xxfclib/formats/other/ospf_packet/xx_ospf_packet.h
    include/xxfclib/formats/other/sctp_packet/xx_sctp_packet.h
    include/xxfclib/formats/other/isakmp_message/xx_isakmp_message.h
    include/xxfclib/formats/other/ssh_transport/xx_ssh_transport.h
    include/xxfclib/formats/other/smtp_transcript/xx_smtp_transcript.h
    include/xxfclib/formats/other/pkcs8_private_key/xx_pkcs8_private_key.h
    include/xxfclib/formats/other/putty_ppk/xx_putty_ppk.h
    include/xxfclib/formats/other/openssh_certificate/xx_openssh_certificate.h
    include/xxfclib/formats/other/safetensors/xx_safetensors.h
    include/xxfclib/formats/other/gguf/xx_gguf.h
    include/xxfclib/formats/database/cdb_database/xx_cdb_database.h
    include/xxfclib/formats/other/stomp_frames/xx_stomp_frames.h
    include/xxfclib/formats/font/fontforge_sfd/xx_fontforge_sfd.h
    include/xxfclib/formats/font/grub_pff2/xx_grub_pff2.h
    include/xxfclib/formats/image/opengex_model/xx_opengex_model.h
    include/xxfclib/formats/image/bvh_motion/xx_bvh_motion.h
    include/xxfclib/formats/image/directx_x/xx_directx_x.h
    include/xxfclib/formats/image/gts_surface/xx_gts_surface.h
    include/xxfclib/formats/science/medit_mesh/xx_medit_mesh.h
    include/xxfclib/formats/science/gocad_model/xx_gocad_model.h
    include/xxfclib/formats/science/nastran_bulk/xx_nastran_bulk.h
    include/xxfclib/formats/science/abaqus_input/xx_abaqus_input.h
    include/xxfclib/formats/science/ensight_gold_geometry/xx_ensight_gold_geometry.h
    include/xxfclib/formats/science/gmv_mesh/xx_gmv_mesh.h
    include/xxfclib/formats/science/usgs_dem/xx_usgs_dem.h
    include/xxfclib/formats/science/dted_elevation/xx_dted_elevation.h
    include/xxfclib/formats/science/mapinfo_mif/xx_mapinfo_mif.h
    include/xxfclib/formats/audio/tracker_coconizer/xx_tracker_coconizer.h
    include/xxfclib/formats/audio/tracker_real/xx_tracker_real.h
    include/xxfclib/formats/audio/tracker_megatracker/xx_tracker_megatracker.h
    include/xxfclib/formats/audio/amos_music_bank/xx_amos_music_bank.h
    include/xxfclib/formats/audio/adlib_rad/xx_adlib_rad.h
    include/xxfclib/formats/audio/adlib_amd/xx_adlib_amd.h
    include/xxfclib/formats/audio/adlib_hsc/xx_adlib_hsc.h
    include/xxfclib/formats/audio/adlib_d00/xx_adlib_d00.h
    include/xxfclib/formats/audio/adlib_bnk/xx_adlib_bnk.h
    include/xxfclib/formats/audio/dosbox_dro/xx_dosbox_dro.h
    include/xxfclib/formats/science/genomics_genbank/xx_genomics_genbank.h
    include/xxfclib/formats/science/genomics_embl/xx_genomics_embl.h
    include/xxfclib/formats/science/genomics_swissprot/xx_genomics_swissprot.h
    include/xxfclib/formats/science/alignment_clustal/xx_alignment_clustal.h
    include/xxfclib/formats/science/alignment_stockholm/xx_alignment_stockholm.h
    include/xxfclib/formats/science/alignment_phylip/xx_alignment_phylip.h
    include/xxfclib/formats/science/alignment_maf/xx_alignment_maf.h
    include/xxfclib/formats/science/alignment_mauve/xx_alignment_mauve.h
    include/xxfclib/formats/science/ucsc_nib/xx_ucsc_nib.h
    include/xxfclib/formats/science/assembly_gfa/xx_assembly_gfa.h
    include/xxfclib/formats/other/http1_message/xx_http1_message.h
    include/xxfclib/formats/other/websocket_frames/xx_websocket_frames.h
    include/xxfclib/formats/other/coap_message/xx_coap_message.h
    include/xxfclib/formats/other/stun_message/xx_stun_message.h
    include/xxfclib/formats/other/dhcp_message/xx_dhcp_message.h
    include/xxfclib/formats/other/radius_packet/xx_radius_packet.h
    include/xxfclib/formats/other/snmp_message/xx_snmp_message.h
    include/xxfclib/formats/other/ldap_message/xx_ldap_message.h
    include/xxfclib/formats/other/tls_records/xx_tls_records.h
    include/xxfclib/formats/other/jks_keystore/xx_jks_keystore.h
    include/xxfclib/formats/other/java_serialization/xx_java_serialization.h
    include/xxfclib/formats/other/x509_crl/xx_x509_crl.h
    include/xxfclib/formats/other/ocsp_response/xx_ocsp_response.h
    include/xxfclib/formats/database/lmdb_data/xx_lmdb_data.h
    include/xxfclib/formats/database/gdbm_dump/xx_gdbm_dump.h
    include/xxfclib/formats/other/adobe_acb/xx_adobe_acb.h
    include/xxfclib/formats/image/jasc_palette/xx_jasc_palette.h
    include/xxfclib/formats/image/x11_xbm/xx_x11_xbm.h
    include/xxfclib/formats/image/jpeg2000_pgx/xx_jpeg2000_pgx.h
    include/xxfclib/formats/image/amiga_diskobject/xx_amiga_diskobject.h
    include/xxfclib/formats/font/tex_vf/xx_tex_vf.h
    include/xxfclib/formats/science/esri_ascii_grid/xx_esri_ascii_grid.h
    include/xxfclib/formats/science/surfer_grid/xx_surfer_grid.h
    include/xxfclib/formats/science/gxf_grid/xx_gxf_grid.h
    include/xxfclib/formats/science/ogc_wkt/xx_ogc_wkt.h
    include/xxfclib/formats/science/step_part21/xx_step_part21.h
    include/xxfclib/formats/science/gerber_rs274x/xx_gerber_rs274x.h
    include/xxfclib/formats/science/excellon_drill/xx_excellon_drill.h
    include/xxfclib/formats/image/vrml_scene/xx_vrml_scene.h
    include/xxfclib/formats/image/renderman_rib/xx_renderman_rib.h
    include/xxfclib/formats/audio/asylum_amf/xx_asylum_amf.h
    include/xxfclib/formats/audio/tracker_stx/xx_tracker_stx.h
    include/xxfclib/formats/audio/tracker_dtm/xx_tracker_dtm.h
    include/xxfclib/formats/audio/tracker_soundfx/xx_tracker_soundfx.h
    include/xxfclib/formats/audio/tracker_funk/xx_tracker_funk.h
    include/xxfclib/formats/audio/tracker_archimedes/xx_tracker_archimedes.h
    include/xxfclib/formats/filesystem/pce_psi/xx_pce_psi.h
    include/xxfclib/formats/filesystem/pc98_d88/xx_pc98_d88.h
    include/xxfclib/formats/filesystem/hxc_mfm/xx_hxc_mfm.h
    include/xxfclib/formats/filesystem/yaze_ydsk/xx_yaze_ydsk.h
    include/xxfclib/formats/science/lammps_data/xx_lammps_data.h
    include/xxfclib/formats/science/lammps_dump/xx_lammps_dump.h
    include/xxfclib/formats/science/shelx_res/xx_shelx_res.h
    include/xxfclib/formats/science/turbomole_coord/xx_turbomole_coord.h
    include/xxfclib/formats/science/charmm_crd/xx_charmm_crd.h
    include/xxfclib/formats/science/castep_cell/xx_castep_cell.h
    include/xxfclib/formats/science/crystal_fort34/xx_crystal_fort34.h
    include/xxfclib/formats/science/siesta_xv/xx_siesta_xv.h
    include/xxfclib/formats/science/harwell_boeing/xx_harwell_boeing.h
    include/xxfclib/formats/science/openfoam_points/xx_openfoam_points.h
    include/xxfclib/formats/game/minecraft_nbt/xx_minecraft_nbt.h
    include/xxfclib/formats/other/amazon_ion_binary/xx_amazon_ion_binary.h
    include/xxfclib/formats/database/leveldb_log/xx_leveldb_log.h
    include/xxfclib/formats/other/dns_message/xx_dns_message.h
    include/xxfclib/formats/database/rocksdb_blob/xx_rocksdb_blob.h
    include/xxfclib/formats/other/mongodb_wire/xx_mongodb_wire.h
    include/xxfclib/formats/other/redis_resp/xx_redis_resp.h
    include/xxfclib/formats/other/mqtt_packets/xx_mqtt_packets.h
    include/xxfclib/formats/other/amqp_frames/xx_amqp_frames.h
    include/xxfclib/formats/other/thrift_compact/xx_thrift_compact.h
    include/xxfclib/formats/other/x509_certificate/xx_x509_certificate.h
    include/xxfclib/formats/other/pkcs10_csr/xx_pkcs10_csr.h
    include/xxfclib/formats/other/pkcs12_pfx/xx_pkcs12_pfx.h
    include/xxfclib/formats/other/openssh_private_key/xx_openssh_private_key.h
    include/xxfclib/formats/other/kerberos_keytab/xx_kerberos_keytab.h
    include/xxfclib/formats/image/gimp_gpl/xx_gimp_gpl.h
    include/xxfclib/formats/image/gimp_ggr/xx_gimp_ggr.h
    include/xxfclib/formats/image/iridas_cube_lut/xx_iridas_cube_lut.h
    include/xxfclib/formats/image/hpgl_plot/xx_hpgl_plot.h
    include/xxfclib/formats/image/paintshop_psp/xx_paintshop_psp.h
    include/xxfclib/formats/image/photoshop_pat/xx_photoshop_pat.h
    include/xxfclib/formats/image/mmd_pmx/xx_mmd_pmx.h
    include/xxfclib/formats/image/metasequoia_mqo/xx_metasequoia_mqo.h
    include/xxfclib/formats/science/calma_gdsii/xx_calma_gdsii.h
    include/xxfclib/formats/image/autodesk_ase/xx_autodesk_ase.h
    include/xxfclib/formats/science/freesurfer_surface/xx_freesurfer_surface.h
    include/xxfclib/formats/science/gmsh_msh/xx_gmsh_msh.h
    include/xxfclib/formats/science/netgen_vol/xx_netgen_vol.h
    include/xxfclib/formats/font/font_afm/xx_font_afm.h
    include/xxfclib/formats/game/tiled_tmx/xx_tiled_tmx.h
    include/xxfclib/formats/audio/nintendo_sdat/xx_nintendo_sdat.h
    include/xxfclib/formats/audio/sony_vab/xx_sony_vab.h
    include/xxfclib/formats/audio/yamaha_ym/xx_yamaha_ym.h
    include/xxfclib/formats/audio/zx_ayemul/xx_zx_ayemul.h
    include/xxfclib/formats/filesystem/dragon_vdk/xx_dragon_vdk.h
    include/xxfclib/formats/filesystem/apple_a2r/xx_apple_a2r.h
    include/xxfclib/formats/filesystem/atari_atr/xx_atari_atr.h
    include/xxfclib/formats/filesystem/atari_pasti_stx/xx_atari_pasti_stx.h
    include/xxfclib/formats/filesystem/amiga_ipf/xx_amiga_ipf.h
    include/xxfclib/formats/audio/tracker_dtt/xx_tracker_dtt.h
    include/xxfclib/formats/science/gaussian_cube/xx_gaussian_cube.h
    include/xxfclib/formats/science/molecule_xyz/xx_molecule_xyz.h
    include/xxfclib/formats/science/mdl_molfile/xx_mdl_molfile.h
    include/xxfclib/formats/science/tripos_mol2/xx_tripos_mol2.h
    include/xxfclib/formats/science/xcrysden_xsf/xx_xcrysden_xsf.h
    include/xxfclib/formats/science/amber_prmtop/xx_amber_prmtop.h
    include/xxfclib/formats/science/amber_restart/xx_amber_restart.h
    include/xxfclib/formats/science/gaussian_fchk/xx_gaussian_fchk.h
    include/xxfclib/formats/science/jcamp_dx/xx_jcamp_dx.h
    include/xxfclib/formats/science/dl_poly_config/xx_dl_poly_config.h
    include/xxfclib/formats/archive/nix_nar/xx_nix_nar.h
    include/xxfclib/formats/database/redis_rdb/xx_redis_rdb.h
    include/xxfclib/formats/database/postgres_custom/xx_postgres_custom.h
    include/xxfclib/formats/database/mysql_binlog/xx_mysql_binlog.h
    include/xxfclib/formats/other/kafka_record_batch/xx_kafka_record_batch.h
    include/xxfclib/formats/executable/android_binary_xml/xx_android_binary_xml.h
    include/xxfclib/formats/executable/android_resources_arsc/xx_android_resources_arsc.h
    include/xxfclib/formats/other/msgpack/xx_msgpack.h
    include/xxfclib/formats/other/ubjson/xx_ubjson.h
    include/xxfclib/formats/other/bittorrent_metainfo/xx_bittorrent_metainfo.h
    include/xxfclib/formats/other/erlang_external_term/xx_erlang_external_term.h
    include/xxfclib/formats/other/capnproto_message/xx_capnproto_message.h
    include/xxfclib/formats/other/dbus_message/xx_dbus_message.h
    include/xxfclib/formats/other/windows_shell_link/xx_windows_shell_link.h
    include/xxfclib/formats/other/pkcs7_cms/xx_pkcs7_cms.h
    include/xxfclib/formats/image/wavefront_obj/xx_wavefront_obj.h
    include/xxfclib/formats/image/off_mesh/xx_off_mesh.h
    include/xxfclib/formats/image/ac3d_model/xx_ac3d_model.h
    include/xxfclib/formats/image/qubicle_qb/xx_qubicle_qb.h
    include/xxfclib/formats/image/terragen_ter/xx_terragen_ter.h
    include/xxfclib/formats/image/gimp_xcf/xx_gimp_xcf.h
    include/xxfclib/formats/image/photoshop_abr/xx_photoshop_abr.h
    include/xxfclib/formats/image/softimage_pic/xx_softimage_pic.h
    include/xxfclib/formats/image/alias_pix/xx_alias_pix.h
    include/xxfclib/formats/image/qt_qpicture/xx_qt_qpicture.h
    include/xxfclib/formats/font/font_type1_pfb/xx_font_type1_pfb.h
    include/xxfclib/formats/font/font_gem_fnt/xx_font_gem_fnt.h
    include/xxfclib/formats/font/tex_gf/xx_tex_gf.h
    include/xxfclib/formats/image/bpg_image/xx_bpg_image.h
    include/xxfclib/formats/video/mng_animation/xx_mng_animation.h
    include/xxfclib/formats/game/atari_7800_a78/xx_atari_7800_a78.h
    include/xxfclib/formats/filesystem/commodore_pc64/xx_commodore_pc64.h
    include/xxfclib/formats/filesystem/atari_cas/xx_atari_cas.h
    include/xxfclib/formats/filesystem/msx_cas/xx_msx_cas.h
    include/xxfclib/formats/filesystem/oric_tap/xx_oric_tap.h
    include/xxfclib/formats/filesystem/dragon_cas/xx_dragon_cas.h
    include/xxfclib/formats/audio/amiga_ahx/xx_amiga_ahx.h
    include/xxfclib/formats/game/amstrad_cpc_sna/xx_amstrad_cpc_sna.h
    include/xxfclib/formats/executable/vtech_vz/xx_vtech_vz.h
    include/xxfclib/formats/filesystem/zx_hobeta/xx_zx_hobeta.h
    include/xxfclib/formats/science/genomics_fasta/xx_genomics_fasta.h
    include/xxfclib/formats/science/genomics_fastq/xx_genomics_fastq.h
    include/xxfclib/formats/science/genomics_sam/xx_genomics_sam.h
    include/xxfclib/formats/science/opendx_field/xx_opendx_field.h
    include/xxfclib/formats/science/genomics_vcf/xx_genomics_vcf.h
    include/xxfclib/formats/science/genomics_gff3/xx_genomics_gff3.h
    include/xxfclib/formats/science/protein_pdb/xx_protein_pdb.h
    include/xxfclib/formats/science/protein_mmcif/xx_protein_mmcif.h
    include/xxfclib/formats/science/matrix_market/xx_matrix_market.h
    include/xxfclib/formats/science/gromacs_gro/xx_gromacs_gro.h
    include/xxfclib/formats/executable/microsoft_msf/xx_microsoft_msf.h
    include/xxfclib/formats/executable/pef/xx_pef.h
    include/xxfclib/formats/other/rprj/xx_rprj.h
    include/xxfclib/formats/other/windows_registry_hive/xx_windows_registry_hive.h
    include/xxfclib/formats/other/windows_evtx/xx_windows_evtx.h
    include/xxfclib/formats/other/binary_plist/xx_binary_plist.h
    include/xxfclib/formats/other/mongodb_bson/xx_mongodb_bson.h
    include/xxfclib/formats/other/cbor/xx_cbor.h
    include/xxfclib/formats/document/openzim/xx_openzim.h
    include/xxfclib/formats/database/apache_orc/xx_apache_orc.h
    include/xxfclib/formats/database/hadoop_sequencefile/xx_hadoop_sequencefile.h
    include/xxfclib/formats/database/leveldb_sstable/xx_leveldb_sstable.h
    include/xxfclib/formats/archive/snappy_framed/xx_snappy_framed.h
    include/xxfclib/formats/archive/lzf_stream/xx_lzf_stream.h
    include/xxfclib/formats/archive/fastlz_sixpack/xx_fastlz_sixpack.h
    include/xxfclib/formats/executable/linux_btf/xx_linux_btf.h
    include/xxfclib/formats/science/flatgeobuf/xx_flatgeobuf.h
    include/xxfclib/formats/image/astc_texture/xx_astc_texture.h
    include/xxfclib/formats/image/pkm_texture/xx_pkm_texture.h
    include/xxfclib/formats/image/basis_texture/xx_basis_texture.h
    include/xxfclib/formats/image/openctm_mesh/xx_openctm_mesh.h
    include/xxfclib/formats/font/font_bdf/xx_font_bdf.h
    include/xxfclib/formats/font/font_pcf/xx_font_pcf.h
    include/xxfclib/formats/font/font_psf/xx_font_psf.h
    include/xxfclib/formats/font/font_windows_fnt/xx_font_windows_fnt.h
    include/xxfclib/formats/font/tex_tfm/xx_tex_tfm.h
    include/xxfclib/formats/font/tex_pk/xx_tex_pk.h
    include/xxfclib/formats/document/tex_dvi/xx_tex_dvi.h
    include/xxfclib/formats/image/netpbm_pfm/xx_netpbm_pfm.h
    include/xxfclib/formats/audio/steinberg_vst3preset/xx_steinberg_vst3preset.h
    include/xxfclib/formats/font/font_bmfont/xx_font_bmfont.h
    include/xxfclib/formats/font/processing_vlw/xx_processing_vlw.h
    include/xxfclib/formats/audio/snes_spc/xx_snes_spc.h
    include/xxfclib/formats/audio/gameboy_gbs/xx_gameboy_gbs.h
    include/xxfclib/formats/audio/sega_sgc/xx_sega_sgc.h
    include/xxfclib/formats/audio/s98_log/xx_s98_log.h
    include/xxfclib/formats/audio/atari_sap/xx_atari_sap.h
    include/xxfclib/formats/audio/sc68_music/xx_sc68_music.h
    include/xxfclib/formats/filesystem/zx_spectrum_pzx/xx_zx_spectrum_pzx.h
    include/xxfclib/formats/filesystem/acorn_uef/xx_acorn_uef.h
    include/xxfclib/formats/game/nintendo_unif/xx_nintendo_unif.h
    include/xxfclib/formats/game/nintendo_fds/xx_nintendo_fds.h
    include/xxfclib/formats/science/ucsc_bigwig/xx_ucsc_bigwig.h
    include/xxfclib/formats/science/ucsc_bigbed/xx_ucsc_bigbed.h
    include/xxfclib/formats/science/phylo_nexus/xx_phylo_nexus.h
    include/xxfclib/formats/science/phylo_newick/xx_phylo_newick.h
    include/xxfclib/formats/database/sqlite_rollback_journal/xx_sqlite_rollback_journal.h
    include/xxfclib/formats/science/neuroscan_cnt/xx_neuroscan_cnt.h
    include/xxfclib/formats/science/axona_tetrode/xx_axona_tetrode.h
    include/xxfclib/formats/other/python_pickle/xx_python_pickle.h
    include/xxfclib/formats/science/inivation_aedat/xx_inivation_aedat.h
    include/xxfclib/formats/executable/python_marshal/xx_python_marshal.h
    include/xxfclib/formats/filesystem/vice_x64/xx_vice_x64.h
    include/xxfclib/formats/other/vice_snapshot/xx_vice_snapshot.h
    include/xxfclib/formats/filesystem/commodore_g64/xx_commodore_g64.h
    include/xxfclib/formats/filesystem/commodore_p64/xx_commodore_p64.h
    include/xxfclib/formats/filesystem/commodore_tap/xx_commodore_tap.h
    include/xxfclib/formats/filesystem/zx_spectrum_tzx/xx_zx_spectrum_tzx.h
    include/xxfclib/formats/game/zx_spectrum_szx/xx_zx_spectrum_szx.h
    include/xxfclib/formats/filesystem/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.h
    include/xxfclib/formats/filesystem/atari_st_msa/xx_atari_st_msa.h
    include/xxfclib/formats/filesystem/supercard_scp/xx_supercard_scp.h
    include/xxfclib/formats/filesystem/apple_woz/xx_apple_woz.h
    include/xxfclib/formats/audio/nintendo_nsf/xx_nintendo_nsf.h
    include/xxfclib/formats/audio/vgm_log/xx_vgm_log.h
    include/xxfclib/formats/audio/psid_sid/xx_psid_sid.h
    include/xxfclib/formats/audio/hes_sound/xx_hes_sound.h
    include/xxfclib/formats/audio/audio_dolby_ac3/xx_audio_dolby_ac3.h
    include/xxfclib/formats/audio/audio_mpeg_mp3/xx_audio_mpeg_mp3.h
    include/xxfclib/formats/audio/audio_aac_adts/xx_audio_aac_adts.h
    include/xxfclib/formats/audio/audio_monkeys_ape/xx_audio_monkeys_ape.h
    include/xxfclib/formats/video/mpeg_transport_stream/xx_mpeg_transport_stream.h
    include/xxfclib/formats/video/mpeg_program_stream/xx_mpeg_program_stream.h
    include/xxfclib/formats/video/realmedia_rm/xx_realmedia_rm.h
    include/xxfclib/formats/video/idtech_roq/xx_idtech_roq.h
    include/xxfclib/formats/video/rad_bink/xx_rad_bink.h
    include/xxfclib/formats/video/rad_smacker/xx_rad_smacker.h
    include/xxfclib/formats/video/scummvm_dxa/xx_scummvm_dxa.h
    include/xxfclib/formats/archive/apple_diags/xx_apple_diags.h
    include/xxfclib/formats/archive/eid/xx_eid.h
    include/xxfclib/formats/installer/natata_ebook/xx_natata_ebook.h
    include/xxfclib/formats/archive/stor_resource/xx_stor_resource.h
    include/xxfclib/formats/archive/installshield_obl/xx_installshield_obl.h
    include/xxfclib/formats/archive/pftw_pkg/xx_pftw_pkg.h
    include/xxfclib/formats/archive/sod_installer/xx_sod_installer.h
    include/xxfclib/formats/archive/starforce_ovl/xx_starforce_ovl.h
    include/xxfclib/formats/archive/self_unstuffer/xx_self_unstuffer.h
    include/xxfclib/formats/archive/secura/xx_secura.h
    include/xxfclib/formats/archive/patchwise/xx_patchwise.h
    include/xxfclib/formats/archive/installjammer/xx_installjammer.h
    include/xxfclib/formats/video/interplay_mve/xx_interplay_mve.h
    include/xxfclib/formats/video/westwood_vqa/xx_westwood_vqa.h
    include/xxfclib/formats/video/autodesk_flic/xx_autodesk_flic.h
    include/xxfclib/formats/game/idtech_md5anim/xx_idtech_md5anim.h
    include/xxfclib/formats/image/stereolithography_stl/xx_stereolithography_stl.h
    include/xxfclib/formats/science/garmin_fit/xx_garmin_fit.h
    include/xxfclib/formats/science/rosbag1/xx_rosbag1.h
    include/xxfclib/formats/other/mcap/xx_mcap.h
    include/xxfclib/formats/science/seismic_sac/xx_seismic_sac.h
    include/xxfclib/formats/science/seismic_seg2/xx_seismic_seg2.h
    include/xxfclib/formats/science/ucsc_twobit/xx_ucsc_twobit.h
    include/xxfclib/formats/science/genomics_bgen/xx_genomics_bgen.h
    include/xxfclib/formats/science/openephys_continuous/xx_openephys_continuous.h
    include/xxfclib/formats/science/mountainsort_mda/xx_mountainsort_mda.h
    include/xxfclib/formats/science/igor_ibw/xx_igor_ibw.h
    include/xxfclib/formats/science/princeton_spe/xx_princeton_spe.h
    include/xxfclib/formats/science/microscopy_spider/xx_microscopy_spider.h
    include/xxfclib/formats/science/wmo_grib/xx_wmo_grib.h
    include/xxfclib/formats/science/wmo_bufr/xx_wmo_bufr.h
    include/xxfclib/formats/image/autocad_dxf/xx_autocad_dxf.h
    include/xxfclib/formats/science/blackrock_nsx/xx_blackrock_nsx.h
    include/xxfclib/formats/science/blackrock_nev/xx_blackrock_nev.h
    include/xxfclib/formats/science/lecroy_trc/xx_lecroy_trc.h
    include/xxfclib/formats/science/tektronix_isf/xx_tektronix_isf.h
    include/xxfclib/formats/audio/ircam_sdif/xx_ircam_sdif.h
    include/xxfclib/formats/audio/tracker_liquid/xx_tracker_liquid.h
    include/xxfclib/formats/audio/tracker_dmf/xx_tracker_dmf.h
    include/xxfclib/formats/audio/tracker_ptm/xx_tracker_ptm.h
    include/xxfclib/formats/audio/tracker_ams/xx_tracker_ams.h
    include/xxfclib/formats/audio/tracker_digi/xx_tracker_digi.h
    include/xxfclib/formats/audio/tracker_emod/xx_tracker_emod.h
    include/xxfclib/formats/audio/tracker_mt2/xx_tracker_mt2.h
    include/xxfclib/formats/audio/audio_dsf/xx_audio_dsf.h
    include/xxfclib/formats/audio/audio_dff/xx_audio_dff.h
    include/xxfclib/formats/audio/audio_wave64/xx_audio_wave64.h
    include/xxfclib/formats/audio/audio_adx/xx_audio_adx.h
    include/xxfclib/formats/audio/audio_ast/xx_audio_ast.h
    include/xxfclib/formats/audio/audio_hca/xx_audio_hca.h
    include/xxfclib/formats/audio/iff_8svx/xx_iff_8svx.h
    include/xxfclib/formats/audio/audio_wavpack/xx_audio_wavpack.h
    include/xxfclib/formats/image/blender_blend/xx_blender_blend.h
    include/xxfclib/formats/image/autodesk_fbx/xx_autodesk_fbx.h
    include/xxfclib/formats/image/autodesk_3ds/xx_autodesk_3ds.h
    include/xxfclib/formats/image/lightwave_lwo2/xx_lightwave_lwo2.h
    include/xxfclib/formats/image/lightwave_mdd/xx_lightwave_mdd.h
    include/xxfclib/formats/game/sony_psp_pbp/xx_sony_psp_pbp.h
    include/xxfclib/formats/video/flash_video_flv/xx_flash_video_flv.h
    include/xxfclib/formats/game/nintendo_n64_rom/xx_nintendo_n64_rom.h
    include/xxfclib/formats/game/nintendo_gb_rom/xx_nintendo_gb_rom.h
    include/xxfclib/formats/game/nintendo_gba_rom/xx_nintendo_gba_rom.h
    include/xxfclib/formats/executable/menuet/xx_menuet.h
    include/xxfclib/formats/game/msx_rom/xx_msx_rom.h
    include/xxfclib/formats/executable/plan9_exec/xx_plan9_exec.h
    include/xxfclib/formats/game/sega_sms_rom/xx_sega_sms_rom.h
    include/xxfclib/formats/game/tic80/xx_tic80.h
    include/xxfclib/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom.h
    include/xxfclib/formats/image/spring_s3o/xx_spring_s3o.h
    include/xxfclib/formats/game/xna_xnb/xx_xna_xnb.h
    include/xxfclib/formats/executable/lua_bytecode51/xx_lua_bytecode51.h
    include/xxfclib/formats/game/quake_md5mesh/xx_quake_md5mesh.h
    include/xxfclib/formats/audio/tracker_mod/xx_tracker_mod.h
    include/xxfclib/formats/audio/tracker_far/xx_tracker_far.h
    include/xxfclib/formats/audio/tracker_mdl/xx_tracker_mdl.h
    include/xxfclib/formats/audio/tracker_gdm/xx_tracker_gdm.h
    include/xxfclib/formats/audio/tracker_dbm/xx_tracker_dbm.h
    include/xxfclib/formats/audio/tracker_med/xx_tracker_med.h
    include/xxfclib/formats/audio/tracker_imf/xx_tracker_imf.h
    include/xxfclib/formats/audio/tracker_amf/xx_tracker_amf.h
    include/xxfclib/formats/audio/tracker_psm/xx_tracker_psm.h
    include/xxfclib/formats/audio/steinberg_fxb/xx_steinberg_fxb.h
    include/xxfclib/formats/science/astronomy_ser/xx_astronomy_ser.h
    include/xxfclib/formats/science/photontiming_ptu/xx_photontiming_ptu.h
    include/xxfclib/formats/science/photontiming_phu/xx_photontiming_phu.h
    include/xxfclib/formats/science/charmm_dcd/xx_charmm_dcd.h
    include/xxfclib/formats/science/gromacs_trr/xx_gromacs_trr.h
    include/xxfclib/formats/science/microscopy_ics/xx_microscopy_ics.h
    include/xxfclib/formats/science/tecplot_plt/xx_tecplot_plt.h
    include/xxfclib/formats/image/fujifilm_raf/xx_fujifilm_raf.h
    include/xxfclib/formats/image/sigma_x3f/xx_sigma_x3f.h
    include/xxfclib/formats/image/minolta_mrw/xx_minolta_mrw.h
    include/xxfclib/formats/archive/sfx_imp/xx_sfx_imp.h
    include/xxfclib/formats/installer/sfx_red/xx_sfx_red.h
    include/xxfclib/formats/archive/sfx_ha/xx_sfx_ha.h
    include/xxfclib/formats/archive/sfx_lzx/xx_sfx_lzx.h
    include/xxfclib/formats/archive/sfx_sqx/xx_sfx_sqx.h
    include/xxfclib/formats/archive/sfx_ain/xx_sfx_ain.h
    include/xxfclib/formats/archive/sfx_hap/xx_sfx_hap.h
    include/xxfclib/formats/archive/sfx_zoo/xx_sfx_zoo.h
    include/xxfclib/formats/archive/sfx_cazip/xx_sfx_cazip.h
    include/xxfclib/formats/archive/sfx_tgcf/xx_sfx_tgcf.h
    include/xxfclib/formats/archive/sfx_starkit/xx_sfx_starkit.h
    include/xxfclib/formats/archive/sfx_alz/xx_sfx_alz.h
    include/xxfclib/formats/document/sfx_chm/xx_sfx_chm.h
    include/xxfclib/formats/archive/egg/xx_egg.h
    include/xxfclib/formats/archive/nufx/xx_nufx.h
    include/xxfclib/formats/executable/nintendo_dol/xx_nintendo_dol.h
    include/xxfclib/formats/game/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.h
    include/xxfclib/formats/game/nintendo_j3d_btk/xx_nintendo_j3d_btk.h
    include/xxfclib/formats/audio/nintendo_brstm/xx_nintendo_brstm.h
    include/xxfclib/formats/audio/nintendo_brwav/xx_nintendo_brwav.h
    include/xxfclib/formats/game/nintendo_brlyt/xx_nintendo_brlyt.h
    include/xxfclib/formats/game/nintendo_brlan/xx_nintendo_brlan.h
    include/xxfclib/formats/game/nintendo_bfsha/xx_nintendo_bfsha.h
    include/xxfclib/formats/video/cri_usm/xx_cri_usm.h
    include/xxfclib/formats/game/cri_utf/xx_cri_utf.h
    include/xxfclib/formats/game/idtech_iqm/xx_idtech_iqm.h
    include/xxfclib/formats/game/unreal_psk/xx_unreal_psk.h
    include/xxfclib/formats/game/unreal_psa/xx_unreal_psa.h
    include/xxfclib/formats/image/torque_dts/xx_torque_dts.h
    include/xxfclib/formats/image/magicavoxel_vox/xx_magicavoxel_vox.h
    include/xxfclib/formats/audio/audio_au/xx_audio_au.h
    include/xxfclib/formats/audio/creative_voc/xx_creative_voc.h
    include/xxfclib/formats/audio/tracker_xm/xx_tracker_xm.h
    include/xxfclib/formats/audio/tracker_s3m/xx_tracker_s3m.h
    include/xxfclib/formats/audio/tracker_it/xx_tracker_it.h
    include/xxfclib/formats/audio/tracker_mtm/xx_tracker_mtm.h
    include/xxfclib/formats/audio/tracker_stm/xx_tracker_stm.h
    include/xxfclib/formats/audio/tracker_669/xx_tracker_669.h
    include/xxfclib/formats/audio/tracker_ult/xx_tracker_ult.h
    include/xxfclib/formats/audio/tracker_okt/xx_tracker_okt.h
    include/xxfclib/formats/science/nifti2/xx_nifti2.h
    include/xxfclib/formats/science/lidar_las/xx_lidar_las.h
    include/xxfclib/formats/science/esri_shp/xx_esri_shp.h
    include/xxfclib/formats/image/polygon_ply/xx_polygon_ply.h
    include/xxfclib/formats/image/pointcloud_pcd/xx_pointcloud_pcd.h
    include/xxfclib/formats/science/matlab_mat4/xx_matlab_mat4.h
    include/xxfclib/formats/science/seismic_segy/xx_seismic_segy.h
    include/xxfclib/formats/science/biomedical_bdf/xx_biomedical_bdf.h
    include/xxfclib/formats/executable/erlang_beam/xx_erlang_beam.h
    include/xxfclib/formats/executable/java_jmod/xx_java_jmod.h
    include/xxfclib/formats/archive/sfx_arcv2/xx_sfx_arcv2.h
    include/xxfclib/formats/archive/sfx_chz/xx_sfx_chz.h
    include/xxfclib/formats/archive/sfx_szdd/xx_sfx_szdd.h
    include/xxfclib/formats/game/sfx_mpq/xx_sfx_mpq.h
    include/xxfclib/formats/archive/sfx_swag/xx_sfx_swag.h
    include/xxfclib/formats/installer/sfx_zpak/xx_sfx_zpak.h
    include/xxfclib/formats/archive/sfx_diskexpress/xx_sfx_diskexpress.h
    include/xxfclib/formats/archive/sfx_bzip2/xx_sfx_bzip2.h
    include/xxfclib/formats/archive/sfx_gzip/xx_sfx_gzip.h
    include/xxfclib/formats/archive/sfx_tar/xx_sfx_tar.h
    include/xxfclib/formats/archive/sfx_cab/xx_sfx_cab.h
    include/xxfclib/formats/archive/pmarc_sfx/xx_pmarc_sfx.h
    include/xxfclib/formats/archive/sfx_7zip/xx_sfx_7zip.h
    include/xxfclib/formats/archive/sfx_ace/xx_sfx_ace.h
    include/xxfclib/formats/archive/sfx_zipcentral/xx_sfx_zipcentral.h
    include/xxfclib/formats/executable/sony_psx_exe/xx_sony_psx_exe.h
    include/xxfclib/formats/audio/sony_psf/xx_sony_psf.h
    include/xxfclib/formats/filesystem/xbox_xdvdfs/xx_xbox_xdvdfs.h
    include/xxfclib/formats/game/nintendo_wbfs/xx_nintendo_wbfs.h
    include/xxfclib/formats/image/godot_ctex/xx_godot_ctex.h
    include/xxfclib/formats/game/unity_serialized/xx_unity_serialized.h
    include/xxfclib/formats/game/idtech_mdl/xx_idtech_mdl.h
    include/xxfclib/formats/game/valve_studio_mdl/xx_valve_studio_mdl.h
    include/xxfclib/formats/image/blitz3d_b3d/xx_blitz3d_b3d.h
    include/xxfclib/formats/image/milkshape_ms3d/xx_milkshape_ms3d.h
    include/xxfclib/formats/image/nintendo_bch/xx_nintendo_bch.h
    include/xxfclib/formats/game/nintendo_cgfx/xx_nintendo_cgfx.h
    include/xxfclib/formats/game/nintendo_byaml/xx_nintendo_byaml.h
    include/xxfclib/formats/game/relic_chunky/xx_relic_chunky.h
    include/xxfclib/formats/image/ogre_mesh/xx_ogre_mesh.h
    include/xxfclib/formats/image/adobe_ase/xx_adobe_ase.h
    include/xxfclib/formats/image/adobe_aco/xx_adobe_aco.h
    include/xxfclib/formats/image/gimp_gbr/xx_gimp_gbr.h
    include/xxfclib/formats/image/gimp_gih/xx_gimp_gih.h
    include/xxfclib/formats/image/gimp_pat/xx_gimp_pat.h
    include/xxfclib/formats/image/jbig2/xx_jbig2.h
    include/xxfclib/formats/document/djvu/xx_djvu.h
    include/xxfclib/formats/image/emf/xx_emf.h
    include/xxfclib/formats/image/wmf/xx_wmf.h
    include/xxfclib/formats/image/xfig/xx_xfig.h
    include/xxfclib/formats/science/nifti1/xx_nifti1.h
    include/xxfclib/formats/science/nrrd/xx_nrrd.h
    include/xxfclib/formats/science/mrc/xx_mrc.h
    include/xxfclib/formats/science/metaimage/xx_metaimage.h
    include/xxfclib/formats/science/vtk_legacy/xx_vtk_legacy.h
    include/xxfclib/formats/science/gipl/xx_gipl.h
    include/xxfclib/formats/science/freesurfer_mgh/xx_freesurfer_mgh.h
    include/xxfclib/formats/science/edf/xx_edf.h
    include/xxfclib/formats/science/fcs/xx_fcs.h
    include/xxfclib/formats/other/tensorflow_tfrecord/xx_tensorflow_tfrecord.h
    include/xxfclib/formats/archive/sfx_arc/xx_sfx_arc.h
    include/xxfclib/formats/archive/sfx_arj/xx_sfx_arj.h
    include/xxfclib/formats/archive/sfx_bsn/xx_sfx_bsn.h
    include/xxfclib/formats/archive/sfx_arq/xx_sfx_arq.h
    include/xxfclib/formats/archive/sfx_gxl/xx_sfx_gxl.h
    include/xxfclib/formats/installer/sfx_asymetrix/xx_sfx_asymetrix.h
    include/xxfclib/formats/archive/sfx_rta/xx_sfx_rta.h
    include/xxfclib/formats/installer/sfx_rtpatch/xx_sfx_rtpatch.h
    include/xxfclib/formats/archive/esp_archive/xx_esp_archive.h
    include/xxfclib/formats/archive/sfx_kwaj/xx_sfx_kwaj.h
    include/xxfclib/formats/archive/gemdos_lha/xx_gemdos_lha.h
    include/xxfclib/formats/filesystem/winimage_zip/xx_winimage_zip.h
    include/xxfclib/formats/other/hp3000_wrq/xx_hp3000_wrq.h
    include/xxfclib/formats/other/icu_data_package/xx_icu_data_package.h
    include/xxfclib/formats/archive/sfx_sqz/xx_sfx_sqz.h
    include/xxfclib/formats/audio/nintendo_bfstm/xx_nintendo_bfstm.h
    include/xxfclib/formats/audio/nintendo_bfwav/xx_nintendo_bfwav.h
    include/xxfclib/formats/audio/nintendo_bcwav/xx_nintendo_bcwav.h
    include/xxfclib/formats/game/nintendo_bfres/xx_nintendo_bfres.h
    include/xxfclib/formats/game/nintendo_bflyt/xx_nintendo_bflyt.h
    include/xxfclib/formats/game/nintendo_bclyt/xx_nintendo_bclyt.h
    include/xxfclib/formats/font/nintendo_bfnt/xx_nintendo_bfnt.h
    include/xxfclib/formats/font/nintendo_bcfnt/xx_nintendo_bcfnt.h
    include/xxfclib/formats/executable/nintendo_3dsx/xx_nintendo_3dsx.h
    include/xxfclib/formats/image/sony_tim2/xx_sony_tim2.h
    include/xxfclib/formats/video/sony_pamf/xx_sony_pamf.h
    include/xxfclib/formats/image/sega_gvr/xx_sega_gvr.h
    include/xxfclib/formats/audio/microsoft_xwb/xx_microsoft_xwb.h
    include/xxfclib/formats/audio/microsoft_xsb/xx_microsoft_xsb.h
    include/xxfclib/formats/game/relic_sga/xx_relic_sga.h
    include/xxfclib/formats/image/xpm/xx_xpm.h
    include/xxfclib/formats/image/pcx/xx_pcx.h
    include/xxfclib/formats/image/iff_ilbm/xx_iff_ilbm.h
    include/xxfclib/formats/image/utah_rle/xx_utah_rle.h
    include/xxfclib/formats/image/radiance_hdr/xx_radiance_hdr.h
    include/xxfclib/formats/image/dpx/xx_dpx.h
    include/xxfclib/formats/image/cineon/xx_cineon.h
    include/xxfclib/formats/image/xwd/xx_xwd.h
    include/xxfclib/formats/image/sgi_rgb/xx_sgi_rgb.h
    include/xxfclib/formats/image/aseprite/xx_aseprite.h
    include/xxfclib/formats/science/numpy_npy/xx_numpy_npy.h
    include/xxfclib/formats/science/matlab_mat5/xx_matlab_mat5.h
    include/xxfclib/formats/science/netcdf_classic/xx_netcdf_classic.h
    include/xxfclib/formats/science/hdf4/xx_hdf4.h
    include/xxfclib/formats/database/dbase_dbf/xx_dbase_dbf.h
    include/xxfclib/formats/science/sas_xport/xx_sas_xport.h
    include/xxfclib/formats/science/spss_sav/xx_spss_sav.h
    include/xxfclib/formats/science/stata_dta/xx_stata_dta.h
    include/xxfclib/formats/database/apache_arrow_file/xx_apache_arrow_file.h
    include/xxfclib/formats/database/apache_parquet/xx_apache_parquet.h
    include/xxfclib/formats/installer/makeself/xx_makeself.h
    include/xxfclib/formats/installer/sun_java_binsh/xx_sun_java_binsh.h
    include/xxfclib/formats/installer/installanywhere_unix/xx_installanywhere_unix.h
    include/xxfclib/formats/installer/sfx_packagefortheweb/xx_sfx_packagefortheweb.h
    include/xxfclib/formats/archive/sfx_spis/xx_sfx_spis.h
    include/xxfclib/formats/archive/sfx_lha/xx_sfx_lha.h
    include/xxfclib/formats/other/lmd_container/xx_lmd_container.h
    include/xxfclib/formats/game/totalannihilation_hpi/xx_totalannihilation_hpi.h
    include/xxfclib/formats/game/ravensoft_rff/xx_ravensoft_rff.h
    include/xxfclib/formats/game/terminalreality_pod/xx_terminalreality_pod.h
    include/xxfclib/formats/game/volition_vpp/xx_volition_vpp.h
    include/xxfclib/formats/game/kirikiri_xp3/xx_kirikiri_xp3.h
    include/xxfclib/formats/game/fromsoftware_binder/xx_fromsoftware_binder.h
    include/xxfclib/formats/game/mythic_myp/xx_mythic_myp.h
    include/xxfclib/formats/game/lithtech_rez/xx_lithtech_rez.h
    include/xxfclib/formats/game/nintendo_ncch/xx_nintendo_ncch.h
    include/xxfclib/formats/game/nintendo_ncsd/xx_nintendo_ncsd.h
    include/xxfclib/formats/game/nintendo_cia/xx_nintendo_cia.h
    include/xxfclib/formats/game/nintendo_nds/xx_nintendo_nds.h
    include/xxfclib/formats/game/nintendo_gcm/xx_nintendo_gcm.h
    include/xxfclib/formats/image/nintendo_tpl/xx_nintendo_tpl.h
    include/xxfclib/formats/image/sony_tim/xx_sony_tim.h
    include/xxfclib/formats/audio/sony_vag/xx_sony_vag.h
    include/xxfclib/formats/game/larian_lspk/xx_larian_lspk.h
    include/xxfclib/formats/game/larian_lsf/xx_larian_lsf.h
    include/xxfclib/formats/game/valve_hpak/xx_valve_hpak.h
    include/xxfclib/formats/game/renpy_rpa/xx_renpy_rpa.h
    include/xxfclib/formats/game/unreal_package/xx_unreal_package.h
    include/xxfclib/formats/image/sega_pvr2/xx_sega_pvr2.h
    include/xxfclib/formats/image/nintendo_bntx/xx_nintendo_bntx.h
    include/xxfclib/formats/image/icns/xx_icns.h
    include/xxfclib/formats/image/xcursor/xx_xcursor.h
    include/xxfclib/formats/other/icc/xx_icc.h
    include/xxfclib/formats/image/qoi/xx_qoi.h
    include/xxfclib/formats/image/farbfeld/xx_farbfeld.h
    include/xxfclib/formats/image/pnm/xx_pnm.h
    include/xxfclib/formats/image/tga/xx_tga.h
    include/xxfclib/formats/image/sun_raster/xx_sun_raster.h
    include/xxfclib/formats/science/fits/xx_fits.h
    include/xxfclib/formats/science/dicom/xx_dicom.h
    include/xxfclib/formats/other/pcap/xx_pcap.h
    include/xxfclib/formats/other/btsnoop/xx_btsnoop.h
    include/xxfclib/formats/executable/java_class/xx_java_class.h
    include/xxfclib/formats/font/sfnt_collection/xx_sfnt_collection.h
    include/xxfclib/formats/database/sqlite3/xx_sqlite3.h
    include/xxfclib/formats/database/sqlite_wal/xx_sqlite_wal.h
    include/xxfclib/formats/other/avro_object/xx_avro_object.h
    include/xxfclib/formats/image/glb/xx_glb.h
    include/xxfclib/formats/executable/spirv/xx_spirv.h
    include/xxfclib/formats/installer/crx/xx_crx.h
    include/xxfclib/formats/game/bethesda_bsa/xx_bethesda_bsa.h
    include/xxfclib/formats/game/bethesda_ba2/xx_bethesda_ba2.h
    include/xxfclib/formats/game/unityfs/xx_unityfs.h
    include/xxfclib/formats/game/bioware_biff/xx_bioware_biff.h
    include/xxfclib/formats/game/bioware_erf/xx_bioware_erf.h
    include/xxfclib/formats/game/bioware_rim/xx_bioware_rim.h
    include/xxfclib/formats/game/lucas_lab/xx_lucas_lab.h
    include/xxfclib/formats/game/lucas_bun/xx_lucas_bun.h
    include/xxfclib/formats/game/idtech_bsp/xx_idtech_bsp.h
    include/xxfclib/formats/game/valve_bsp/xx_valve_bsp.h
    include/xxfclib/formats/game/idtech_md2/xx_idtech_md2.h
    include/xxfclib/formats/game/idtech_md3/xx_idtech_md3.h
    include/xxfclib/formats/game/idtech_qvm/xx_idtech_qvm.h
    include/xxfclib/formats/game/mohawk_mhk/xx_mohawk_mhk.h
    include/xxfclib/formats/game/quake_sprite/xx_quake_sprite.h
    include/xxfclib/formats/game/nintendo_narc/xx_nintendo_narc.h
    include/xxfclib/formats/game/nintendo_sarc/xx_nintendo_sarc.h
    include/xxfclib/formats/game/nintendo_pfs0/xx_nintendo_pfs0.h
    include/xxfclib/formats/game/nintendo_hfs0/xx_nintendo_hfs0.h
    include/xxfclib/formats/game/nintendo_brres/xx_nintendo_brres.h
    include/xxfclib/formats/audio/nintendo_bcstm/xx_nintendo_bcstm.h
    include/xxfclib/formats/audio/nintendo_bfsar/xx_nintendo_bfsar.h
    include/xxfclib/formats/audio/nintendo_bcsar/xx_nintendo_bcsar.h
    include/xxfclib/formats/game/sony_psarc/xx_sony_psarc.h
    include/xxfclib/formats/image/ktx/xx_ktx.h
    include/xxfclib/formats/image/ktx2/xx_ktx2.h
    include/xxfclib/formats/image/dds/xx_dds.h
    include/xxfclib/formats/image/pvr/xx_pvr.h
    include/xxfclib/formats/image/valve_vtf/xx_valve_vtf.h
    include/xxfclib/formats/executable/xbox_xbe/xx_xbox_xbe.h
    include/xxfclib/formats/audio/flac/xx_flac.h
    include/xxfclib/formats/audio/ogg/xx_ogg.h
    include/xxfclib/formats/video/mp4/xx_mp4.h
    include/xxfclib/formats/video/matroska/xx_matroska.h
    include/xxfclib/formats/audio/aiff/xx_aiff.h
    include/xxfclib/formats/audio/caf/xx_caf.h
    include/xxfclib/formats/image/photoshop_psd/xx_photoshop_psd.h
    include/xxfclib/formats/image/tiff/xx_tiff.h
    include/xxfclib/formats/image/openexr/xx_openexr.h
    include/xxfclib/formats/image/jpeg2000_jp2/xx_jpeg2000_jp2.h
    include/xxfclib/formats/firmware/android_vendor_boot/xx_android_vendor_boot.h
    include/xxfclib/formats/firmware/android_dtbo/xx_android_dtbo.h
    include/xxfclib/formats/firmware/android_vbmeta/xx_android_vbmeta.h
    include/xxfclib/formats/firmware/espressif_image/xx_espressif_image.h
    include/xxfclib/formats/executable/wasm/xx_wasm.h
    include/xxfclib/formats/executable/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.h
    include/xxfclib/formats/executable/dotnet_metadata/xx_dotnet_metadata.h
    include/xxfclib/formats/font/sfnt/xx_sfnt.h
    include/xxfclib/formats/font/woff/xx_woff.h
    include/xxfclib/formats/font/woff2/xx_woff2.h
    include/xxfclib/formats/filesystem/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.h
    include/xxfclib/formats/filesystem/adam/xx_adam.h
    include/xxfclib/formats/other/base16/xx_base16.h
    include/xxfclib/formats/filesystem/bondwell_2_disk/xx_bondwell_2_disk.h
    include/xxfclib/formats/filesystem/casio_fz_1_disk/xx_casio_fz_1_disk.h
    include/xxfclib/formats/filesystem/isz/xx_isz.h
    include/xxfclib/formats/filesystem/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.h
    include/xxfclib/formats/filesystem/parallels_hdd/xx_parallels_hdd.h
    include/xxfclib/formats/filesystem/pc_magazine_flp/xx_pc_magazine_flp.h
    include/xxfclib/formats/firmware/pchrom/xx_pchrom.h
    include/xxfclib/formats/other/pem/xx_pem.h
    include/xxfclib/formats/filesystem/prodos/xx_prodos.h
    include/xxfclib/formats/filesystem/qemu_enhanced_disk/xx_qemu_enhanced_disk.h
    include/xxfclib/formats/filesystem/rawcd/xx_rawcd.h
    include/xxfclib/formats/filesystem/rsdos_fs/xx_rsdos_fs.h
    include/xxfclib/formats/game/sar_ns/xx_sar_ns.h
    include/xxfclib/formats/video/swf/xx_swf.h
    include/xxfclib/formats/filesystem/t64/xx_t64.h
    include/xxfclib/formats/archive/uue/xx_uue.h
    include/xxfclib/formats/filesystem/vdi/xx_vdi.h
    include/xxfclib/formats/filesystem/dfsee_imz/xx_dfsee_imz.h
    include/xxfclib/formats/archive/dh/xx_dh.h
    include/xxfclib/formats/archive/djcomp/xx_djcomp.h
    include/xxfclib/formats/image/bmp/xx_bmp.h
    include/xxfclib/formats/image/bmp/xx_bmp_make.h
    include/xxfclib/formats/firmware/cfe/xx_cfe.h
    include/xxfclib/formats/executable/dxbc/xx_dxbc.h
    include/xxfclib/formats/image/gif/xx_gif.h
    include/xxfclib/formats/image/jpeg/xx_jpeg.h
    include/xxfclib/formats/firmware/linuxarm64/xx_linuxarm64.h
    include/xxfclib/formats/firmware/linuxboot/xx_linuxboot.h
    include/xxfclib/formats/firmware/linuxzimage/xx_linuxzimage.h
    include/xxfclib/formats/other/pcapng/xx_pcapng.h
    include/xxfclib/formats/document/pjl/xx_pjl.h
    include/xxfclib/formats/image/png/xx_png.h
    include/xxfclib/formats/other/riff/xx_riff.h
    include/xxfclib/formats/image/svg/xx_svg.h
    include/xxfclib/formats/game/quake_pak/xx_quake_pak.h
    include/xxfclib/formats/game/doom_wad/xx_doom_wad.h
    include/xxfclib/formats/game/quake_wad2/xx_quake_wad2.h
    include/xxfclib/formats/game/halflife_wad3/xx_halflife_wad3.h
    include/xxfclib/formats/game/build_grp/xx_build_grp.h
    include/xxfclib/formats/game/cri_afs/xx_cri_afs.h
    include/xxfclib/formats/audio/cri_awb/xx_cri_awb.h
    include/xxfclib/formats/game/valve_vpk/xx_valve_vpk.h
    include/xxfclib/formats/game/nintendo_u8/xx_nintendo_u8.h
    include/xxfclib/formats/game/nintendo_rarc/xx_nintendo_rarc.h
    include/xxfclib/formats/other/android_ab/xx_android_ab.h
    include/xxfclib/formats/game/nes_rom/xx_nes_rom.h
    include/xxfclib/formats/game/lynx_lnx/xx_lynx_lnx.h
    include/xxfclib/formats/game/commodore_crt/xx_commodore_crt.h
    include/xxfclib/formats/firmware/uf2/xx_uf2.h
    include/xxfclib/formats/image/ico/xx_ico.h
    include/xxfclib/formats/image/ico/xx_ico_make.h
    include/xxfclib/formats/audio/midi/xx_midi.h
    include/xxfclib/formats/installer/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.h
    include/xxfclib/formats/installer/ardi_installer/xx_ardi_installer.h
    include/xxfclib/formats/installer/arni_installer_container/xx_arni_installer_container.h
    include/xxfclib/formats/installer/ej_technologies_install/xx_ej_technologies_install.h
    include/xxfclib/formats/installer/finstall/xx_finstall.h
    include/xxfclib/formats/installer/ghost_installer/xx_ghost_installer.h
    include/xxfclib/formats/installer/ibm_zpak_installer/xx_ibm_zpak_installer.h
    include/xxfclib/formats/installer/ifah_installer/xx_ifah_installer.h
    include/xxfclib/formats/installer/inno_setup/xx_inno_setup.h
    include/xxfclib/formats/installer/installer_vise_windows/xx_installer_vise_windows.h
    include/xxfclib/formats/installer/installshield_12_setup/xx_installshield_12_setup.h
    include/xxfclib/formats/installer/installshield_3/xx_installshield_3.h
    include/xxfclib/formats/installer/installshield_7_setup/xx_installshield_7_setup.h
    include/xxfclib/formats/installer/installshield_7_setup2/xx_installshield_7_setup2.h
    include/xxfclib/formats/installer/installshield_developer/xx_installshield_developer.h
    include/xxfclib/formats/installer/installshield_issetupstream/xx_installshield_issetupstream.h
    include/xxfclib/formats/installer/installshield_multiplatform/xx_installshield_multiplatform.h
    include/xxfclib/formats/installer/installshield_skin/xx_installshield_skin.h
    include/xxfclib/formats/archive/microfox_put/xx_microfox_put.h
    include/xxfclib/formats/installer/o_setup/xx_o_setup.h
    include/xxfclib/formats/installer/pc_install_setup/xx_pc_install_setup.h
    include/xxfclib/formats/executable/pyinstaller_one_executable/xx_pyinstaller_one_executable.h
    include/xxfclib/formats/installer/qsetup_installation_suite/xx_qsetup_installation_suite.h
    include/xxfclib/formats/installer/rtpatch_setup_data/xx_rtpatch_setup_data.h
    include/xxfclib/formats/installer/setup_factory/xx_setup_factory.h
    include/xxfclib/formats/document/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.h
    include/xxfclib/formats/installer/spoon_installer/xx_spoon_installer.h
    include/xxfclib/formats/installer/tarma_installer/xx_tarma_installer.h
    include/xxfclib/formats/filesystem/adf/xx_adf.h
    include/xxfclib/formats/filesystem/apm/xx_apm.h
    include/xxfclib/formats/filesystem/vhdx/xx_vhdx.h
    include/xxfclib/formats/other/base64/xx_base64.h
    include/xxfclib/formats/archive/btoa/xx_btoa.h
    include/xxfclib/formats/filesystem/chd/xx_chd.h
    include/xxfclib/formats/document/chm/xx_chm.h
    include/xxfclib/formats/filesystem/cloop/xx_cloop.h
    include/xxfclib/formats/filesystem/cue/xx_cue.h
    include/xxfclib/formats/firmware/dahuazip/xx_dahuazip.h
    include/xxfclib/formats/firmware/dmsfw/xx_dmsfw.h
    include/xxfclib/formats/filesystem/ewf/xx_ewf.h
    include/xxfclib/formats/game/godot_engine_pck/xx_godot_engine_pck.h
    include/xxfclib/formats/other/gpgsigned/xx_gpgsigned.h
    include/xxfclib/formats/firmware/ihex/xx_ihex.h
    include/xxfclib/formats/archive/kwaj/xx_kwaj.h
    include/xxfclib/formats/archive/lbr/xx_lbr.h
    include/xxfclib/formats/archive/lzfsestream/xx_lzfsestream.h
    include/xxfclib/formats/filesystem/nrg/xx_nrg.h
    include/xxfclib/formats/archive/packit_mac/xx_packit_mac.h
    include/xxfclib/formats/installer/rpm/xx_rpm.h
    include/xxfclib/formats/archive/stuffit5/xx_stuffit5.h
    include/xxfclib/formats/executable/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.h
    include/xxfclib/formats/filesystem/x68000_dim/xx_x68000_dim.h
    include/xxfclib/formats/game/visionaire_studio_vis/xx_visionaire_studio_vis.h
    include/xxfclib/formats/archive/uharc/xx_uharc.h
    include/xxfclib/formats/filesystem/trs_80_jv3/xx_trs_80_jv3.h
    include/xxfclib/formats/filesystem/trs_80_jv1/xx_trs_80_jv1.h
    include/xxfclib/formats/filesystem/t98_next_nfd/xx_t98_next_nfd.h
    include/xxfclib/formats/archive/stuffit_split_file/xx_stuffit_split_file.h
    include/xxfclib/formats/filesystem/rdb/xx_rdb.h
    include/xxfclib/formats/firmware/qnap_nas_firmware/xx_qnap_nas_firmware.h
    include/xxfclib/formats/filesystem/qcow1/xx_qcow1.h
    include/xxfclib/formats/game/nsa/xx_nsa.h
    include/xxfclib/formats/game/ns2/xx_ns2.h
    include/xxfclib/formats/filesystem/nec_pc_98_fdi/xx_nec_pc_98_fdi.h
    include/xxfclib/formats/archive/ms_dos_backup/xx_ms_dos_backup.h
    include/xxfclib/formats/filesystem/hxc_stream_hfe/xx_hxc_stream_hfe.h
    include/xxfclib/formats/filesystem/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.h
    include/xxfclib/formats/filesystem/apple_sparse_bundle/xx_apple_sparse_bundle.h
    include/xxfclib/formats/filesystem/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.h
    include/xxfclib/formats/archive/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.h
)

set(XXFCLIB_SOURCES
    ${XXFC_CODEPAGE_SOURCES}
    src/formats/installer/mozilla_mar/xx_mozilla_mar.c
    src/formats/installer/mozilla_mar/xx_mozilla_mar_extractor.c
    src/formats/game/westwood_pak/xx_westwood_pak.c
    src/formats/game/westwood_pak/xx_westwood_pak_extractor.c
    src/formats/filesystem/fatx/xx_fatx.c
    src/formats/filesystem/fatx/xx_fatx_extractor.c
    src/formats/audio/soundfont2/xx_soundfont2.c
    src/formats/audio/soundfont2/xx_soundfont2_extractor.c
    src/formats/video/ivf/xx_ivf.c
    src/formats/video/ivf/xx_ivf_extractor.c
    src/formats/image/windows_ani/xx_windows_ani.c
    src/formats/image/windows_ani/xx_windows_ani_extractor.c
    src/formats/audio/interplay_acm/xx_interplay_acm.c
    src/formats/audio/interplay_acm/xx_interplay_acm_extractor.c
    src/formats/audio/cri_ahx/xx_cri_ahx.c
    src/formats/audio/cri_ahx/xx_cri_ahx_extractor.c
    src/formats/video/adobe_director_cxt/xx_adobe_director_cxt.c
    src/formats/video/adobe_director_cxt/xx_adobe_director_cxt_extractor.c
    src/formats/audio/olympus_dss/xx_olympus_dss.c
    src/formats/audio/olympus_dss/xx_olympus_dss_extractor.c
    src/formats/audio/ea_exa/xx_ea_exa.c
    src/formats/audio/ea_exa/xx_ea_exa_extractor.c
    src/formats/audio/audio_nitro_strm/xx_audio_nitro_strm.c
    src/formats/audio/audio_nitro_strm/xx_audio_nitro_strm_extractor.c
    src/formats/audio/audio_wwise_wem/xx_audio_wwise_wem.c
    src/formats/audio/audio_wwise_wem/xx_audio_wwise_wem_extractor.c
    src/formats/game/audio_scumm_sou/xx_audio_scumm_sou.c
    src/formats/game/audio_scumm_sou/xx_audio_scumm_sou_extractor.c
    src/formats/audio/audio_riff_ima/xx_audio_riff_ima.c
    src/formats/audio/audio_riff_ima/xx_audio_riff_ima_extractor.c
    src/formats/audio/hmi_midi/xx_hmi_midi.c
    src/formats/audio/hmi_midi/xx_hmi_midi_extractor.c
    src/formats/audio/ensoniq_paf/xx_ensoniq_paf.c
    src/formats/audio/ensoniq_paf/xx_ensoniq_paf_extractor.c
    src/formats/audio/abylight_strm/xx_abylight_strm.c
    src/formats/audio/abylight_strm/xx_abylight_strm_extractor.c
    src/formats/audio/lego_alp/xx_lego_alp.c
    src/formats/audio/lego_alp/xx_lego_alp_extractor.c
    src/formats/audio/audio_pvf/xx_audio_pvf.c
    src/formats/audio/audio_pvf/xx_audio_pvf_extractor.c
    src/formats/audio/audio_rifx_wave/xx_audio_rifx_wave.c
    src/formats/audio/audio_rifx_wave/xx_audio_rifx_wave_extractor.c
    src/formats/audio/audio_shockwave_swa/xx_audio_shockwave_swa.c
    src/formats/audio/audio_shockwave_swa/xx_audio_shockwave_swa_extractor.c
    src/formats/audio/die_music/xx_die_music.c
    src/formats/filesystem/acorn_adfs/xx_acorn_adfs.c
    src/formats/filesystem/aaruformat/xx_aaruformat.c
    src/formats/game/gdi/xx_gdi.c
    src/formats/filesystem/mds/xx_mds.c
    src/formats/filesystem/cbm_d64/xx_cbm_d64.c
    src/formats/filesystem/cbm_d71/xx_cbm_d71.c
    src/formats/filesystem/cbm_d81/xx_cbm_d81.c
    src/formats/filesystem/cbm_d8x/xx_cbm_d8x.c
    src/formats/filesystem/cbm_d67/xx_cbm_d67.c
    src/formats/filesystem/cbm_d90/xx_cbm_d90.c
    src/formats/filesystem/libdsk_extra/xx_libdsk_extra.c
    src/formats/filesystem/simh_disk/xx_simh_disk.c
    src/formats/filesystem/snatchit_cp2/xx_snatchit_cp2.c
    src/formats/filesystem/northstar_nsi/xx_northstar_nsi.c
    src/formats/filesystem/thomson_fd/xx_thomson_fd.c
    src/formats/filesystem/cmd_fd/xx_cmd_fd.c
    src/formats/filesystem/pce_pri/xx_pce_pri.c
    src/formats/filesystem/pce_pfi/xx_pce_pfi.c
    src/formats/filesystem/pce_pfdc/xx_pce_pfdc.c
    src/formats/filesystem/pce_pbi/xx_pce_pbi.c
    src/formats/filesystem/pce_pbit/xx_pce_pbit.c
    src/formats/filesystem/pce_tc/xx_pce_tc.c
    src/formats/filesystem/pce_anadisk/xx_pce_anadisk.c
    src/formats/filesystem/pce_xdf/xx_pce_xdf.c
    src/formats/filesystem/os9_rbf/xx_os9_rbf.c
    src/formats/filesystem/ldbs/xx_ldbs.c
    src/formats/filesystem/ldbst/xx_ldbst.c
    src/formats/filesystem/ccd/xx_ccd.c
    src/formats/filesystem/cdrdao_toc/xx_cdrdao_toc.c
    src/formats/filesystem/diskcopy42/xx_diskcopy42.c
    src/formats/filesystem/acorn_dfs/xx_acorn_dfs.c
    src/formats/filesystem/fdcopy_cfi/xx_fdcopy_cfi.c
    src/formats/filesystem/atari_dos2/xx_atari_dos2.c
    src/formats/filesystem/apridisk/xx_apridisk.c
    src/formats/filesystem/ti99_dsk/xx_ti99_dsk.c
    src/formats/filesystem/apple_dos32/xx_apple_dos32.c
    src/formats/filesystem/apple_dos33_32/xx_apple_dos33_32.c
    src/formats/archive/bytekiller/xx_bytekiller.c
    src/formats/filesystem/partimage/xx_partimage.c
    src/formats/game/qlie_pack/xx_qlie_pack.c
    src/formats/filesystem/hfsplus/xx_hfsplus.c
    src/formats/filesystem/hfsplus/xx_hfsplus_unicode.h
    src/formats/filesystem/hfsplus/xx_hfsplus_hostfold.h
    src/formats/filesystem/xva/xx_xva.c
    src/formats/filesystem/hfs/xx_hfs.c
    src/formats/game/nexas_pac/xx_nexas_pac.c
    src/formats/game/nitroplus_npa/xx_nitroplus_npa.c
    src/formats/filesystem/cpm/xx_cpm.c
    src/formats/filesystem/cpm/xx_cpm_presets.c
    src/formats/filesystem/ufs1/xx_ufs1.c
    src/formats/filesystem/apple_dos33/xx_apple_dos33.c
    src/formats/game/malie_lib/xx_malie_lib.c
    src/formats/filesystem/dmk/xx_dmk.c
    src/formats/filesystem/mfs/xx_mfs.c
    src/formats/game/catsystem_kif/xx_catsystem_kif.c
    src/formats/game/catsystem_kif/xx_kif_crypto_private.h
    src/formats/filesystem/apple_pascal/xx_apple_pascal.c
    src/formats/game/nitroplus_npk2/xx_nitroplus_npk2.c
    src/formats/filesystem/thomson_sap/xx_thomson_sap.c
    src/formats/filesystem/anex86_hdi/xx_anex86_hdi.c
    src/formats/filesystem/exfat/xx_exfat.c
    src/formats/game/majiro/xx_majiro.c
    src/formats/game/wux/xx_wux.c
    src/formats/game/wux/xx_disk_containers_native.h
    src/formats/filesystem/sdi/xx_sdi.c
    src/formats/filesystem/nhd/xx_nhd.c
    src/formats/filesystem/virtual98/xx_virtual98.c
    src/algo/bzip2/xx_bzip2_random.h
    src/formats/archive/lpak/xx_lpak.c
    src/formats/archive/freeze/xx_freeze.c
    src/formats/archive/freeze/xx_freeze_codec.c
    src/formats/archive/bzip1/xx_bzip1.c
    src/formats/archive/bzip1/xx_bzip1_codec.c
    src/formats/archive/bsn/xx_bsn_codec.c
    src/formats/archive/ppmd/xx_ppmd_varh.c
    src/formats/archive/ppmd/xx_ppmd_vari.c
    src/formats/xx_format_additional.c
    src/formats/filesystem/acorn_atom_disk/xx_acorn_atom_disk.c
    src/formats/filesystem/apollo_afd/xx_apollo_afd.c
    src/formats/archive/bga/xx_bga.c
    src/formats/game/bgi/xx_bgi.c
    src/formats/game/bgi2/xx_bgi2.c
    src/formats/archive/binscii/xx_binscii.c
    src/formats/filesystem/blindwrite_5_6_image/xx_blindwrite_5_6_image.c
    src/formats/filesystem/btrfs_stream/xx_btrfs_stream.c
    src/formats/filesystem/camputers_lynx_ldf/xx_camputers_lynx_ldf.c
    src/formats/game/cpk/xx_cpk.c
    src/formats/game/crt/xx_crt.c
    src/formats/firmware/d_link_alpha_encimg_v2/xx_d_link_alpha_encimg_v2.c
    src/formats/firmware/d_link_fpkg_cpkg/xx_d_link_fpkg_cpkg.c
    src/formats/filesystem/daemon_tools_mdx/xx_daemon_tools_mdx.c
    src/formats/filesystem/dart/xx_dart.c
    src/formats/filesystem/ddd/xx_ddd.c
    src/formats/archive/diet_compression/xx_diet_compression.c
    src/formats/game/dxa/xx_dxa.c
    src/formats/image/ea_fsh/xx_ea_fsh.c
    src/formats/filesystem/ewf2_ex01/xx_ewf2_ex01.c
    src/formats/filesystem/ewf2_lx01/xx_ewf2_lx01.c
    src/formats/filesystem/ewf_l01/xx_ewf_l01.c
    src/formats/audio/fmod_sample_bank/xx_fmod_sample_bank.c
    src/formats/filesystem/gbi/xx_gbi.c
    src/formats/game/goldsrc_bsp/xx_goldsrc_bsp.c
    src/formats/filesystem/hsf/xx_hsf.c
    src/formats/firmware/htc_nbh_rom_image/xx_htc_nbh_rom_image.c
    src/formats/filesystem/hxc_hfe_extended/xx_hxc_hfe_extended.c
    src/formats/filesystem/hxc_hfe_hddd_a2_variant/xx_hxc_hfe_hddd_a2_variant.c
    src/formats/filesystem/hxc_hfe_v3/xx_hxc_hfe_v3.c
    src/formats/document/hxs/xx_hxs.c
    src/formats/filesystem/jffs2_old/xx_jffs2_old.c
    src/formats/filesystem/jvc/xx_jvc.c
    src/formats/archive/kgb_archiver/xx_kgb_archiver.c
    src/formats/filesystem/kryoflux_stream/xx_kryoflux_stream.c
    src/formats/game/livemaker/xx_livemaker.c
    src/formats/archive/lzma86/xx_lzma86.c
    src/formats/game/maxis_far_archive/xx_maxis_far_archive.c
    src/formats/filesystem/mgt/xx_mgt.c
    src/formats/filesystem/minix/xx_minix.c
    src/formats/filesystem/moof/xx_moof.c
    src/formats/archive/ms_dos_backup2/xx_ms_dos_backup2.c
    src/formats/executable/mub/xx_mub.c
    src/formats/game/noa/xx_noa.c
    src/formats/document/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.c
    src/formats/filesystem/partclone_image/xx_partclone_image.c
    src/formats/archive/ppmd/xx_ppmd.c
    src/formats/archive/quoted_printable_encoded_fil/xx_quoted_printable_encoded_fil.c
    src/formats/image/risc_os_sprite/xx_risc_os_sprite.c
    src/formats/game/rpa/xx_rpa.c
    src/formats/game/rpg_maker_rgssad/xx_rpg_maker_rgssad.c
    src/formats/audio/sfark_compressed_soundfont/xx_sfark_compressed_soundfont.c
    src/formats/installer/sis/xx_sis.c
    src/formats/filesystem/spectrum_udi/xx_spectrum_udi.c
    src/formats/filesystem/squashfs_sqlz/xx_squashfs_sqlz.c
    src/formats/game/stos_memory_bank/xx_stos_memory_bank.c
    src/formats/archive/stuffitx/xx_stuffitx.c
    src/formats/filesystem/sufs/xx_sufs.c
    src/formats/filesystem/sunvtoc/xx_sunvtoc.c
    src/formats/game/telltale_ttarch/xx_telltale_ttarch.c
    src/formats/filesystem/ufs2/xx_ufs2.c
    src/formats/filesystem/uif/xx_uif.c
    src/formats/game/valve_gcf_cache/xx_valve_gcf_cache.c
    src/formats/game/valve_xzp/xx_valve_xzp.c
    src/formats/filesystem/vmdk_cowd_sparse/xx_vmdk_cowd_sparse.c
    src/formats/filesystem/vmdk_sesparse/xx_vmdk_sesparse.c
    src/formats/firmware/xiaomi_hdr1/xx_xiaomi_hdr1.c
    src/formats/firmware/xiaomi_hdr2/xx_xiaomi_hdr2.c
    src/formats/game/xp3/xx_xp3.c
    src/formats/archive/xpk_compressed_file/xx_xpk_compressed_file.c
    src/formats/archive/xpk_compressed_file/xx_xpk_nuke_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_lin_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_lz_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_zeno_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_rake_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_tdcs_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_cyber_native.h
    src/formats/archive/xpk_compressed_file/xx_xpk_rake_table.h
    src/formats/archive/yenc_encoded_file/xx_yenc_encoded_file.c
    src/formats/game/ypf/xx_ypf.c

    # CRT compatibility core. Each group keeps its portable half beside the
    # subsystem it serves and its platform half in that subsystem's platforms/
    # directory: process, I/O and formatting in src/rt, runtime memory
    # primitives in src/memory, and narrow/wide string primitives in
    # src/strings. On Windows the platform halves are Win32-backed or
    # hand-rolled, which lets the /NODEFAULTLIB target link without a CRT.
    src/rt/xx_rt.c
    src/sort/xx_sort.c
    src/sort/platforms/xx_sort_sse2.c
    src/sort/platforms/xx_sort_avx2.c
    src/sort/platforms/xx_sort_neon.c
    src/sort/xx_sort_internal.h
    src/sort/xx_sort_algorithms.inc
    src/rt/xx_rt_fp.c
    src/rt/xx_rt_math.c
    src/buf/xx_buf.c
    src/json/xx_json.c
    src/xml/xx_xml.c
    src/fs/xx_fs.c
    # Shared scan models, signature databases and engine callback interface.
    src/scan/xx_scan.c
    src/scan/xx_scan_db.c
    src/scan/xx_scan_result.c
    src/scan/xx_scan_color.c
    src/sort/xx_sort_scan.c
    src/scan/xx_scan_engine.c
    src/scan/xx_scan_types.c
    src/scan/xx_scan_overlay.c
    src/scan/xx_scan_resources.c
    src/scan/xx_scan_archives.c
    src/sig/xx_sig.c
    # Embedded ECMAScript interpreter.
    src/js/xx_js_deps.c
    src/js/xx_js_engine.c
    src/js/xx_js_lex.c
    src/js/xx_js_parse.c
    src/js/xx_js_interp.c
    src/js/xx_js_value.c
    src/js/xx_js_builtins.c
    src/regex/xx_regex.c
    src/regex/xx_regex_search.c
    src/regex/xx_regex_js.c
    src/regex/xx_regex_js_lex.c
    src/regex/xx_regex_js_strings.c
    src/js/xx_js_bytecode.c
    # Detect It Easy signature execution engine, moved here from cdie. The
    # script API binds the JavaScript interpreter above to the scan driver.
    # xx_die_engine_compat.* carries the handful of cdie runtime
    # primitives whose semantics differ from their xx_* counterparts; the rest
    # alias straight onto xx_rt.
    src/die_engine/xx_die_engine_compat.c
    src/die_engine/xx_die_engine_engine.c
    src/die_engine/xx_die_engine_legacy.c
    src/die_engine/xx_die_engine_scan.c
    src/die_engine/xx_die_engine_api.c
    src/regex/xx_regex_die.c
    # Its binary-format parsers. These answer predicate queries for signature
    # scripts ("is this import present?"), which is a different job from the
    # readers in src/formats/ -- those enumerate structure for extraction and
    # display. The two are not interchangeable; see docs.
    src/die_engine/xx_die_engine_xdisasm.c
    src/formats/executable/pe/xx_pe_inspect.c
    src/formats/executable/pe/xx_pe_unwind_validate.c
    src/formats/executable/pe/xx_pe_import.c
    src/formats/executable/pe/xx_pe_export.c
    src/formats/executable/pe/xx_pe_metadata.c
    src/formats/executable/pe/xx_pe_stream_common.c
    src/formats/executable/dotnet/xx_dotnet.c
    src/formats/executable/dotnet/xx_dotnet_inspect.c
    src/formats/executable/dotnet/platforms/xx_dotnet_us_simd.h
    src/formats/executable/dotnet/platforms/xx_dotnet_us_sse2.c
    src/formats/executable/dotnet/platforms/xx_dotnet_us_avx2.c
    src/formats/executable/dotnet/xx_dotnet_reader.c
    src/formats/executable/dotnet/xx_dotnet_datastructs.c
    src/formats/executable/dotnet/xx_dotnet_extractor.c
    src/formats/installer/apk/xx_apk_inspect.c
    src/formats/document/pdf/xxpdf.c
    src/formats/document/pdf/xxpdf_decode.c
    src/formats/document/pdf/xxpdf_extractor.c
    src/formats/executable/elf/xx_elf_inspect.c
    src/formats/executable/dex/xx_dex_inspect.c
    src/formats/executable/macho/xx_macho_inspect.c
    src/formats/executable/pyc/xx_pyc_extractor.c
    src/formats/executable/pyc/xx_pyc.c
    src/formats/archive/zip/xx_zip_inspect.c
    src/formats/xx_data_signature.c
    src/global/xx_global.c
    src/global/xx_settings_global.c
    src/terminal/xx_terminal.c
    src/terminal/xx_terminal_color.c
    src/settings/xx_settings.c
    src/settings/xx_settings_ini.c
    src/settings/xx_settings_value.c
    src/settings/xx_shortcuts.c
    src/io/xx_io.c
    src/translation/xx_translation.c
    src/translation/xx_translation_io.c
    src/history/xx_history.c
    src/io/xx_io_buffer.c
    src/io/xx_io_snapshot.c
    src/data/xx_byte_transform.c
    src/io/xx_io_file.c
    src/io/xx_io_mem.c
    src/io/xx_io_memory_only.c
    src/io/xx_io_sub.c
    src/io/xx_io_process.c
    src/process/xx_process_structs.c
    src/memory/xx_memory.c
    src/memory/xx_memory_rt.c
    src/memory/platforms/xx_memory_sse2.c
    src/memory/platforms/xx_memory_avx2.c
    src/memory/platforms/xx_memory_neon.c
    src/strings/xx_string.c
    src/strings/xx_strings_scan.c
    src/extractor/xx_extractor.c
    src/data/xx_pd.c
    src/data/xx_data_raw.c
    src/data/xx_data_io.c
    src/global/platforms/xx_global_cpu.c
    src/data/platforms/xx_data_sse2.c
    src/data/platforms/xx_data_avx2.c
    src/data/platforms/xx_data_neon.c
    src/list/xx_list.c
    src/threads/xx_threads.c
    src/threads/xx_thread_pool.c
    src/sort/xx_sort_list.c
    src/var/xx_var.c
    src/algo/crc/xx_crc.c
    src/algo/md5/xx_md5.c
    src/algo/sha/xx_sha.c
    src/algo/sha/xx_sha_simd.c
    src/algo/sha/platforms/xx_sha_neon.c
    src/algo/sha/platforms/xx_sha_arm_crypto.c
    src/algo/adler32/xx_adler32.c
    src/algo/adler32/xx_adler32_io.c
    src/algo/adler32/platforms/xx_adler32_sse2.c
    src/algo/adler32/platforms/xx_adler32_avx2.c
    src/algo/adler32/platforms/xx_adler32_neon.c
    src/algo/adler32/platforms/xx_adler32_arm_dotprod.c
    src/algo/lzfse/xx_lzfse.c
    src/algo/lzfse/xx_lzfse_enc.c
    src/algo/lzfse/xx_lzfse_encode_reference.c
    src/algo/lzfse/xx_lzfse_encode_base_reference.c
    src/algo/lzfse/xx_lzfse_fse_reference.c
    src/algo/lzfse/xx_lzvn_encode_base_reference.c
    src/algo/crc/xx_crc8.c
    src/algo/crc/xx_crc16.c
    src/algo/crc/xx_crc32.c
    src/algo/crc/platforms/xx_crc32_arm_pmull.c
    src/algo/crc/platforms/xx_crc32_arm.c
    src/algo/crc/platforms/xx_crc32c_sse42.c
    src/algo/crc/xx_crc_io.c
    src/algo/crc/xx_crc64.c
    src/algo/crc/platforms/xx_crc64_sse2.c
    src/algo/crc/platforms/xx_crc64_avx2.c
    src/algo/crc/platforms/xx_crc64_vpclmul.c
    src/algo/blake2/xx_blake2.c
    src/algo/entropy/xx_entropy.c
    src/algo/entropy/platforms/xx_entropy_sse2.c
    src/algo/entropy/platforms/xx_entropy_avx2.c
    src/algo/entropy/platforms/xx_entropy_neon.c
    src/algo/kpa/xx_kpa.c
    src/algo/store/xx_store.c
    src/algo/deflate/xx_deflate.c
    src/algo/deflate/xx_zlib_stream.c
    src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
    src/algo/deflate/platforms/xx_deflate_neon.c
    src/algo/bzip2/xx_bzip2.c
    src/algo/bzip2/xx_bzip2_bwt.c
    src/algo/bzip2/xx_bzip2_dec.c
    src/algo/bzip2/xx_bzip2_enc.c
    src/algo/bzip2/xx_bzip2_huffman.c
    src/algo/bzip2/xx_bzip2_mtf.c
    src/algo/lzma/xx_lzma.c
    src/algo/lzma/xx_lzma_dec.c
    src/algo/lzma/xx_lzma_stream.c
    src/algo/lzma/xx_lzma2_filters.c
    src/algo/lzma/xx_lzma_enc.c
    src/algo/lzma/platforms/xx_lzma_sse2.c
    src/algo/lzma/platforms/xx_lzma_avx2.c
    src/algo/lzma/platforms/xx_lzma_neon.c
    src/algo/ppmd7/xx_ppmd7.c
    src/algo/ppmd7/xx_ppmd7_common.c
    src/algo/ppmd7/xx_ppmd7_dec.c
    src/algo/ppmd7/xx_ppmd7_enc.c
    src/algo/ppmd8/xx_ppmd8.c
    src/algo/ppmd8/xx_ppmd8_common.c
    src/algo/ppmd8/xx_ppmd8_dec.c
    src/algo/ppmd8/xx_ppmd8_enc.c
    src/algo/shrink/xx_shrink.c
    src/algo/reduce/xx_reduce.c
    src/algo/implode/xx_implode.c
    src/algo/lz4/xx_lz4.c
    src/algo/lz5/xx_lz5.c
    src/algo/lizard/xx_lizard.c
    src/algo/brotli/xx_brotli.c
    src/algo/brotli/xx_brotli_data.c
    src/algo/zstd/xx_zstd.c
    src/algo/zstd/xx_zstd_dec.c
    src/algo/zipcrypto/xx_zipcrypto.c
    src/algo/aes/xx_aes.c
    src/algo/eid/xx_eid_crypto.c
    src/algo/ripemd256/xx_ripemd256.c
    src/algo/aes/xx_aes_internal.h
    src/algo/aes/platforms/xx_aes_arm_crypto.c
    src/algo/aes_winzip/xx_aes_winzip.c
    src/algo/cmpsc/xx_cmpsc.c
    src/algo/cmpsc/xx_cmpsc_enc.c
    src/algo/packmp3/xx_packmp3.c
    src/algo/wavpack/xx_wavpack.c
    src/algo/wavpack/xx_wavpack_dec.c
    src/algo/winzipjpeg/xx_winzipjpeg.c
    src/algo/ain/xx_ain.c
    src/algo/ampk/xx_ampk.c
    src/algo/arcfs/xx_arcfs_lzw.c
    src/algo/arcv2/xx_arcv2_lzhuf.c
    src/algo/arcv4/xx_arcv4.c
    src/algo/aldus/xx_aldus_lzw.c
    src/algo/aldus/xx_aldus_lzsh.c
    src/algo/dcl/xx_dcl.c
    src/algo/ftcomp/xx_ftcomp_entropy33.c
    src/algo/claylz/xx_claylz.c
    src/algo/corelltec/xx_corelltec.c
    src/algo/genius/xx_genius.c
    src/algo/hap/xx_hap.c
    src/algo/imp/xx_imp.c
    src/algo/irwinpac/xx_irwinpac.c
    src/algo/ivt/xx_ivt.c
    src/algo/kolibrikpack/xx_kolibrikpack.c
    src/algo/lim/xx_lim.c
    src/algo/npack/xx_npack.c
    src/algo/pakleo/xx_pakleo.c
    src/algo/pkt/xx_pkt.c
    src/algo/qda/xx_qda.c
    src/algo/rid/xx_rid.c
    src/algo/rompaq/xx_rompaq.c
    src/algo/tivoli/xx_tivoli.c
    src/algo/ulead/xx_ulead.c
    src/algo/xeditpack/xx_xeditpack.c
    src/algo/zcmp/xx_zcmp.c
    src/algo/zie/xx_zie.c
    src/algo/asymetrix/xx_asymetrix.c
    src/algo/c64wraptor/xx_c64wraptor.c
    src/algo/charc/xx_charc.c
    src/algo/coktellz/xx_coktellz.c
    src/algo/fpak/xx_fpak.c
    src/algo/gamos/xx_gamos.c
    src/algo/gtu/xx_gtu.c
    src/algo/lofi/xx_lofi.c
    src/algo/mi10/xx_mi10.c
    src/algo/stylus/xx_stylus.c
    src/algo/chieflz/xx_chieflz.c
    src/algo/cmp/xx_cmp.c
    src/algo/compactpro/xx_compactpro.c
    src/algo/diskdoubler/xx_diskdoubler.c
    src/algo/ea/xx_ea.c
    src/algo/flslz/xx_flslz.c
    src/algo/ha/xx_ha.c
    src/algo/hzl/xx_hzl.c
    src/algo/lzhuf/xx_lzhuf.c
    src/algo/lzw15v/xx_lzw15v.c
    src/algo/lzwvariants/xx_lzwvariants.c
    src/algo/netwarepack/xx_netwarepack.c
    src/algo/pcommos2/xx_pcommos2.c
    src/algo/qnxbase/xx_qnxbase.c
    src/algo/rtpatch/xx_rtpatch.c
    src/algo/sclsectors/xx_sclsectors.c
    src/algo/sqx/xx_sqx.c
    src/algo/sqz/xx_sqz.c
    src/algo/tps/xx_tps.c
    src/algo/zxzip/xx_zxzip.c
    src/algo/zoom/xx_zoom.c
    src/algo/sfpack/xx_sfpack.c
    src/algo/pcsecure/xx_pcsecure.c
    src/algo/rsvk/xx_rsvk.c
    src/algo/saf/xx_saf.c
    src/algo/sls/xx_sls.c
    src/algo/nid/xx_nid.c
    src/algo/topspeed/xx_topspeed.c
    src/algo/panorama/xx_panorama.c
    src/algo/terse/xx_terse.c
    src/algo/ti99arc/xx_ti99arc.c
    src/algo/vmarc/xx_vmarc.c
    src/algo/wintersoft/xx_wintersoft.c
    src/algo/wpk/xx_wpk.c
    src/algo/zoo/xx_zoo.c
    src/algo/rar/xx_rarx.c
    src/algo/rar/xx_rarx15.c
    src/algo/rar/xx_rarx20.c
    src/algo/rar/xx_rarx29.c
    src/algo/rar/xx_rarx50.c
    src/algo/arj/xx_arj_dec.c
    src/algo/unixpack/xx_unixpack.c
    src/algo/compress/xx_compress.c
    src/algo/mscompress/xx_mscompress.c
    src/algo/ash0/xx_ash0.c
    src/algo/wiilz77/xx_wiilz77.c
    src/algo/lzv1/xx_lzv1.c
    src/algo/oraclesqueeze/xx_oraclesqueeze.c
    src/algo/softronics/xx_softronics.c
    src/algo/gashuff/xx_gashuff.c
    src/algo/huf/xx_huf.c
    src/algo/lzdiet/xx_lzdiet.c
    src/algo/lzpis2/xx_lzpis2.c
    src/algo/lzip/xx_lzip.c
    src/algo/lzma_alone/xx_lzma_alone.c
    src/algo/lzo/xx_lzo.c
    src/algo/lzop/xx_lzop.c
    src/algo/tarx/xx_tarx.c
    src/algo/tarx/xx_tarx2.c
    src/algo/lzh/xx_lzh.c
    src/algo/lzh/xx_lzh5.c
    src/algo/lzx/xx_lzx.c
    src/algo/quantum/xx_quantum.c
    src/formats/xx_format.c
    src/formats/xx_format_base.c
    src/formats/xx_format_datastructs.c
    src/formats/xx_format_handle_types.c
    src/formats/xx_format_unpack_handles.c
    src/formats/xx_format_streams.c
    src/formats/xx_format_catalog.c
    src/formats/xx_format_detect_fast.c
    src/formats/xx_format_extractor.c
    src/formats/xx_memory_map.c
    src/formats/archive/zip/xx_zip.c
    src/formats/archive/zip/xx_zip_legacy_encoder.c
    src/formats/archive/zip/xx_zip_extractor.c
    src/formats/executable/jar/xx_jar.c
    src/formats/executable/jar/xx_jar_extractor.c
    src/formats/installer/apk/xx_apk.c
    src/formats/installer/apk/xx_apk_extractor.c
    src/formats/installer/ipa/xx_ipa.c
    src/formats/installer/ipa/xx_ipa_extractor.c
    src/formats/archive/7zip/xx_7zip.c
    src/formats/archive/7zip/xx_7zip_extractor.c
    src/formats/archive/7zip/xx_7zip_branch.c
    src/formats/archive/7zip/xx_7zip_stream.c
    src/formats/archive/7zip/xx_7zip_writer.c
    src/formats/archive/rar/xx_rar.c
    src/formats/archive/rar/xx_rar_extractor.c
    src/formats/archive/ar/xx_ar.c
    src/formats/archive/ar/xx_ar_extractor.c
    src/formats/archive/bz2/xx_bz2.c
    src/formats/archive/bz2/xx_bz2_extractor.c
    src/formats/archive/gz/xx_gz.c
    src/formats/archive/gz/xx_gz_extractor.c
    src/formats/archive/xz/xx_xz.c
    src/formats/archive/xz/xx_xz_riscv_native.h
    src/formats/archive/xz/xx_xz_writer.c
    src/formats/archive/xz/xx_xz_extractor.c
    src/formats/archive/tar/xx_tar.c
    src/formats/archive/tar/xx_tar_extractor.c
    src/formats/archive/tar_common/xx_tar_common.c
    src/formats/archive/tar_gz/xx_tar_gz.c
    src/formats/archive/tar_gz/xx_tar_gz_extractor.c
    src/formats/installer/npm/xx_npm.c
    src/formats/installer/npm/xx_npm_extractor.c
    src/formats/archive/tar_bz2/xx_tar_bz2.c
    src/formats/archive/tar_bz2/xx_tar_bz2_extractor.c
    src/formats/archive/tar_xz/xx_tar_xz.c
    src/formats/archive/tar_xz/xx_tar_xz_extractor.c
    src/formats/archive/tar_lz4/xx_tar_lz4.c
    src/formats/archive/tar_lz4/xx_tar_lz4_extractor.c
    src/formats/archive/lz4/xx_lz4.c
    src/formats/archive/lz4/xx_lz4_extractor.c
    src/formats/archive/lz5/xx_lz5.c
    src/formats/archive/lz5/xx_lz5_extractor.c
    src/formats/archive/lizard/xx_lizard.c
    src/formats/archive/lizard/xx_lizard_extractor.c
    src/formats/archive/brotli/xx_brotli.c
    src/formats/archive/brotli/xx_brotli_extractor.c
    src/formats/archive/unixpack/xx_unixpack.c
    src/formats/archive/unixpack/xx_unixpack_extractor.c
    src/formats/archive/zlib/xx_zlib.c
    src/formats/archive/zlib/xx_zlib_extractor.c
    src/formats/archive/unixcompress/xx_unixcompress.c
    src/formats/archive/unixcompress/xx_unixcompress_extractor.c
    src/formats/other/gitobject/xx_gitobject.c
    src/formats/other/gitobject/xx_gitobject_extractor.c
    src/formats/archive/mscompress/xx_mscompress.c
    src/formats/archive/mscompress/xx_mscompress_extractor.c
    src/formats/archive/ash0/xx_ash0.c
    src/formats/archive/ash0/xx_ash0_extractor.c
    src/formats/archive/wiilz77/xx_wiilz77.c
    src/formats/archive/wiilz77/xx_wiilz77_extractor.c
    src/formats/archive/lzv1/xx_lzv1.c
    src/formats/archive/lzv1/xx_lzv1_extractor.c
    src/formats/archive/oraclesqueeze/xx_oraclesqueeze.c
    src/formats/archive/oraclesqueeze/xx_oraclesqueeze_extractor.c
    src/formats/archive/softronics/xx_softronics.c
    src/formats/archive/softronics/xx_softronics_extractor.c
    src/formats/archive/logitechcompress/xx_logitechcompress.c
    src/formats/archive/logitechcompress/xx_logitechcompress_extractor.c
    src/formats/archive/dmapacked/xx_dmapacked.c
    src/formats/archive/dmapacked/xx_dmapacked_extractor.c
    src/formats/archive/gashuff/xx_gashuff.c
    src/formats/archive/gashuff/xx_gashuff_extractor.c
    src/formats/archive/huf/xx_huf.c
    src/formats/archive/huf/xx_huf_extractor.c
    src/formats/archive/lzdiet/xx_lzdiet.c
    src/formats/archive/lzdiet/xx_lzdiet_extractor.c
    src/formats/archive/lzpis2/xx_lzpis2.c
    src/formats/archive/lzpis2/xx_lzpis2_extractor.c
    src/formats/archive/zie/xx_zie.c
    src/formats/archive/zie/xx_zie_extractor.c
    src/formats/archive/xeditpack/xx_xeditpack.c
    src/formats/archive/xeditpack/xx_xeditpack_extractor.c
    src/formats/installer/wpk/xx_wpk.c
    src/formats/installer/wpk/xx_wpk_extractor.c
    src/formats/archive/wintersoft/xx_wintersoft.c
    src/formats/archive/wintersoft/xx_wintersoft_extractor.c
    src/formats/archive/vmarc/xx_vmarc.c
    src/formats/archive/vmarc/xx_vmarc_extractor.c
    src/formats/installer/tivoli/xx_tivoli.c
    src/formats/installer/tivoli/xx_tivoli_extractor.c
    src/formats/archive/ti99arc/xx_ti99arc.c
    src/formats/archive/ti99arc/xx_ti99arc_extractor.c
    src/formats/document/stylus/xx_stylus.c
    src/formats/document/stylus/xx_stylus_extractor.c
    src/formats/installer/rtpatch/xx_rtpatch.c
    src/formats/installer/rtpatch/xx_rtpatch_extractor.c
    src/formats/installer/rta/xx_rta.c
    src/formats/installer/rta/xx_rta_extractor.c
    src/formats/firmware/rompaq/xx_rompaq.c
    src/formats/firmware/rompaq/xx_rompaq_extractor.c
    src/formats/installer/rid/xx_rid.c
    src/formats/installer/rid/xx_rid_extractor.c
    src/formats/firmware/qnxbase/xx_qnxbase.c
    src/formats/firmware/qnxbase/xx_qnxbase_extractor.c
    src/formats/archive/qda/xx_qda.c
    src/formats/archive/qda/xx_qda_extractor.c
    src/formats/document/pkt/xx_pkt.c
    src/formats/document/pkt/xx_pkt_extractor.c
    src/formats/filesystem/lofi/xx_lofi.c
    src/formats/filesystem/lofi/xx_lofi_extractor.c
    src/formats/archive/lim/xx_lim.c
    src/formats/archive/lim/xx_lim_extractor.c
    src/formats/archive/kolibrikpack/xx_kolibrikpack.c
    src/formats/archive/kolibrikpack/xx_kolibrikpack_extractor.c
    src/formats/document/ivt/xx_ivt.c
    src/formats/document/ivt/xx_ivt_extractor.c
    src/formats/installer/irwinpac/xx_irwinpac.c
    src/formats/installer/irwinpac/xx_irwinpac_extractor.c
    src/formats/archive/hap/xx_hap.c
    src/formats/archive/hap/xx_hap_extractor.c
    src/formats/archive/compactpro/xx_compactpro.c
    src/formats/archive/compactpro/xx_compactpro_extractor.c
    src/formats/archive/imp/xx_imp.c
    src/formats/archive/imp/xx_imp_extractor.c
    src/formats/archive/sqx/xx_sqx.c
    src/formats/archive/sqx/xx_sqx_extractor.c
    src/formats/archive/zoo/xx_zoo.c
    src/formats/archive/zoo/xx_zoo_extractor.c
    src/formats/archive/terse/xx_terse.c
    src/formats/archive/terse/xx_terse_extractor.c
    src/formats/game/stk/xx_stk.c
    src/formats/game/stk/xx_stk_extractor.c
    src/formats/archive/pcommos2/xx_pcommos2.c
    src/formats/archive/pcommos2/xx_pcommos2_extractor.c
    src/formats/archive/jbf/xx_jbf.c
    src/formats/archive/jbf/xx_jbf_extractor.c
    src/formats/archive/ibmspack/xx_ibmspack.c
    src/formats/archive/ibmspack/xx_ibmspack_extractor.c
    src/formats/installer/gtu/xx_gtu.c
    src/formats/installer/gtu/xx_gtu_extractor.c
    src/formats/archive/glu/xx_glu.c
    src/formats/archive/glu/xx_glu_extractor.c
    src/formats/archive/ztc/xx_ztc.c
    src/formats/archive/ztc/xx_ztc_extractor.c
    src/formats/archive/netwarepacked/xx_netwarepacked.c
    src/formats/archive/netwarepacked/xx_netwarepacked_extractor.c
    src/formats/archive/zpak/xx_zpak.c
    src/formats/archive/zpak/xx_zpak_extractor.c
    src/formats/archive/zcmp/xx_zcmp.c
    src/formats/archive/zcmp/xx_zcmp_extractor.c
    src/formats/filesystem/scl/xx_scl.c
    src/formats/filesystem/scl/xx_scl_extractor.c
    src/formats/archive/pakleo/xx_pakleo.c
    src/formats/archive/pakleo/xx_pakleo_extractor.c
    src/formats/archive/npack/xx_npack.c
    src/formats/archive/npack/xx_npack_extractor.c
    src/formats/archive/mi10/xx_mi10.c
    src/formats/archive/mi10/xx_mi10_extractor.c
    src/formats/archive/lzwd/xx_lzwd.c
    src/formats/archive/lzwd/xx_lzwd_extractor.c
    src/formats/archive/lzhcxp/xx_lzhcxp.c
    src/formats/archive/lzhcxp/xx_lzhcxp_extractor.c
    src/formats/archive/kboom/xx_kboom.c
    src/formats/archive/kboom/xx_kboom_extractor.c
    src/formats/archive/hzl/xx_hzl.c
    src/formats/archive/hzl/xx_hzl_extractor.c
    src/formats/archive/ha/xx_ha.c
    src/formats/archive/ha/xx_ha_extractor.c
    src/formats/archive/genius/xx_genius.c
    src/formats/archive/genius/xx_genius_extractor.c
    src/formats/archive/fls/xx_fls.c
    src/formats/archive/fls/xx_fls_extractor.c
    src/formats/game/earefpack/xx_earefpack.c
    src/formats/game/earefpack/xx_earefpack_extractor.c
    src/formats/archive/ealib/xx_ealib.c
    src/formats/archive/ealib/xx_ealib_extractor.c
    src/formats/other/elm/xx_elm.c
    src/formats/other/elm/xx_elm_extractor.c
    src/formats/game/ea/xx_ea.c
    src/formats/game/ea/xx_ea_extractor.c
    src/formats/archive/diskdoubler/xx_diskdoubler.c
    src/formats/archive/diskdoubler/xx_diskdoubler_extractor.c
    src/formats/archive/cmp/xx_cmp.c
    src/formats/archive/cmp/xx_cmp_extractor.c
    src/formats/other/clp/xx_clp.c
    src/formats/other/clp/xx_clp_extractor.c
    src/formats/archive/chieflzmulti/xx_chieflzmulti.c
    src/formats/archive/chieflzmulti/xx_chieflzmulti_extractor.c
    src/formats/archive/chieflz/xx_chieflz.c
    src/formats/archive/chieflz/xx_chieflz_extractor.c
    src/formats/archive/bwcf/xx_bwcf.c
    src/formats/archive/bwcf/xx_bwcf_extractor.c
    src/formats/installer/asymetrix/xx_asymetrix.c
    src/formats/installer/asymetrix/xx_asymetrix_extractor.c
    src/formats/archive/seaarc/xx_seaarc.c
    src/formats/archive/seaarc/xx_seaarc_extractor.c
    src/formats/archive/amigalzx/xx_amigalzx.c
    src/formats/archive/amigalzx/xx_amigalzx_extractor.c
    src/formats/archive/spis/xx_spis.c
    src/formats/archive/spis/xx_spis_extractor.c
    src/formats/archive/lha/xx_lha.c
    src/formats/archive/lha/xx_lha_extractor.c
    src/formats/archive/xar/xx_xar.c
    src/formats/archive/xar/xx_xar_extractor.c
    src/formats/archive/fmc1/xx_fmc1.c
    src/formats/archive/fmc1/xx_fmc1_extractor.c
    src/formats/executable/pyz/xx_pyz.c
    src/formats/executable/pyz/xx_pyz_extractor.c
    src/formats/archive/ascendbackup/xx_ascendbackup.c
    src/formats/archive/ascendbackup/xx_ascendbackup_extractor.c
    src/formats/installer/stork/xx_stork.c
    src/formats/installer/stork/xx_stork_extractor.c
    src/formats/archive/zap/xx_zap.c
    src/formats/archive/zap/xx_zap_extractor.c
    src/formats/archive/bwf/xx_bwf.c
    src/formats/archive/bwf/xx_bwf_extractor.c
    src/formats/installer/qualitas/xx_qualitas.c
    src/formats/installer/qualitas/xx_qualitas_extractor.c
    src/formats/archive/jetbbs/xx_jetbbs.c
    src/formats/archive/jetbbs/xx_jetbbs_extractor.c
    src/formats/archive/ecmpacked/xx_ecmpacked.c
    src/formats/archive/ecmpacked/xx_ecmpacked_extractor.c
    src/formats/archive/borlandpack/xx_borlandpack.c
    src/formats/archive/borlandpack/xx_borlandpack_extractor.c
    src/formats/archive/jgpak/xx_jgpak.c
    src/formats/archive/jgpak/xx_jgpak_extractor.c
    src/formats/archive/zzz/xx_zzz.c
    src/formats/archive/zzz/xx_zzz_extractor.c
    src/formats/archive/zz/xx_zz.c
    src/formats/archive/zz/xx_zz_extractor.c
    src/formats/archive/zlwb/xx_zlwb.c
    src/formats/archive/zlwb/xx_zlwb_extractor.c
    src/formats/archive/trc/xx_trc.c
    src/formats/archive/trc/xx_trc_extractor.c
    src/formats/archive/tgcf/xx_tgcf.c
    src/formats/archive/tgcf/xx_tgcf_extractor.c
    src/formats/archive/swag/xx_swag.c
    src/formats/archive/swag/xx_swag_extractor.c
    src/formats/archive/riversoft/xx_riversoft.c
    src/formats/archive/riversoft/xx_riversoft_extractor.c
    src/formats/installer/rcf/xx_rcf.c
    src/formats/installer/rcf/xx_rcf_extractor.c
    src/formats/installer/quarterdeckqp/xx_quarterdeckqp.c
    src/formats/installer/quarterdeckqp/xx_quarterdeckqp_extractor.c
    src/formats/installer/qip1/xx_qip1.c
    src/formats/installer/qip1/xx_qip1_extractor.c
    src/formats/archive/powerarc/xx_powerarc.c
    src/formats/archive/powerarc/xx_powerarc_extractor.c
    src/formats/archive/povlablzh/xx_povlablzh.c
    src/formats/archive/povlablzh/xx_povlablzh_extractor.c
    src/formats/installer/mva/xx_mva.c
    src/formats/installer/mva/xx_mva_extractor.c
    src/formats/archive/miz/xx_miz.c
    src/formats/archive/miz/xx_miz_extractor.c
    src/formats/document/lsz/xx_lsz.c
    src/formats/document/lsz/xx_lsz_extractor.c
    src/formats/archive/jm93/xx_jm93.c
    src/formats/archive/jm93/xx_jm93_extractor.c
    src/formats/archive/inteduft/xx_inteduft.c
    src/formats/archive/inteduft/xx_inteduft_extractor.c
    src/formats/installer/igf2/xx_igf2.c
    src/formats/installer/igf2/xx_igf2_extractor.c
    src/formats/archive/igf1/xx_igf1.c
    src/formats/archive/igf1/xx_igf1_extractor.c
    src/formats/archive/ibmzpak/xx_ibmzpak.c
    src/formats/archive/ibmzpak/xx_ibmzpak_extractor.c
    src/formats/installer/fld/xx_fld.c
    src/formats/installer/fld/xx_fld_extractor.c
    src/formats/archive/fiz/xx_fiz.c
    src/formats/archive/fiz/xx_fiz_extractor.c
    src/formats/archive/dtpacked/xx_dtpacked.c
    src/formats/archive/dtpacked/xx_dtpacked_extractor.c
    src/formats/installer/dsl2/xx_dsl2.c
    src/formats/installer/dsl2/xx_dsl2_extractor.c
    src/formats/archive/dpk/xx_dpk.c
    src/formats/archive/dpk/xx_dpk_extractor.c
    src/formats/archive/cfl/xx_cfl.c
    src/formats/archive/cfl/xx_cfl_extractor.c
    src/formats/archive/trcpak/xx_trcpak.c
    src/formats/archive/trcpak/xx_trcpak_extractor.c
    src/formats/archive/swagpacket/xx_swagpacket.c
    src/formats/archive/swagpacket/xx_swagpacket_extractor.c
    src/formats/installer/sw/xx_sw.c
    src/formats/installer/sw/xx_sw_extractor.c
    src/formats/filesystem/sos/xx_sos.c
    src/formats/filesystem/sos/xx_sos_extractor.c
    src/formats/image/secondnature/xx_secondnature.c
    src/formats/image/secondnature/xx_secondnature_extractor.c
    src/formats/game/seadata/xx_seadata.c
    src/formats/game/seadata/xx_seadata_extractor.c
    src/formats/game/sci/xx_sci.c
    src/formats/game/sci/xx_sci_extractor.c
    src/formats/archive/powerboardbbs/xx_powerboardbbs.c
    src/formats/archive/powerboardbbs/xx_powerboardbbs_extractor.c
    src/formats/executable/minidump/xx_minidump.c
    src/formats/executable/minidump/xx_minidump_extractor.c
    src/formats/executable/lbrcobol/xx_lbrcobol.c
    src/formats/executable/lbrcobol/xx_lbrcobol_extractor.c
    src/formats/game/krml/xx_krml.c
    src/formats/game/krml/xx_krml_extractor.c
    src/formats/archive/jam/xx_jam.c
    src/formats/archive/jam/xx_jam_extractor.c
    src/formats/filesystem/irixsa/xx_irixsa.c
    src/formats/filesystem/irixsa/xx_irixsa_extractor.c
    src/formats/archive/hlb/xx_hlb.c
    src/formats/archive/hlb/xx_hlb_extractor.c
    src/formats/document/frontpagetheme/xx_frontpagetheme.c
    src/formats/document/frontpagetheme/xx_frontpagetheme_extractor.c
    src/formats/archive/cru/xx_cru.c
    src/formats/archive/cru/xx_cru_extractor.c
    src/formats/archive/bigaf/xx_bigaf.c
    src/formats/archive/bigaf/xx_bigaf_extractor.c
    src/formats/archive/tws/xx_tws.c
    src/formats/archive/tws/xx_tws_extractor.c
    src/formats/archive/packit/xx_packit.c
    src/formats/archive/packit/xx_packit_extractor.c
    src/formats/archive/zfsf/xx_zfsf.c
    src/formats/archive/zfsf/xx_zfsf_extractor.c
    src/formats/archive/marc/xx_marc.c
    src/formats/archive/marc/xx_marc_extractor.c
    src/formats/game/bigf/xx_bigf.c
    src/formats/game/bigf/xx_bigf_extractor.c
    src/formats/game/ptero_bigf/xx_ptero_bigf.c
    src/formats/game/ptero_bigf/xx_ptero_bigf_extractor.c
    src/formats/game/rvz/xx_rvz.c
    src/formats/game/rvz/xx_rvz_extractor.c
    src/formats/archive/ckpedp/xx_ckpedp.c
    src/formats/game/ckp/xx_ckp.c
    src/formats/game/ckp/xx_ckp_extractor.c
    src/formats/game/edp/xx_edp.c
    src/formats/game/edp/xx_edp_extractor.c
    src/formats/game/parsec_rib/xx_parsec_rib.c
    src/formats/game/parsec_rib/xx_parsec_rib_extractor.c
    src/formats/game/parsec_archive/xx_parsec_archive.c
    src/formats/game/parsec_archive/xx_parsec_archive_extractor.c
    src/formats/audio/parsec_pmm/xx_parsec_pmm.c
    src/formats/audio/parsec_pmm/xx_parsec_pmm_extractor.c
    src/formats/archive/ascend/xx_ascend.c
    src/formats/archive/ascend/xx_ascend_extractor.c
    src/formats/archive/asar/xx_asar.c
    src/formats/archive/asar/xx_asar_extractor.c
    src/formats/archive/arq/xx_arq.c
    src/formats/archive/arq/xx_arq_extractor.c
    src/formats/audio/ap4/xx_ap4.c
    src/formats/audio/ap4/xx_ap4_extractor.c
    src/formats/archive/freearc/xx_freearc.c
    src/formats/archive/freearc/xx_freearc_extractor.c
    src/formats/archive/zpaq/xx_zpaq.c
    src/formats/archive/zpaq/xx_zpaq_extractor.c
    src/formats/archive/pea/xx_pea.c
    src/formats/archive/pea/xx_pea_extractor.c
    src/formats/archive/lpaq8/xx_lpaq8.c
    src/formats/archive/lpaq8/xx_lpaq8_extractor.c
    src/formats/archive/bcm/xx_bcm.c
    src/formats/archive/bcm/xx_bcm_extractor.c
    src/formats/archive/tar_zstd/xx_tar_zstd.c
    src/formats/archive/tar_zstd/xx_tar_zstd_extractor.c
    src/formats/archive/zstd/xx_zstd.c
    src/formats/archive/zstd/xx_zstd_extractor.c
    src/formats/archive/cpio/xx_cpio.c
    src/formats/archive/cpio/xx_cpio_extractor.c
    src/formats/other/mtree/xx_mtree.c
    src/formats/other/mtree/xx_mtree_extractor.c
    src/formats/archive/tar_nextstep/xx_tar_nextstep.c
    src/formats/archive/tar_nextstep/xx_tar_nextstep_extractor.c
    src/formats/archive/tar_compress/xx_tar_compress.c
    src/formats/archive/tar_compress/xx_tar_compress_extractor.c
    src/formats/archive/tar_lzip/xx_tar_lzip.c
    src/formats/archive/tar_lzip/xx_tar_lzip_extractor.c
    src/formats/archive/lzip/xx_lzip.c
    src/formats/archive/lzip/xx_lzip_extractor.c
    src/formats/archive/lzma/xx_lzma.c
    src/formats/archive/lzma/xx_lzma_writer.c
    src/formats/archive/lzma/xx_lzma_extractor.c
    src/formats/archive/tar_lzma/xx_tar_lzma.c
    src/formats/archive/tar_lzma/xx_tar_lzma_extractor.c
    src/formats/archive/tar_lzop/xx_tar_lzop.c
    src/formats/archive/tar_lzop/xx_tar_lzop_extractor.c
    src/formats/archive/tarx1/xx_tarx1.c
    src/formats/archive/tarx1/xx_tarx1_extractor.c
    src/formats/archive/tarx2/xx_tarx2.c
    src/formats/archive/tarx2/xx_tarx2_extractor.c
    src/formats/filesystem/iso9660/xx_iso9660.c
    src/formats/filesystem/iso9660/xx_iso_zisofs_native.c
    src/formats/filesystem/iso9660/xx_iso9660_extractor.c
    src/formats/firmware/trx/xx_trx.c
    src/formats/firmware/trx/xx_trx_extractor.c
    src/formats/firmware/seama/xx_seama.c
    src/formats/firmware/seama/xx_seama_extractor.c
    src/formats/firmware/chk/xx_chk.c
    src/formats/firmware/chk/xx_chk_extractor.c
    src/formats/firmware/packimg/xx_packimg.c
    src/formats/firmware/packimg/xx_packimg_extractor.c
    src/formats/firmware/dlob/xx_dlob.c
    src/formats/firmware/dlob/xx_dlob_extractor.c
    src/formats/firmware/wince/xx_wince.c
    src/formats/firmware/wince/xx_wince_extractor.c
    src/formats/firmware/binhdr/xx_binhdr.c
    src/formats/firmware/binhdr/xx_binhdr_extractor.c
    src/formats/firmware/rtk/xx_rtk.c
    src/formats/firmware/rtk/xx_rtk_extractor.c
    src/formats/firmware/csman/xx_csman.c
    src/formats/firmware/csman/xx_csman_extractor.c
    src/formats/firmware/vxworks/xx_vxworks.c
    src/formats/firmware/vxworks/xx_vxworks_extractor.c
    src/formats/firmware/uefi_fv/xx_uefi_fv.c
    src/formats/firmware/uefi_fv/xx_uefi_fv_extractor.c
    src/formats/firmware/uefi_capsule/xx_uefi_capsule.c
    src/formats/firmware/uefi_capsule/xx_uefi_capsule_extractor.c
    src/formats/filesystem/qcow/xx_qcow.c
    src/formats/filesystem/qcow/xx_qcow_extractor.c
    src/formats/filesystem/qnx6/xx_qnx6.c
    src/formats/filesystem/qnx6/xx_qnx6_extractor.c
    src/formats/filesystem/luks/xx_luks.c
    src/formats/filesystem/luks/xx_luks_extractor.c
    src/formats/filesystem/apfs/xx_apfs.c
    src/formats/filesystem/apfs/xx_apfs_extractor.c
    src/formats/filesystem/btrfs/xx_btrfs.c
    src/formats/filesystem/btrfs/xx_btrfs_extractor.c
    src/formats/filesystem/logfs/xx_logfs.c
    src/formats/filesystem/logfs/xx_logfs_extractor.c
    src/formats/filesystem/dmg/xx_dmg.c
    src/formats/filesystem/dmg/xx_dmg_extractor.c
    src/formats/filesystem/dms/xx_dms.c
    src/formats/archive/lzop/xx_lzop.c
    src/formats/archive/lzop/xx_lzop_extractor.c
    src/formats/firmware/srec/xx_srec.c
    src/formats/firmware/srec/xx_srec_extractor.c
    src/formats/archive/dclraw/xx_dclraw.c
    src/formats/archive/dclraw/xx_dclraw_extractor.c
    src/formats/archive/xpak/xx_xpak.c
    src/formats/archive/xpak/xx_xpak_extractor.c
    src/formats/database/pdb/xx_pdb.c
    src/formats/database/pdb/xx_pdb_extractor.c
    src/formats/game/infogramesft/xx_infogramesft.c
    src/formats/game/infogramesft/xx_infogramesft_extractor.c
    src/formats/firmware/uboot/xx_uboot.c
    src/formats/firmware/uboot/xx_uboot_extractor.c
    src/formats/installer/twrx/xx_twrx.c
    src/formats/installer/twrx/xx_twrx_extractor.c
    src/formats/firmware/tplink/xx_tplink.c
    src/formats/firmware/tplink/xx_tplink_extractor.c
    src/formats/game/silmarilsft/xx_silmarilsft.c
    src/formats/game/silmarilsft/xx_silmarilsft_extractor.c
    src/formats/firmware/shrs/xx_shrs.c
    src/formats/firmware/shrs/xx_shrs_extractor.c
    src/formats/firmware/mh01/xx_mh01.c
    src/formats/firmware/mh01/xx_mh01_extractor.c
    src/formats/firmware/matter_ota/xx_matter_ota.c
    src/formats/firmware/matter_ota/xx_matter_ota_extractor.c
    src/formats/archive/lz4demo/xx_lz4demo.c
    src/formats/archive/lz4demo/xx_lz4demo_extractor.c
    src/formats/archive/lingvoarc/xx_lingvoarc.c
    src/formats/archive/lingvoarc/xx_lingvoarc_extractor.c
    src/formats/firmware/jboot/xx_jboot.c
    src/formats/firmware/jboot/xx_jboot_extractor.c
    src/formats/firmware/encrpted_img/xx_encrpted_img.c
    src/formats/firmware/encrpted_img/xx_encrpted_img_extractor.c
    src/formats/firmware/encfw/xx_encfw.c
    src/formats/firmware/encfw/xx_encfw_extractor.c
    src/formats/firmware/ecos/xx_ecos.c
    src/formats/firmware/ecos/xx_ecos_extractor.c
    src/formats/firmware/dlke/xx_dlke.c
    src/formats/firmware/dlke/xx_dlke_extractor.c
    src/formats/firmware/dlink_tlv/xx_dlink_tlv.c
    src/formats/firmware/dlink_tlv/xx_dlink_tlv_extractor.c
    src/formats/firmware/dkbs/xx_dkbs.c
    src/formats/firmware/dkbs/xx_dkbs_extractor.c
    src/formats/firmware/autel/xx_autel.c
    src/formats/firmware/autel/xx_autel_extractor.c
    src/formats/firmware/arcadyan/xx_arcadyan.c
    src/formats/firmware/arcadyan/xx_arcadyan_extractor.c
    src/formats/firmware/androidboot/xx_androidboot.c
    src/formats/firmware/androidboot/xx_androidboot_extractor.c
    src/formats/filesystem/dms/xx_dms_extractor.c
    src/formats/archive/rawstac/xx_rawstac.c
    src/formats/archive/rawstac/xx_rawstac_extractor.c
    src/formats/archive/vmssaveset/xx_vmssaveset.c
    src/formats/archive/vmssaveset/xx_vmssaveset_extractor.c
    src/formats/archive/boo/xx_boo.c
    src/formats/archive/boo/xx_boo_extractor.c
    src/formats/game/wolfft/xx_wolfft.c
    src/formats/game/wolfft/xx_wolfft_extractor.c
    src/formats/game/settlersft/xx_settlersft.c
    src/formats/game/settlersft/xx_settlersft_extractor.c
    src/formats/game/teacy/xx_teacy.c
    src/formats/game/teacy/xx_teacy_extractor.c
    src/formats/game/rsc/xx_rsc.c
    src/formats/game/rsc/xx_rsc_extractor.c
    src/formats/game/res/xx_res.c
    src/formats/executable/winres/xx_winres_make.c
    src/formats/game/res/xx_res_extractor.c
    src/formats/archive/bsn/xx_bsn.c
    src/formats/archive/bsn/xx_bsn_extractor.c
    src/formats/game/wintermutedcp/xx_wintermutedcp.c
    src/formats/game/wintermutedcp/xx_wintermutedcp_extractor.c
    src/formats/game/volitionvpft/xx_volitionvpft.c
    src/formats/game/volitionvpft/xx_volitionvpft_extractor.c
    src/formats/game/agis/xx_agis.c
    src/formats/game/agis/xx_agis_extractor.c
    src/formats/game/hog/xx_hog.c
    src/formats/game/hog/xx_hog_extractor.c
    src/formats/archive/rnca/xx_rnca.c
    src/formats/archive/rnca/xx_rnca_extractor.c
    src/formats/document/paperport/xx_paperport.c
    src/formats/document/paperport/xx_paperport_extractor.c
    src/formats/executable/starkit/xx_starkit.c
    src/formats/executable/starkit/xx_starkit_extractor.c
    src/formats/archive/lspack10/xx_lspack10.c
    src/formats/archive/lspack10/xx_lspack10_extractor.c
    src/formats/archive/ixa/xx_ixa.c
    src/formats/archive/mlb_ft/xx_mlb_ft.c
    src/formats/image/fss/xx_fss.c
    src/formats/game/epf/xx_epf.c
    src/formats/game/epf/xx_epf_extractor.c
    src/formats/archive/dfc/xx_dfc.c
    src/formats/archive/dfc/xx_dfc_extractor.c
    src/formats/archive/sfx_rsfx/xx_sfx_rsfx.c
    src/formats/archive/sfx_rsfx/xx_sfx_rsfx_extractor.c
    src/formats/installer/sfx_softpaq4/xx_sfx_softpaq4.c
    src/formats/installer/sfx_softpaq4/xx_sfx_softpaq4_extractor.c
    src/formats/archive/ka/xx_ka.c
    src/formats/archive/ka/xx_ka_extractor.c
    src/formats/filesystem/nextstep_diskimage/xx_nextstep_diskimage.c
    src/formats/filesystem/nextstep_diskimage/xx_nextstep_diskimage_extractor.c
    src/formats/archive/dn/xx_dn.c
    src/formats/archive/dn/xx_dn_extractor.c
    src/formats/archive/insa/xx_insa.c
    src/formats/archive/insa/xx_insa_extractor.c
    src/formats/archive/sfx_vms_dcx/xx_sfx_vms_dcx.c
    src/formats/archive/sfx_vms_dcx/xx_sfx_vms_dcx_extractor.c
    src/formats/installer/oberon/xx_oberon.c
    src/formats/installer/oberon/xx_oberon_extractor.c
    src/formats/game/ppd/xx_ppd.c
    src/formats/game/ppd/xx_ppd_extractor.c
    src/formats/installer/sfx_ad01/xx_sfx_ad01.c
    src/formats/installer/sfx_ad01/xx_sfx_ad01_extractor.c
    src/formats/archive/sfx_nss/xx_sfx_nss.c
    src/formats/archive/sfx_nss/xx_sfx_nss_extractor.c
    src/formats/xx_corpus_extra_extractors.c
    src/formats/installer/solitaire_deluxe/xx_solitaire_deluxe.c
    src/formats/installer/solitaire_deluxe/xx_solitaire_deluxe_extractor.c
    src/formats/document/thebat_msb/xx_thebat_msb.c
    src/formats/archive/sfx_localzip/xx_sfx_localzip.c
    src/formats/archive/ixa/xx_ixa_extractor.c
    src/formats/archive/lif/xx_lif.c
    src/formats/archive/lif/xx_lif_extractor.c
    src/formats/installer/qip2/xx_qip2.c
    src/formats/installer/qip2/xx_qip2_extractor.c
    src/formats/filesystem/emt/xx_emt.c
    src/formats/filesystem/emt/xx_emt_extractor.c
    src/formats/archive/bagf/xx_bagf.c
    src/formats/archive/bagf/xx_bagf_extractor.c
    src/formats/archive/wrzl/xx_wrzl.c
    src/formats/archive/wrzl/xx_wrzl_extractor.c
    src/formats/archive/spk/xx_spk.c
    src/formats/archive/spk/xx_spk_extractor.c
    src/formats/archive/stac/xx_stac.c
    src/formats/archive/stac/xx_stac_extractor.c
    src/formats/database/dbz/xx_dbz.c
    src/formats/database/dbz/xx_dbz_extractor.c
    src/formats/archive/squeeze2/xx_squeeze2.c
    src/formats/archive/squeeze2/xx_squeeze2_extractor.c
    src/formats/archive/sq/xx_sq.c
    src/formats/archive/sq/xx_sq_extractor.c
    src/formats/archive/phar/xx_phar.c
    src/formats/archive/phar/xx_phar_extractor.c
    src/formats/game/mpq/xx_mpq.c
    src/formats/game/mpq/xx_mpq_extractor.c
    src/formats/filesystem/sabdu/xx_sabdu.c
    src/formats/filesystem/sabdu/xx_sabdu_extractor.c
    src/formats/filesystem/apricot/xx_apricot.c
    src/formats/filesystem/apricot/xx_apricot_extractor.c
    src/formats/filesystem/hdcopy/xx_hdcopy.c
    src/formats/filesystem/hdcopy/xx_hdcopy_extractor.c
    src/formats/filesystem/copydisk/xx_copydisk.c
    src/formats/filesystem/copydisk/xx_copydisk_extractor.c
    src/formats/filesystem/ciso/xx_ciso.c
    src/formats/filesystem/ciso/xx_ciso_extractor.c
    src/formats/filesystem/vmdk/xx_vmdk.c
    src/formats/filesystem/vmdk/xx_vmdk_extractor.c
    src/formats/filesystem/vhddynamic/xx_vhddynamic.c
    src/formats/filesystem/vhddynamic/xx_vhddynamic_extractor.c
    src/formats/archive/wim/xx_wim.c
    src/formats/archive/wim/xx_wim_codec.c
    src/formats/archive/wim/xx_wim_extractor.c
    src/formats/installer/softpaq2/xx_softpaq2.c
    src/formats/installer/softpaq2/xx_softpaq2_extractor.c
    src/formats/archive/aiaff/xx_aiaff.c
    src/formats/archive/aiaff/xx_aiaff_extractor.c
    src/formats/image/gxl/xx_gxl.c
    src/formats/image/gxl/xx_gxl_extractor.c
    src/formats/filesystem/shrinkwrap/xx_shrinkwrap.c
    src/formats/filesystem/shrinkwrap/xx_shrinkwrap_extractor.c
    src/formats/installer/red/xx_red.c
    src/formats/installer/red/xx_red_extractor.c
    src/formats/filesystem/diskexpress/xx_diskexpress.c
    src/formats/filesystem/diskexpress/xx_diskexpress_extractor.c
    src/formats/archive/cpx/xx_cpx.c
    src/formats/archive/cpx/xx_cpx_extractor.c
    src/formats/installer/smsipak/xx_smsipak.c
    src/formats/installer/smsipak/xx_smsipak_extractor.c
    src/formats/installer/bnd/xx_bnd.c
    src/formats/installer/bnd/xx_bnd_extractor.c
    src/formats/game/cat/xx_cat.c
    src/formats/game/cat/xx_cat_extractor.c
    src/formats/filesystem/csidos/xx_csidos.c
    src/formats/filesystem/csidos/xx_csidos_extractor.c
    src/formats/document/binder/xx_binder.c
    src/formats/document/binder/xx_binder_extractor.c
    src/formats/installer/jasc/xx_jasc.c
    src/formats/installer/jasc/xx_jasc_extractor.c
    src/formats/installer/recognita/xx_recognita.c
    src/formats/installer/recognita/xx_recognita_extractor.c
    src/formats/installer/scf/xx_scf.c
    src/formats/installer/scf/xx_scf_extractor.c
    src/formats/installer/bcw/xx_bcw.c
    src/formats/installer/bcw/xx_bcw_extractor.c
    src/formats/archive/bvrp/xx_bvrp.c
    src/formats/archive/bvrp/xx_bvrp_extractor.c
    src/formats/installer/cpoint/xx_cpoint.c
    src/formats/archive/ssm/xx_ssm.c
    src/formats/archive/ssm/xx_ssm_extractor.c
    src/formats/archive/mdcd/xx_mdcd.c
    src/formats/archive/mdcd/xx_mdcd_extractor.c
    src/formats/archive/xlas/xx_xlas.c
    src/formats/archive/xlas/xx_xlas_extractor.c
    src/formats/document/pain/xx_pain.c
    src/formats/document/pain/xx_pain_extractor.c
    src/formats/filesystem/qrst/xx_qrst.c
    src/formats/filesystem/qrst/xx_qrst_extractor.c
    src/formats/installer/opc/xx_opc.c
    src/formats/installer/opc/xx_opc_extractor.c
    src/formats/document/tnef/xx_tnef.c
    src/formats/document/tnef/xx_tnef_extractor.c
    src/formats/archive/mcc/xx_mcc.c
    src/formats/archive/mcc/xx_mcc_extractor.c
    src/formats/document/notetab/xx_notetab.c
    src/formats/document/notetab/xx_notetab_extractor.c
    src/formats/game/stunts/xx_stunts.c
    src/formats/game/stunts/xx_stunts_extractor.c
    src/formats/game/megatechvol/xx_megatechvol.c
    src/formats/game/megatechvol/xx_megatechvol_extractor.c
    src/formats/video/grasp/xx_grasp.c
    src/formats/video/grasp/xx_grasp_extractor.c
    src/formats/archive/psn/xx_psn.c
    src/formats/archive/psn/xx_psn_extractor.c
    src/formats/game/sinner/xx_sinner.c
    src/formats/game/sinner/xx_sinner_extractor.c
    src/formats/game/hog2/xx_hog2.c
    src/formats/game/hog2/xx_hog2_extractor.c
    src/formats/image/pcxlib/xx_pcxlib.c
    src/formats/image/pcxlib/xx_pcxlib_extractor.c
    src/formats/installer/vmsdb/xx_vmsdb.c
    src/formats/installer/vmsdb/xx_vmsdb_extractor.c
    src/formats/installer/vmspcsi/xx_vmspcsi.c
    src/formats/installer/vmspcsi/xx_vmspcsi_extractor.c
    src/formats/installer/beospkg/xx_beospkg.c
    src/formats/installer/beospkg/xx_beospkg_extractor.c
    src/formats/installer/solarispkg/xx_solarispkg.c
    src/formats/installer/solarispkg/xx_solarispkg_extractor.c
    src/formats/archive/pax/xx_pax.c
    src/formats/archive/pax/xx_pax_extractor.c
    src/formats/executable/copyqmexe/xx_copyqmexe.c
    src/formats/executable/copyqmexe/xx_copyqmexe_extractor.c
    src/formats/filesystem/diskjuggler/xx_diskjuggler.c
    src/formats/filesystem/diskjuggler/xx_diskjuggler_extractor.c
    src/formats/filesystem/pmdiskcopy/xx_pmdiskcopy.c
    src/formats/filesystem/pmdiskcopy/xx_pmdiskcopy_extractor.c
    src/formats/filesystem/diskdupe/xx_diskdupe.c
    src/formats/filesystem/diskdupe/xx_diskdupe_extractor.c
    src/formats/filesystem/imd/xx_imd.c
    src/formats/filesystem/imd/xx_imd_extractor.c
    src/formats/filesystem/twoimg/xx_twoimg.c
    src/formats/filesystem/twoimg/xx_twoimg_extractor.c
    src/formats/filesystem/fdi/xx_fdi.c
    src/formats/filesystem/fdi/xx_fdi_extractor.c
    src/formats/filesystem/hfe/xx_hfe.c
    src/formats/filesystem/hfe/xx_hfe_extractor.c
    src/formats/filesystem/teledisk/xx_teledisk.c
    src/formats/filesystem/teledisk/xx_teledisk_extractor.c
    src/formats/filesystem/copyqm/xx_copyqm.c
    src/formats/filesystem/copyqm/xx_copyqm_extractor.c
    src/formats/installer/pcinstall/xx_pcinstall.c
    src/formats/installer/pcinstall/xx_pcinstall_extractor.c
    src/formats/installer/gksetup/xx_gksetup.c
    src/formats/installer/gksetup/xx_gksetup_extractor.c
    src/formats/installer/is11/xx_is11.c
    src/formats/installer/is11/xx_is11_extractor.c
    src/formats/installer/izpack/xx_izpack.c
    src/formats/installer/izpack/xx_izpack_extractor.c
    src/formats/archive/squeeze1/xx_squeeze1.c
    src/formats/archive/squeeze1/xx_squeeze1_extractor.c
    src/formats/filesystem/trdos/xx_trdos.c
    src/formats/filesystem/trdos/xx_trdos_extractor.c
    src/formats/installer/lifkd/xx_lifkd.c
    src/formats/installer/lifkd/xx_lifkd_extractor.c
    src/formats/installer/arcv/xx_arcv.c
    src/formats/installer/arcv/xx_arcv_extractor.c
    src/formats/archive/compaqlzh/xx_compaqlzh.c
    src/formats/archive/compaqlzh/xx_compaqlzh_extractor.c
    src/formats/archive/lzk00/xx_lzk00.c
    src/formats/archive/lzk00/xx_lzk00_extractor.c
    src/formats/archive/pma/xx_pma.c
    src/formats/archive/pma/xx_pma_extractor.c
    src/formats/archive/binhex/xx_binhex.c
    src/formats/archive/binhex/xx_binhex_extractor.c
    src/formats/archive/binaryii/xx_binaryii.c
    src/formats/archive/binaryii/xx_binaryii_extractor.c
    src/formats/archive/stuffit/xx_stuffit.c
    src/formats/archive/stuffit/xx_stuffit_extractor.c
    src/formats/archive/dclft/xx_dclft.c
    src/formats/archive/dclft/xx_dclft_extractor.c
    src/formats/executable/debugscr/xx_debugscr.c
    src/formats/executable/debugscr/xx_debugscr_extractor.c
    src/formats/game/gob/xx_gob.c
    src/formats/game/gob/xx_gob_extractor.c
    src/formats/filesystem/savedskf/xx_savedskf.c
    src/formats/filesystem/savedskf/xx_savedskf_extractor.c
    src/formats/archive/edilzss/xx_edilzss.c
    src/formats/archive/edilzss/xx_edilzss_extractor.c
    src/formats/installer/is7inx/xx_is7inx.c
    src/formats/installer/is7inx/xx_is7inx_extractor.c
    src/formats/installer/is5/xx_is5.c
    src/formats/installer/is5/xx_is5_extractor.c
    src/formats/installer/is3/xx_is3.c
    src/formats/installer/is3/xx_is3_extractor.c
    src/formats/archive/psdc/xx_psdc.c
    src/formats/archive/psdc/xx_psdc_extractor.c
    src/formats/archive/unixcompact/xx_unixcompact.c
    src/formats/archive/unixcompact/xx_unixcompact_extractor.c
    src/formats/archive/sco/xx_sco.c
    src/formats/archive/sco/xx_sco_extractor.c
    src/formats/archive/gpfpack/xx_gpfpack.c
    src/formats/archive/gpfpack/xx_gpfpack_extractor.c
    src/formats/archive/ftcomp/xx_ftcomp.c
    src/formats/archive/ftcomp/xx_ftcomp_extractor.c
    src/formats/archive/winlink/xx_winlink.c
    src/formats/archive/winlink/xx_winlink_extractor.c
    src/formats/installer/gst/xx_gst.c
    src/formats/installer/gst/xx_gst_extractor.c
    src/formats/archive/finear/xx_finear.c
    src/formats/archive/finear/xx_finear_extractor.c
    src/formats/firmware/mwave/xx_mwave.c
    src/formats/firmware/mwave/xx_mwave_extractor.c
    src/formats/archive/xorarchive/xx_xorarchive.c
    src/formats/archive/xorarchive/xx_xorarchive_extractor.c
    src/formats/game/mxs/xx_mxs.c
    src/formats/game/mxs/xx_mxs_extractor.c
    src/formats/archive/edc/xx_edc.c
    src/formats/archive/edc/xx_edc_extractor.c
    src/formats/archive/mrnz/xx_mrnz.c
    src/formats/archive/mrnz/xx_mrnz_extractor.c
    src/formats/archive/tpwm/xx_tpwm.c
    src/formats/archive/tpwm/xx_tpwm_extractor.c
    src/formats/archive/cazip/xx_cazip.c
    src/formats/archive/cazip/xx_cazip_extractor.c
    src/formats/archive/ibmpack/xx_ibmpack.c
    src/formats/archive/ibmpack/xx_ibmpack_extractor.c
    src/formats/archive/rnc/xx_rnc.c
    src/formats/archive/rnc/xx_rnc_extractor.c
    src/formats/archive/shar/xx_shar.c
    src/formats/archive/shar/xx_shar_extractor.c
    src/formats/installer/netware2/xx_netware2.c
    src/formats/installer/netware2/xx_netware2_extractor.c
    src/formats/document/mathcad/xx_mathcad.c
    src/formats/document/mathcad/xx_mathcad_extractor.c
    src/formats/document/perform/xx_perform.c
    src/formats/document/perform/xx_perform_extractor.c
    src/formats/game/battleisle/xx_battleisle.c
    src/formats/game/battleisle/xx_battleisle_extractor.c
    src/formats/archive/kpck/xx_kpck.c
    src/formats/archive/kpck/xx_kpck_extractor.c
    src/formats/game/beatthehouse/xx_beatthehouse.c
    src/formats/game/beatthehouse/xx_beatthehouse_extractor.c
    src/formats/archive/pp20/xx_pp20.c
    src/formats/archive/pp20/xx_pp20_extractor.c
    src/formats/archive/macbinary/xx_macbinary.c
    src/formats/archive/macbinary/xx_macbinary_extractor.c
    src/formats/archive/applesingle/xx_applesingle.c
    src/formats/archive/applesingle/xx_applesingle_extractor.c
    src/formats/executable/resourcefork/xx_resourcefork.c
    src/formats/executable/resourcefork/xx_resourcefork_extractor.c
    src/formats/filesystem/cramfs/xx_cramfs.c
    src/formats/filesystem/cramfs/xx_cramfs_direct_native.c
    src/formats/filesystem/cramfs/xx_cramfs_extractor.c
    src/formats/filesystem/jffs2/xx_jffs2.c
    src/formats/filesystem/jffs2/xx_jffs2_extractor.c
    src/formats/filesystem/yaffs/xx_yaffs.c
    src/formats/filesystem/yaffs/xx_yaffs_extractor.c
    src/formats/filesystem/ubi/xx_ubi.c
    src/formats/filesystem/ubi/xx_ubi_extractor.c
    src/formats/filesystem/ubifs/xx_ubifs.c
    src/formats/filesystem/ubifs/xx_ubifs_extractor.c
    src/formats/filesystem/ext/xx_ext.c
    src/formats/filesystem/erofs/xx_erofs.c
    src/formats/filesystem/ext/xx_ext_extractor.c
    src/formats/filesystem/fat/xx_fat.c
    src/formats/filesystem/fat/xx_fat_extractor.c
    src/formats/filesystem/mbr/xx_mbr.c
    src/formats/filesystem/mbr/xx_mbr_extractor.c
    src/formats/filesystem/gpt/xx_gpt.c
    src/formats/filesystem/gpt/xx_gpt_extractor.c
    src/formats/firmware/sparse/xx_sparse.c
    src/formats/firmware/sparse/xx_sparse_extractor.c
    src/formats/firmware/uimage/xx_uimage.c
    src/formats/firmware/uimage/xx_uimage_lz4_native.c
    src/formats/firmware/uimage/xx_uimage_extractor.c
    src/formats/firmware/dtb/xx_dtb.c
    src/formats/firmware/dtb/xx_dtb_extractor.c
    src/formats/filesystem/squashfs/xx_squashfs.c
    src/formats/filesystem/squashfs/xx_squashfs_extractor.c
    src/formats/filesystem/ntfs/xx_ntfs.c
    src/formats/filesystem/ntfs/xx_ntfs_extractor.c
    src/formats/filesystem/udf/xx_udf.c
    src/formats/filesystem/udf/xx_udf_extractor.c
    src/formats/filesystem/romfs/xx_romfs.c
    src/formats/filesystem/romfs/xx_romfs_extractor.c
    src/formats/archive/sqz/xx_sqz.c
    src/formats/archive/sqz/xx_sqz_extractor.c
    src/formats/installer/topspeed/xx_topspeed.c
    src/formats/installer/topspeed/xx_topspeed_extractor.c
    src/formats/database/tps/xx_tps.c
    src/formats/database/tps/xx_tps_extractor.c
    src/formats/archive/ulead/xx_ulead.c
    src/formats/archive/ulead/xx_ulead_extractor.c
    src/formats/archive/quantum/xx_quantum.c
    src/formats/archive/quantum/xx_quantum_extractor.c
    src/formats/archive/zxzip/xx_zxzip.c
    src/formats/archive/zxzip/xx_zxzip_extractor.c
    src/formats/filesystem/zoom/xx_zoom.c
    src/formats/filesystem/zoom/xx_zoom_extractor.c
    src/formats/audio/sfpack/xx_sfpack.c
    src/formats/audio/sfpack/xx_sfpack_extractor.c
    src/formats/archive/claylz/xx_claylz.c
    src/formats/archive/claylz/xx_claylz_extractor.c
    src/formats/archive/c64wraptor/xx_c64wraptor.c
    src/formats/archive/c64wraptor/xx_c64wraptor_extractor.c
    src/formats/installer/corelltec/xx_corelltec.c
    src/formats/installer/corelltec/xx_corelltec_extractor.c
    src/formats/archive/pcsecure/xx_pcsecure.c
    src/formats/archive/pcsecure/xx_pcsecure_extractor.c
    src/formats/archive/rsvk/xx_rsvk.c
    src/formats/archive/rsvk/xx_rsvk_extractor.c
    src/formats/archive/lzw15v/xx_lzw15v.c
    src/formats/archive/lzw15v/xx_lzw15v_extractor.c
    src/formats/archive/saf/xx_saf.c
    src/formats/archive/saf/xx_saf_extractor.c
    src/formats/archive/sls/xx_sls.c
    src/formats/archive/sls/xx_sls_extractor.c
    src/formats/installer/nid/xx_nid.c
    src/formats/installer/nid/xx_nid_extractor.c
    src/formats/archive/gamos/xx_gamos.c
    src/formats/archive/gamos/xx_gamos_extractor.c
    src/formats/archive/panorama/xx_panorama.c
    src/formats/archive/panorama/xx_panorama_extractor.c
    src/formats/installer/fpak/xx_fpak.c
    src/formats/installer/fpak/xx_fpak_extractor.c
    src/formats/archive/ace/xx_ace.c
    src/formats/archive/ace/xx_ace_extractor.c
    src/formats/archive/ace/xx_ace_dec.c
    src/formats/archive/arj/xx_arj.c
    src/formats/archive/arj/xx_arj_extractor.c
    src/formats/archive/ain/xx_ain.c
    src/formats/archive/ain/xx_ain_extractor.c
    src/formats/archive/aldus/xx_aldus.c
    src/formats/archive/aldus/xx_aldus_extractor.c
    src/formats/archive/alz/xx_alz.c
    src/formats/archive/alz/xx_alz_extractor.c
    src/formats/archive/ampk/xx_ampk.c
    src/formats/archive/ampk/xx_ampk_extractor.c
    src/formats/filesystem/aodos/xx_aodos.c
    src/formats/filesystem/aodos/xx_aodos_extractor.c
    src/formats/archive/arcfs/xx_arcfs.c
    src/formats/archive/arcfs/xx_arcfs_extractor.c
    src/formats/archive/pdp11ar/xx_pdp11ar.c
    src/formats/archive/pdp11ar/xx_pdp11ar_extractor.c
    src/formats/archive/artipack/xx_artipack.c
    src/formats/archive/artipack/xx_artipack_extractor.c
    src/formats/installer/arcv2/xx_arcv2.c
    src/formats/installer/arcv2/xx_arcv2_extractor.c
    src/formats/installer/arcv4/xx_arcv4.c
    src/formats/installer/arcv4/xx_arcv4_extractor.c
    src/formats/archive/warc/xx_warc.c
    src/formats/archive/warc/xx_warc_extractor.c
    src/formats/installer/aixbff/xx_aixbff.c
    src/formats/installer/aixbff/xx_aixbff_extractor.c
    src/formats/archive/arx/xx_arx.c
    src/formats/archive/arx/xx_arx_extractor.c
    src/formats/archive/cab/xx_cab.c
    src/formats/archive/cab/xx_cab_extractor.c
    src/formats/executable/msdos/xx_msdos.c
    src/formats/executable/msdos/xx_msdos_extractor.c
    src/formats/executable/com/xx_com.c
    src/formats/executable/com/xx_com_extractor.c
    src/formats/executable/dos16m/xx_dos16m.c
    src/formats/executable/dos16m/xx_dos16m_extractor.c
    src/formats/executable/atarist/xx_atarist.c
    src/formats/executable/atarist/xx_atarist_extractor.c
    src/formats/executable/amigahunk/xx_amigahunk.c
    src/formats/executable/amigahunk/xx_amigahunk_extractor.c
    src/formats/executable/pe/xx_pe.c
    src/formats/executable/pe/xx_pe_make.c
    src/formats/executable/pe/xx_pe_extractor.c
    src/formats/executable/pe/xx_pe_datastructs.c
    src/formats/executable/pe/xx_pe_exception.c
    src/formats/executable/elf/xx_elf.c
    src/formats/executable/elf/xx_elf_symbols.c
    src/formats/executable/elf/xx_elf_resources.c
    src/formats/executable/elf/xx_elf_extractor.c
    src/formats/executable/elf/xx_elf_datastructs.c
    src/formats/executable/elf/xx_elf_dwarf.c
    src/formats/executable/elf/xx_elf_dwarf_analysis.c
    src/formats/executable/dwarf/xx_dwarf_loclists.c
    src/formats/firmware/bios_firmware/xx_bios_firmware.c
    src/formats/firmware/gns1/xx_gns1.c
    src/formats/executable/pebble/xx_pebble.c
    src/formats/executable/hp_som/xx_hp_som.c
    src/formats/executable/qnx_lmf/xx_qnx_lmf.c
    src/formats/executable/nintendo_rel/xx_nintendo_rel.c
    src/formats/executable/xcoff/xx_xcoff.c
    src/formats/executable/macho/xx_macho.c
    src/formats/executable/macho/xx_macho_exception.c
    src/formats/executable/macho/xx_macho_extractor.c
    src/formats/executable/macho/xx_macho_datastructs.c
    src/formats/executable/ne/xx_ne.c
    src/formats/executable/ne/xx_ne_extractor.c
    src/formats/executable/le/xx_le.c
    src/formats/executable/le/xx_le_extractor.c
    src/formats/executable/lx/xx_lx.c
    src/formats/executable/lx/xx_lx_extractor.c
    src/formats/executable/dex/xx_dex.c
    src/formats/executable/dex/xx_dex_extractor.c
    # Readers already registered by xx_format and the public API.
    src/formats/executable/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.c
    src/formats/executable/xamarin_compressed_assembly/xx_xamarin_compressed_assembly_extractor.c
    src/formats/filesystem/x68000_dim/xx_x68000_dim.c
    src/formats/filesystem/x68000_dim/xx_x68000_dim_extractor.c
    src/formats/game/visionaire_studio_vis/xx_visionaire_studio_vis.c
    src/formats/game/visionaire_studio_vis/xx_visionaire_studio_vis_extractor.c
    src/formats/archive/uharc/xx_uharc.c
    src/formats/archive/uharc/xx_uharc_extractor.c
    src/formats/filesystem/trs_80_jv3/xx_trs_80_jv3.c
    src/formats/filesystem/trs_80_jv3/xx_trs_80_jv3_extractor.c
    src/formats/filesystem/trs_80_jv1/xx_trs_80_jv1.c
    src/formats/filesystem/trs_80_jv1/xx_trs_80_jv1_extractor.c
    src/formats/filesystem/t98_next_nfd/xx_t98_next_nfd.c
    src/formats/filesystem/t98_next_nfd/xx_t98_next_nfd_extractor.c
    src/formats/archive/stuffit_split_file/xx_stuffit_split_file.c
    src/formats/archive/stuffit_split_file/xx_stuffit_split_file_extractor.c
    src/formats/filesystem/rdb/xx_rdb.c
    src/formats/filesystem/rdb/xx_rdb_extractor.c
    src/formats/firmware/qnap_nas_firmware/xx_qnap_nas_firmware.c
    src/formats/firmware/qnap_nas_firmware/xx_qnap_nas_firmware_extractor.c
    src/formats/filesystem/qcow1/xx_qcow1.c
    src/formats/filesystem/qcow1/xx_qcow1_extractor.c
    src/formats/game/nsa/xx_nsa.c
    src/formats/game/nsa/xx_nsa_extractor.c
    src/formats/game/ns2/xx_ns2.c
    src/formats/game/ns2/xx_ns2_extractor.c
    src/formats/filesystem/nec_pc_98_fdi/xx_nec_pc_98_fdi.c
    src/formats/filesystem/nec_pc_98_fdi/xx_nec_pc_98_fdi_extractor.c
    src/formats/archive/ms_dos_backup/xx_ms_dos_backup.c
    src/formats/archive/ms_dos_backup/xx_ms_dos_backup_extractor.c
    src/formats/filesystem/hxc_stream_hfe/xx_hxc_stream_hfe.c
    src/formats/filesystem/hxc_stream_hfe/xx_hxc_stream_hfe_extractor.c
    src/formats/filesystem/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.c
    src/formats/filesystem/encrypted_apple_disk_image/xx_encrypted_apple_disk_image_extractor.c
    src/formats/filesystem/apple_sparse_bundle/xx_apple_sparse_bundle.c
    src/formats/filesystem/apple_sparse_bundle/xx_apple_sparse_bundle_extractor.c
    src/formats/filesystem/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.c
    src/formats/filesystem/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image_extractor.c
    src/formats/archive/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.c
    src/formats/archive/raw_deflate_compressed_data/xx_raw_deflate_compressed_data_extractor.c
    src/formats/installer/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.c
    src/formats/installer/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs_extractor.c
    src/formats/installer/sfx_krzip/xx_sfx_krzip.c
    src/formats/installer/sfx_krzip/xx_sfx_krzip_extractor.c
    src/formats/installer/sfx_warpin_package/xx_sfx_warpin_package.c
    src/formats/installer/sfx_warpin_package/xx_sfx_warpin_package_extractor.c
    src/formats/installer/sfx_hci_instalit/xx_sfx_hci_instalit.c
    src/formats/installer/sfx_hci_instalit/xx_sfx_hci_instalit_extractor.c
    src/formats/game/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.c
    src/formats/game/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion_extractor.c
    src/formats/installer/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.c
    src/formats/installer/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects_extractor.c
    src/formats/executable/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.c
    src/formats/executable/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor_extractor.c
    src/formats/installer/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.c
    src/formats/installer/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package_extractor.c
    src/formats/archive/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.c
    src/formats/archive/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image_extractor.c
    src/formats/installer/sfx_sbx_extractor/xx_sfx_sbx_extractor.c
    src/formats/installer/sfx_sbx_extractor/xx_sfx_sbx_extractor_extractor.c
    src/formats/installer/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.c
    src/formats/installer/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp_extractor.c
    src/formats/filesystem/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.c
    src/formats/filesystem/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image_extractor.c
    src/formats/installer/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.c
    src/formats/installer/sfx_compaq_softpaq/xx_sfx_compaq_softpaq_extractor.c
    src/formats/installer/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.c
    src/formats/installer/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto_extractor.c
    src/formats/installer/wise_installation_system/xx_wise_installation_system.c
    src/formats/installer/wise_installation_system/xx_wise_installation_system_extractor.c
    src/formats/installer/eschalon_setup_epsf/xx_eschalon_setup_epsf.c
    src/formats/installer/eschalon_setup_epsf/xx_eschalon_setup_epsf_extractor.c
    src/formats/installer/gentee_installer/xx_gentee_installer.c
    src/formats/installer/gentee_installer/xx_gentee_installer_extractor.c
    src/formats/installer/clickteam_install_creator/xx_clickteam_install_creator.c
    src/formats/installer/clickteam_install_creator/xx_clickteam_install_creator_extractor.c
    src/formats/installer/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.c
    src/formats/installer/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor_extractor.c
    src/formats/installer/sfxstart/xx_sfxstart.c
    src/formats/installer/sfxstart/xx_sfxstart_extractor.c
    src/formats/other/modbus_tcp/xx_modbus_tcp.c
    src/formats/other/modbus_tcp/xx_modbus_tcp_extractor.c
    src/formats/other/someip_message/xx_someip_message.c
    src/formats/other/someip_message/xx_someip_message_extractor.c
    src/formats/other/dds_rtps/xx_dds_rtps.c
    src/formats/other/dds_rtps/xx_dds_rtps_extractor.c
    src/formats/other/rip_message/xx_rip_message.c
    src/formats/other/rip_message/xx_rip_message_extractor.c
    src/formats/other/vrrp_message/xx_vrrp_message.c
    src/formats/other/vrrp_message/xx_vrrp_message_extractor.c
    src/formats/other/igmp_message/xx_igmp_message.c
    src/formats/other/igmp_message/xx_igmp_message_extractor.c
    src/formats/other/pim_message/xx_pim_message.c
    src/formats/other/pim_message/xx_pim_message_extractor.c
    src/formats/other/ldp_message/xx_ldp_message.c
    src/formats/other/ldp_message/xx_ldp_message_extractor.c
    src/formats/other/gre_packet/xx_gre_packet.c
    src/formats/other/gre_packet/xx_gre_packet_extractor.c
    src/formats/other/l2tp_packet/xx_l2tp_packet.c
    src/formats/other/l2tp_packet/xx_l2tp_packet_extractor.c
    src/formats/other/lldp_message/xx_lldp_message.c
    src/formats/other/lldp_message/xx_lldp_message_extractor.c
    src/formats/other/netflow_datagram/xx_netflow_datagram.c
    src/formats/other/netflow_datagram/xx_netflow_datagram_extractor.c
    src/formats/other/ntlm_message/xx_ntlm_message.c
    src/formats/other/ntlm_message/xx_ntlm_message_extractor.c
    src/formats/other/dcerpc_pdu/xx_dcerpc_pdu.c
    src/formats/other/dcerpc_pdu/xx_dcerpc_pdu_extractor.c
    src/formats/other/ethereum_rlp/xx_ethereum_rlp.c
    src/formats/other/ethereum_rlp/xx_ethereum_rlp_extractor.c
    src/formats/image/imagemagick_miff/xx_imagemagick_miff.c
    src/formats/image/imagemagick_miff/xx_imagemagick_miff_extractor.c
    src/formats/image/avs_image/xx_avs_image.c
    src/formats/image/avs_image/xx_avs_image_extractor.c
    src/formats/science/scanalytics_iplab/xx_scanalytics_iplab.c
    src/formats/science/scanalytics_iplab/xx_scanalytics_iplab_extractor.c
    src/formats/image/mtv_image/xx_mtv_image.c
    src/formats/image/mtv_image/xx_mtv_image_extractor.c
    src/formats/image/nokia_ota_bitmap/xx_nokia_ota_bitmap.c
    src/formats/image/nokia_ota_bitmap/xx_nokia_ota_bitmap_extractor.c
    src/formats/image/apple_pict/xx_apple_pict.c
    src/formats/image/apple_pict/xx_apple_pict_extractor.c
    src/formats/image/wordperfect_wpg/xx_wordperfect_wpg.c
    src/formats/image/wordperfect_wpg/xx_wordperfect_wpg_extractor.c
    src/formats/science/nasa_vicar/xx_nasa_vicar.c
    src/formats/science/nasa_vicar/xx_nasa_vicar_extractor.c
    src/formats/image/khoros_viff/xx_khoros_viff.c
    src/formats/image/khoros_viff/xx_khoros_viff_extractor.c
    src/formats/image/imagemagick_mvg/xx_imagemagick_mvg.c
    src/formats/image/imagemagick_mvg/xx_imagemagick_mvg_extractor.c
    src/formats/image/motif_uil/xx_motif_uil.c
    src/formats/image/motif_uil/xx_motif_uil_extractor.c
    src/formats/science/iges_model/xx_iges_model.c
    src/formats/science/iges_model/xx_iges_model_extractor.c
    src/formats/image/openusd_usda/xx_openusd_usda.c
    src/formats/image/openusd_usda/xx_openusd_usda_extractor.c
    src/formats/font/ufo_glif/xx_ufo_glif.c
    src/formats/font/ufo_glif/xx_ufo_glif_extractor.c
    src/formats/font/unifont_hex/xx_unifont_hex.c
    src/formats/font/unifont_hex/xx_unifont_hex_extractor.c
    src/formats/audio/adlib_sop/xx_adlib_sop.c
    src/formats/audio/adlib_sop/xx_adlib_sop_extractor.c
    src/formats/audio/cudfm_cff/xx_cudfm_cff.c
    src/formats/audio/cudfm_cff/xx_cudfm_cff_extractor.c
    src/formats/audio/adlib_jbm/xx_adlib_jbm.c
    src/formats/audio/adlib_jbm/xx_adlib_jbm_extractor.c
    src/formats/audio/ceres_msc/xx_ceres_msc.c
    src/formats/audio/ceres_msc/xx_ceres_msc_extractor.c
    src/formats/audio/adlib_xsm/xx_adlib_xsm.c
    src/formats/audio/adlib_xsm/xx_adlib_xsm_extractor.c
    src/formats/audio/ken_ksm/xx_ken_ksm.c
    src/formats/audio/ken_ksm/xx_ken_ksm_extractor.c
    src/formats/audio/implay_music/xx_implay_music.c
    src/formats/audio/implay_music/xx_implay_music_extractor.c
    src/formats/audio/adlib_mtr/xx_adlib_mtr.c
    src/formats/audio/adlib_mtr/xx_adlib_mtr_extractor.c
    src/formats/audio/rdos_raw/xx_rdos_raw.c
    src/formats/audio/rdos_raw/xx_rdos_raw_extractor.c
    src/formats/audio/mad_tracker/xx_mad_tracker.c
    src/formats/audio/mad_tracker/xx_mad_tracker_extractor.c
    src/formats/science/vasp_poscar/xx_vasp_poscar.c
    src/formats/science/vasp_poscar/xx_vasp_poscar_extractor.c
    src/formats/science/quantum_espresso_input/xx_quantum_espresso_input.c
    src/formats/science/quantum_espresso_input/xx_quantum_espresso_input_extractor.c
    src/formats/science/cp2k_input/xx_cp2k_input.c
    src/formats/science/cp2k_input/xx_cp2k_input_extractor.c
    src/formats/science/nwchem_input/xx_nwchem_input.c
    src/formats/science/nwchem_input/xx_nwchem_input_extractor.c
    src/formats/science/gamess_input/xx_gamess_input.c
    src/formats/science/gamess_input/xx_gamess_input_extractor.c
    src/formats/science/gaussian_input/xx_gaussian_input.c
    src/formats/science/gaussian_input/xx_gaussian_input_extractor.c
    src/formats/science/abinit_input/xx_abinit_input.c
    src/formats/science/abinit_input/xx_abinit_input_extractor.c
    src/formats/science/aims_geometry/xx_aims_geometry.c
    src/formats/science/aims_geometry/xx_aims_geometry_extractor.c
    src/formats/science/orca_input/xx_orca_input.c
    src/formats/science/orca_input/xx_orca_input_extractor.c
    src/formats/science/demon_input/xx_demon_input.c
    src/formats/science/demon_input/xx_demon_input_extractor.c
    src/formats/other/ethernet_frame/xx_ethernet_frame.c
    src/formats/other/ethernet_frame/xx_ethernet_frame_extractor.c
    src/formats/other/ip_packet/xx_ip_packet.c
    src/formats/other/ip_packet/xx_ip_packet_extractor.c
    src/formats/other/arp_packet/xx_arp_packet.c
    src/formats/other/arp_packet/xx_arp_packet_extractor.c
    src/formats/other/icmp_message/xx_icmp_message.c
    src/formats/other/icmp_message/xx_icmp_message_extractor.c
    src/formats/other/sip_message/xx_sip_message.c
    src/formats/other/sip_message/xx_sip_message_extractor.c
    src/formats/other/rtsp_message/xx_rtsp_message.c
    src/formats/other/rtsp_message/xx_rtsp_message_extractor.c
    src/formats/other/diameter_message/xx_diameter_message.c
    src/formats/other/diameter_message/xx_diameter_message_extractor.c
    src/formats/other/tacacs_packet/xx_tacacs_packet.c
    src/formats/other/tacacs_packet/xx_tacacs_packet_extractor.c
    src/formats/other/gtp_message/xx_gtp_message.c
    src/formats/other/gtp_message/xx_gtp_message_extractor.c
    src/formats/other/pfcp_message/xx_pfcp_message.c
    src/formats/other/pfcp_message/xx_pfcp_message_extractor.c
    src/formats/other/pptp_message/xx_pptp_message.c
    src/formats/other/pptp_message/xx_pptp_message_extractor.c
    src/formats/other/rsvp_message/xx_rsvp_message.c
    src/formats/other/rsvp_message/xx_rsvp_message_extractor.c
    src/formats/other/age_encrypted/xx_age_encrypted.c
    src/formats/other/age_encrypted/xx_age_encrypted_extractor.c
    src/formats/other/kerberos_ccache/xx_kerberos_ccache.c
    src/formats/other/kerberos_ccache/xx_kerberos_ccache_extractor.c
    src/formats/other/jose_jws/xx_jose_jws.c
    src/formats/other/jose_jws/xx_jose_jws_extractor.c
    src/formats/image/wbmp_image/xx_wbmp_image.c
    src/formats/image/wbmp_image/xx_wbmp_image_extractor.c
    src/formats/image/dec_sixel/xx_dec_sixel.c
    src/formats/image/dec_sixel/xx_dec_sixel_extractor.c
    src/formats/image/palm_bitmap/xx_palm_bitmap.c
    src/formats/image/palm_bitmap/xx_palm_bitmap_extractor.c
    src/formats/image/adobe_acv/xx_adobe_acv.c
    src/formats/image/adobe_acv/xx_adobe_acv_extractor.c
    src/formats/image/adobe_act/xx_adobe_act.c
    src/formats/image/adobe_act/xx_adobe_act_extractor.c
    src/formats/image/ogre_skeleton/xx_ogre_skeleton.c
    src/formats/image/ogre_skeleton/xx_ogre_skeleton_extractor.c
    src/formats/image/cal3d_skeleton/xx_cal3d_skeleton.c
    src/formats/image/cal3d_skeleton/xx_cal3d_skeleton_extractor.c
    src/formats/image/collada_dae/xx_collada_dae.c
    src/formats/image/collada_dae/xx_collada_dae_extractor.c
    src/formats/image/lightwave_scene/xx_lightwave_scene.c
    src/formats/image/lightwave_scene/xx_lightwave_scene_extractor.c
    src/formats/science/dsn6_density/xx_dsn6_density.c
    src/formats/science/dsn6_density/xx_dsn6_density_extractor.c
    src/formats/science/crystallography_mtz/xx_crystallography_mtz.c
    src/formats/science/crystallography_mtz/xx_crystallography_mtz_extractor.c
    src/formats/science/amira_mesh/xx_amira_mesh.c
    src/formats/science/amira_mesh/xx_amira_mesh_extractor.c
    src/formats/science/tetgen_mesh/xx_tetgen_mesh.c
    src/formats/science/tetgen_mesh/xx_tetgen_mesh_extractor.c
    src/formats/firmware/jedec_fuse/xx_jedec_fuse.c
    src/formats/firmware/jedec_fuse/xx_jedec_fuse_extractor.c
    src/formats/science/qchem_input/xx_qchem_input.c
    src/formats/science/qchem_input/xx_qchem_input_extractor.c
    src/formats/audio/adlib_bam/xx_adlib_bam.c
    src/formats/audio/adlib_bam/xx_adlib_bam_extractor.c
    src/formats/audio/adlib_bmf/xx_adlib_bmf.c
    src/formats/audio/adlib_bmf/xx_adlib_bmf_extractor.c
    src/formats/audio/creative_cmf/xx_creative_cmf.c
    src/formats/audio/creative_cmf/xx_creative_cmf_extractor.c
    src/formats/audio/adlib_dfm/xx_adlib_dfm.c
    src/formats/audio/adlib_dfm/xx_adlib_dfm_extractor.c
    src/formats/audio/adlib_lds/xx_adlib_lds.c
    src/formats/audio/adlib_lds/xx_adlib_lds_extractor.c
    src/formats/audio/adlib_mkj/xx_adlib_mkj.c
    src/formats/audio/adlib_mkj/xx_adlib_mkj_extractor.c
    src/formats/audio/adlib_rol/xx_adlib_rol.c
    src/formats/audio/adlib_rol/xx_adlib_rol_extractor.c
    src/formats/audio/adlib_sa2/xx_adlib_sa2.c
    src/formats/audio/adlib_sa2/xx_adlib_sa2_extractor.c
    src/formats/audio/faust_fmc/xx_faust_fmc.c
    src/formats/audio/faust_fmc/xx_faust_fmc_extractor.c
    src/formats/audio/softstar_rix/xx_softstar_rix.c
    src/formats/audio/softstar_rix/xx_softstar_rix_extractor.c
    src/formats/science/genomics_bed/xx_genomics_bed.c
    src/formats/science/genomics_bed/xx_genomics_bed_extractor.c
    src/formats/science/genomics_wiggle/xx_genomics_wiggle.c
    src/formats/science/genomics_wiggle/xx_genomics_wiggle_extractor.c
    src/formats/science/genomics_gtf/xx_genomics_gtf.c
    src/formats/science/genomics_gtf/xx_genomics_gtf_extractor.c
    src/formats/science/genomics_agp/xx_genomics_agp.c
    src/formats/science/genomics_agp/xx_genomics_agp_extractor.c
    src/formats/science/sequencing_abif/xx_sequencing_abif.c
    src/formats/science/sequencing_abif/xx_sequencing_abif_extractor.c
    src/formats/science/sequencing_scf/xx_sequencing_scf.c
    src/formats/science/sequencing_scf/xx_sequencing_scf_extractor.c
    src/formats/science/genomics_sff/xx_genomics_sff.c
    src/formats/science/genomics_sff/xx_genomics_sff_extractor.c
    src/formats/image/lut_spi1d/xx_lut_spi1d.c
    src/formats/image/lut_spi1d/xx_lut_spi1d_extractor.c
    src/formats/image/lut_spi3d/xx_lut_spi3d.c
    src/formats/image/lut_spi3d/xx_lut_spi3d_extractor.c
    src/formats/image/lut_cinespace_csp/xx_lut_cinespace_csp.c
    src/formats/image/lut_cinespace_csp/xx_lut_cinespace_csp_extractor.c
    src/formats/other/ntp_message/xx_ntp_message.c
    src/formats/other/ntp_message/xx_ntp_message_extractor.c
    src/formats/other/rtp_rtcp/xx_rtp_rtcp.c
    src/formats/other/rtp_rtcp/xx_rtp_rtcp_extractor.c
    src/formats/other/bgp_messages/xx_bgp_messages.c
    src/formats/other/bgp_messages/xx_bgp_messages_extractor.c
    src/formats/other/ospf_packet/xx_ospf_packet.c
    src/formats/other/ospf_packet/xx_ospf_packet_extractor.c
    src/formats/other/sctp_packet/xx_sctp_packet.c
    src/formats/other/sctp_packet/xx_sctp_packet_extractor.c
    src/formats/other/isakmp_message/xx_isakmp_message.c
    src/formats/other/isakmp_message/xx_isakmp_message_extractor.c
    src/formats/other/ssh_transport/xx_ssh_transport.c
    src/formats/other/ssh_transport/xx_ssh_transport_extractor.c
    src/formats/other/smtp_transcript/xx_smtp_transcript.c
    src/formats/other/smtp_transcript/xx_smtp_transcript_extractor.c
    src/formats/other/pkcs8_private_key/xx_pkcs8_private_key.c
    src/formats/other/pkcs8_private_key/xx_pkcs8_private_key_extractor.c
    src/formats/other/putty_ppk/xx_putty_ppk.c
    src/formats/other/putty_ppk/xx_putty_ppk_extractor.c
    src/formats/other/openssh_certificate/xx_openssh_certificate.c
    src/formats/other/openssh_certificate/xx_openssh_certificate_extractor.c
    src/formats/other/safetensors/xx_safetensors.c
    src/formats/other/safetensors/xx_safetensors_extractor.c
    src/formats/other/gguf/xx_gguf.c
    src/formats/other/gguf/xx_gguf_extractor.c
    src/formats/database/cdb_database/xx_cdb_database.c
    src/formats/database/cdb_database/xx_cdb_database_extractor.c
    src/formats/other/stomp_frames/xx_stomp_frames.c
    src/formats/other/stomp_frames/xx_stomp_frames_extractor.c
    src/formats/font/fontforge_sfd/xx_fontforge_sfd.c
    src/formats/font/fontforge_sfd/xx_fontforge_sfd_extractor.c
    src/formats/font/grub_pff2/xx_grub_pff2.c
    src/formats/font/grub_pff2/xx_grub_pff2_extractor.c
    src/formats/image/opengex_model/xx_opengex_model.c
    src/formats/image/opengex_model/xx_opengex_model_extractor.c
    src/formats/image/bvh_motion/xx_bvh_motion.c
    src/formats/image/bvh_motion/xx_bvh_motion_extractor.c
    src/formats/image/directx_x/xx_directx_x.c
    src/formats/image/directx_x/xx_directx_x_extractor.c
    src/formats/image/gts_surface/xx_gts_surface.c
    src/formats/image/gts_surface/xx_gts_surface_extractor.c
    src/formats/science/medit_mesh/xx_medit_mesh.c
    src/formats/science/medit_mesh/xx_medit_mesh_extractor.c
    src/formats/science/gocad_model/xx_gocad_model.c
    src/formats/science/gocad_model/xx_gocad_model_extractor.c
    src/formats/science/nastran_bulk/xx_nastran_bulk.c
    src/formats/science/nastran_bulk/xx_nastran_bulk_extractor.c
    src/formats/science/abaqus_input/xx_abaqus_input.c
    src/formats/science/abaqus_input/xx_abaqus_input_extractor.c
    src/formats/science/ensight_gold_geometry/xx_ensight_gold_geometry.c
    src/formats/science/ensight_gold_geometry/xx_ensight_gold_geometry_extractor.c
    src/formats/science/gmv_mesh/xx_gmv_mesh.c
    src/formats/science/gmv_mesh/xx_gmv_mesh_extractor.c
    src/formats/science/usgs_dem/xx_usgs_dem.c
    src/formats/science/usgs_dem/xx_usgs_dem_extractor.c
    src/formats/science/dted_elevation/xx_dted_elevation.c
    src/formats/science/dted_elevation/xx_dted_elevation_extractor.c
    src/formats/science/mapinfo_mif/xx_mapinfo_mif.c
    src/formats/science/mapinfo_mif/xx_mapinfo_mif_extractor.c
    src/formats/audio/tracker_coconizer/xx_tracker_coconizer.c
    src/formats/audio/tracker_coconizer/xx_tracker_coconizer_extractor.c
    src/formats/audio/tracker_real/xx_tracker_real.c
    src/formats/audio/tracker_real/xx_tracker_real_extractor.c
    src/formats/audio/tracker_megatracker/xx_tracker_megatracker.c
    src/formats/audio/tracker_megatracker/xx_tracker_megatracker_extractor.c
    src/formats/audio/amos_music_bank/xx_amos_music_bank.c
    src/formats/audio/amos_music_bank/xx_amos_music_bank_extractor.c
    src/formats/audio/adlib_rad/xx_adlib_rad.c
    src/formats/audio/adlib_rad/xx_adlib_rad_extractor.c
    src/formats/audio/adlib_amd/xx_adlib_amd.c
    src/formats/audio/adlib_amd/xx_adlib_amd_extractor.c
    src/formats/audio/adlib_hsc/xx_adlib_hsc.c
    src/formats/audio/adlib_hsc/xx_adlib_hsc_extractor.c
    src/formats/audio/adlib_d00/xx_adlib_d00.c
    src/formats/audio/adlib_d00/xx_adlib_d00_extractor.c
    src/formats/audio/adlib_bnk/xx_adlib_bnk.c
    src/formats/audio/adlib_bnk/xx_adlib_bnk_extractor.c
    src/formats/audio/dosbox_dro/xx_dosbox_dro.c
    src/formats/audio/dosbox_dro/xx_dosbox_dro_extractor.c
    src/formats/science/genomics_genbank/xx_genomics_genbank.c
    src/formats/science/genomics_genbank/xx_genomics_genbank_extractor.c
    src/formats/science/genomics_embl/xx_genomics_embl.c
    src/formats/science/genomics_embl/xx_genomics_embl_extractor.c
    src/formats/science/genomics_swissprot/xx_genomics_swissprot.c
    src/formats/science/genomics_swissprot/xx_genomics_swissprot_extractor.c
    src/formats/science/alignment_clustal/xx_alignment_clustal.c
    src/formats/science/alignment_clustal/xx_alignment_clustal_extractor.c
    src/formats/science/alignment_stockholm/xx_alignment_stockholm.c
    src/formats/science/alignment_stockholm/xx_alignment_stockholm_extractor.c
    src/formats/science/alignment_phylip/xx_alignment_phylip.c
    src/formats/science/alignment_phylip/xx_alignment_phylip_extractor.c
    src/formats/science/alignment_maf/xx_alignment_maf.c
    src/formats/science/alignment_maf/xx_alignment_maf_extractor.c
    src/formats/science/alignment_mauve/xx_alignment_mauve.c
    src/formats/science/alignment_mauve/xx_alignment_mauve_extractor.c
    src/formats/science/ucsc_nib/xx_ucsc_nib.c
    src/formats/science/ucsc_nib/xx_ucsc_nib_extractor.c
    src/formats/science/assembly_gfa/xx_assembly_gfa.c
    src/formats/science/assembly_gfa/xx_assembly_gfa_extractor.c
    src/formats/other/http1_message/xx_http1_message.c
    src/formats/other/http1_message/xx_http1_message_extractor.c
    src/formats/other/websocket_frames/xx_websocket_frames.c
    src/formats/other/websocket_frames/xx_websocket_frames_extractor.c
    src/formats/other/coap_message/xx_coap_message.c
    src/formats/other/coap_message/xx_coap_message_extractor.c
    src/formats/other/stun_message/xx_stun_message.c
    src/formats/other/stun_message/xx_stun_message_extractor.c
    src/formats/other/dhcp_message/xx_dhcp_message.c
    src/formats/other/dhcp_message/xx_dhcp_message_extractor.c
    src/formats/other/radius_packet/xx_radius_packet.c
    src/formats/other/radius_packet/xx_radius_packet_extractor.c
    src/formats/other/snmp_message/xx_snmp_message.c
    src/formats/other/snmp_message/xx_snmp_message_extractor.c
    src/formats/other/ldap_message/xx_ldap_message.c
    src/formats/other/ldap_message/xx_ldap_message_extractor.c
    src/formats/other/tls_records/xx_tls_records.c
    src/formats/other/tls_records/xx_tls_records_extractor.c
    src/formats/other/jks_keystore/xx_jks_keystore.c
    src/formats/other/jks_keystore/xx_jks_keystore_extractor.c
    src/formats/other/java_serialization/xx_java_serialization.c
    src/formats/other/java_serialization/xx_java_serialization_extractor.c
    src/formats/other/x509_crl/xx_x509_crl.c
    src/formats/other/x509_crl/xx_x509_crl_extractor.c
    src/formats/other/ocsp_response/xx_ocsp_response.c
    src/formats/other/ocsp_response/xx_ocsp_response_extractor.c
    src/formats/database/lmdb_data/xx_lmdb_data.c
    src/formats/database/lmdb_data/xx_lmdb_data_extractor.c
    src/formats/database/gdbm_dump/xx_gdbm_dump.c
    src/formats/database/gdbm_dump/xx_gdbm_dump_extractor.c
    src/formats/other/adobe_acb/xx_adobe_acb.c
    src/formats/other/adobe_acb/xx_adobe_acb_extractor.c
    src/formats/image/jasc_palette/xx_jasc_palette.c
    src/formats/image/jasc_palette/xx_jasc_palette_extractor.c
    src/formats/image/x11_xbm/xx_x11_xbm.c
    src/formats/image/x11_xbm/xx_x11_xbm_extractor.c
    src/formats/image/jpeg2000_pgx/xx_jpeg2000_pgx.c
    src/formats/image/jpeg2000_pgx/xx_jpeg2000_pgx_extractor.c
    src/formats/image/amiga_diskobject/xx_amiga_diskobject.c
    src/formats/image/amiga_diskobject/xx_amiga_diskobject_extractor.c
    src/formats/font/tex_vf/xx_tex_vf.c
    src/formats/font/tex_vf/xx_tex_vf_extractor.c
    src/formats/science/esri_ascii_grid/xx_esri_ascii_grid.c
    src/formats/science/esri_ascii_grid/xx_esri_ascii_grid_extractor.c
    src/formats/science/surfer_grid/xx_surfer_grid.c
    src/formats/science/surfer_grid/xx_surfer_grid_extractor.c
    src/formats/science/gxf_grid/xx_gxf_grid.c
    src/formats/science/gxf_grid/xx_gxf_grid_extractor.c
    src/formats/science/ogc_wkt/xx_ogc_wkt.c
    src/formats/science/ogc_wkt/xx_ogc_wkt_extractor.c
    src/formats/science/step_part21/xx_step_part21.c
    src/formats/science/step_part21/xx_step_part21_extractor.c
    src/formats/science/gerber_rs274x/xx_gerber_rs274x.c
    src/formats/science/gerber_rs274x/xx_gerber_rs274x_extractor.c
    src/formats/science/excellon_drill/xx_excellon_drill.c
    src/formats/science/excellon_drill/xx_excellon_drill_extractor.c
    src/formats/image/vrml_scene/xx_vrml_scene.c
    src/formats/image/vrml_scene/xx_vrml_scene_extractor.c
    src/formats/image/renderman_rib/xx_renderman_rib.c
    src/formats/image/renderman_rib/xx_renderman_rib_extractor.c
    src/formats/audio/asylum_amf/xx_asylum_amf.c
    src/formats/audio/asylum_amf/xx_asylum_amf_extractor.c
    src/formats/audio/tracker_stx/xx_tracker_stx.c
    src/formats/audio/tracker_stx/xx_tracker_stx_extractor.c
    src/formats/audio/tracker_dtm/xx_tracker_dtm.c
    src/formats/audio/tracker_dtm/xx_tracker_dtm_extractor.c
    src/formats/audio/tracker_soundfx/xx_tracker_soundfx.c
    src/formats/audio/tracker_soundfx/xx_tracker_soundfx_extractor.c
    src/formats/audio/tracker_funk/xx_tracker_funk.c
    src/formats/audio/tracker_funk/xx_tracker_funk_extractor.c
    src/formats/audio/tracker_archimedes/xx_tracker_archimedes.c
    src/formats/audio/tracker_archimedes/xx_tracker_archimedes_extractor.c
    src/formats/filesystem/pce_psi/xx_pce_psi.c
    src/formats/filesystem/pce_psi/xx_pce_psi_extractor.c
    src/formats/filesystem/pc98_d88/xx_pc98_d88.c
    src/formats/filesystem/pc98_d88/xx_pc98_d88_extractor.c
    src/formats/filesystem/hxc_mfm/xx_hxc_mfm.c
    src/formats/filesystem/hxc_mfm/xx_hxc_mfm_extractor.c
    src/formats/filesystem/yaze_ydsk/xx_yaze_ydsk.c
    src/formats/filesystem/yaze_ydsk/xx_yaze_ydsk_extractor.c
    src/formats/science/lammps_data/xx_lammps_data.c
    src/formats/science/lammps_data/xx_lammps_data_extractor.c
    src/formats/science/lammps_dump/xx_lammps_dump.c
    src/formats/science/lammps_dump/xx_lammps_dump_extractor.c
    src/formats/science/shelx_res/xx_shelx_res.c
    src/formats/science/shelx_res/xx_shelx_res_extractor.c
    src/formats/science/turbomole_coord/xx_turbomole_coord.c
    src/formats/science/turbomole_coord/xx_turbomole_coord_extractor.c
    src/formats/science/charmm_crd/xx_charmm_crd.c
    src/formats/science/charmm_crd/xx_charmm_crd_extractor.c
    src/formats/science/castep_cell/xx_castep_cell.c
    src/formats/science/castep_cell/xx_castep_cell_extractor.c
    src/formats/science/crystal_fort34/xx_crystal_fort34.c
    src/formats/science/crystal_fort34/xx_crystal_fort34_extractor.c
    src/formats/science/siesta_xv/xx_siesta_xv.c
    src/formats/science/siesta_xv/xx_siesta_xv_extractor.c
    src/formats/science/harwell_boeing/xx_harwell_boeing.c
    src/formats/science/harwell_boeing/xx_harwell_boeing_extractor.c
    src/formats/science/openfoam_points/xx_openfoam_points.c
    src/formats/science/openfoam_points/xx_openfoam_points_extractor.c
    src/formats/game/minecraft_nbt/xx_minecraft_nbt.c
    src/formats/game/minecraft_nbt/xx_minecraft_nbt_extractor.c
    src/formats/other/amazon_ion_binary/xx_amazon_ion_binary.c
    src/formats/other/amazon_ion_binary/xx_amazon_ion_binary_extractor.c
    src/formats/database/leveldb_log/xx_leveldb_log.c
    src/formats/database/leveldb_log/xx_leveldb_log_extractor.c
    src/formats/other/dns_message/xx_dns_message.c
    src/formats/other/dns_message/xx_dns_message_extractor.c
    src/formats/database/rocksdb_blob/xx_rocksdb_blob.c
    src/formats/database/rocksdb_blob/xx_rocksdb_blob_extractor.c
    src/formats/other/mongodb_wire/xx_mongodb_wire.c
    src/formats/other/mongodb_wire/xx_mongodb_wire_extractor.c
    src/formats/other/redis_resp/xx_redis_resp.c
    src/formats/other/redis_resp/xx_redis_resp_extractor.c
    src/formats/other/mqtt_packets/xx_mqtt_packets.c
    src/formats/other/mqtt_packets/xx_mqtt_packets_extractor.c
    src/formats/other/amqp_frames/xx_amqp_frames.c
    src/formats/other/amqp_frames/xx_amqp_frames_extractor.c
    src/formats/other/thrift_compact/xx_thrift_compact.c
    src/formats/other/thrift_compact/xx_thrift_compact_extractor.c
    src/formats/other/x509_certificate/xx_x509_certificate.c
    src/formats/other/x509_certificate/xx_x509_certificate_extractor.c
    src/formats/other/pkcs10_csr/xx_pkcs10_csr.c
    src/formats/other/pkcs10_csr/xx_pkcs10_csr_extractor.c
    src/formats/other/pkcs12_pfx/xx_pkcs12_pfx.c
    src/formats/other/pkcs12_pfx/xx_pkcs12_pfx_extractor.c
    src/formats/other/openssh_private_key/xx_openssh_private_key.c
    src/formats/other/openssh_private_key/xx_openssh_private_key_extractor.c
    src/formats/other/kerberos_keytab/xx_kerberos_keytab.c
    src/formats/other/kerberos_keytab/xx_kerberos_keytab_extractor.c
    src/formats/image/gimp_gpl/xx_gimp_gpl.c
    src/formats/image/gimp_gpl/xx_gimp_gpl_extractor.c
    src/formats/image/gimp_ggr/xx_gimp_ggr.c
    src/formats/image/gimp_ggr/xx_gimp_ggr_extractor.c
    src/formats/image/iridas_cube_lut/xx_iridas_cube_lut.c
    src/formats/image/iridas_cube_lut/xx_iridas_cube_lut_extractor.c
    src/formats/image/hpgl_plot/xx_hpgl_plot.c
    src/formats/image/hpgl_plot/xx_hpgl_plot_extractor.c
    src/formats/image/paintshop_psp/xx_paintshop_psp.c
    src/formats/image/paintshop_psp/xx_paintshop_psp_extractor.c
    src/formats/image/photoshop_pat/xx_photoshop_pat.c
    src/formats/image/photoshop_pat/xx_photoshop_pat_extractor.c
    src/formats/image/mmd_pmx/xx_mmd_pmx.c
    src/formats/image/mmd_pmx/xx_mmd_pmx_extractor.c
    src/formats/image/metasequoia_mqo/xx_metasequoia_mqo.c
    src/formats/image/metasequoia_mqo/xx_metasequoia_mqo_extractor.c
    src/formats/science/calma_gdsii/xx_calma_gdsii.c
    src/formats/science/calma_gdsii/xx_calma_gdsii_extractor.c
    src/formats/image/autodesk_ase/xx_autodesk_ase.c
    src/formats/image/autodesk_ase/xx_autodesk_ase_extractor.c
    src/formats/science/freesurfer_surface/xx_freesurfer_surface.c
    src/formats/science/freesurfer_surface/xx_freesurfer_surface_extractor.c
    src/formats/science/gmsh_msh/xx_gmsh_msh.c
    src/formats/science/gmsh_msh/xx_gmsh_msh_extractor.c
    src/formats/science/netgen_vol/xx_netgen_vol.c
    src/formats/science/netgen_vol/xx_netgen_vol_extractor.c
    src/formats/font/font_afm/xx_font_afm.c
    src/formats/font/font_afm/xx_font_afm_extractor.c
    src/formats/game/tiled_tmx/xx_tiled_tmx.c
    src/formats/game/tiled_tmx/xx_tiled_tmx_extractor.c
    src/formats/audio/nintendo_sdat/xx_nintendo_sdat.c
    src/formats/audio/nintendo_sdat/xx_nintendo_sdat_extractor.c
    src/formats/audio/sony_vab/xx_sony_vab.c
    src/formats/audio/sony_vab/xx_sony_vab_extractor.c
    src/formats/audio/yamaha_ym/xx_yamaha_ym.c
    src/formats/audio/yamaha_ym/xx_yamaha_ym_extractor.c
    src/formats/audio/zx_ayemul/xx_zx_ayemul.c
    src/formats/audio/zx_ayemul/xx_zx_ayemul_extractor.c
    src/formats/filesystem/dragon_vdk/xx_dragon_vdk.c
    src/formats/filesystem/dragon_vdk/xx_dragon_vdk_extractor.c
    src/formats/filesystem/apple_a2r/xx_apple_a2r.c
    src/formats/filesystem/apple_a2r/xx_apple_a2r_extractor.c
    src/formats/filesystem/atari_atr/xx_atari_atr.c
    src/formats/filesystem/atari_atr/xx_atari_atr_extractor.c
    src/formats/filesystem/atari_pasti_stx/xx_atari_pasti_stx.c
    src/formats/filesystem/atari_pasti_stx/xx_atari_pasti_stx_extractor.c
    src/formats/filesystem/amiga_ipf/xx_amiga_ipf.c
    src/formats/filesystem/amiga_ipf/xx_amiga_ipf_extractor.c
    src/formats/audio/tracker_dtt/xx_tracker_dtt.c
    src/formats/audio/tracker_dtt/xx_tracker_dtt_extractor.c
    src/formats/science/gaussian_cube/xx_gaussian_cube.c
    src/formats/science/gaussian_cube/xx_gaussian_cube_extractor.c
    src/formats/science/molecule_xyz/xx_molecule_xyz.c
    src/formats/science/molecule_xyz/xx_molecule_xyz_extractor.c
    src/formats/science/mdl_molfile/xx_mdl_molfile.c
    src/formats/science/mdl_molfile/xx_mdl_molfile_extractor.c
    src/formats/science/tripos_mol2/xx_tripos_mol2.c
    src/formats/science/tripos_mol2/xx_tripos_mol2_extractor.c
    src/formats/science/xcrysden_xsf/xx_xcrysden_xsf.c
    src/formats/science/xcrysden_xsf/xx_xcrysden_xsf_extractor.c
    src/formats/science/amber_prmtop/xx_amber_prmtop.c
    src/formats/science/amber_prmtop/xx_amber_prmtop_extractor.c
    src/formats/science/amber_restart/xx_amber_restart.c
    src/formats/science/amber_restart/xx_amber_restart_extractor.c
    src/formats/science/gaussian_fchk/xx_gaussian_fchk.c
    src/formats/science/gaussian_fchk/xx_gaussian_fchk_extractor.c
    src/formats/science/jcamp_dx/xx_jcamp_dx.c
    src/formats/science/jcamp_dx/xx_jcamp_dx_extractor.c
    src/formats/science/dl_poly_config/xx_dl_poly_config.c
    src/formats/science/dl_poly_config/xx_dl_poly_config_extractor.c
    src/formats/archive/nix_nar/xx_nix_nar.c
    src/formats/archive/nix_nar/xx_nix_nar_extractor.c
    src/formats/database/redis_rdb/xx_redis_rdb.c
    src/formats/database/redis_rdb/xx_redis_rdb_extractor.c
    src/formats/database/postgres_custom/xx_postgres_custom.c
    src/formats/database/postgres_custom/xx_postgres_custom_extractor.c
    src/formats/database/mysql_binlog/xx_mysql_binlog.c
    src/formats/database/mysql_binlog/xx_mysql_binlog_extractor.c
    src/formats/other/kafka_record_batch/xx_kafka_record_batch.c
    src/formats/other/kafka_record_batch/xx_kafka_record_batch_extractor.c
    src/formats/executable/android_binary_xml/xx_android_binary_xml.c
    src/formats/executable/android_binary_xml/xx_android_binary_xml_extractor.c
    src/formats/executable/android_resources_arsc/xx_android_resources_arsc.c
    src/formats/executable/android_resources_arsc/xx_android_resources_arsc_extractor.c
    src/formats/other/msgpack/xx_msgpack.c
    src/formats/other/msgpack/xx_msgpack_extractor.c
    src/formats/other/ubjson/xx_ubjson.c
    src/formats/other/ubjson/xx_ubjson_extractor.c
    src/formats/other/bittorrent_metainfo/xx_bittorrent_metainfo.c
    src/formats/other/bittorrent_metainfo/xx_bittorrent_metainfo_extractor.c
    src/formats/other/erlang_external_term/xx_erlang_external_term.c
    src/formats/other/erlang_external_term/xx_erlang_external_term_extractor.c
    src/formats/other/capnproto_message/xx_capnproto_message.c
    src/formats/other/capnproto_message/xx_capnproto_message_extractor.c
    src/formats/other/dbus_message/xx_dbus_message.c
    src/formats/other/dbus_message/xx_dbus_message_extractor.c
    src/formats/other/windows_shell_link/xx_windows_shell_link.c
    src/formats/other/windows_shell_link/xx_windows_shell_link_extractor.c
    src/formats/other/pkcs7_cms/xx_pkcs7_cms.c
    src/formats/other/pkcs7_cms/xx_pkcs7_cms_extractor.c
    src/formats/image/wavefront_obj/xx_wavefront_obj.c
    src/formats/image/wavefront_obj/xx_wavefront_obj_extractor.c
    src/formats/image/off_mesh/xx_off_mesh.c
    src/formats/image/off_mesh/xx_off_mesh_extractor.c
    src/formats/image/ac3d_model/xx_ac3d_model.c
    src/formats/image/ac3d_model/xx_ac3d_model_extractor.c
    src/formats/image/qubicle_qb/xx_qubicle_qb.c
    src/formats/image/qubicle_qb/xx_qubicle_qb_extractor.c
    src/formats/image/terragen_ter/xx_terragen_ter.c
    src/formats/image/terragen_ter/xx_terragen_ter_extractor.c
    src/formats/image/gimp_xcf/xx_gimp_xcf.c
    src/formats/image/gimp_xcf/xx_gimp_xcf_extractor.c
    src/formats/image/photoshop_abr/xx_photoshop_abr.c
    src/formats/image/photoshop_abr/xx_photoshop_abr_extractor.c
    src/formats/image/softimage_pic/xx_softimage_pic.c
    src/formats/image/softimage_pic/xx_softimage_pic_extractor.c
    src/formats/image/alias_pix/xx_alias_pix.c
    src/formats/image/alias_pix/xx_alias_pix_extractor.c
    src/formats/image/qt_qpicture/xx_qt_qpicture.c
    src/formats/image/qt_qpicture/xx_qt_qpicture_extractor.c
    src/formats/font/font_type1_pfb/xx_font_type1_pfb.c
    src/formats/font/font_type1_pfb/xx_font_type1_pfb_extractor.c
    src/formats/font/font_gem_fnt/xx_font_gem_fnt.c
    src/formats/font/font_gem_fnt/xx_font_gem_fnt_extractor.c
    src/formats/font/tex_gf/xx_tex_gf.c
    src/formats/font/tex_gf/xx_tex_gf_extractor.c
    src/formats/image/bpg_image/xx_bpg_image.c
    src/formats/image/bpg_image/xx_bpg_image_extractor.c
    src/formats/video/mng_animation/xx_mng_animation.c
    src/formats/video/mng_animation/xx_mng_animation_extractor.c
    src/formats/game/atari_7800_a78/xx_atari_7800_a78.c
    src/formats/game/atari_7800_a78/xx_atari_7800_a78_extractor.c
    src/formats/filesystem/commodore_pc64/xx_commodore_pc64.c
    src/formats/filesystem/commodore_pc64/xx_commodore_pc64_extractor.c
    src/formats/filesystem/atari_cas/xx_atari_cas.c
    src/formats/filesystem/atari_cas/xx_atari_cas_extractor.c
    src/formats/filesystem/msx_cas/xx_msx_cas.c
    src/formats/filesystem/msx_cas/xx_msx_cas_extractor.c
    src/formats/filesystem/oric_tap/xx_oric_tap.c
    src/formats/filesystem/oric_tap/xx_oric_tap_extractor.c
    src/formats/filesystem/dragon_cas/xx_dragon_cas.c
    src/formats/filesystem/dragon_cas/xx_dragon_cas_extractor.c
    src/formats/audio/amiga_ahx/xx_amiga_ahx.c
    src/formats/audio/amiga_ahx/xx_amiga_ahx_extractor.c
    src/formats/game/amstrad_cpc_sna/xx_amstrad_cpc_sna.c
    src/formats/game/amstrad_cpc_sna/xx_amstrad_cpc_sna_extractor.c
    src/formats/executable/vtech_vz/xx_vtech_vz.c
    src/formats/executable/vtech_vz/xx_vtech_vz_extractor.c
    src/formats/filesystem/zx_hobeta/xx_zx_hobeta.c
    src/formats/filesystem/zx_hobeta/xx_zx_hobeta_extractor.c
    src/formats/science/genomics_fasta/xx_genomics_fasta.c
    src/formats/science/genomics_fasta/xx_genomics_fasta_extractor.c
    src/formats/science/genomics_fastq/xx_genomics_fastq.c
    src/formats/science/genomics_fastq/xx_genomics_fastq_extractor.c
    src/formats/science/genomics_sam/xx_genomics_sam.c
    src/formats/science/genomics_sam/xx_genomics_sam_extractor.c
    src/formats/science/opendx_field/xx_opendx_field.c
    src/formats/science/opendx_field/xx_opendx_field_extractor.c
    src/formats/science/genomics_vcf/xx_genomics_vcf.c
    src/formats/science/genomics_vcf/xx_genomics_vcf_extractor.c
    src/formats/science/genomics_gff3/xx_genomics_gff3.c
    src/formats/science/genomics_gff3/xx_genomics_gff3_extractor.c
    src/formats/science/protein_pdb/xx_protein_pdb.c
    src/formats/science/protein_pdb/xx_protein_pdb_extractor.c
    src/formats/science/protein_mmcif/xx_protein_mmcif.c
    src/formats/science/protein_mmcif/xx_protein_mmcif_extractor.c
    src/formats/science/matrix_market/xx_matrix_market.c
    src/formats/science/matrix_market/xx_matrix_market_extractor.c
    src/formats/science/gromacs_gro/xx_gromacs_gro.c
    src/formats/science/gromacs_gro/xx_gromacs_gro_extractor.c
    src/formats/executable/microsoft_msf/xx_microsoft_msf.c
    src/formats/executable/microsoft_msf/xx_microsoft_msf_extractor.c
    src/formats/executable/pef/xx_pef.c
    src/formats/executable/pef/xx_pef_extractor.c
    src/formats/other/rprj/xx_rprj.c
    src/formats/other/rprj/xx_rprj_extractor.c
    src/formats/other/windows_registry_hive/xx_windows_registry_hive.c
    src/formats/other/windows_registry_hive/xx_windows_registry_hive_extractor.c
    src/formats/other/windows_evtx/xx_windows_evtx.c
    src/formats/other/windows_evtx/xx_windows_evtx_extractor.c
    src/formats/other/binary_plist/xx_binary_plist.c
    src/formats/other/binary_plist/xx_binary_plist_extractor.c
    src/formats/other/mongodb_bson/xx_mongodb_bson.c
    src/formats/other/mongodb_bson/xx_mongodb_bson_extractor.c
    src/formats/other/cbor/xx_cbor.c
    src/formats/other/cbor/xx_cbor_extractor.c
    src/formats/document/openzim/xx_openzim.c
    src/formats/document/openzim/xx_openzim_extractor.c
    src/formats/database/apache_orc/xx_apache_orc.c
    src/formats/database/apache_orc/xx_apache_orc_extractor.c
    src/formats/database/hadoop_sequencefile/xx_hadoop_sequencefile.c
    src/formats/database/hadoop_sequencefile/xx_hadoop_sequencefile_extractor.c
    src/formats/database/leveldb_sstable/xx_leveldb_sstable.c
    src/formats/database/leveldb_sstable/xx_leveldb_sstable_extractor.c
    src/formats/archive/snappy_framed/xx_snappy_framed.c
    src/formats/archive/snappy_framed/xx_snappy_framed_extractor.c
    src/formats/archive/lzf_stream/xx_lzf_stream.c
    src/formats/archive/lzf_stream/xx_lzf_stream_extractor.c
    src/formats/archive/fastlz_sixpack/xx_fastlz_sixpack.c
    src/formats/archive/fastlz_sixpack/xx_fastlz_sixpack_extractor.c
    src/formats/executable/linux_btf/xx_linux_btf.c
    src/formats/executable/linux_btf/xx_linux_btf_extractor.c
    src/formats/science/flatgeobuf/xx_flatgeobuf.c
    src/formats/science/flatgeobuf/xx_flatgeobuf_extractor.c
    src/formats/image/astc_texture/xx_astc_texture.c
    src/formats/image/astc_texture/xx_astc_texture_extractor.c
    src/formats/image/pkm_texture/xx_pkm_texture.c
    src/formats/image/pkm_texture/xx_pkm_texture_extractor.c
    src/formats/image/basis_texture/xx_basis_texture.c
    src/formats/image/basis_texture/xx_basis_texture_extractor.c
    src/formats/image/openctm_mesh/xx_openctm_mesh.c
    src/formats/image/openctm_mesh/xx_openctm_mesh_extractor.c
    src/formats/font/font_bdf/xx_font_bdf.c
    src/formats/font/font_bdf/xx_font_bdf_extractor.c
    src/formats/font/font_pcf/xx_font_pcf.c
    src/formats/font/font_pcf/xx_font_pcf_extractor.c
    src/formats/font/font_psf/xx_font_psf.c
    src/formats/font/font_psf/xx_font_psf_extractor.c
    src/formats/font/font_windows_fnt/xx_font_windows_fnt.c
    src/formats/font/font_windows_fnt/xx_font_windows_fnt_extractor.c
    src/formats/font/tex_tfm/xx_tex_tfm.c
    src/formats/font/tex_tfm/xx_tex_tfm_extractor.c
    src/formats/font/tex_pk/xx_tex_pk.c
    src/formats/font/tex_pk/xx_tex_pk_extractor.c
    src/formats/document/tex_dvi/xx_tex_dvi.c
    src/formats/document/tex_dvi/xx_tex_dvi_extractor.c
    src/formats/image/netpbm_pfm/xx_netpbm_pfm.c
    src/formats/image/netpbm_pfm/xx_netpbm_pfm_extractor.c
    src/formats/audio/steinberg_vst3preset/xx_steinberg_vst3preset.c
    src/formats/audio/steinberg_vst3preset/xx_steinberg_vst3preset_extractor.c
    src/formats/font/font_bmfont/xx_font_bmfont.c
    src/formats/font/font_bmfont/xx_font_bmfont_extractor.c
    src/formats/font/processing_vlw/xx_processing_vlw.c
    src/formats/font/processing_vlw/xx_processing_vlw_extractor.c
    src/formats/audio/snes_spc/xx_snes_spc.c
    src/formats/audio/snes_spc/xx_snes_spc_extractor.c
    src/formats/audio/gameboy_gbs/xx_gameboy_gbs.c
    src/formats/audio/gameboy_gbs/xx_gameboy_gbs_extractor.c
    src/formats/audio/sega_sgc/xx_sega_sgc.c
    src/formats/audio/sega_sgc/xx_sega_sgc_extractor.c
    src/formats/audio/s98_log/xx_s98_log.c
    src/formats/audio/s98_log/xx_s98_log_extractor.c
    src/formats/audio/atari_sap/xx_atari_sap.c
    src/formats/audio/atari_sap/xx_atari_sap_extractor.c
    src/formats/audio/sc68_music/xx_sc68_music.c
    src/formats/audio/sc68_music/xx_sc68_music_extractor.c
    src/formats/filesystem/zx_spectrum_pzx/xx_zx_spectrum_pzx.c
    src/formats/filesystem/zx_spectrum_pzx/xx_zx_spectrum_pzx_extractor.c
    src/formats/filesystem/acorn_uef/xx_acorn_uef.c
    src/formats/filesystem/acorn_uef/xx_acorn_uef_extractor.c
    src/formats/game/nintendo_unif/xx_nintendo_unif.c
    src/formats/game/nintendo_unif/xx_nintendo_unif_extractor.c
    src/formats/game/nintendo_fds/xx_nintendo_fds.c
    src/formats/game/nintendo_fds/xx_nintendo_fds_extractor.c
    src/formats/science/ucsc_bigwig/xx_ucsc_bigwig.c
    src/formats/science/ucsc_bigwig/xx_ucsc_bigwig_extractor.c
    src/formats/science/ucsc_bigbed/xx_ucsc_bigbed.c
    src/formats/science/ucsc_bigbed/xx_ucsc_bigbed_extractor.c
    src/formats/science/phylo_nexus/xx_phylo_nexus.c
    src/formats/science/phylo_nexus/xx_phylo_nexus_extractor.c
    src/formats/science/phylo_newick/xx_phylo_newick.c
    src/formats/science/phylo_newick/xx_phylo_newick_extractor.c
    src/formats/database/sqlite_rollback_journal/xx_sqlite_rollback_journal.c
    src/formats/database/sqlite_rollback_journal/xx_sqlite_rollback_journal_extractor.c
    src/formats/science/neuroscan_cnt/xx_neuroscan_cnt.c
    src/formats/science/neuroscan_cnt/xx_neuroscan_cnt_extractor.c
    src/formats/science/axona_tetrode/xx_axona_tetrode.c
    src/formats/science/axona_tetrode/xx_axona_tetrode_extractor.c
    src/formats/other/python_pickle/xx_python_pickle.c
    src/formats/other/python_pickle/xx_python_pickle_extractor.c
    src/formats/science/inivation_aedat/xx_inivation_aedat.c
    src/formats/science/inivation_aedat/xx_inivation_aedat_extractor.c
    src/formats/executable/python_marshal/xx_python_marshal.c
    src/formats/executable/python_marshal/xx_python_marshal_extractor.c
    src/formats/filesystem/vice_x64/xx_vice_x64.c
    src/formats/filesystem/vice_x64/xx_vice_x64_extractor.c
    src/formats/other/vice_snapshot/xx_vice_snapshot.c
    src/formats/other/vice_snapshot/xx_vice_snapshot_extractor.c
    src/formats/filesystem/commodore_g64/xx_commodore_g64.c
    src/formats/filesystem/commodore_g64/xx_commodore_g64_extractor.c
    src/formats/filesystem/commodore_p64/xx_commodore_p64.c
    src/formats/filesystem/commodore_p64/xx_commodore_p64_extractor.c
    src/formats/filesystem/commodore_tap/xx_commodore_tap.c
    src/formats/filesystem/commodore_tap/xx_commodore_tap_extractor.c
    src/formats/filesystem/zx_spectrum_tzx/xx_zx_spectrum_tzx.c
    src/formats/filesystem/zx_spectrum_tzx/xx_zx_spectrum_tzx_extractor.c
    src/formats/game/zx_spectrum_szx/xx_zx_spectrum_szx.c
    src/formats/game/zx_spectrum_szx/xx_zx_spectrum_szx_extractor.c
    src/formats/filesystem/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.c
    src/formats/filesystem/amstrad_cpc_dsk/xx_amstrad_cpc_dsk_extractor.c
    src/formats/filesystem/atari_st_msa/xx_atari_st_msa.c
    src/formats/filesystem/atari_st_msa/xx_atari_st_msa_extractor.c
    src/formats/filesystem/supercard_scp/xx_supercard_scp.c
    src/formats/filesystem/supercard_scp/xx_supercard_scp_extractor.c
    src/formats/filesystem/apple_woz/xx_apple_woz.c
    src/formats/filesystem/apple_woz/xx_apple_woz_extractor.c
    src/formats/audio/nintendo_nsf/xx_nintendo_nsf.c
    src/formats/audio/nintendo_nsf/xx_nintendo_nsf_extractor.c
    src/formats/audio/vgm_log/xx_vgm_log.c
    src/formats/audio/vgm_log/xx_vgm_log_extractor.c
    src/formats/audio/psid_sid/xx_psid_sid.c
    src/formats/audio/psid_sid/xx_psid_sid_extractor.c
    src/formats/audio/hes_sound/xx_hes_sound.c
    src/formats/audio/hes_sound/xx_hes_sound_extractor.c
    src/formats/audio/audio_dolby_ac3/xx_audio_dolby_ac3.c
    src/formats/audio/audio_dolby_ac3/xx_audio_dolby_ac3_extractor.c
    src/formats/audio/audio_mpeg_mp3/xx_audio_mpeg_mp3.c
    src/formats/audio/audio_mpeg_mp3/xx_audio_mpeg_mp3_extractor.c
    src/formats/audio/audio_aac_adts/xx_audio_aac_adts.c
    src/formats/audio/audio_aac_adts/xx_audio_aac_adts_extractor.c
    src/formats/audio/audio_monkeys_ape/xx_audio_monkeys_ape.c
    src/formats/audio/audio_monkeys_ape/xx_audio_monkeys_ape_extractor.c
    src/formats/video/mpeg_transport_stream/xx_mpeg_transport_stream.c
    src/formats/video/mpeg_transport_stream/xx_mpeg_transport_stream_extractor.c
    src/formats/video/mpeg_program_stream/xx_mpeg_program_stream.c
    src/formats/video/mpeg_program_stream/xx_mpeg_program_stream_extractor.c
    src/formats/video/realmedia_rm/xx_realmedia_rm.c
    src/formats/video/realmedia_rm/xx_realmedia_rm_extractor.c
    src/formats/video/idtech_roq/xx_idtech_roq.c
    src/formats/video/idtech_roq/xx_idtech_roq_extractor.c
    src/formats/video/rad_bink/xx_rad_bink.c
    src/formats/video/rad_bink/xx_rad_bink_extractor.c
    src/formats/video/rad_smacker/xx_rad_smacker.c
    src/formats/video/rad_smacker/xx_rad_smacker_extractor.c
    src/formats/archive/apple_diags/xx_apple_diags.c
    src/formats/archive/eid/xx_eid.c
    src/formats/installer/natata_ebook/xx_natata_ebook.c
    src/formats/archive/stor_resource/xx_stor_resource.c
    src/formats/archive/installshield_obl/xx_installshield_obl.c
    src/formats/archive/pftw_pkg/xx_pftw_pkg.c
    src/formats/archive/sod_installer/xx_sod_installer.c
    src/formats/archive/starforce_ovl/xx_starforce_ovl.c
    src/formats/archive/self_unstuffer/xx_self_unstuffer.c
    src/formats/archive/secura/xx_secura.c
    src/formats/archive/patchwise/xx_patchwise.c
    src/formats/archive/installjammer/xx_installjammer.c
    src/formats/video/scummvm_dxa/xx_scummvm_dxa.c
    src/formats/video/scummvm_dxa/xx_scummvm_dxa_reader_adapters.c
    src/formats/video/interplay_mve/xx_interplay_mve.c
    src/formats/video/interplay_mve/xx_interplay_mve_extractor.c
    src/formats/video/westwood_vqa/xx_westwood_vqa.c
    src/formats/video/westwood_vqa/xx_westwood_vqa_extractor.c
    src/formats/video/autodesk_flic/xx_autodesk_flic.c
    src/formats/video/autodesk_flic/xx_autodesk_flic_extractor.c
    src/formats/game/idtech_md5anim/xx_idtech_md5anim.c
    src/formats/game/idtech_md5anim/xx_idtech_md5anim_extractor.c
    src/formats/image/stereolithography_stl/xx_stereolithography_stl.c
    src/formats/image/stereolithography_stl/xx_stereolithography_stl_extractor.c
    src/formats/science/garmin_fit/xx_garmin_fit.c
    src/formats/science/garmin_fit/xx_garmin_fit_extractor.c
    src/formats/science/rosbag1/xx_rosbag1.c
    src/formats/science/rosbag1/xx_rosbag1_extractor.c
    src/formats/other/mcap/xx_mcap.c
    src/formats/other/mcap/xx_mcap_extractor.c
    src/formats/science/seismic_sac/xx_seismic_sac.c
    src/formats/science/seismic_sac/xx_seismic_sac_extractor.c
    src/formats/science/seismic_seg2/xx_seismic_seg2.c
    src/formats/science/seismic_seg2/xx_seismic_seg2_extractor.c
    src/formats/science/ucsc_twobit/xx_ucsc_twobit.c
    src/formats/science/ucsc_twobit/xx_ucsc_twobit_extractor.c
    src/formats/science/genomics_bgen/xx_genomics_bgen.c
    src/formats/science/genomics_bgen/xx_genomics_bgen_extractor.c
    src/formats/science/openephys_continuous/xx_openephys_continuous.c
    src/formats/science/openephys_continuous/xx_openephys_continuous_extractor.c
    src/formats/science/mountainsort_mda/xx_mountainsort_mda.c
    src/formats/science/mountainsort_mda/xx_mountainsort_mda_extractor.c
    src/formats/science/igor_ibw/xx_igor_ibw.c
    src/formats/science/igor_ibw/xx_igor_ibw_extractor.c
    src/formats/science/princeton_spe/xx_princeton_spe.c
    src/formats/science/princeton_spe/xx_princeton_spe_extractor.c
    src/formats/science/microscopy_spider/xx_microscopy_spider.c
    src/formats/science/microscopy_spider/xx_microscopy_spider_extractor.c
    src/formats/science/wmo_grib/xx_wmo_grib.c
    src/formats/science/wmo_grib/xx_wmo_grib_extractor.c
    src/formats/science/wmo_bufr/xx_wmo_bufr.c
    src/formats/science/wmo_bufr/xx_wmo_bufr_extractor.c
    src/formats/image/autocad_dxf/xx_autocad_dxf.c
    src/formats/image/autocad_dxf/xx_autocad_dxf_extractor.c
    src/formats/science/blackrock_nsx/xx_blackrock_nsx.c
    src/formats/science/blackrock_nsx/xx_blackrock_nsx_extractor.c
    src/formats/science/blackrock_nev/xx_blackrock_nev.c
    src/formats/science/blackrock_nev/xx_blackrock_nev_extractor.c
    src/formats/science/lecroy_trc/xx_lecroy_trc.c
    src/formats/science/lecroy_trc/xx_lecroy_trc_extractor.c
    src/formats/science/tektronix_isf/xx_tektronix_isf.c
    src/formats/science/tektronix_isf/xx_tektronix_isf_extractor.c
    src/formats/audio/ircam_sdif/xx_ircam_sdif.c
    src/formats/audio/ircam_sdif/xx_ircam_sdif_extractor.c
    src/formats/audio/tracker_liquid/xx_tracker_liquid.c
    src/formats/audio/tracker_liquid/xx_tracker_liquid_extractor.c
    src/formats/audio/tracker_dmf/xx_tracker_dmf.c
    src/formats/audio/tracker_dmf/xx_tracker_dmf_extractor.c
    src/formats/audio/tracker_ptm/xx_tracker_ptm.c
    src/formats/audio/tracker_ptm/xx_tracker_ptm_extractor.c
    src/formats/audio/tracker_ams/xx_tracker_ams.c
    src/formats/audio/tracker_ams/xx_tracker_ams_extractor.c
    src/formats/audio/tracker_digi/xx_tracker_digi.c
    src/formats/audio/tracker_digi/xx_tracker_digi_extractor.c
    src/formats/audio/tracker_emod/xx_tracker_emod.c
    src/formats/audio/tracker_emod/xx_tracker_emod_extractor.c
    src/formats/audio/tracker_mt2/xx_tracker_mt2.c
    src/formats/audio/tracker_mt2/xx_tracker_mt2_extractor.c
    src/formats/audio/audio_dsf/xx_audio_dsf.c
    src/formats/audio/audio_dsf/xx_audio_dsf_extractor.c
    src/formats/audio/audio_dff/xx_audio_dff.c
    src/formats/audio/audio_dff/xx_audio_dff_extractor.c
    src/formats/audio/audio_wave64/xx_audio_wave64.c
    src/formats/audio/audio_wave64/xx_audio_wave64_extractor.c
    src/formats/audio/audio_adx/xx_audio_adx.c
    src/formats/audio/audio_adx/xx_audio_adx_extractor.c
    src/formats/audio/audio_ast/xx_audio_ast.c
    src/formats/audio/audio_ast/xx_audio_ast_extractor.c
    src/formats/audio/audio_hca/xx_audio_hca.c
    src/formats/audio/audio_hca/xx_audio_hca_extractor.c
    src/formats/audio/iff_8svx/xx_iff_8svx.c
    src/formats/audio/iff_8svx/xx_iff_8svx_extractor.c
    src/formats/audio/audio_wavpack/xx_audio_wavpack.c
    src/formats/audio/audio_wavpack/xx_audio_wavpack_extractor.c
    src/formats/image/blender_blend/xx_blender_blend.c
    src/formats/image/blender_blend/xx_blender_blend_extractor.c
    src/formats/image/autodesk_fbx/xx_autodesk_fbx.c
    src/formats/image/autodesk_fbx/xx_autodesk_fbx_extractor.c
    src/formats/image/autodesk_3ds/xx_autodesk_3ds.c
    src/formats/image/autodesk_3ds/xx_autodesk_3ds_extractor.c
    src/formats/image/lightwave_lwo2/xx_lightwave_lwo2.c
    src/formats/image/lightwave_lwo2/xx_lightwave_lwo2_extractor.c
    src/formats/image/lightwave_mdd/xx_lightwave_mdd.c
    src/formats/image/lightwave_mdd/xx_lightwave_mdd_extractor.c
    src/formats/game/sony_psp_pbp/xx_sony_psp_pbp.c
    src/formats/game/sony_psp_pbp/xx_sony_psp_pbp_extractor.c
    src/formats/video/flash_video_flv/xx_flash_video_flv.c
    src/formats/video/flash_video_flv/xx_flash_video_flv_extractor.c
    src/formats/game/nintendo_n64_rom/xx_nintendo_n64_rom.c
    src/formats/game/nintendo_n64_rom/xx_nintendo_n64_rom_extractor.c
    src/formats/game/nintendo_gb_rom/xx_nintendo_gb_rom.c
    src/formats/game/nintendo_gb_rom/xx_nintendo_gb_rom_extractor.c
    src/formats/game/nintendo_gba_rom/xx_nintendo_gba_rom.c
    src/formats/game/nintendo_gba_rom/xx_nintendo_gba_rom_extractor.c
    src/formats/executable/menuet/xx_menuet.c
    src/formats/game/msx_rom/xx_msx_rom.c
    src/formats/executable/plan9_exec/xx_plan9_exec.c
    src/formats/game/sega_sms_rom/xx_sega_sms_rom.c
    src/formats/game/tic80/xx_tic80.c
    src/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom.c
    src/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom_info.c
    src/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom_strings.c
    src/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom_extractor.c
    src/formats/image/spring_s3o/xx_spring_s3o.c
    src/formats/image/spring_s3o/xx_spring_s3o_extractor.c
    src/formats/game/xna_xnb/xx_xna_xnb.c
    src/formats/game/xna_xnb/xx_xna_xnb_extractor.c
    src/formats/executable/lua_bytecode51/xx_lua_bytecode51.c
    src/formats/executable/lua_bytecode51/xx_lua_bytecode51_extractor.c
    src/formats/game/quake_md5mesh/xx_quake_md5mesh.c
    src/formats/game/quake_md5mesh/xx_quake_md5mesh_extractor.c
    src/formats/audio/tracker_mod/xx_tracker_mod.c
    src/formats/audio/tracker_mod/xx_tracker_mod_extractor.c
    src/formats/audio/tracker_far/xx_tracker_far.c
    src/formats/audio/tracker_far/xx_tracker_far_extractor.c
    src/formats/audio/tracker_mdl/xx_tracker_mdl.c
    src/formats/audio/tracker_mdl/xx_tracker_mdl_extractor.c
    src/formats/audio/tracker_gdm/xx_tracker_gdm.c
    src/formats/audio/tracker_gdm/xx_tracker_gdm_extractor.c
    src/formats/audio/tracker_dbm/xx_tracker_dbm.c
    src/formats/audio/tracker_dbm/xx_tracker_dbm_extractor.c
    src/formats/audio/tracker_med/xx_tracker_med.c
    src/formats/audio/tracker_med/xx_tracker_med_extractor.c
    src/formats/audio/tracker_imf/xx_tracker_imf.c
    src/formats/audio/tracker_imf/xx_tracker_imf_extractor.c
    src/formats/audio/tracker_amf/xx_tracker_amf.c
    src/formats/audio/tracker_amf/xx_tracker_amf_extractor.c
    src/formats/audio/tracker_psm/xx_tracker_psm.c
    src/formats/audio/tracker_psm/xx_tracker_psm_extractor.c
    src/formats/audio/steinberg_fxb/xx_steinberg_fxb.c
    src/formats/audio/steinberg_fxb/xx_steinberg_fxb_extractor.c
    src/formats/science/astronomy_ser/xx_astronomy_ser.c
    src/formats/science/astronomy_ser/xx_astronomy_ser_extractor.c
    src/formats/science/photontiming_ptu/xx_photontiming_ptu.c
    src/formats/science/photontiming_ptu/xx_photontiming_ptu_extractor.c
    src/formats/science/photontiming_phu/xx_photontiming_phu.c
    src/formats/science/photontiming_phu/xx_photontiming_phu_extractor.c
    src/formats/science/charmm_dcd/xx_charmm_dcd.c
    src/formats/science/charmm_dcd/xx_charmm_dcd_extractor.c
    src/formats/science/gromacs_trr/xx_gromacs_trr.c
    src/formats/science/gromacs_trr/xx_gromacs_trr_extractor.c
    src/formats/science/microscopy_ics/xx_microscopy_ics.c
    src/formats/science/microscopy_ics/xx_microscopy_ics_extractor.c
    src/formats/science/tecplot_plt/xx_tecplot_plt.c
    src/formats/science/tecplot_plt/xx_tecplot_plt_extractor.c
    src/formats/image/fujifilm_raf/xx_fujifilm_raf.c
    src/formats/image/fujifilm_raf/xx_fujifilm_raf_extractor.c
    src/formats/image/sigma_x3f/xx_sigma_x3f.c
    src/formats/image/sigma_x3f/xx_sigma_x3f_extractor.c
    src/formats/image/minolta_mrw/xx_minolta_mrw.c
    src/formats/image/minolta_mrw/xx_minolta_mrw_extractor.c
    src/formats/archive/sfx_imp/xx_sfx_imp.c
    src/formats/archive/sfx_imp/xx_sfx_imp_extractor.c
    src/formats/installer/sfx_red/xx_sfx_red.c
    src/formats/installer/sfx_red/xx_sfx_red_extractor.c
    src/formats/archive/sfx_ha/xx_sfx_ha.c
    src/formats/archive/sfx_ha/xx_sfx_ha_extractor.c
    src/formats/archive/sfx_lzx/xx_sfx_lzx.c
    src/formats/archive/sfx_lzx/xx_sfx_lzx_extractor.c
    src/formats/archive/sfx_sqx/xx_sfx_sqx.c
    src/formats/archive/sfx_sqx/xx_sfx_sqx_extractor.c
    src/formats/archive/sfx_ain/xx_sfx_ain.c
    src/formats/archive/sfx_ain/xx_sfx_ain_extractor.c
    src/formats/archive/sfx_hap/xx_sfx_hap.c
    src/formats/archive/sfx_hap/xx_sfx_hap_extractor.c
    src/formats/archive/sfx_zoo/xx_sfx_zoo.c
    src/formats/archive/sfx_zoo/xx_sfx_zoo_extractor.c
    src/formats/archive/sfx_cazip/xx_sfx_cazip.c
    src/formats/archive/sfx_cazip/xx_sfx_cazip_extractor.c
    src/formats/archive/sfx_tgcf/xx_sfx_tgcf.c
    src/formats/archive/sfx_tgcf/xx_sfx_tgcf_extractor.c
    src/formats/archive/sfx_starkit/xx_sfx_starkit.c
    src/formats/archive/sfx_starkit/xx_sfx_starkit_extractor.c
    src/formats/archive/sfx_alz/xx_sfx_alz.c
    src/formats/archive/sfx_alz/xx_sfx_alz_extractor.c
    src/formats/document/sfx_chm/xx_sfx_chm.c
    src/formats/document/sfx_chm/xx_sfx_chm_extractor.c
    src/formats/archive/egg/xx_egg.c
    src/formats/archive/egg/xx_egg_extractor.c
    src/formats/archive/nufx/xx_nufx.c
    src/formats/archive/nufx/xx_nufx_extractor.c
    src/formats/executable/nintendo_dol/xx_nintendo_dol.c
    src/formats/executable/nintendo_dol/xx_nintendo_dol_extractor.c
    src/formats/game/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.c
    src/formats/game/nintendo_j3d_bmd/xx_nintendo_j3d_bmd_extractor.c
    src/formats/game/nintendo_j3d_btk/xx_nintendo_j3d_btk.c
    src/formats/game/nintendo_j3d_btk/xx_nintendo_j3d_btk_extractor.c
    src/formats/audio/nintendo_brstm/xx_nintendo_brstm.c
    src/formats/audio/nintendo_brstm/xx_nintendo_brstm_extractor.c
    src/formats/audio/nintendo_brwav/xx_nintendo_brwav.c
    src/formats/audio/nintendo_brwav/xx_nintendo_brwav_extractor.c
    src/formats/game/nintendo_brlyt/xx_nintendo_brlyt.c
    src/formats/game/nintendo_brlyt/xx_nintendo_brlyt_extractor.c
    src/formats/game/nintendo_brlan/xx_nintendo_brlan.c
    src/formats/game/nintendo_brlan/xx_nintendo_brlan_extractor.c
    src/formats/game/nintendo_bfsha/xx_nintendo_bfsha.c
    src/formats/game/nintendo_bfsha/xx_nintendo_bfsha_extractor.c
    src/formats/video/cri_usm/xx_cri_usm.c
    src/formats/video/cri_usm/xx_cri_usm_extractor.c
    src/formats/game/cri_utf/xx_cri_utf.c
    src/formats/game/cri_utf/xx_cri_utf_extractor.c
    src/formats/game/idtech_iqm/xx_idtech_iqm.c
    src/formats/game/idtech_iqm/xx_idtech_iqm_extractor.c
    src/formats/game/unreal_psk/xx_unreal_psk.c
    src/formats/game/unreal_psk/xx_unreal_psk_extractor.c
    src/formats/game/unreal_psa/xx_unreal_psa.c
    src/formats/game/unreal_psa/xx_unreal_psa_extractor.c
    src/formats/image/torque_dts/xx_torque_dts.c
    src/formats/image/torque_dts/xx_torque_dts_extractor.c
    src/formats/image/magicavoxel_vox/xx_magicavoxel_vox.c
    src/formats/image/magicavoxel_vox/xx_magicavoxel_vox_extractor.c
    src/formats/audio/audio_au/xx_audio_au.c
    src/formats/audio/audio_au/xx_audio_au_extractor.c
    src/formats/audio/creative_voc/xx_creative_voc.c
    src/formats/audio/creative_voc/xx_creative_voc_extractor.c
    src/formats/audio/tracker_xm/xx_tracker_xm.c
    src/formats/audio/tracker_xm/xx_tracker_xm_extractor.c
    src/formats/audio/tracker_s3m/xx_tracker_s3m.c
    src/formats/audio/tracker_s3m/xx_tracker_s3m_extractor.c
    src/formats/audio/tracker_it/xx_tracker_it.c
    src/formats/audio/tracker_it/xx_tracker_it_extractor.c
    src/formats/audio/tracker_mtm/xx_tracker_mtm.c
    src/formats/audio/tracker_mtm/xx_tracker_mtm_extractor.c
    src/formats/audio/tracker_stm/xx_tracker_stm.c
    src/formats/audio/tracker_stm/xx_tracker_stm_extractor.c
    src/formats/audio/tracker_669/xx_tracker_669.c
    src/formats/audio/tracker_669/xx_tracker_669_extractor.c
    src/formats/audio/tracker_ult/xx_tracker_ult.c
    src/formats/audio/tracker_ult/xx_tracker_ult_extractor.c
    src/formats/audio/tracker_okt/xx_tracker_okt.c
    src/formats/audio/tracker_okt/xx_tracker_okt_extractor.c
    src/formats/science/nifti2/xx_nifti2.c
    src/formats/science/nifti2/xx_nifti2_extractor.c
    src/formats/science/lidar_las/xx_lidar_las.c
    src/formats/science/lidar_las/xx_lidar_las_extractor.c
    src/formats/science/esri_shp/xx_esri_shp.c
    src/formats/science/esri_shp/xx_esri_shp_extractor.c
    src/formats/image/polygon_ply/xx_polygon_ply.c
    src/formats/image/polygon_ply/xx_polygon_ply_extractor.c
    src/formats/image/pointcloud_pcd/xx_pointcloud_pcd.c
    src/formats/image/pointcloud_pcd/xx_pointcloud_pcd_extractor.c
    src/formats/science/matlab_mat4/xx_matlab_mat4.c
    src/formats/science/matlab_mat4/xx_matlab_mat4_extractor.c
    src/formats/science/seismic_segy/xx_seismic_segy.c
    src/formats/science/seismic_segy/xx_seismic_segy_extractor.c
    src/formats/science/biomedical_bdf/xx_biomedical_bdf.c
    src/formats/science/biomedical_bdf/xx_biomedical_bdf_extractor.c
    src/formats/executable/erlang_beam/xx_erlang_beam.c
    src/formats/executable/erlang_beam/xx_erlang_beam_extractor.c
    src/formats/executable/java_jmod/xx_java_jmod.c
    src/formats/executable/java_jmod/xx_java_jmod_extractor.c
    src/formats/archive/sfx_arcv2/xx_sfx_arcv2.c
    src/formats/archive/sfx_arcv2/xx_sfx_arcv2_extractor.c
    src/formats/archive/sfx_chz/xx_sfx_chz.c
    src/formats/archive/sfx_chz/xx_sfx_chz_extractor.c
    src/formats/archive/sfx_szdd/xx_sfx_szdd.c
    src/formats/archive/sfx_szdd/xx_sfx_szdd_extractor.c
    src/formats/game/sfx_mpq/xx_sfx_mpq.c
    src/formats/game/sfx_mpq/xx_sfx_mpq_extractor.c
    src/formats/archive/sfx_swag/xx_sfx_swag.c
    src/formats/archive/sfx_swag/xx_sfx_swag_extractor.c
    src/formats/installer/sfx_zpak/xx_sfx_zpak.c
    src/formats/installer/sfx_zpak/xx_sfx_zpak_extractor.c
    src/formats/archive/sfx_diskexpress/xx_sfx_diskexpress.c
    src/formats/archive/sfx_diskexpress/xx_sfx_diskexpress_extractor.c
    src/formats/archive/sfx_bzip2/xx_sfx_bzip2.c
    src/formats/archive/sfx_bzip2/xx_sfx_bzip2_extractor.c
    src/formats/archive/sfx_gzip/xx_sfx_gzip.c
    src/formats/archive/sfx_gzip/xx_sfx_gzip_extractor.c
    src/formats/archive/sfx_tar/xx_sfx_tar.c
    src/formats/archive/sfx_tar/xx_sfx_tar_extractor.c
    src/formats/archive/sfx_cab/xx_sfx_cab.c
    src/formats/archive/sfx_cab/xx_sfx_cab_extractor.c
    src/formats/archive/pmarc_sfx/xx_pmarc_sfx.c
    src/formats/archive/pmarc_sfx/xx_pmarc_sfx_extractor.c
    src/formats/archive/sfx_7zip/xx_sfx_7zip.c
    src/formats/archive/sfx_7zip/xx_sfx_7zip_extractor.c
    src/formats/archive/sfx_ace/xx_sfx_ace.c
    src/formats/archive/sfx_ace/xx_sfx_ace_extractor.c
    src/formats/archive/sfx_zipcentral/xx_sfx_zipcentral.c
    src/formats/archive/sfx_zipcentral/xx_sfx_zipcentral_extractor.c
    src/formats/executable/sony_psx_exe/xx_sony_psx_exe.c
    src/formats/executable/sony_psx_exe/xx_sony_psx_exe_extractor.c
    src/formats/audio/sony_psf/xx_sony_psf.c
    src/formats/audio/sony_psf/xx_sony_psf_extractor.c
    src/formats/filesystem/xbox_xdvdfs/xx_xbox_xdvdfs.c
    src/formats/filesystem/xbox_xdvdfs/xx_xbox_xdvdfs_extractor.c
    src/formats/game/nintendo_wbfs/xx_nintendo_wbfs.c
    src/formats/game/nintendo_wbfs/xx_nintendo_wbfs_extractor.c
    src/formats/image/godot_ctex/xx_godot_ctex.c
    src/formats/image/godot_ctex/xx_godot_ctex_extractor.c
    src/formats/game/unity_serialized/xx_unity_serialized.c
    src/formats/game/unity_serialized/xx_unity_serialized_extractor.c
    src/formats/game/idtech_mdl/xx_idtech_mdl.c
    src/formats/game/idtech_mdl/xx_idtech_mdl_extractor.c
    src/formats/game/valve_studio_mdl/xx_valve_studio_mdl.c
    src/formats/game/valve_studio_mdl/xx_valve_studio_mdl_extractor.c
    src/formats/image/blitz3d_b3d/xx_blitz3d_b3d.c
    src/formats/image/blitz3d_b3d/xx_blitz3d_b3d_extractor.c
    src/formats/image/milkshape_ms3d/xx_milkshape_ms3d.c
    src/formats/image/milkshape_ms3d/xx_milkshape_ms3d_extractor.c
    src/formats/image/nintendo_bch/xx_nintendo_bch.c
    src/formats/image/nintendo_bch/xx_nintendo_bch_extractor.c
    src/formats/game/nintendo_cgfx/xx_nintendo_cgfx.c
    src/formats/game/nintendo_cgfx/xx_nintendo_cgfx_extractor.c
    src/formats/game/nintendo_byaml/xx_nintendo_byaml.c
    src/formats/game/nintendo_byaml/xx_nintendo_byaml_extractor.c
    src/formats/game/relic_chunky/xx_relic_chunky.c
    src/formats/game/relic_chunky/xx_relic_chunky_extractor.c
    src/formats/image/ogre_mesh/xx_ogre_mesh.c
    src/formats/image/ogre_mesh/xx_ogre_mesh_extractor.c
    src/formats/image/adobe_ase/xx_adobe_ase.c
    src/formats/image/adobe_ase/xx_adobe_ase_extractor.c
    src/formats/image/adobe_aco/xx_adobe_aco.c
    src/formats/image/adobe_aco/xx_adobe_aco_extractor.c
    src/formats/image/gimp_gbr/xx_gimp_gbr.c
    src/formats/image/gimp_gbr/xx_gimp_gbr_extractor.c
    src/formats/image/gimp_gih/xx_gimp_gih.c
    src/formats/image/gimp_gih/xx_gimp_gih_extractor.c
    src/formats/image/gimp_pat/xx_gimp_pat.c
    src/formats/image/gimp_pat/xx_gimp_pat_extractor.c
    src/formats/image/jbig2/xx_jbig2.c
    src/formats/image/jbig2/xx_jbig2_extractor.c
    src/formats/document/djvu/xx_djvu.c
    src/formats/document/djvu/xx_djvu_extractor.c
    src/formats/image/emf/xx_emf.c
    src/formats/image/emf/xx_emf_extractor.c
    src/formats/image/wmf/xx_wmf.c
    src/formats/image/wmf/xx_wmf_extractor.c
    src/formats/image/xfig/xx_xfig.c
    src/formats/image/xfig/xx_xfig_extractor.c
    src/formats/science/nifti1/xx_nifti1.c
    src/formats/science/nifti1/xx_nifti1_extractor.c
    src/formats/science/nrrd/xx_nrrd.c
    src/formats/science/nrrd/xx_nrrd_extractor.c
    src/formats/science/mrc/xx_mrc.c
    src/formats/science/mrc/xx_mrc_extractor.c
    src/formats/science/metaimage/xx_metaimage.c
    src/formats/science/metaimage/xx_metaimage_extractor.c
    src/formats/science/vtk_legacy/xx_vtk_legacy.c
    src/formats/science/vtk_legacy/xx_vtk_legacy_extractor.c
    src/formats/science/gipl/xx_gipl.c
    src/formats/science/gipl/xx_gipl_extractor.c
    src/formats/science/freesurfer_mgh/xx_freesurfer_mgh.c
    src/formats/science/freesurfer_mgh/xx_freesurfer_mgh_extractor.c
    src/formats/science/edf/xx_edf.c
    src/formats/science/edf/xx_edf_extractor.c
    src/formats/science/fcs/xx_fcs.c
    src/formats/science/fcs/xx_fcs_extractor.c
    src/formats/other/tensorflow_tfrecord/xx_tensorflow_tfrecord.c
    src/formats/other/tensorflow_tfrecord/xx_tensorflow_tfrecord_extractor.c
    src/formats/archive/sfx_arc/xx_sfx_arc.c
    src/formats/archive/sfx_arc/xx_sfx_arc_extractor.c
    src/formats/archive/sfx_arj/xx_sfx_arj.c
    src/formats/archive/sfx_arj/xx_sfx_arj_extractor.c
    src/formats/archive/sfx_bsn/xx_sfx_bsn.c
    src/formats/archive/sfx_bsn/xx_sfx_bsn_extractor.c
    src/formats/archive/sfx_arq/xx_sfx_arq.c
    src/formats/archive/sfx_arq/xx_sfx_arq_extractor.c
    src/formats/archive/sfx_gxl/xx_sfx_gxl.c
    src/formats/archive/sfx_gxl/xx_sfx_gxl_extractor.c
    src/formats/installer/sfx_asymetrix/xx_sfx_asymetrix.c
    src/formats/installer/sfx_asymetrix/xx_sfx_asymetrix_extractor.c
    src/formats/archive/sfx_rta/xx_sfx_rta.c
    src/formats/archive/sfx_rta/xx_sfx_rta_extractor.c
    src/formats/installer/sfx_rtpatch/xx_sfx_rtpatch.c
    src/formats/installer/sfx_rtpatch/xx_sfx_rtpatch_extractor.c
    src/formats/archive/esp_archive/xx_esp_archive.c
    src/formats/archive/esp_archive/xx_esp_archive_extractor.c
    src/formats/archive/sfx_kwaj/xx_sfx_kwaj.c
    src/formats/archive/sfx_kwaj/xx_sfx_kwaj_extractor.c
    src/formats/archive/gemdos_lha/xx_gemdos_lha.c
    src/formats/archive/gemdos_lha/xx_gemdos_lha_extractor.c
    src/formats/filesystem/winimage_zip/xx_winimage_zip.c
    src/formats/filesystem/winimage_zip/xx_winimage_zip_extractor.c
    src/formats/other/hp3000_wrq/xx_hp3000_wrq.c
    src/formats/other/hp3000_wrq/xx_hp3000_wrq_extractor.c
    src/formats/other/icu_data_package/xx_icu_data_package.c
    src/formats/other/icu_data_package/xx_icu_data_package_extractor.c
    src/formats/archive/sfx_sqz/xx_sfx_sqz.c
    src/formats/archive/sfx_sqz/xx_sfx_sqz_extractor.c
    src/formats/audio/nintendo_bfstm/xx_nintendo_bfstm.c
    src/formats/audio/nintendo_bfstm/xx_nintendo_bfstm_extractor.c
    src/formats/audio/nintendo_bfwav/xx_nintendo_bfwav.c
    src/formats/audio/nintendo_bfwav/xx_nintendo_bfwav_extractor.c
    src/formats/audio/nintendo_bcwav/xx_nintendo_bcwav.c
    src/formats/audio/nintendo_bcwav/xx_nintendo_bcwav_extractor.c
    src/formats/game/nintendo_bfres/xx_nintendo_bfres.c
    src/formats/game/nintendo_bfres/xx_nintendo_bfres_extractor.c
    src/formats/game/nintendo_bflyt/xx_nintendo_bflyt.c
    src/formats/game/nintendo_bflyt/xx_nintendo_bflyt_extractor.c
    src/formats/game/nintendo_bclyt/xx_nintendo_bclyt.c
    src/formats/game/nintendo_bclyt/xx_nintendo_bclyt_extractor.c
    src/formats/font/nintendo_bfnt/xx_nintendo_bfnt.c
    src/formats/font/nintendo_bfnt/xx_nintendo_bfnt_extractor.c
    src/formats/font/nintendo_bcfnt/xx_nintendo_bcfnt.c
    src/formats/font/nintendo_bcfnt/xx_nintendo_bcfnt_extractor.c
    src/formats/executable/nintendo_3dsx/xx_nintendo_3dsx.c
    src/formats/executable/nintendo_3dsx/xx_nintendo_3dsx_extractor.c
    src/formats/image/sony_tim2/xx_sony_tim2.c
    src/formats/image/sony_tim2/xx_sony_tim2_extractor.c
    src/formats/video/sony_pamf/xx_sony_pamf.c
    src/formats/video/sony_pamf/xx_sony_pamf_extractor.c
    src/formats/image/sega_gvr/xx_sega_gvr.c
    src/formats/image/sega_gvr/xx_sega_gvr_extractor.c
    src/formats/audio/microsoft_xwb/xx_microsoft_xwb.c
    src/formats/audio/microsoft_xwb/xx_microsoft_xwb_extractor.c
    src/formats/audio/microsoft_xsb/xx_microsoft_xsb.c
    src/formats/audio/microsoft_xsb/xx_microsoft_xsb_extractor.c
    src/formats/game/relic_sga/xx_relic_sga.c
    src/formats/game/relic_sga/xx_relic_sga_extractor.c
    src/formats/image/xpm/xx_xpm.c
    src/formats/image/xpm/xx_xpm_extractor.c
    src/formats/image/pcx/xx_pcx.c
    src/formats/image/pcx/xx_pcx_extractor.c
    src/formats/image/iff_ilbm/xx_iff_ilbm.c
    src/formats/image/iff_ilbm/xx_iff_ilbm_extractor.c
    src/formats/image/utah_rle/xx_utah_rle.c
    src/formats/image/utah_rle/xx_utah_rle_extractor.c
    src/formats/image/radiance_hdr/xx_radiance_hdr.c
    src/formats/image/radiance_hdr/xx_radiance_hdr_extractor.c
    src/formats/image/dpx/xx_dpx.c
    src/formats/image/dpx/xx_dpx_extractor.c
    src/formats/image/cineon/xx_cineon.c
    src/formats/image/cineon/xx_cineon_extractor.c
    src/formats/image/xwd/xx_xwd.c
    src/formats/image/xwd/xx_xwd_extractor.c
    src/formats/image/sgi_rgb/xx_sgi_rgb.c
    src/formats/image/sgi_rgb/xx_sgi_rgb_extractor.c
    src/formats/image/aseprite/xx_aseprite.c
    src/formats/image/aseprite/xx_aseprite_extractor.c
    src/formats/science/numpy_npy/xx_numpy_npy.c
    src/formats/science/numpy_npy/xx_numpy_npy_extractor.c
    src/formats/science/matlab_mat5/xx_matlab_mat5.c
    src/formats/science/matlab_mat5/xx_matlab_mat5_extractor.c
    src/formats/science/netcdf_classic/xx_netcdf_classic.c
    src/formats/science/netcdf_classic/xx_netcdf_classic_extractor.c
    src/formats/science/hdf4/xx_hdf4.c
    src/formats/science/hdf4/xx_hdf4_extractor.c
    src/formats/database/dbase_dbf/xx_dbase_dbf.c
    src/formats/database/dbase_dbf/xx_dbase_dbf_extractor.c
    src/formats/science/sas_xport/xx_sas_xport.c
    src/formats/science/sas_xport/xx_sas_xport_extractor.c
    src/formats/science/spss_sav/xx_spss_sav.c
    src/formats/science/spss_sav/xx_spss_sav_extractor.c
    src/formats/science/stata_dta/xx_stata_dta.c
    src/formats/science/stata_dta/xx_stata_dta_extractor.c
    src/formats/database/apache_arrow_file/xx_apache_arrow_file.c
    src/formats/database/apache_arrow_file/xx_apache_arrow_file_extractor.c
    src/formats/database/apache_parquet/xx_apache_parquet.c
    src/formats/database/apache_parquet/xx_apache_parquet_extractor.c
    src/formats/installer/makeself/xx_makeself.c
    src/formats/installer/makeself/xx_makeself_extractor.c
    src/formats/installer/sun_java_binsh/xx_sun_java_binsh.c
    src/formats/installer/sun_java_binsh/xx_sun_java_binsh_extractor.c
    src/formats/installer/installanywhere_unix/xx_installanywhere_unix.c
    src/formats/installer/installanywhere_unix/xx_installanywhere_unix_extractor.c
    src/formats/installer/sfx_packagefortheweb/xx_sfx_packagefortheweb.c
    src/formats/installer/sfx_packagefortheweb/xx_sfx_packagefortheweb_extractor.c
    src/formats/archive/sfx_spis/xx_sfx_spis.c
    src/formats/archive/sfx_spis/xx_sfx_spis_extractor.c
    src/formats/archive/sfx_lha/xx_sfx_lha.c
    src/formats/archive/sfx_lha/xx_sfx_lha_extractor.c
    src/formats/other/lmd_container/xx_lmd_container.c
    src/formats/other/lmd_container/xx_lmd_container_extractor.c
    src/formats/game/totalannihilation_hpi/xx_totalannihilation_hpi.c
    src/formats/game/totalannihilation_hpi/xx_totalannihilation_hpi_extractor.c
    src/formats/game/ravensoft_rff/xx_ravensoft_rff.c
    src/formats/game/ravensoft_rff/xx_ravensoft_rff_extractor.c
    src/formats/game/terminalreality_pod/xx_terminalreality_pod.c
    src/formats/game/terminalreality_pod/xx_terminalreality_pod_extractor.c
    src/formats/game/volition_vpp/xx_volition_vpp.c
    src/formats/game/volition_vpp/xx_volition_vpp_extractor.c
    src/formats/game/kirikiri_xp3/xx_kirikiri_xp3.c
    src/formats/game/kirikiri_xp3/xx_kirikiri_xp3_extractor.c
    src/formats/game/fromsoftware_binder/xx_fromsoftware_binder.c
    src/formats/game/fromsoftware_binder/xx_fromsoftware_binder_extractor.c
    src/formats/game/mythic_myp/xx_mythic_myp.c
    src/formats/game/mythic_myp/xx_mythic_myp_extractor.c
    src/formats/game/lithtech_rez/xx_lithtech_rez.c
    src/formats/game/lithtech_rez/xx_lithtech_rez_extractor.c
    src/formats/game/nintendo_ncch/xx_nintendo_ncch.c
    src/formats/game/nintendo_ncch/xx_nintendo_ncch_extractor.c
    src/formats/game/nintendo_ncsd/xx_nintendo_ncsd.c
    src/formats/game/nintendo_ncsd/xx_nintendo_ncsd_extractor.c
    src/formats/game/nintendo_cia/xx_nintendo_cia.c
    src/formats/game/nintendo_cia/xx_nintendo_cia_extractor.c
    src/formats/game/nintendo_nds/xx_nintendo_nds.c
    src/formats/game/nintendo_nds/xx_nintendo_nds_extractor.c
    src/formats/game/nintendo_gcm/xx_nintendo_gcm.c
    src/formats/game/nintendo_gcm/xx_nintendo_gcm_extractor.c
    src/formats/image/nintendo_tpl/xx_nintendo_tpl.c
    src/formats/image/nintendo_tpl/xx_nintendo_tpl_extractor.c
    src/formats/image/sony_tim/xx_sony_tim.c
    src/formats/image/sony_tim/xx_sony_tim_extractor.c
    src/formats/audio/sony_vag/xx_sony_vag.c
    src/formats/audio/sony_vag/xx_sony_vag_extractor.c
    src/formats/game/larian_lspk/xx_larian_lspk.c
    src/formats/game/larian_lspk/xx_larian_lspk_extractor.c
    src/formats/game/larian_lsf/xx_larian_lsf.c
    src/formats/game/larian_lsf/xx_larian_lsf_extractor.c
    src/formats/game/valve_hpak/xx_valve_hpak.c
    src/formats/game/valve_hpak/xx_valve_hpak_extractor.c
    src/formats/game/renpy_rpa/xx_renpy_rpa.c
    src/formats/game/renpy_rpa/xx_renpy_rpa_extractor.c
    src/formats/game/unreal_package/xx_unreal_package.c
    src/formats/game/unreal_package/xx_unreal_package_extractor.c
    src/formats/image/sega_pvr2/xx_sega_pvr2.c
    src/formats/image/sega_pvr2/xx_sega_pvr2_extractor.c
    src/formats/image/nintendo_bntx/xx_nintendo_bntx.c
    src/formats/image/nintendo_bntx/xx_nintendo_bntx_extractor.c
    src/formats/image/icns/xx_icns.c
    src/formats/image/icns/xx_icns_extractor.c
    src/formats/image/xcursor/xx_xcursor.c
    src/formats/image/xcursor/xx_xcursor_extractor.c
    src/formats/other/icc/xx_icc.c
    src/formats/other/icc/xx_icc_extractor.c
    src/formats/image/qoi/xx_qoi.c
    src/formats/image/qoi/xx_qoi_extractor.c
    src/formats/image/farbfeld/xx_farbfeld.c
    src/formats/image/farbfeld/xx_farbfeld_extractor.c
    src/formats/image/pnm/xx_pnm.c
    src/formats/image/pnm/xx_pnm_extractor.c
    src/formats/image/tga/xx_tga.c
    src/formats/image/tga/xx_tga_extractor.c
    src/formats/image/sun_raster/xx_sun_raster.c
    src/formats/image/sun_raster/xx_sun_raster_extractor.c
    src/formats/science/fits/xx_fits.c
    src/formats/science/fits/xx_fits_extractor.c
    src/formats/science/dicom/xx_dicom.c
    src/formats/science/dicom/xx_dicom_extractor.c
    src/formats/other/pcap/xx_pcap.c
    src/formats/other/pcap/xx_pcap_extractor.c
    src/formats/other/btsnoop/xx_btsnoop.c
    src/formats/other/btsnoop/xx_btsnoop_extractor.c
    src/formats/executable/java_class/xx_java_class.c
    src/formats/executable/java_class/xx_java_class_extractor.c
    src/formats/font/sfnt_collection/xx_sfnt_collection.c
    src/formats/font/sfnt_collection/xx_sfnt_collection_extractor.c
    src/formats/database/sqlite3/xx_sqlite3.c
    src/formats/database/sqlite3/xx_sqlite3_extractor.c
    src/formats/database/sqlite_wal/xx_sqlite_wal.c
    src/formats/database/sqlite_wal/xx_sqlite_wal_extractor.c
    src/formats/other/avro_object/xx_avro_object.c
    src/formats/other/avro_object/xx_avro_object_extractor.c
    src/formats/image/glb/xx_glb.c
    src/formats/image/glb/xx_glb_extractor.c
    src/formats/executable/spirv/xx_spirv.c
    src/formats/executable/spirv/xx_spirv_extractor.c
    src/formats/installer/crx/xx_crx.c
    src/formats/installer/crx/xx_crx_extractor.c
    src/formats/game/bethesda_bsa/xx_bethesda_bsa.c
    src/formats/game/bethesda_bsa/xx_bethesda_bsa_extractor.c
    src/formats/game/bethesda_ba2/xx_bethesda_ba2.c
    src/formats/game/bethesda_ba2/xx_bethesda_ba2_extractor.c
    src/formats/game/unityfs/xx_unityfs.c
    src/formats/game/unityfs/xx_unityfs_extractor.c
    src/formats/game/bioware_biff/xx_bioware_biff.c
    src/formats/game/bioware_biff/xx_bioware_biff_extractor.c
    src/formats/game/bioware_erf/xx_bioware_erf.c
    src/formats/game/bioware_erf/xx_bioware_erf_extractor.c
    src/formats/game/bioware_rim/xx_bioware_rim.c
    src/formats/game/bioware_rim/xx_bioware_rim_extractor.c
    src/formats/game/lucas_lab/xx_lucas_lab.c
    src/formats/game/lucas_lab/xx_lucas_lab_extractor.c
    src/formats/game/lucas_bun/xx_lucas_bun.c
    src/formats/game/lucas_bun/xx_lucas_bun_extractor.c
    src/formats/game/idtech_bsp/xx_idtech_bsp.c
    src/formats/game/idtech_bsp/xx_idtech_bsp_extractor.c
    src/formats/game/valve_bsp/xx_valve_bsp.c
    src/formats/game/valve_bsp/xx_valve_bsp_extractor.c
    src/formats/game/idtech_md2/xx_idtech_md2.c
    src/formats/game/idtech_md2/xx_idtech_md2_extractor.c
    src/formats/game/idtech_md3/xx_idtech_md3.c
    src/formats/game/idtech_md3/xx_idtech_md3_extractor.c
    src/formats/game/idtech_qvm/xx_idtech_qvm.c
    src/formats/game/idtech_qvm/xx_idtech_qvm_extractor.c
    src/formats/game/mohawk_mhk/xx_mohawk_mhk.c
    src/formats/game/mohawk_mhk/xx_mohawk_mhk_extractor.c
    src/formats/game/quake_sprite/xx_quake_sprite.c
    src/formats/game/quake_sprite/xx_quake_sprite_extractor.c
    src/formats/game/nintendo_narc/xx_nintendo_narc.c
    src/formats/game/nintendo_narc/xx_nintendo_narc_extractor.c
    src/formats/game/nintendo_sarc/xx_nintendo_sarc.c
    src/formats/game/nintendo_sarc/xx_nintendo_sarc_extractor.c
    src/formats/game/nintendo_pfs0/xx_nintendo_pfs0.c
    src/formats/game/nintendo_pfs0/xx_nintendo_pfs0_extractor.c
    src/formats/game/nintendo_hfs0/xx_nintendo_hfs0.c
    src/formats/game/nintendo_hfs0/xx_nintendo_hfs0_extractor.c
    src/formats/game/nintendo_brres/xx_nintendo_brres.c
    src/formats/game/nintendo_brres/xx_nintendo_brres_extractor.c
    src/formats/audio/nintendo_bcstm/xx_nintendo_bcstm.c
    src/formats/audio/nintendo_bcstm/xx_nintendo_bcstm_extractor.c
    src/formats/audio/nintendo_bfsar/xx_nintendo_bfsar.c
    src/formats/audio/nintendo_bfsar/xx_nintendo_bfsar_extractor.c
    src/formats/audio/nintendo_bcsar/xx_nintendo_bcsar.c
    src/formats/audio/nintendo_bcsar/xx_nintendo_bcsar_extractor.c
    src/formats/game/sony_psarc/xx_sony_psarc.c
    src/formats/game/sony_psarc/xx_sony_psarc_extractor.c
    src/formats/image/ktx/xx_ktx.c
    src/formats/image/ktx/xx_ktx_extractor.c
    src/formats/image/ktx2/xx_ktx2.c
    src/formats/image/ktx2/xx_ktx2_extractor.c
    src/formats/image/dds/xx_dds.c
    src/formats/image/dds/xx_dds_extractor.c
    src/formats/image/pvr/xx_pvr.c
    src/formats/image/pvr/xx_pvr_extractor.c
    src/formats/image/valve_vtf/xx_valve_vtf.c
    src/formats/image/valve_vtf/xx_valve_vtf_extractor.c
    src/formats/executable/xbox_xbe/xx_xbox_xbe.c
    src/formats/executable/xbox_xbe/xx_xbox_xbe_extractor.c
    src/formats/audio/flac/xx_flac.c
    src/formats/audio/flac/xx_flac_extractor.c
    src/formats/audio/ogg/xx_ogg.c
    src/formats/audio/ogg/xx_ogg_extractor.c
    src/formats/video/mp4/xx_mp4.c
    src/formats/video/mp4/xx_mp4_extractor.c
    src/formats/video/matroska/xx_matroska.c
    src/formats/video/matroska/xx_matroska_extractor.c
    src/formats/audio/aiff/xx_aiff.c
    src/formats/audio/aiff/xx_aiff_extractor.c
    src/formats/audio/caf/xx_caf.c
    src/formats/audio/caf/xx_caf_extractor.c
    src/formats/image/photoshop_psd/xx_photoshop_psd.c
    src/formats/image/photoshop_psd/xx_photoshop_psd_extractor.c
    src/formats/image/tiff/xx_tiff.c
    src/formats/image/tiff/xx_tiff_extractor.c
    src/formats/image/openexr/xx_openexr.c
    src/formats/image/openexr/xx_openexr_extractor.c
    src/formats/image/jpeg2000_jp2/xx_jpeg2000_jp2.c
    src/formats/image/jpeg2000_jp2/xx_jpeg2000_jp2_extractor.c
    src/formats/firmware/android_vendor_boot/xx_android_vendor_boot.c
    src/formats/firmware/android_vendor_boot/xx_android_vendor_boot_extractor.c
    src/formats/firmware/android_dtbo/xx_android_dtbo.c
    src/formats/firmware/android_dtbo/xx_android_dtbo_extractor.c
    src/formats/firmware/android_vbmeta/xx_android_vbmeta.c
    src/formats/firmware/android_vbmeta/xx_android_vbmeta_extractor.c
    src/formats/firmware/espressif_image/xx_espressif_image.c
    src/formats/firmware/espressif_image/xx_espressif_image_extractor.c
    src/formats/executable/wasm/xx_wasm.c
    src/formats/executable/wasm/xx_wasm_extractor.c
    src/formats/executable/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.c
    src/formats/executable/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper_extractor.c
    src/formats/executable/dotnet_metadata/xx_dotnet_metadata.c
    src/formats/executable/dotnet_metadata/xx_dotnet_metadata_extractor.c
    src/formats/font/sfnt/xx_sfnt.c
    src/formats/font/sfnt/xx_sfnt_extractor.c
    src/formats/font/woff/xx_woff.c
    src/formats/font/woff/xx_woff_extractor.c
    src/formats/font/woff2/xx_woff2.c
    src/formats/font/woff2/xx_woff2_extractor.c
    src/formats/filesystem/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.c
    src/formats/filesystem/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw_extractor.c
    src/formats/filesystem/adam/xx_adam.c
    src/formats/filesystem/adam/xx_adam_extractor.c
    src/formats/other/base16/xx_base16.c
    src/formats/other/base16/xx_base16_extractor.c
    src/formats/filesystem/bondwell_2_disk/xx_bondwell_2_disk.c
    src/formats/filesystem/bondwell_2_disk/xx_bondwell_2_disk_extractor.c
    src/formats/filesystem/casio_fz_1_disk/xx_casio_fz_1_disk.c
    src/formats/filesystem/casio_fz_1_disk/xx_casio_fz_1_disk_extractor.c
    src/formats/filesystem/isz/xx_isz.c
    src/formats/filesystem/isz/xx_isz_extractor.c
    src/formats/filesystem/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.c
    src/formats/filesystem/mame_floppy_image_mfi/xx_mame_floppy_image_mfi_extractor.c
    src/formats/filesystem/parallels_hdd/xx_parallels_hdd.c
    src/formats/filesystem/parallels_hdd/xx_parallels_hdd_extractor.c
    src/formats/filesystem/pc_magazine_flp/xx_pc_magazine_flp.c
    src/formats/filesystem/pc_magazine_flp/xx_pc_magazine_flp_extractor.c
    src/formats/firmware/pchrom/xx_pchrom.c
    src/formats/firmware/pchrom/xx_pchrom_extractor.c
    src/formats/other/pem/xx_pem.c
    src/formats/other/pem/xx_pem_extractor.c
    src/formats/filesystem/prodos/xx_prodos.c
    src/formats/filesystem/prodos/xx_prodos_extractor.c
    src/formats/filesystem/qemu_enhanced_disk/xx_qemu_enhanced_disk.c
    src/formats/filesystem/qemu_enhanced_disk/xx_qemu_enhanced_disk_extractor.c
    src/formats/filesystem/rawcd/xx_rawcd.c
    src/formats/filesystem/rawcd/xx_rawcd_extractor.c
    src/formats/filesystem/rsdos_fs/xx_rsdos_fs.c
    src/formats/filesystem/rsdos_fs/xx_rsdos_fs_extractor.c
    src/formats/game/sar_ns/xx_sar_ns.c
    src/formats/game/sar_ns/xx_sar_ns_extractor.c
    src/formats/video/swf/xx_swf.c
    src/formats/video/swf/xx_swf_extractor.c
    src/formats/filesystem/t64/xx_t64.c
    src/formats/filesystem/t64/xx_t64_extractor.c
    src/formats/archive/uue/xx_uue.c
    src/formats/archive/uue/xx_uue_extractor.c
    src/formats/filesystem/vdi/xx_vdi.c
    src/formats/filesystem/dfsee_imz/xx_dfsee_imz.c
    src/formats/filesystem/dfsee_imz/xx_dfsee_imz_reader_adapters.c
    src/formats/archive/dh/xx_dh.c
    src/formats/archive/dh/xx_dh_reader_adapters.c
    src/formats/archive/djcomp/xx_djcomp.c
    src/formats/archive/djcomp/xx_djcomp_reader_adapters.c
    src/formats/filesystem/vdi/xx_vdi_extractor.c
    src/formats/image/bmp/xx_bmp.c
    src/formats/image/bmp/xx_bmp_make.c
    src/formats/image/bmp/xx_bmp_extractor.c
    src/formats/firmware/cfe/xx_cfe.c
    src/formats/firmware/cfe/xx_cfe_extractor.c
    src/formats/executable/dxbc/xx_dxbc.c
    src/formats/executable/dxbc/xx_dxbc_extractor.c
    src/formats/image/gif/xx_gif.c
    src/formats/image/gif/xx_gif_extractor.c
    src/formats/image/jpeg/xx_jpeg.c
    src/formats/image/jpeg/xx_jpeg_extractor.c
    src/formats/firmware/linuxarm64/xx_linuxarm64.c
    src/formats/firmware/linuxarm64/xx_linuxarm64_extractor.c
    src/formats/firmware/linuxboot/xx_linuxboot.c
    src/formats/firmware/linuxboot/xx_linuxboot_extractor.c
    src/formats/firmware/linuxzimage/xx_linuxzimage.c
    src/formats/firmware/linuxzimage/xx_linuxzimage_extractor.c
    src/formats/other/pcapng/xx_pcapng.c
    src/formats/other/pcapng/xx_pcapng_extractor.c
    src/formats/document/pjl/xx_pjl.c
    src/formats/document/pjl/xx_pjl_extractor.c
    src/formats/image/png/xx_png.c
    src/formats/image/png/xx_png_extractor.c
    src/formats/other/riff/xx_riff.c
    src/formats/other/riff/xx_riff_extractor.c
    src/formats/image/svg/xx_svg.c
    src/formats/image/svg/xx_svg_extractor.c
    src/formats/game/quake_pak/xx_quake_pak.c
    src/formats/game/quake_pak/xx_quake_pak_extractor.c
    src/formats/game/doom_wad/xx_doom_wad.c
    src/formats/game/doom_wad/xx_doom_wad_extractor.c
    src/formats/game/quake_wad2/xx_quake_wad2.c
    src/formats/game/quake_wad2/xx_quake_wad2_extractor.c
    src/formats/game/halflife_wad3/xx_halflife_wad3.c
    src/formats/game/halflife_wad3/xx_halflife_wad3_extractor.c
    src/formats/game/build_grp/xx_build_grp.c
    src/formats/game/build_grp/xx_build_grp_extractor.c
    src/formats/game/cri_afs/xx_cri_afs.c
    src/formats/game/cri_afs/xx_cri_afs_extractor.c
    src/formats/audio/cri_awb/xx_cri_awb.c
    src/formats/audio/cri_awb/xx_cri_awb_extractor.c
    src/formats/game/valve_vpk/xx_valve_vpk.c
    src/formats/game/valve_vpk/xx_valve_vpk_extractor.c
    src/formats/game/nintendo_u8/xx_nintendo_u8.c
    src/formats/game/nintendo_u8/xx_nintendo_u8_extractor.c
    src/formats/game/nintendo_rarc/xx_nintendo_rarc.c
    src/formats/game/nintendo_rarc/xx_nintendo_rarc_extractor.c
    src/formats/other/android_ab/xx_android_ab.c
    src/formats/other/android_ab/xx_android_ab_extractor.c
    src/formats/game/nes_rom/xx_nes_rom.c
    src/formats/game/nes_rom/xx_nes_rom_extractor.c
    src/formats/game/lynx_lnx/xx_lynx_lnx.c
    src/formats/game/lynx_lnx/xx_lynx_lnx_extractor.c
    src/formats/game/commodore_crt/xx_commodore_crt.c
    src/formats/game/commodore_crt/xx_commodore_crt_extractor.c
    src/formats/firmware/uf2/xx_uf2.c
    src/formats/firmware/uf2/xx_uf2_extractor.c
    src/formats/image/ico/xx_ico.c
    src/formats/image/ico/xx_ico_make.c
    src/formats/image/ico/xx_ico_extractor.c
    src/formats/audio/midi/xx_midi.c
    src/formats/audio/midi/xx_midi_extractor.c
    src/formats/installer/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.c
    src/formats/installer/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper_extractor.c
    src/formats/installer/ardi_installer/xx_ardi_installer.c
    src/formats/installer/ardi_installer/xx_ardi_installer_extractor.c
    src/formats/installer/arni_installer_container/xx_arni_installer_container.c
    src/formats/installer/arni_installer_container/xx_arni_installer_container_extractor.c
    src/formats/installer/ej_technologies_install/xx_ej_technologies_install.c
    src/formats/installer/ej_technologies_install/xx_ej_technologies_install_extractor.c
    src/formats/installer/finstall/xx_finstall.c
    src/formats/installer/finstall/xx_finstall_extractor.c
    src/formats/installer/ghost_installer/xx_ghost_installer.c
    src/formats/installer/ghost_installer/xx_ghost_installer_extractor.c
    src/formats/installer/ibm_zpak_installer/xx_ibm_zpak_installer.c
    src/formats/installer/ibm_zpak_installer/xx_ibm_zpak_installer_extractor.c
    src/formats/installer/ifah_installer/xx_ifah_installer.c
    src/formats/installer/ifah_installer/xx_ifah_installer_extractor.c
    src/formats/installer/inno_setup/xx_inno_setup.c
    src/formats/installer/inno_setup/xx_inno_setup_extractor.c
    src/formats/installer/installer_vise_windows/xx_installer_vise_windows.c
    src/formats/installer/installer_vise_windows/xx_installer_vise_windows_extractor.c
    src/formats/installer/installshield_12_setup/xx_installshield_12_setup.c
    src/formats/installer/installshield_12_setup/xx_installshield_12_setup_extractor.c
    src/formats/installer/installshield_3/xx_installshield_3.c
    src/formats/installer/installshield_3/xx_installshield_3_extractor.c
    src/formats/installer/installshield_7_setup/xx_installshield_7_setup.c
    src/formats/installer/installshield_7_setup/xx_installshield_7_setup_extractor.c
    src/formats/installer/installshield_7_setup2/xx_installshield_7_setup2.c
    src/formats/installer/installshield_7_setup2/xx_installshield_7_setup2_extractor.c
    src/formats/installer/installshield_developer/xx_installshield_developer.c
    src/formats/installer/installshield_developer/xx_installshield_developer_extractor.c
    src/formats/installer/installshield_issetupstream/xx_installshield_issetupstream.c
    src/formats/installer/installshield_issetupstream/xx_installshield_issetupstream_extractor.c
    src/formats/installer/installshield_multiplatform/xx_installshield_multiplatform.c
    src/formats/installer/installshield_multiplatform/xx_installshield_multiplatform_extractor.c
    src/formats/installer/installshield_skin/xx_installshield_skin.c
    src/formats/installer/installshield_skin/xx_installshield_skin_extractor.c
    src/formats/archive/microfox_put/xx_microfox_put.c
    src/formats/archive/microfox_put/xx_microfox_put_extractor.c
    src/formats/installer/o_setup/xx_o_setup.c
    src/formats/installer/o_setup/xx_o_setup_extractor.c
    src/formats/installer/pc_install_setup/xx_pc_install_setup.c
    src/formats/installer/pc_install_setup/xx_pc_install_setup_extractor.c
    src/formats/executable/pyinstaller_one_executable/xx_pyinstaller_one_executable.c
    src/formats/executable/pyinstaller_one_executable/xx_pyinstaller_one_executable_extractor.c
    src/formats/installer/qsetup_installation_suite/xx_qsetup_installation_suite.c
    src/formats/installer/qsetup_installation_suite/xx_qsetup_installation_suite_extractor.c
    src/formats/installer/rtpatch_setup_data/xx_rtpatch_setup_data.c
    src/formats/installer/rtpatch_setup_data/xx_rtpatch_setup_data_extractor.c
    src/formats/installer/setup_factory/xx_setup_factory.c
    src/formats/installer/setup_factory/xx_setup_factory_extractor.c
    src/formats/document/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.c
    src/formats/document/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables_extractor.c
    src/formats/installer/spoon_installer/xx_spoon_installer.c
    src/formats/installer/spoon_installer/xx_spoon_installer_extractor.c
    src/formats/installer/tarma_installer/xx_tarma_installer.c
    src/formats/installer/tarma_installer/xx_tarma_installer_extractor.c
    src/formats/filesystem/adf/xx_adf.c
    src/formats/filesystem/adf/xx_adf_extractor.c
    src/formats/filesystem/apm/xx_apm.c
    src/formats/filesystem/apm/xx_apm_extractor.c
    src/formats/filesystem/vhdx/xx_vhdx.c
    src/formats/filesystem/vhdx/xx_vhdx_extractor.c
    src/formats/other/base64/xx_base64.c
    src/formats/other/base64/xx_base64_extractor.c
    src/formats/archive/btoa/xx_btoa.c
    src/formats/archive/btoa/xx_btoa_extractor.c
    src/formats/filesystem/chd/xx_chd.c
    src/formats/filesystem/chd/xx_chd_extractor.c
    src/formats/document/chm/xx_chm.c
    src/formats/document/chm/xx_chm_extractor.c
    src/formats/filesystem/cloop/xx_cloop.c
    src/formats/filesystem/cloop/xx_cloop_extractor.c
    src/formats/filesystem/cue/xx_cue.c
    src/formats/filesystem/cue/xx_cue_extractor.c
    src/formats/firmware/dahuazip/xx_dahuazip.c
    src/formats/firmware/dahuazip/xx_dahuazip_extractor.c
    src/formats/firmware/dmsfw/xx_dmsfw.c
    src/formats/firmware/dmsfw/xx_dmsfw_extractor.c
    src/formats/filesystem/ewf/xx_ewf.c
    src/formats/filesystem/ewf/xx_ewf_extractor.c
    src/formats/game/godot_engine_pck/xx_godot_engine_pck.c
    src/formats/game/godot_engine_pck/xx_godot_engine_pck_extractor.c
    src/formats/other/gpgsigned/xx_gpgsigned.c
    src/formats/other/gpgsigned/xx_gpgsigned_extractor.c
    src/formats/firmware/ihex/xx_ihex.c
    src/formats/firmware/ihex/xx_ihex_extractor.c
    src/formats/archive/kwaj/xx_kwaj.c
    src/formats/archive/kwaj/xx_kwaj_extractor.c
    src/formats/archive/lbr/xx_lbr.c
    src/formats/archive/lbr/xx_lbr_extractor.c
    src/formats/archive/lzfsestream/xx_lzfsestream.c
    src/formats/archive/lzfsestream/xx_lzfsestream_extractor.c
    src/formats/filesystem/nrg/xx_nrg.c
    src/formats/filesystem/nrg/xx_nrg_extractor.c
    src/formats/archive/packit_mac/xx_packit_mac.c
    src/formats/archive/packit_mac/xx_packit_mac_extractor.c
    src/formats/installer/rpm/xx_rpm.c
    src/formats/installer/rpm/xx_rpm_extractor.c
    src/formats/archive/stuffit5/xx_stuffit5.c
    src/formats/archive/stuffit5/xx_stuffit5_extractor.c
)

# One generated C reader in its own subfolder for each DIE music family.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/formats/audio/die_music/xx_die_music_sources.cmake")
list(APPEND XXFCLIB_SOURCES ${XX_DIE_MUSIC_SOURCES})
include(src/formats/xx_native_format_sources.cmake)
list(APPEND XXFCLIB_SOURCES
    src/formats/document/ue2_documents/xx_ue2_documents.c
    src/formats/document/microsoft_lit/xx_microsoft_lit.c
    src/formats/game/ue2_games/xx_ue2_games.c
    src/formats/database/sqlite_sql/xx_sqlite_sql.c
    src/formats/database/sqlite_sql/xx_sqlite_amalgamation.c
    src/formats/archive/chromium_pak/xx_chromium_pak.c
    src/formats/image/windows_thumbnail_cache/xx_windows_thumbnail_cache.c)
list(APPEND XXFCLIB_SOURCES src/formats/executable/enigma_virtual_box/xx_enigma_virtual_box.c src/formats/video/media_engine/xx_media_engine.c)
list(APPEND XXFCLIB_SOURCES src/formats/installer/bitrock/xx_bitrock.c)
list(APPEND XXFCLIB_SOURCES src/formats/installer/smart_install_maker/xx_smart_install_maker.c)
list(APPEND XXFCLIB_SOURCES src/formats/executable/upx_engine/xx_upx_engine.c)
list(APPEND XXFCLIB_SOURCES src/formats/executable/molebox/xx_molebox.c)
list(APPEND XXFCLIB_SOURCES src/formats/installer/superdat/xx_superdat.c)
list(APPEND XXFCLIB_SOURCES
    src/formats/installer/fead/xx_fead.c src/formats/installer/fead/xx_fead_restore.c
    src/formats/installer/fead/xx_fead_bcj2.c
    src/formats/installer/fead/xx_fead_zlib_encoder.c
    src/formats/installer/fead/encoder/deflate.c src/formats/installer/fead/encoder/trees.c
    src/formats/installer/fead/encoder/adler32.c src/formats/installer/fead/encoder/crc32.c
    src/formats/installer/fead/encoder/compress.c)
list(APPEND XXFCLIB_SOURCES src/formats/installer/excelsior/xx_excelsior.c src/formats/archive/legacy_archive_engine/xx_legacy_archive_engine.c)
list(APPEND XXFCLIB_SOURCES
    src/formats/archive/dgca/xx_dgca_decode.c
    src/formats/archive/dgca/xx_dgca_payload.c
    src/formats/archive/dgca/xx_dgca_entropy.c
    src/formats/archive/dgca/xx_dgca_lzp.c
    src/formats/archive/dgca/xx_dgca_range.c
    src/formats/archive/dgca/xx_dgca_cipher.c
    src/formats/archive/dgca/xx_dgca_transform.c
    src/formats/archive/dgca/xx_dgca_adapter.c)

if(WIN32)
    list(APPEND XXFCLIB_SOURCES
        src/platforms/xx_entry_windows.c
        src/io/platforms/xx_io_windows.c
        src/process/platforms/xx_process_structs_win32.c
        src/process/platforms/xx_process_peb_layouts.h
        src/process/platforms/xx_process_teb_layouts.h
        src/process/platforms/xx_process_params_layouts.h
        src/process/platforms/xx_process_loader_layouts.h
        src/process/platforms/xx_process_related_layouts.h
        src/process/platforms/xx_process_heap_layouts.h
        src/process/platforms/xx_process_segment_heap_layouts.h
        src/process/platforms/xx_process_wow64_heap_identity.h
        src/process/platforms/xx_process_wow64_heap_layouts.h
        src/process/platforms/xx_process_wow64_segment_heap_layouts.h
        src/process/platforms/xx_process_heap_legacy_children.h
        src/process/platforms/xx_process_heap_context_children.h
        src/process/platforms/xx_process_heap_suballocator_layouts.h
        src/process/platforms/xx_process_wow64_heap_suballocator_layouts.h
        src/process/platforms/xx_process_heap_suballocator_discovery.h
        src/process/platforms/xx_process_heap_vs_chunk_layouts.h
        src/process/platforms/xx_process_wow64_heap_vs_chunk_layouts.h
        src/process/platforms/xx_process_heap_vs_chunk_discovery.h
        src/process/platforms/xx_process_heap_discovery.h
        src/memory/platforms/xx_memory_windows.c
        src/strings/platforms/xx_string_windows.c
        src/fs/platforms/xx_fs_windows.c
        src/rt/platforms/xx_rt_windows.c
        src/sort/platforms/xx_sort_windows.c
        src/global/platforms/xx_global_windows.c
        src/threads/platforms/xx_threads_windows.c
        src/terminal/platforms/xx_terminal_windows.c
        src/settings/platforms/xx_settings_file_windows.c
        src/settings/platforms/xx_settings_windows.c
        src/die_engine/platforms/xx_die_engine_windows.c
    )
else()
    list(APPEND XXFCLIB_SOURCES
        src/io/platforms/xx_io_posix.c
        src/process/platforms/xx_process_structs_posix.c
        src/memory/platforms/xx_memory_posix.c
        src/strings/platforms/xx_string_posix.c
        src/fs/platforms/xx_fs_posix.c
        src/rt/platforms/xx_rt_posix.c
        src/sort/platforms/xx_sort_posix.c
        src/global/platforms/xx_global_posix.c
        src/threads/platforms/xx_threads_posix.c
        src/terminal/platforms/xx_terminal_posix.c
        src/settings/platforms/xx_settings_file_posix.c
        src/die_engine/platforms/xx_die_engine_posix.c
    )
endif()

include(GNUInstallDirs)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    # The generic POSIX process collector excludes FreeBSD; its public kernel
    # query ABI and process-memory discovery are implemented separately.
    list(REMOVE_ITEM XXFCLIB_SOURCES src/process/platforms/xx_process_structs_posix.c)
    list(APPEND XXFCLIB_SOURCES src/process/platforms/xx_process_structs_freebsd.c)
endif()

if(APPLE)
    list(APPEND XXFCLIB_SOURCES src/settings/platforms/xx_settings_macos.c)
elseif(NOT WIN32)
    list(APPEND XXFCLIB_SOURCES src/settings/platforms/xx_settings_posix.c)
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

xxfclib_select_stream_sources(XXFCLIB_SOURCES)

# Helper to configure target include dirs, warnings, and export/import definitions
include(src/die_engine/xx_die_engine_cdisasm.cmake)

function(xxfclib_configure_target target is_shared)
    xxfclib_configure_streams(${target})
    target_compile_definitions(${target} PRIVATE XXFC_PROCESS_STRUCTS_USE_RUNTIME)
    if(is_shared)
        target_link_libraries(${target} PRIVATE cdisasm::cdisasm)
    else()
        target_link_libraries(${target} PUBLIC cdisasm::cdisasm)
    endif()
    if(APPLE)
        target_link_libraries(${target} PUBLIC "-framework CoreFoundation")
    endif()
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/global
            ${CMAKE_CURRENT_SOURCE_DIR}/src/io
            ${CMAKE_CURRENT_SOURCE_DIR}/src/memory
            ${CMAKE_CURRENT_SOURCE_DIR}/src/strings
            ${CMAKE_CURRENT_SOURCE_DIR}/src/data
            ${CMAKE_CURRENT_SOURCE_DIR}/src/data/platforms
            ${CMAKE_CURRENT_SOURCE_DIR}/src/list
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sort
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/crc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/entropy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/store
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/deflate
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/bzip2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/ppmd7
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/ppmd8
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/shrink
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/reduce
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/implode
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lz4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lz5
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lizard
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/brotli
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/zstd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/zipcrypto
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/aes
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/aes_winzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/sha
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/md5
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/ampk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/arcfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/arcv2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/arcv4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/aldus
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/dcl
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/rar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/arj
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/unixpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/compress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/mscompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/ash0
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/wiilz77
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzv1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/oraclesqueeze
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/softronics
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/gashuff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/huf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzdiet
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzpis2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzma_alone
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzop
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/tarx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzh
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/zip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/executable/jar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/apk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/ipa
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/7zip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/rar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/bz2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/gz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/xz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_common
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_gz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/npm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_bz2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_xz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_lz4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lz4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lz5
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lizard
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/brotli
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/unixpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/zlib
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/unixcompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/other/gitobject
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/mscompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ash0
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/wiilz77
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzv1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/oraclesqueeze
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/softronics
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/logitechcompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/dmapacked
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/gashuff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/huf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzdiet
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzpis2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_zstd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/zstd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/cpio
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/other/mtree
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_nextstep
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_compress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_lzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_lzma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tar_lzop
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tarx1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tarx2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/iso9660
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/trx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/seama
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/chk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/packimg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/dlob
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/wince
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/binhdr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/rtk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/csman
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/vxworks
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/uefi_fv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/uefi_capsule
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/qcow
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/qnx6
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/luks
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/apfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/btrfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/logfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/dmg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/dms
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzop
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/srec
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/dclraw
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/xpak
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/database/pdb
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/infogramesft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/uboot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/twrx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/tplink
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/silmarilsft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/shrs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/mh01
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/matter_ota
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lz4demo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lingvoarc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/jboot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/encrpted_img
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/encfw
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/ecos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/dlke
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/dlink_tlv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/dkbs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/autel
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/arcadyan
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/androidboot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/rawstac
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/rtpatch
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/softronics
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/vmssaveset
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/boo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/wolfft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/settlersft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/teacy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/rsc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/res
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/bsn
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/wintermutedcp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/volitionvpft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/agis
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/hog
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzpis2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/rnca
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/paperport
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/executable/starkit
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lspack10
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ixa
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lif
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/qip2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/emt
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/bagf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/wrzl
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/spk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/stac
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/database/dbz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/squeeze2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/sq
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/phar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/mpq
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/sabdu
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/apricot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/hdcopy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/copydisk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/ciso
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/vmdk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/vhddynamic
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/wim
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/softpaq2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/aiaff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/image/gxl
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/shrinkwrap
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/red
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/diskexpress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/cpx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/smsipak
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/bnd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/cat
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/csidos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/binder
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/jasc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/recognita
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/scf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/bcw
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/bvrp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ssm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/mdcd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/xlas
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/pain
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/qrst
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/opc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/tnef
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/mcc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/notetab
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/stunts
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/megatechvol
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/video/grasp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/psn
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/sinner
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/hog2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/image/pcxlib
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/vmsdb
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/vmspcsi
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/beospkg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/solarispkg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/pax
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/executable/copyqmexe
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/diskjuggler
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/pmdiskcopy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/diskdupe
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/imd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/twoimg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/fdi
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/hfe
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/teledisk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/copyqm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/pcinstall
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/gksetup
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/is11
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/izpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/squeeze1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/trdos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/lifkd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/arcv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/compaqlzh
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzk00
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/pma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/binhex
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/binaryii
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/stuffit
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/dclft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/executable/debugscr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/gob
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/savedskf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/edilzss
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/is7inx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/is5
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/is3
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/psdc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/unixcompact
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/sco
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/gpfpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ftcomp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/winlink
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/gst
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/finear
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/mwave
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/xorarchive
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/mxs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/edc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/mrnz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/tpwm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/cazip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ibmpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/rnc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/shar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/netware2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/mathcad
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/document/perform
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/battleisle
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/kpck
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/game/beatthehouse
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/pp20
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/macbinary
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/applesingle
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/executable/resourcefork
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/cramfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/jffs2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/yaffs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/ubi
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/ubifs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/ext
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/fat
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/mbr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/gpt
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/sparse
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/uimage
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/firmware/dtb
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/squashfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/ntfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/udf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/romfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/sqz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/topspeed
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/database/tps
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ulead
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/quantum
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/zxzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/zoom
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/audio
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/audio/die_music
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/audio/sfpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/claylz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/c64wraptor
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/corelltec
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/pcsecure
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/rsvk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/lzw15v
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/saf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/sls
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/nid
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/gamos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/panorama
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/fpak
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ace
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ain
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/aldus
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/alz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/ampk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/aodos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/arcfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/pdp11ar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/artipack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/arcv2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/arcv4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/warc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/installer/aixbff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/arx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/cab
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/executable/msdos
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/crc
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/store
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/compress
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/deflate
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/bzip2
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/lzma
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/ppmd7
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/ppmd8
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/shrink
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/reduce
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/implode
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/lz4
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/lz5
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/lizard
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/brotli
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/zstd
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/zipcrypto
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/aes
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/aes_winzip
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/sha
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/md5
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/ain
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/ampk
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/arcfs
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/arcv2
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/arcv4
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/aldus
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/dcl
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/rar
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/arj
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/unixpack
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/lzh
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/lzx
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/algo/quantum
            ${CMAKE_CURRENT_SOURCE_DIR}/include/xxfclib/formats
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platforms
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:/W4>
            $<$<COMPILE_LANGUAGE:C>:/utf-8>
            $<$<COMPILE_LANGUAGE:C>:/MP>
            # A parallel Ninja build otherwise has many cl.exe instances
            # writing one target PDB, intermittently failing with C1041.
            # Keep Debug information in each object instead.
            $<$<AND:$<COMPILE_LANGUAGE:C>,$<CONFIG:Debug>>:/Z7>
            $<$<AND:$<COMPILE_LANGUAGE:C>,$<CONFIG:RelWithDebInfo>>:/Z7>
        )
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if((XXFC_MSVC_X64 AND XXFC_SHARED_NO_CRT AND is_shared) OR
           (XXFC_MSVC_STATIC_NO_CRT_ARCH AND NOT is_shared AND XXFC_STATIC_NO_CRT))
            # SQLite's hosted amalgamation must not introduce CRT imports into
            # the existing freestanding builds. The raw SQLite page reader is
            # still available there; hosted builds provide the SQL export.
            target_compile_definitions(${target} PRIVATE XXFC_SQLITE_HOSTED_DISABLED)
        endif()

        if(is_shared AND XXFC_MSVC_X64 AND XXFC_SHARED_NO_CRT)
            # This configuration links without the CRT, so xx_entry_windows.c
            # must supply memset/memcpy/memmove/_fltused itself. The static
            # target keeps the CRT and must NOT get them, or the link fails
            # with LNK2005 against LIBCMT.
            target_compile_definitions(${target} PRIVATE XXFC_NO_CRT)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:/GS->
                $<$<COMPILE_LANGUAGE:C>:/sdl->
                $<$<COMPILE_LANGUAGE:C>:/GL->
                $<$<COMPILE_LANGUAGE:C>:/Zl>)
            if(POLICY CMP0091)
                set_property(TARGET ${target} PROPERTY
                    MSVC_RUNTIME_LIBRARY "MultiThreaded")
            else()
                target_compile_options(${target} PRIVATE /MT)
            endif()
            if(POLICY CMP0184)
                set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_CHECKS "")
            endif()

            target_sources(${target} PRIVATE src/platforms/xx_chkstk_x64.asm)

            target_link_options(${target} PRIVATE
                /NODEFAULTLIB
                /ENTRY:DllMain
                /OPT:REF
                /OPT:ICF
            )
            target_link_libraries(${target} PRIVATE kernel32.lib)
        elseif(NOT is_shared)
            target_link_libraries(${target} INTERFACE kernel32.lib)

            if(XXFC_STATIC_NO_CRT AND XXFC_MSVC_STATIC_NO_CRT_ARCH)
                # Freestanding archive. /Zl drops the /DEFAULTLIB records and
                # /GS- removes the security-cookie references. x64 carries its
                # stack probe in the archive; ARM64 consumers also link the
                # xxfclib::no_crt_runtime helper target declared above.
                # No XXFC_NO_CRT: see the option's comment -- the consumer's
                # own entry object owns memset/memcpy/memmove/_fltused.
                target_compile_options(${target} PRIVATE
                    $<$<COMPILE_LANGUAGE:C>:/GS->
                    $<$<COMPILE_LANGUAGE:C>:/sdl->
                    $<$<COMPILE_LANGUAGE:C>:/GL->
                    $<$<COMPILE_LANGUAGE:C>:/Zl>)
                if(POLICY CMP0091)
                    set_property(TARGET ${target} PROPERTY
                        MSVC_RUNTIME_LIBRARY "MultiThreaded")
                else()
                    target_compile_options(${target} PRIVATE /MT)
                endif()
                if(POLICY CMP0184)
                    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_CHECKS "")
                endif()

                if(XXFC_MSVC_X64)
                    target_sources(${target} PRIVATE src/platforms/xx_chkstk_x64.asm)
                    target_link_options(${target} INTERFACE
                        /INCLUDE:__chkstk)
                endif()
            endif()
        else()
            # Hosted MSVC shared builds use the compiler's normal runtime.
            # This is always required on x86 and ARM64 and can be selected on
            # x64 when the complete format set contains hosted C code.
            target_link_libraries(${target} PRIVATE kernel32.lib)
        endif()

        if(XXFC_MSVC_X64)
            target_sources(${target} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzma/asm/lzma_x64.asm"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/bzip2/asm/bzip2_x64.asm"
            )
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
            target_sources(${target} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/lzma/asm/lzma_x86.asm"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/bzip2/asm/bzip2_x86.asm"
            )
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(CMAKE_DL_LIBS)
            if(is_shared)
                target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
            else()
                target_link_libraries(${target} INTERFACE ${CMAKE_DL_LIBS})
            endif()
        endif()
        if(NOT WIN32)
            if(is_shared)
                target_link_libraries(${target} PRIVATE Threads::Threads m)
            else()
                target_link_libraries(${target} INTERFACE Threads::Threads m)
            endif()
        endif()
    endif()

    if(is_shared)
        target_compile_definitions(${target} PRIVATE XXFC_BUILD_SHARED)
        set_target_properties(${target} PROPERTIES
            C_VISIBILITY_PRESET hidden
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
        )
    else()
        target_compile_definitions(${target} PUBLIC XXFC_STATIC)
        # The static archive is routinely linked INTO someone else's shared
        # library. Without position-independent code that link is rejected
        # outright on ELF ("recompile with -fPIC"); on Windows the property is
        # a no-op. Cheaper to always be linkable than to make every consumer
        # discover this.
        set_target_properties(${target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
    endif()
endfunction()

# Target definition
if(XXFC_BUILD_BOTH)
    # Build static target
    add_library(xxfclib_static STATIC
        ${XXFCLIB_PUBLIC_HEADERS}
        ${XXFCLIB_SOURCES}
    )
    set_target_properties(xxfclib_static PROPERTIES OUTPUT_NAME xxfclib_static)
    xxfclib_configure_target(xxfclib_static FALSE)
    add_library(xxfclib::xxfclib_static ALIAS xxfclib_static)

    # Build shared target
    add_library(xxfclib_shared SHARED
        ${XXFCLIB_PUBLIC_HEADERS}
        ${XXFCLIB_SOURCES}
    )
    set_target_properties(xxfclib_shared PROPERTIES OUTPUT_NAME xxfclib)
    xxfclib_configure_target(xxfclib_shared TRUE)
    add_library(xxfclib::xxfclib_shared ALIAS xxfclib_shared)

    if(BUILD_SHARED_LIBS)
        add_library(xxfclib::xxfclib ALIAS xxfclib_shared)
    else()
        add_library(xxfclib::xxfclib ALIAS xxfclib_static)
    endif()

    set(XXFCLIB_INSTALL_TARGETS xxfclib_static xxfclib_shared)
else()
    if(BUILD_SHARED_LIBS)
        set(_xxfclib_type SHARED)
        set(_is_shared TRUE)
    else()
        set(_xxfclib_type STATIC)
        set(_is_shared FALSE)
    endif()

    add_library(xxfclib ${_xxfclib_type}
        ${XXFCLIB_PUBLIC_HEADERS}
        ${XXFCLIB_SOURCES}
    )
    xxfclib_configure_target(xxfclib ${_is_shared})
    add_library(xxfclib::xxfclib ALIAS xxfclib)

    set(XXFCLIB_INSTALL_TARGETS xxfclib)
endif()

# die_engine: the DIE signature engine alone, as its own static archive
# (die_engine.lib / libdie_engine.a). die_engine.cmake lists its sources --
# the engine's exact link closure -- so a consumer that only scans with DIE
# signatures links ~80 objects instead of the full ~3400-object archive.
# Configured like the static xxfclib, including XXFC_STATIC_NO_CRT.
if(XXFC_BUILD_DIE_ENGINE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/die_engine.cmake")
    xxfclib_select_stream_sources(XXFC_DIE_ENGINE_SOURCES)
    add_library(die_engine STATIC ${XXFC_DIE_ENGINE_SOURCES})
    xxfclib_configure_target(die_engine FALSE)
    if(XXFC_DIE_ENGINE_TAR_ONLY_WRITING)
        target_compile_definitions(die_engine PRIVATE XXFC_ARCHIVE_WRITE_TAR_ONLY=1)
    endif()
    add_library(xxfclib::die_engine ALIAS die_engine)
    list(APPEND XXFCLIB_INSTALL_TARGETS die_engine)
endif()

if(TARGET xxfclib_no_crt_runtime)
    # Keep an installed freestanding ARM64 archive usable outside this source
    # tree by shipping the compiler-support library beside it.
    list(APPEND XXFCLIB_INSTALL_TARGETS xxfclib_no_crt_runtime)
endif()

if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
    set_source_files_properties(src/algo/adler32/platforms/xx_adler32_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/data/platforms/xx_data_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/memory/platforms/xx_memory_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/algo/entropy/platforms/xx_entropy_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/algo/lzma/platforms/xx_lzma_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/algo/crc/platforms/xx_crc64_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/algo/deflate/platforms/xx_deflate_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
endif()


# Optional sample programs
if(XXFC_BUILD_SAMPLES)
    add_subdirectory(samples/console)
    add_subdirectory(samples/unpack)
endif()

# Installation Targets
foreach(_native bitrock smart_install_maker molebox superdat excelsior fead wim legacy_archive_engine dgca)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/${_native}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/source/${_native}")
endforeach()
foreach(_documented_native excelsior fead)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_documented_native}/FORMAT.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_documented_native}/provenance.json"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/source/${_documented_native}")
endforeach()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/smart-install-maker-coverage.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/docs")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/bzip2/LICENSE.bzip2"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/bzip2")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/wavpack/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/wavpack")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/packmp3/COPYING"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/packmp3/COPYING.LESSER"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algo/packmp3/PROVENANCE.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/packmp3")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/archive/xpk_compressed_file/LICENSE.ancient"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/ancient")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/die_engine/audio_db/LICENSE.DIE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/die-audio")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/hfsplus/LICENSE.NetBSD"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/filesystem/hfsplus/LICENSE.Unicode"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/hfsplus")
install(TARGETS ${XXFCLIB_INSTALL_TARGETS}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/xxfclib
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/README.md"
    "${CMAKE_CURRENT_LIST_DIR}/LICENSE"
    "${CMAKE_CURRENT_LIST_DIR}/FIND_TEST_RESULTS.md"
    DESTINATION . OPTIONAL
)

# Optional tests
if(XXFC_BUILD_PDF_TESTS)
    if(TARGET xxfclib_static)
        set(_xxfc_pdf_test_library xxfclib_static)
    elseif(TARGET xxfclib AND NOT BUILD_SHARED_LIBS)
        set(_xxfc_pdf_test_library xxfclib)
    else()
        message(FATAL_ERROR "XXFC_BUILD_PDF_TESTS requires a static xxfclib target")
    endif()
    enable_testing()
    add_executable(test_pdf tests/pdf/test_pdf.c)
    target_link_libraries(test_pdf PRIVATE ${_xxfc_pdf_test_library})
    if(MSVC)
        target_compile_definitions(test_pdf PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(test_pdf PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_pdf PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_pdf COMMAND test_pdf)
    unset(_xxfc_pdf_test_library)
endif()

if(XXFC_BUILD_PYC_TESTS)
    if(TARGET xxfclib_static)
        set(_xxfc_pyc_test_library xxfclib_static)
    elseif(TARGET xxfclib AND NOT BUILD_SHARED_LIBS)
        set(_xxfc_pyc_test_library xxfclib)
    else()
        message(FATAL_ERROR "XXFC_BUILD_PYC_TESTS requires a static xxfclib target")
    endif()
    enable_testing()
    add_executable(test_pyc tests/pyc/test_pyc.c)
    target_link_libraries(test_pyc PRIVATE ${_xxfc_pyc_test_library})
    if(MSVC)
        target_compile_definitions(test_pyc PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(test_pyc PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_pyc PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_pyc COMMAND test_pyc)
    unset(_xxfc_pyc_test_library)
endif()

if(XXFC_BUILD_ZIP_SUPPORT_TESTS)
    set(XXFC_ZIP_TEST_LIBRARY xxfclib::xxfclib)
    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/zip_support/RegisterTests.cmake")
endif()

if(XXFC_BUILD_TESTS OR XXFC_BUILD_SCAN_TESTS)
    enable_testing()
    add_executable(test_scan tests/test_scan.c)
    target_link_libraries(test_scan PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scan COMMAND test_scan)
    add_executable(test_scan_types tests/test_scan_types.c)
    target_link_libraries(test_scan_types PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scan_types COMMAND test_scan_types)
    add_executable(test_scan_callbacks tests/test_scan_callbacks.c)
    target_link_libraries(test_scan_callbacks PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scan_callbacks COMMAND test_scan_callbacks)
    add_executable(test_scan_all_types tests/test_scan_all_types.c)
    target_link_libraries(test_scan_all_types PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scan_all_types COMMAND test_scan_all_types)
    add_executable(test_scan_overlay tests/test_scan_overlay.c)
    target_link_libraries(test_scan_overlay PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scan_overlay COMMAND test_scan_overlay)
    add_executable(test_io_sub tests/test_io_sub.c)
    target_link_libraries(test_io_sub PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_io_sub PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(test_io_sub PRIVATE /UNDEBUG)
    else()
        target_compile_options(test_io_sub PRIVATE -UNDEBUG)
    endif()
    add_test(NAME test_io_sub COMMAND test_io_sub)
endif()

if(XXFC_BUILD_TESTS OR XXFC_BUILD_EXTRACTOR_TESTS)
    enable_testing()
    add_executable(test_extractor tests/test_extractor.c)
    if(MSVC)
        target_link_options(test_extractor PRIVATE /STACK:8388608)
        target_compile_definitions(test_extractor PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(TARGET die_engine)
        target_link_libraries(test_extractor PRIVATE xxfclib::die_engine)
    else()
        target_link_libraries(test_extractor PRIVATE xxfclib::xxfclib)
    endif()
    add_test(NAME test_extractor COMMAND test_extractor)
    set_tests_properties(test_extractor PROPERTIES TIMEOUT 60)
endif()

if(XXFC_BUILD_TESTS OR XXFC_BUILD_STRINGS_TESTS)
    enable_testing()
    add_executable(test_strings_scan tests/test_strings_scan.c)
    add_executable(bench_strings_billion tools/bench_strings_billion.c)
    if(TARGET die_engine)
        set(_xxfc_strings_library xxfclib::die_engine)
    else()
        set(_xxfc_strings_library xxfclib::xxfclib)
    endif()
    target_link_libraries(test_strings_scan PRIVATE ${_xxfc_strings_library})
    target_link_libraries(bench_strings_billion PRIVATE ${_xxfc_strings_library})
    add_test(NAME test_strings_scan COMMAND test_strings_scan)
    add_test(NAME test_strings_generated COMMAND bench_strings_billion --count 100000 --text)
    set_tests_properties(test_strings_scan test_strings_generated PROPERTIES TIMEOUT 60)
endif()

if(XXFC_BUILD_TESTS OR XXFC_BUILD_THREAD_TESTS)
    enable_testing()
    add_executable(test_threads tests/test_threads.c)
    target_link_libraries(test_threads PRIVATE xxfclib::xxfclib)
    add_test(NAME test_threads COMMAND test_threads)
    set_tests_properties(test_threads PROPERTIES TIMEOUT 60)
    add_executable(test_thread_pool tests/test_thread_pool.c)
    target_link_libraries(test_thread_pool PRIVATE xxfclib::xxfclib)
    add_test(NAME test_thread_pool COMMAND test_thread_pool)
    set_tests_properties(test_thread_pool PROPERTIES TIMEOUT 60)
endif()

if(XXFC_BUILD_DISASM_TESTS)
    if(TARGET xxfclib_static)
        set(_xxfc_disasm_test_library xxfclib_static)
    elseif(TARGET xxfclib AND NOT BUILD_SHARED_LIBS)
        set(_xxfc_disasm_test_library xxfclib)
    else()
        message(FATAL_ERROR "XXFC_BUILD_DISASM_TESTS requires a static xxfclib target")
    endif()
    enable_testing()
    add_executable(test_die_disasm tests/test_die_disasm.c)
    target_include_directories(test_die_disasm PRIVATE src)
    target_link_libraries(test_die_disasm PRIVATE ${_xxfc_disasm_test_library})
    add_test(NAME test_die_disasm COMMAND test_die_disasm)
    unset(_xxfc_disasm_test_library)
endif()

if(XXFC_BUILD_TESTS)
    enable_testing()
    add_executable(test_file_type tests/file_type/test_file_type.c)
    target_link_libraries(test_file_type PRIVATE xxfclib::xxfclib)
    add_test(NAME test_file_type COMMAND test_file_type)
    add_executable(test_executable_inspection tests/executable_inspect/test_executable_inspection.c)
    target_link_libraries(test_executable_inspection PRIVATE xxfclib::xxfclib)
    add_test(NAME test_executable_inspection COMMAND test_executable_inspection)
    add_executable(test_pe_streams tests/executable_inspect/test_pe_streams.c)
    target_link_libraries(test_pe_streams PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pe_streams COMMAND test_pe_streams)
    add_executable(test_pe_symbols tests/executable_inspect/test_pe_symbols.c)
    target_link_libraries(test_pe_symbols PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pe_symbols COMMAND test_pe_symbols)
    add_executable(test_pe_inspection_regressions tests/executable_inspect/test_pe_inspection_regressions.c)
    target_link_libraries(test_pe_inspection_regressions PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pe_inspection_regressions COMMAND test_pe_inspection_regressions)
    add_executable(test_pe_data_regressions tests/dotnet/test_pe_regressions.c)
    target_link_libraries(test_pe_data_regressions PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pe_data_regressions COMMAND test_pe_data_regressions)
    add_executable(test_dotnet_registration tests/executable_inspect/test_dotnet_registration.c)
    target_link_libraries(test_dotnet_registration PRIVATE xxfclib::xxfclib)
    add_test(NAME test_dotnet_registration COMMAND test_dotnet_registration)
    add_executable(test_die_native_api tests/test_die_native_api.c)
    target_include_directories(test_die_native_api PRIVATE src)
    target_link_libraries(test_die_native_api PRIVATE xxfclib::xxfclib)
    add_test(NAME test_die_native_api COMMAND test_die_native_api)
    add_executable(test_die_primitives tests/test_die_primitives.c)
    target_include_directories(test_die_primitives PRIVATE src)
    target_link_libraries(test_die_primitives PRIVATE xxfclib::xxfclib)
    add_test(NAME test_die_primitives COMMAND test_die_primitives)
    add_executable(test_die_primitives_js tests/test_die_primitives_js.c)
    target_include_directories(test_die_primitives_js PRIVATE src)
    target_link_libraries(test_die_primitives_js PRIVATE xxfclib::xxfclib)
    add_test(NAME test_die_primitives_js COMMAND test_die_primitives_js)
    add_executable(test_die_signature_batch tests/test_die_signature_batch.c)
    target_include_directories(test_die_signature_batch PRIVATE src)
    target_link_libraries(test_die_signature_batch PRIVATE xxfclib::xxfclib)
    add_test(NAME test_die_signature_batch COMMAND test_die_signature_batch)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_blake2.c")
        add_executable(test_blake2 tests/test_blake2.c)
        target_link_libraries(test_blake2 PRIVATE xxfclib::xxfclib)
        add_test(NAME test_blake2 COMMAND test_blake2)
    endif()

    add_executable(test_format_split tests/test_format_split.c)
    target_link_libraries(test_format_split PRIVATE xxfclib::xxfclib)
    add_test(NAME test_format_split COMMAND test_format_split)

    add_executable(test_zip_split tests/test_zip_split.c)
    target_link_libraries(test_zip_split PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_zip_split PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_zip_split COMMAND test_zip_split)

    add_executable(test_rar_split tests/test_rar_split.c)
    target_link_libraries(test_rar_split PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_rar_split PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_rar_split COMMAND test_rar_split)

    add_executable(test_rar_crypto tests/test_rar_crypto.c)
    target_link_libraries(test_rar_crypto PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_rar_crypto PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_rar_crypto COMMAND test_rar_crypto)

    add_executable(test_rar_header_crypto tests/test_rar_header_crypto.c)
    target_link_libraries(test_rar_header_crypto PRIVATE xxfclib::xxfclib)
    add_test(NAME test_rar_header_crypto COMMAND test_rar_header_crypto)

    add_executable(test_rar_cleanup tests/test_rar_cleanup.c)
    target_compile_definitions(test_rar_cleanup PRIVATE XXFC_STATIC)
    target_link_libraries(test_rar_cleanup PRIVATE xxfclib::xxfclib)
    add_test(NAME test_rar_cleanup COMMAND test_rar_cleanup)

    add_executable(test_zip_crypto_progress tests/test_zip_crypto_progress.c)
    target_compile_definitions(test_zip_crypto_progress PRIVATE XXFC_STATIC)
    target_link_libraries(test_zip_crypto_progress PRIVATE xxfclib::xxfclib)
    add_test(NAME test_zip_crypto_progress COMMAND test_zip_crypto_progress)

    add_executable(test_rar_encrypted tests/test_rar_encrypted.c)
    target_link_libraries(test_rar_encrypted PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_rar_encrypted PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    add_executable(probe_multivolume_archives tests/probe_multivolume_archives.c)
    target_link_libraries(probe_multivolume_archives PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(probe_multivolume_archives PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    set(XXFC_SPLIT_ARCHIVE_CORPUS_DIR "" CACHE PATH "Optional generated RAR/7z/ZIP multi-volume corpus")
    set(XXFC_RAR4_SPLIT_CORPUS_DIR "" CACHE PATH "Optional additional RAR4 corpus (requires split archive corpus)")
    set(XXFC_RAR_ENCRYPTED_CORPUS_DIR "" CACHE PATH "Optional native encrypted RAR4/RAR5 corpus")
    set(XXFC_RAR_HASH_CORPUS_DIR "" CACHE PATH "Optional native RAR5 BLAKE2sp/HASHMAC corpus")
    if(XXFC_SPLIT_ARCHIVE_CORPUS_DIR AND WIN32)
        find_program(XXFC_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        if(NOT XXFC_POWERSHELL_EXECUTABLE)
            message(FATAL_ERROR "The generated split archive corpus test requires PowerShell")
        endif()
        set(_xxfc_corpus_args
            -CorpusDir "${XXFC_SPLIT_ARCHIVE_CORPUS_DIR}"
            -ProbePath "$<TARGET_FILE:probe_multivolume_archives>"
            -OutputRoot "${CMAKE_CURRENT_BINARY_DIR}/split_archive_results")
        if(XXFC_RAR4_SPLIT_CORPUS_DIR)
            list(APPEND _xxfc_corpus_args -Rar4CorpusDir "${XXFC_RAR4_SPLIT_CORPUS_DIR}")
        endif()
        add_test(NAME test_split_archive_corpus
                 COMMAND "${XXFC_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                         -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_multivolume_samples.ps1"
                         ${_xxfc_corpus_args})
        set_tests_properties(test_split_archive_corpus PROPERTIES TIMEOUT 180 LABELS "integration;split")
    endif()
    if(XXFC_RAR_ENCRYPTED_CORPUS_DIR AND WIN32)
        find_program(XXFC_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        if(NOT XXFC_POWERSHELL_EXECUTABLE)
            message(FATAL_ERROR "The encrypted RAR corpus test requires PowerShell")
        endif()
        add_test(NAME test_rar_encrypted_corpus
                 COMMAND "${XXFC_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                         -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rar_encrypted_samples.ps1"
                         -CorpusDir "${XXFC_RAR_ENCRYPTED_CORPUS_DIR}"
                         -RunnerPath "$<TARGET_FILE:test_rar_encrypted>"
                         -OutputRoot "${CMAKE_CURRENT_BINARY_DIR}/rar_encrypted_results")
        set_tests_properties(test_rar_encrypted_corpus PROPERTIES TIMEOUT 600 LABELS "integration;rar;encryption")
    endif()
    if(XXFC_RAR_HASH_CORPUS_DIR)
        find_program(XXFC_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        if(NOT XXFC_POWERSHELL_EXECUTABLE)
            message(FATAL_ERROR "The RAR5 BLAKE2sp corpus test requires PowerShell")
        endif()
        add_test(NAME test_rar_hash_corpus
                 COMMAND "${XXFC_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                         -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rar_hash_samples.ps1"
                         -CorpusDir "${XXFC_RAR_HASH_CORPUS_DIR}"
                         -ProbePath "$<TARGET_FILE:probe_multivolume_archives>"
                         -EncryptedRunnerPath "$<TARGET_FILE:test_rar_encrypted>"
                         -OutputRoot "${CMAKE_CURRENT_BINARY_DIR}/rar_hash_results")
        set_tests_properties(test_rar_hash_corpus PROPERTIES TIMEOUT 600 LABELS "integration;rar;hash;encryption")
    endif()

    add_executable(test_io_resize tests/io_resize/test_io_resize.c)
    target_link_libraries(test_io_resize PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_io_resize PRIVATE _CRT_SECURE_NO_WARNINGS)
    elseif(UNIX)
        target_compile_definitions(test_io_resize PRIVATE _POSIX_C_SOURCE=200809L _FILE_OFFSET_BITS=64)
    endif()
    add_test(NAME xxfc.io.resize COMMAND test_io_resize)

    add_executable(test_history tests/history/test_history.c)
    target_link_libraries(test_history PRIVATE xxfclib::xxfclib)
    add_test(NAME xxfc.history COMMAND test_history)

    add_executable(test_io_multivolume tests/test_io_multivolume.c)
    target_link_libraries(test_io_multivolume PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_io_multivolume PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_io_multivolume COMMAND test_io_multivolume)

    set(XXFC_IO_VOLUME_CORPUS_DIR "" CACHE PATH "Optional WinAce split-volume corpus directory")
    if(XXFC_IO_VOLUME_CORPUS_DIR)
        add_test(NAME test_io_multivolume_winace
                 COMMAND test_io_multivolume "${XXFC_IO_VOLUME_CORPUS_DIR}")
    endif()

    add_executable(test_basic tests/test_basic.c)
    target_link_libraries(test_basic PRIVATE xxfclib::xxfclib)
    add_test(NAME test_basic COMMAND test_basic)

    add_executable(test_terminal tests/test_terminal.c)
    target_link_libraries(test_terminal PRIVATE xxfclib::xxfclib)
    add_executable(test_settings tests/test_settings.c)
    target_link_libraries(test_settings PRIVATE xxfclib::xxfclib)
    if(WIN32)
        target_link_libraries(test_settings PRIVATE advapi32)
    endif()
    add_test(NAME settings COMMAND test_settings)
    add_test(NAME test_terminal COMMAND test_terminal)

    add_executable(test_large_find tests/test_large_find.c)
    target_link_libraries(test_large_find PRIVATE xxfclib::xxfclib)
    add_test(NAME test_large_find COMMAND test_large_find)

    add_executable(test_zip_data_structs tests/test_zip_data_structs.c)
    target_link_libraries(test_zip_data_structs PRIVATE xxfclib::xxfclib)
    add_test(NAME test_zip_data_structs COMMAND test_zip_data_structs)

    add_executable(test_zip_archive_records tests/test_zip_archive_records.c)
    target_link_libraries(test_zip_archive_records PRIVATE xxfclib::xxfclib)
    add_test(NAME test_zip_archive_records COMMAND test_zip_archive_records)

    add_executable(test_jar_apk tests/test_jar_apk.c)
    target_link_libraries(test_jar_apk PRIVATE xxfclib::xxfclib)
    add_test(NAME test_jar_apk COMMAND test_jar_apk)

    add_executable(test_ipa_npm tests/test_ipa_npm.c)
    target_link_libraries(test_ipa_npm PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ipa_npm COMMAND test_ipa_npm)

    add_executable(test_zstd_compressed tests/test_zstd_compressed.c)
    target_link_libraries(test_zstd_compressed PRIVATE xxfclib::xxfclib)
    add_test(NAME test_zstd_compressed COMMAND test_zstd_compressed)

    add_executable(test_7zip_aes tests/test_7zip_aes.c)
    target_link_libraries(test_7zip_aes PRIVATE xxfclib::xxfclib)
    add_test(NAME test_7zip_aes COMMAND test_7zip_aes)

    add_executable(test_arj_methods tests/test_arj_methods.c)
    target_link_libraries(test_arj_methods PRIVATE xxfclib::xxfclib)
    add_executable(test_ain tests/test_ain.c)
    target_link_libraries(test_ain PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ain COMMAND test_ain)
    add_executable(test_aldus tests/test_aldus.c)
    target_link_libraries(test_aldus PRIVATE xxfclib::xxfclib)
    add_test(NAME test_aldus COMMAND test_aldus)
    add_executable(test_alz tests/test_alz.c)
    target_link_libraries(test_alz PRIVATE xxfclib::xxfclib)
    add_test(NAME test_alz COMMAND test_alz)
    add_executable(test_ampk tests/test_ampk.c)
    target_link_libraries(test_ampk PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ampk COMMAND test_ampk)
    add_executable(test_aodos tests/test_aodos.c)
    target_link_libraries(test_aodos PRIVATE xxfclib::xxfclib)
    add_test(NAME test_aodos COMMAND test_aodos)
    add_executable(test_arcfs tests/test_arcfs.c)
    target_link_libraries(test_arcfs PRIVATE xxfclib::xxfclib)
    add_test(NAME test_arcfs COMMAND test_arcfs)
    add_executable(test_pdp11ar tests/test_pdp11ar.c)
    target_link_libraries(test_pdp11ar PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pdp11ar COMMAND test_pdp11ar)
    add_executable(test_artipack tests/test_artipack.c)
    target_link_libraries(test_artipack PRIVATE xxfclib::xxfclib)
    add_test(NAME test_artipack COMMAND test_artipack)
    add_executable(test_arcv2 tests/test_arcv2.c)
    target_link_libraries(test_arcv2 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_arcv2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_arcv2 COMMAND test_arcv2)
    add_executable(test_arcv4 tests/test_arcv4.c)
    target_link_libraries(test_arcv4 PRIVATE xxfclib::xxfclib)
    add_test(NAME test_arcv4 COMMAND test_arcv4)
    add_executable(test_warc tests/test_warc.c)
    target_link_libraries(test_warc PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_warc PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_warc COMMAND test_warc)
    add_executable(test_tar_zstd tests/test_tar_zstd.c)
    target_link_libraries(test_tar_zstd PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tar_zstd PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tar_zstd COMMAND test_tar_zstd)
    add_executable(test_cpio tests/test_cpio.c)
    target_link_libraries(test_cpio PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_cpio PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_cpio COMMAND test_cpio)
    add_executable(test_afio_z_native tests/test_afio_z_native.c)
    target_link_libraries(test_afio_z_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_afio_z_native
        COMMAND test_afio_z_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-afio-z")
    add_executable(test_mtree tests/test_mtree.c)
    target_link_libraries(test_mtree PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_mtree PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_mtree COMMAND test_mtree)
    add_executable(test_tar_nextstep tests/test_tar_nextstep.c)
    target_link_libraries(test_tar_nextstep PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tar_nextstep PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tar_nextstep COMMAND test_tar_nextstep)
    add_executable(test_tar_compress tests/test_tar_compress.c)
    target_link_libraries(test_tar_compress PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tar_compress PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tar_compress COMMAND test_tar_compress)
    add_executable(test_tar_lzip tests/test_tar_lzip.c)
    target_link_libraries(test_tar_lzip PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tar_lzip PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tar_lzip COMMAND test_tar_lzip)
    add_executable(test_lzip tests/test_lzip.c)
    target_link_libraries(test_lzip PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lzip PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lzip COMMAND test_lzip)
    add_executable(test_lzma tests/test_lzma.c)
    target_link_libraries(test_lzma PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lzma PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lzma COMMAND test_lzma)
    add_executable(test_zstd tests/test_zstd.c)
    target_link_libraries(test_zstd PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_zstd PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_zstd COMMAND test_zstd)
    add_executable(test_lz4 tests/test_lz4.c)
    target_link_libraries(test_lz4 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lz4 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lz4 COMMAND test_lz4)
    add_executable(test_lz5 tests/test_lz5.c)
    target_link_libraries(test_lz5 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lz5 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lz5 COMMAND test_lz5)
    add_executable(test_lizard tests/test_lizard.c)
    target_link_libraries(test_lizard PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lizard PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lizard COMMAND test_lizard)
    add_executable(test_brotli tests/test_brotli.c)
    target_link_libraries(test_brotli PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_brotli PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_brotli COMMAND test_brotli)
    add_executable(test_unixpack tests/test_unixpack.c)
    target_link_libraries(test_unixpack PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_unixpack PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_unixpack COMMAND test_unixpack)
    add_executable(test_zlib tests/test_zlib.c)
    target_link_libraries(test_zlib PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_zlib PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_zlib COMMAND test_zlib)
    add_executable(test_unixcompress tests/test_unixcompress.c)
    target_link_libraries(test_unixcompress PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_unixcompress PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_unixcompress COMMAND test_unixcompress)
    add_executable(test_gitobject tests/test_gitobject.c)
    target_link_libraries(test_gitobject PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_gitobject PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_gitobject COMMAND test_gitobject)
    add_executable(test_mscompress tests/test_mscompress.c)
    target_link_libraries(test_mscompress PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_mscompress PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_mscompress COMMAND test_mscompress)
    add_executable(test_ash0 tests/test_ash0.c)
    target_link_libraries(test_ash0 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_ash0 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_ash0 COMMAND test_ash0)
    add_executable(test_wiilz77 tests/test_wiilz77.c)
    target_link_libraries(test_wiilz77 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_wiilz77 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_wiilz77 COMMAND test_wiilz77)
    add_executable(test_lzv1 tests/test_lzv1.c)
    target_link_libraries(test_lzv1 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lzv1 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lzv1 COMMAND test_lzv1)
    add_executable(test_oraclesqueeze tests/test_oraclesqueeze.c)
    target_link_libraries(test_oraclesqueeze PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_oraclesqueeze PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_oraclesqueeze COMMAND test_oraclesqueeze)
    add_executable(test_softronics tests/test_softronics.c)
    target_link_libraries(test_softronics PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_softronics PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_softronics COMMAND test_softronics)
    add_executable(test_lzop tests/test_lzop.c)
    target_link_libraries(test_lzop PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lzop PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lzop COMMAND test_lzop)
    add_executable(test_srec tests/test_srec.c)
    target_link_libraries(test_srec PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_srec PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_srec COMMAND test_srec)
    add_executable(test_dclraw tests/test_dclraw.c)
    target_link_libraries(test_dclraw PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_dclraw PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_dclraw COMMAND test_dclraw)
    add_executable(test_xpak tests/test_xpak.c)
    target_link_libraries(test_xpak PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_xpak PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_xpak COMMAND test_xpak)
    add_executable(test_pdb tests/test_pdb.c)
    target_link_libraries(test_pdb PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_pdb PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_pdb COMMAND test_pdb)
    add_executable(test_infogramesft tests/test_infogramesft.c)
    target_link_libraries(test_infogramesft PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_infogramesft PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_infogramesft COMMAND test_infogramesft)
    add_executable(test_uboot tests/test_uboot.c)
    target_link_libraries(test_uboot PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_uboot PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_uboot COMMAND test_uboot)
    add_executable(test_twrx tests/test_twrx.c)
    target_link_libraries(test_twrx PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_twrx PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_twrx COMMAND test_twrx)
    add_executable(test_tplink tests/test_tplink.c)
    target_link_libraries(test_tplink PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tplink PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tplink COMMAND test_tplink)
    add_executable(test_silmarilsft tests/test_silmarilsft.c)
    target_link_libraries(test_silmarilsft PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_silmarilsft PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_silmarilsft COMMAND test_silmarilsft)
    add_executable(test_shrs tests/test_shrs.c)
    target_link_libraries(test_shrs PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_shrs PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_shrs COMMAND test_shrs)
    add_executable(test_mh01 tests/test_mh01.c)
    target_link_libraries(test_mh01 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_mh01 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_mh01 COMMAND test_mh01)
    add_executable(test_matter_ota tests/test_matter_ota.c)
    target_link_libraries(test_matter_ota PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_matter_ota PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_matter_ota COMMAND test_matter_ota)
    add_executable(test_lz4demo tests/test_lz4demo.c)
    target_link_libraries(test_lz4demo PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lz4demo PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lz4demo COMMAND test_lz4demo)
    add_executable(test_lingvoarc tests/test_lingvoarc.c)
    target_link_libraries(test_lingvoarc PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lingvoarc PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lingvoarc COMMAND test_lingvoarc)
    add_executable(test_jboot tests/test_jboot.c)
    target_link_libraries(test_jboot PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_jboot PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_jboot COMMAND test_jboot)
    add_executable(test_encrpted_img tests/test_encrpted_img.c)
    target_link_libraries(test_encrpted_img PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_encrpted_img PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_encrpted_img COMMAND test_encrpted_img)
    add_executable(test_encfw tests/test_encfw.c)
    target_link_libraries(test_encfw PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_encfw PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_encfw COMMAND test_encfw)
    add_executable(test_ecos tests/test_ecos.c)
    target_link_libraries(test_ecos PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_ecos PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_ecos COMMAND test_ecos)
    add_executable(test_dlke tests/test_dlke.c)
    target_link_libraries(test_dlke PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_dlke PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_dlke COMMAND test_dlke)
    add_executable(test_dlink_tlv tests/test_dlink_tlv.c)
    target_link_libraries(test_dlink_tlv PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_dlink_tlv PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_dlink_tlv COMMAND test_dlink_tlv)
    add_executable(test_dkbs tests/test_dkbs.c)
    target_link_libraries(test_dkbs PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_dkbs PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_dkbs COMMAND test_dkbs)
    add_executable(test_autel tests/test_autel.c)
    target_link_libraries(test_autel PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_autel PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_autel COMMAND test_autel)
    add_executable(test_arcadyan tests/test_arcadyan.c)
    target_link_libraries(test_arcadyan PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_arcadyan PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_arcadyan COMMAND test_arcadyan)
    add_executable(test_androidboot tests/test_androidboot.c)
    target_link_libraries(test_androidboot PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_androidboot PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_androidboot COMMAND test_androidboot)
    add_executable(test_format_extractor tests/test_format_extractor.c)
    target_link_libraries(test_format_extractor PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_format_extractor PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_format_extractor COMMAND test_format_extractor)
    add_executable(test_logitechcompress tests/test_logitechcompress.c)
    target_link_libraries(test_logitechcompress PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_logitechcompress PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_logitechcompress COMMAND test_logitechcompress)
    add_executable(test_dmapacked tests/test_dmapacked.c)
    target_link_libraries(test_dmapacked PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_dmapacked PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_dmapacked COMMAND test_dmapacked)
    add_executable(test_gashuff tests/test_gashuff.c)
    target_link_libraries(test_gashuff PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_gashuff PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_gashuff COMMAND test_gashuff)
    add_executable(test_huf tests/test_huf.c)
    target_link_libraries(test_huf PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_huf PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_huf COMMAND test_huf)
    add_executable(test_lzdiet tests/test_lzdiet.c)
    target_link_libraries(test_lzdiet PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lzdiet PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lzdiet COMMAND test_lzdiet)
    add_executable(test_lzpis2 tests/test_lzpis2.c)
    target_link_libraries(test_lzpis2 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_lzpis2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_lzpis2 COMMAND test_lzpis2)
    add_executable(test_tar_lzma tests/test_tar_lzma.c)
    target_link_libraries(test_tar_lzma PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tar_lzma PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tar_lzma COMMAND test_tar_lzma)
    add_executable(test_tar_lzop tests/test_tar_lzop.c)
    target_link_libraries(test_tar_lzop PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tar_lzop PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tar_lzop COMMAND test_tar_lzop)
    add_executable(test_tarx1 tests/test_tarx1.c)
    target_link_libraries(test_tarx1 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tarx1 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tarx1 COMMAND test_tarx1)
    add_executable(test_tarx2 tests/test_tarx2.c)
    target_link_libraries(test_tarx2 PRIVATE xxfclib::xxfclib)
    if(MSVC)
        target_compile_definitions(test_tarx2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME test_tarx2 COMMAND test_tarx2)
    add_executable(test_arj_archive tests/test_arj_archive.c)
    target_link_libraries(test_arj_archive PRIVATE xxfclib::xxfclib)
    add_executable(test_aixbff tests/test_aixbff.c)
    target_link_libraries(test_aixbff PRIVATE xxfclib::xxfclib)
    add_test(NAME test_aixbff COMMAND test_aixbff)
    add_executable(test_pe tests/test_pe.c)
    target_link_libraries(test_pe PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pe COMMAND test_pe)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_msdos.c")
        add_executable(test_msdos tests/test_msdos.c)
        target_link_libraries(test_msdos PRIVATE xxfclib::xxfclib)
        add_test(NAME test_msdos COMMAND test_msdos)
    endif()
    add_executable(test_elf tests/test_elf.c)
    target_link_libraries(test_elf PRIVATE xxfclib::xxfclib)
    add_test(NAME test_elf COMMAND test_elf)
    add_executable(test_macho tests/test_macho.c)
    target_link_libraries(test_macho PRIVATE xxfclib::xxfclib)
    add_test(NAME test_macho COMMAND test_macho)
    add_executable(test_ne tests/test_ne.c)
    target_link_libraries(test_ne PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ne COMMAND test_ne)
    add_executable(test_le_lx tests/test_le_lx.c)
    target_link_libraries(test_le_lx PRIVATE xxfclib::xxfclib)
    add_test(NAME test_le_lx COMMAND test_le_lx)
    add_executable(test_dex tests/test_dex.c)
    target_link_libraries(test_dex PRIVATE xxfclib::xxfclib)
    add_test(NAME test_dex COMMAND test_dex)
    add_executable(test_arx tests/test_arx.c)
    target_link_libraries(test_arx PRIVATE xxfclib::xxfclib)
    set(XXFC_ARX_CORPUS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../main_test/main_test/files/arx"
        CACHE PATH "Directory containing ARX fixtures")
    if(EXISTS "${XXFC_ARX_CORPUS_DIR}/arx_default.arx" AND
       EXISTS "${XXFC_ARX_CORPUS_DIR}/arx100_def.arx")
        add_test(NAME test_arx
                 COMMAND test_arx
                         "${XXFC_ARX_CORPUS_DIR}/arx_default.arx"
                         "${XXFC_ARX_CORPUS_DIR}/arx100_def.arx")
    endif()
    add_executable(test_cab tests/test_cab.c)
    target_link_libraries(test_cab PRIVATE xxfclib::xxfclib)
    add_executable(test_format_samples tests/test_format_samples.c)
    target_link_libraries(test_format_samples PRIVATE xxfclib::xxfclib)
    set(XXFC_FORMAT_SAMPLE_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/../../main_test/main_test/files"
        CACHE PATH "Directory containing copied archive format samples")
    file(GLOB XXFC_AR_SAMPLES CONFIGURE_DEPENDS
         "${XXFC_FORMAT_SAMPLE_ROOT}/ar/*.a"
         "${XXFC_FORMAT_SAMPLE_ROOT}/ar/*.ar")
    file(GLOB XXFC_7ZIP_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/7zip/*.7z")
    file(GLOB XXFC_BZ2_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/bz2/*.bz2")
    file(GLOB XXFC_GZ_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/gz/*.gz")
    file(GLOB XXFC_XZ_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/xz/*.xz")
    file(GLOB XXFC_TAR_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/tar/*.tar")
    file(GLOB XXFC_TAR_GZ_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/tar_gz/*.tar.gz")
    file(GLOB XXFC_TAR_BZ2_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/tar_bz2/*.tar.bz2")
    file(GLOB XXFC_TAR_XZ_SAMPLES CONFIGURE_DEPENDS "${XXFC_FORMAT_SAMPLE_ROOT}/tar_xz/*.tar.xz")
    if(XXFC_7ZIP_SAMPLES AND XXFC_AR_SAMPLES AND XXFC_BZ2_SAMPLES AND XXFC_GZ_SAMPLES AND XXFC_XZ_SAMPLES AND
       XXFC_TAR_SAMPLES AND XXFC_TAR_GZ_SAMPLES AND XXFC_TAR_BZ2_SAMPLES AND XXFC_TAR_XZ_SAMPLES)
        add_test(NAME test_format_samples
                 COMMAND test_format_samples
                         --7zip ${XXFC_7ZIP_SAMPLES}
                         --ar ${XXFC_AR_SAMPLES}
                         --bz2 ${XXFC_BZ2_SAMPLES}
                         --gz ${XXFC_GZ_SAMPLES}
                         --xz ${XXFC_XZ_SAMPLES}
                         --tar ${XXFC_TAR_SAMPLES}
                         --tar-gz ${XXFC_TAR_GZ_SAMPLES}
                         --tar-bz2 ${XXFC_TAR_BZ2_SAMPLES}
                         --tar-xz ${XXFC_TAR_XZ_SAMPLES})
    endif()
    set(XXFC_CAB_CORPUS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../main_test/main_test/files/cab"
        CACHE PATH "Directory containing CAB fixtures")
    if(EXISTS "${XXFC_CAB_CORPUS_DIR}/testcab.cab" AND
       EXISTS "${XXFC_CAB_CORPUS_DIR}/lzx_multi.cab" AND
       EXISTS "${XXFC_CAB_CORPUS_DIR}/quantum_mixed.cab")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cab_test_output")
        add_test(NAME test_cab
                 COMMAND test_cab
                         "${XXFC_CAB_CORPUS_DIR}/testcab.cab"
                         "${XXFC_CAB_CORPUS_DIR}/mszip_single.cab"
                         "${XXFC_CAB_CORPUS_DIR}/mszip_multi.cab"
                          "${XXFC_CAB_CORPUS_DIR}/mszip_large.cab"
                          "${XXFC_CAB_CORPUS_DIR}/mszip_binary.cab"
                          "${XXFC_CAB_CORPUS_DIR}/lzx_multi.cab"
                          "${XXFC_CAB_CORPUS_DIR}/quantum_mixed.cab"
                          "${CMAKE_CURRENT_BINARY_DIR}/cab_test_output")
    endif()
    set(XXFC_ARJ_CORPUS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../main_test/main_test/files/arj"
        CACHE PATH "Directory containing ARJ method 0-4 fixtures")
    if(EXISTS "${XXFC_ARJ_CORPUS_DIR}/arj_stored.arj" AND
       EXISTS "${XXFC_ARJ_CORPUS_DIR}/arj_method1.arj" AND
       EXISTS "${XXFC_ARJ_CORPUS_DIR}/arj_method2.arj" AND
       EXISTS "${XXFC_ARJ_CORPUS_DIR}/arj_method3.arj" AND
       EXISTS "${XXFC_ARJ_CORPUS_DIR}/arj_method4.arj")
        add_test(NAME test_arj_methods
                 COMMAND test_arj_methods
                         "${XXFC_ARJ_CORPUS_DIR}/arj_stored.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method1.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method2.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method3.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method4.arj")
        add_test(NAME test_arj_archive
                 COMMAND test_arj_archive
                         "${XXFC_ARJ_CORPUS_DIR}/arj_stored.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method1.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method2.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method3.arj"
                         "${XXFC_ARJ_CORPUS_DIR}/arj_method4.arj")
    endif()

    add_executable(unpack_all_test_zips tests/unpack_all_test_zips.c)
    target_link_libraries(unpack_all_test_zips PRIVATE xxfclib::xxfclib)
endif()

# -------------------------------------------------------------------- CPack Packaging

if(WIN32)
    set(XXFC_OSNAME "win")
elseif(APPLE)
    set(XXFC_OSNAME "mac")
else()
    set(XXFC_OSNAME "linux")
endif()

if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" XXFC_ARCH)
    if(XXFC_ARCH STREQUAL "win32")
        set(XXFC_ARCH "x86")
    endif()
else()
    if(CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 XXFC_PROCESSOR)
    else()
        set(XXFC_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    string(TOLOWER "${XXFC_PROCESSOR}" XXFC_PROCESSOR)

    if(XXFC_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(XXFC_ARCH "x64")
    elseif(XXFC_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(XXFC_ARCH "arm64")
    elseif(XXFC_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        set(XXFC_ARCH "x86")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(XXFC_ARCH "x64")
    else()
        set(XXFC_ARCH "x86")
    endif()
endif()

set(CPACK_PACKAGE_NAME              "xxfclib")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C library for handling data")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "xxfclib")
if(NOT DEFINED CPACK_PACKAGE_FILE_NAME)
    set(CPACK_PACKAGE_FILE_NAME         "xxfclib_${XXFC_OSNAME}_${XXFC_ARCH}_portable_${PROJECT_VERSION}")
endif()
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

include(CPack)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_apple_diags_tests.cmake")

# Regression checks for the additional archive, disk-image and filesystem readers.
if(XXFC_BUILD_FORMAT_EXPANSION_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(test_scummvm_dxa_native tests/test_scummvm_dxa_native.c)
    target_link_libraries(test_scummvm_dxa_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scummvm_dxa_native COMMAND test_scummvm_dxa_native)
    add_executable(test_freearc_encrypted_native tests/test_freearc_encrypted_native.c)
    target_link_libraries(test_freearc_encrypted_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_freearc_encrypted_native COMMAND test_freearc_encrypted_native
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/freearc_encrypted")
    add_executable(test_die_audio_bundle tests/test_die_audio_bundle.c)
    target_link_libraries(test_die_audio_bundle PRIVATE xxfclib::xxfclib)
    add_test(NAME test_die_audio_bundle COMMAND test_die_audio_bundle)
    add_test(NAME test_die_music_layout
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_music_layout.py")
    add_executable(test_crc32_simd tests/test_crc32_simd.c)
    target_link_libraries(test_crc32_simd PRIVATE xxfclib::xxfclib)
    add_test(NAME test_crc32_simd COMMAND test_crc32_simd)
    add_executable(test_adler32_simd tests/test_adler32_simd.c)
    target_link_libraries(test_adler32_simd PRIVATE xxfclib::xxfclib)
    add_test(NAME test_adler32_simd COMMAND test_adler32_simd)
    add_executable(test_mozilla_mar_native tests/test_mozilla_mar_native.c)
    target_link_libraries(test_mozilla_mar_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_mozilla_mar_native COMMAND test_mozilla_mar_native)
    if(TARGET xxfc_unpack)
        add_test(NAME test_parsec_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_parsec_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_ptero_bigf_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ptero_bigf_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_rvz_synthetic_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rvz_synthetic.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_ckpedp_synthetic_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ckpedp_synthetic.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_music_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_music_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_music_extensions_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_music_extensions_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_nintendo_wbfs_md5_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nintendo_wbfs_md5_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_common_crc_formats_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_common_crc_formats_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_westwood_pak_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_westwood_pak_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_fatx_native
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fatx_native.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_soundfont2_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_soundfont2_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_ivf_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ivf_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_windows_ani_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_windows_ani_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_early_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_early_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_late_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_late_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_midi_paf_strm_alp
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_midi_paf_strm_alp.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_pvf_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_pvf_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_au_variants_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_au_variants_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_rifx_wave_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_rifx_wave_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_die_audio_swa_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_die_audio_swa_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
    endif()
    add_executable(test_zstd_unknown_content_size_native
        tests/test_zstd_unknown_content_size_native.c)
    target_link_libraries(test_zstd_unknown_content_size_native
        PRIVATE xxfclib::xxfclib)
    add_test(NAME test_zstd_unknown_content_size_native
        COMMAND test_zstd_unknown_content_size_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zstd_unknown_content_size")
    if(TARGET xxfc_unpack)
        add_test(NAME test_zstd_unknown_content_size_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zstd_unknown_content_size/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
    endif()
    add_executable(test_gzip_zero_padding_native
        tests/test_gzip_zero_padding_native.c)
    target_link_libraries(test_gzip_zero_padding_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_gzip_zero_padding_native
        COMMAND test_gzip_zero_padding_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gzip_zero_padding")
    if(TARGET xxfc_unpack)
        add_test(NAME test_gzip_zero_padding_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gzip_zero_padding/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
    endif()
    add_executable(test_lzop_filters_native tests/test_lzop_filters_native.c)
    target_link_libraries(test_lzop_filters_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lzop_filters_native
        COMMAND test_lzop_filters_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lzop_filters")
    if(TARGET xxfc_unpack)
        add_test(NAME test_lzop_filters_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lzop_filters/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
    endif()
    foreach(_xxfc_native_test acorn_adfs_native aaruformat_native stuffitx_x86_native gdi_native mds_native cbm_d64_native cbm_d71_native cbm_d81_native cbm_d8x_native cbm_d67_native cbm_d90_native ccd_native cdrdao_toc_native diskcopy42_native acorn_dfs_native fdcopy_cfi_native atari_dos2_native apridisk_native ti99_dsk_native apple_dos32_native apple_dos33_32_native partimage_native qlie_pack_native hfsplus_native xva_native exfat_native apple_pascal_native apple_dos33_native mfs_native hfs_native ufs1_native cpm_native majiro_native nitroplus_npk2_native nitroplus_npa_native catsystem_kif_native malie_lib_native malie_libu_native nexas_pac_native disk_containers_native dmk_native vmdk_compressed_native xpk_native_io bzip2_randomized)
        add_executable(test_${_xxfc_native_test} tests/test_${_xxfc_native_test}.c)
        target_link_libraries(test_${_xxfc_native_test} PRIVATE xxfclib::xxfclib)
        target_compile_definitions(test_${_xxfc_native_test} PRIVATE XXFC_TEST_NATIVE_DETECTION)
    endforeach()
    add_test(NAME test_acorn_adfs_native COMMAND test_acorn_adfs_native)
    add_test(NAME test_acorn_adfs_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-adfs/verify_native_adfs.py"
            "$<TARGET_FILE:test_acorn_adfs_native>")
    add_test(NAME test_aaruformat_native COMMAND test_aaruformat_native)
    add_test(NAME test_stuffitx_x86_native COMMAND test_stuffitx_x86_native)
    add_test(NAME test_gdi_native COMMAND test_gdi_native)
    add_test(NAME test_mds_native COMMAND test_mds_native)
    add_test(NAME test_cbm_d64_native COMMAND test_cbm_d64_native)
    add_test(NAME test_cbm_d71_native COMMAND test_cbm_d71_native)
    add_test(NAME test_cbm_d81_native COMMAND test_cbm_d81_native)
    add_test(NAME test_cbm_d8x_native COMMAND test_cbm_d8x_native)
    add_test(NAME test_cbm_d8x_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cbm-d8x/verify_native_cbm_d8x.py"
            "$<TARGET_FILE:test_cbm_d8x_native>")
    add_test(NAME test_cbm_d67_native COMMAND test_cbm_d67_native)
    add_test(NAME test_cbm_d67_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cbm-d67/verify_native_cbm_d67.py"
            "$<TARGET_FILE:test_cbm_d67_native>")
    add_test(NAME test_cbm_d90_native COMMAND test_cbm_d90_native)
    add_test(NAME test_cbm_d90_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cbm-d90/verify_native_cbm_d90.py"
            "$<TARGET_FILE:test_cbm_d90_native>")
    add_test(NAME test_ccd_native COMMAND test_ccd_native)
    add_test(NAME test_cdrdao_toc_native COMMAND test_cdrdao_toc_native)
    add_test(NAME test_diskcopy42_native COMMAND test_diskcopy42_native)
    add_test(NAME test_fdcopy_cfi_native COMMAND test_fdcopy_cfi_native)
    add_test(NAME test_atari_dos2_native COMMAND test_atari_dos2_native)
    add_test(NAME test_apridisk_native COMMAND test_apridisk_native)
    add_test(NAME test_ti99_dsk_native COMMAND test_ti99_dsk_native)
    add_test(NAME test_apple_dos32_native COMMAND test_apple_dos32_native)
    add_test(NAME test_apple_dos32_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-apple-dos32/verify_native_apple_dos32.py"
            "$<TARGET_FILE:test_apple_dos32_native>")
    add_test(NAME test_apple_dos33_32_native COMMAND test_apple_dos33_32_native)
    add_test(NAME test_apple_dos33_32_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-apple-dos33-32/verify_native_apple_dos33_32.py"
            "$<TARGET_FILE:test_apple_dos33_32_native>")
    add_test(NAME test_ti99_dsk_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ti99-dsk/verify_native_ti99_dsk.py"
            "$<TARGET_FILE:test_ti99_dsk_native>")
    add_test(NAME test_atari_dos2_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-atari-dos2/verify_native_atari_dos2.py"
            "$<TARGET_FILE:test_atari_dos2_native>")
    add_test(NAME test_acorn_dfs_native COMMAND test_acorn_dfs_native)
    add_test(NAME test_acorn_dfs_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-acorn-dfs/verify_native_acorn_dfs.py"
            "$<TARGET_FILE:test_acorn_dfs_native>")
    add_test(NAME test_cbm_d64_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cbm-d64/verify_native_cbm_d64.py"
            "$<TARGET_FILE:test_cbm_d64_native>")
    add_test(NAME test_cbm_d71_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cbm-d71/verify_native_cbm_d71.py"
            "$<TARGET_FILE:test_cbm_d71_native>")
    add_test(NAME test_cbm_d81_independent
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cbm-d81/verify_native_cbm_d81.py"
            "$<TARGET_FILE:test_cbm_d81_native>")
    add_test(NAME test_partimage_native COMMAND test_partimage_native)
    add_test(NAME test_qlie_pack_native COMMAND test_qlie_pack_native)
    add_test(NAME test_hfsplus_native COMMAND test_hfsplus_native)
    add_test(NAME test_xva_native COMMAND test_xva_native)
    add_test(NAME test_exfat_native COMMAND test_exfat_native)
    add_test(NAME test_apple_pascal_native COMMAND test_apple_pascal_native)
    add_test(NAME test_apple_dos33_native COMMAND test_apple_dos33_native)
    add_test(NAME test_mfs_native COMMAND test_mfs_native)
    add_test(NAME test_hfs_native COMMAND test_hfs_native)
    add_test(NAME test_ufs1_native COMMAND test_ufs1_native)
    add_test(NAME test_cpm_native COMMAND test_cpm_native)
    add_test(NAME test_majiro_native COMMAND test_majiro_native)
    add_test(NAME test_nitroplus_npk2_native COMMAND test_nitroplus_npk2_native)
    add_test(NAME test_nitroplus_npa_native COMMAND test_nitroplus_npa_native)
    add_test(NAME test_catsystem_kif_native COMMAND test_catsystem_kif_native)
    add_test(NAME test_malie_lib_native COMMAND test_malie_lib_native)
    add_test(NAME test_malie_libu_native COMMAND test_malie_libu_native)
    add_test(NAME test_nexas_pac_native COMMAND test_nexas_pac_native)
    add_test(NAME test_disk_containers_native COMMAND test_disk_containers_native)
    add_test(NAME test_dmk_native COMMAND test_dmk_native)
    add_test(NAME test_vmdk_compressed_native COMMAND test_vmdk_compressed_native)
    add_executable(test_vmdk_external_native tests/test_vmdk_external_native.c)
    target_link_libraries(test_vmdk_external_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_vmdk_external_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vmdk_external.py"
            native "$<TARGET_FILE:test_vmdk_external_native>")
    add_test(NAME test_xpk_native_io COMMAND test_xpk_native_io
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xpk/test_C1_none.xpkf"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xpk/test_C1.raw")
    add_executable(test_xpk_nuke_native tests/test_xpk_nuke_native.c)
    target_include_directories(test_xpk_nuke_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_nuke_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_nuke_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_nuke_native.py"
            "$<TARGET_FILE:test_xpk_nuke_native>")
    add_executable(test_xpk_lin_native tests/test_xpk_lin_native.c)
    target_include_directories(test_xpk_lin_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_lin_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_lin_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_lin_native.py"
            "$<TARGET_FILE:test_xpk_lin_native>")
    add_test(NAME test_xpk_lz_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_lz_native.py"
            "$<TARGET_FILE:test_xpk_lin_native>")
    add_test(NAME test_xpk_zeno_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_zeno_native.py"
            "$<TARGET_FILE:test_xpk_lin_native>")
    add_test(NAME test_xpk_rake_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_rake_native.py"
            "$<TARGET_FILE:test_xpk_lin_native>")
    add_test(NAME test_xpk_tdcs_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_tdcs_native.py"
            "$<TARGET_FILE:test_xpk_lin_native>")
    add_test(NAME test_xpk_cyber_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_cyber_native.py"
            "$<TARGET_FILE:test_xpk_lin_native>")
    add_executable(test_xpk_yafa_native tests/test_xpk_yafa_native.c)
    target_include_directories(test_xpk_yafa_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_yafa_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_yafa_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_yafa_native.py"
            "$<TARGET_FILE:test_xpk_yafa_native>")
    add_executable(test_xpk_huffman_native tests/test_xpk_huffman_native.c)
    target_include_directories(test_xpk_huffman_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_huffman_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_huffman_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_huffman_native.py"
            "$<TARGET_FILE:test_xpk_huffman_native>")
    add_executable(test_xpk_acca_native tests/test_xpk_acca_native.c)
    target_include_directories(test_xpk_acca_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_acca_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_acca_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_acca_native.py"
            "$<TARGET_FILE:test_xpk_acca_native>")
    add_executable(test_xpk_artm_native tests/test_xpk_artm_native.c)
    target_include_directories(test_xpk_artm_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_artm_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_artm_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_artm_native.py"
            "$<TARGET_FILE:test_xpk_artm_native>")
    add_executable(test_xpk_pwpk_native tests/test_xpk_pwpk_native.c)
    target_include_directories(test_xpk_pwpk_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_pwpk_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_pwpk_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_pwpk_native.py"
            "$<TARGET_FILE:test_xpk_pwpk_native>")
    add_executable(test_xpk_lzcb_native tests/test_xpk_lzcb_native.c)
    target_include_directories(test_xpk_lzcb_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_lzcb_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_lzcb_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_lzcb_native.py"
            "$<TARGET_FILE:test_xpk_lzcb_native>")
    add_executable(test_xpk_crm_native tests/test_xpk_crm_native.c)
    target_include_directories(test_xpk_crm_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_crm_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_crm_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_crm_native.py"
            "$<TARGET_FILE:test_xpk_crm_native>")
    add_executable(test_xpk_cyb2_native tests/test_xpk_cyb2_native.c)
    target_include_directories(test_xpk_cyb2_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_cyb2_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_cyb2_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_cyb2_native.py"
            "$<TARGET_FILE:test_xpk_cyb2_native>")
    add_executable(test_xpk_impl_native tests/test_xpk_impl_native.c)
    target_include_directories(test_xpk_impl_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_impl_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_impl_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_impl_native.py"
            "$<TARGET_FILE:test_xpk_impl_native>")
    add_executable(test_xpk_lhlb_native tests/test_xpk_lhlb_native.c)
    target_include_directories(test_xpk_lhlb_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_lhlb_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_lhlb_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_lhlb_native.py"
            "$<TARGET_FILE:test_xpk_lhlb_native>")
    add_executable(test_xpk_lzx_native tests/test_xpk_lzx_native.c)
    target_include_directories(test_xpk_lzx_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_lzx_native PRIVATE XXFC_STATIC)
    target_link_libraries(test_xpk_lzx_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_xpk_lzx_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_lzx_native.py"
            "$<TARGET_FILE:test_xpk_lzx_native>")
    add_executable(test_xpk_sdhc_native tests/test_xpk_sdhc_native.c)
    target_include_directories(test_xpk_sdhc_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_sdhc_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_sdhc_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_sdhc_native.py"
            "$<TARGET_FILE:test_xpk_sdhc_native>")
    add_executable(test_xpk_shrx_native tests/test_xpk_shrx_native.c)
    target_include_directories(test_xpk_shrx_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_shrx_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_shrx_native
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_shrx_native.py"
            "$<TARGET_FILE:test_xpk_shrx_native>")
    add_executable(test_xpk_sasc_native tests/test_xpk_sasc_native.c)
    target_include_directories(test_xpk_sasc_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_xpk_sasc_native PRIVATE XXFC_STATIC)
    add_test(NAME test_xpk_sasc_native
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_sasc_native.py"
            "$<TARGET_FILE:test_xpk_sasc_native>")
    add_executable(test_7zip_deflate_native tests/test_7zip_deflate_native.c)
    target_link_libraries(test_7zip_deflate_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_7zip_deflate_native
        COMMAND test_7zip_deflate_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_deflate")
    add_executable(test_7zip_simple_filters_native tests/test_7zip_simple_filters_native.c)
    target_link_libraries(test_7zip_simple_filters_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_7zip_simple_filters_native
        COMMAND test_7zip_simple_filters_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_filters")
    add_executable(test_lha_legacy_native tests/test_lha_legacy_native.c)
    target_link_libraries(test_lha_legacy_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lha_legacy_native
        COMMAND test_lha_legacy_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lha_legacy")
    add_executable(test_lha_lhx_native tests/test_lha_lhx_native.c)
    target_link_libraries(test_lha_lhx_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lha_lhx_native
        COMMAND test_lha_lhx_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lha_lhx")
    add_executable(test_lha_lk7_native tests/test_lha_lk7_native.c)
    target_link_libraries(test_lha_lk7_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lha_lk7_native
        COMMAND test_lha_lk7_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lha_lk7")
    add_executable(test_lha_lh3_native tests/test_lha_lh3_native.c)
    target_link_libraries(test_lha_lh3_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lha_lh3_native
        COMMAND test_lha_lh3_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lha_lh3")
    add_executable(test_lha_lh2_native tests/test_lha_lh2_native.c)
    target_link_libraries(test_lha_lh2_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lha_lh2_native
        COMMAND test_lha_lh2_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/lha_lh2")
    add_executable(test_dmg_variants_native tests/test_dmg_variants_native.c)
    target_link_libraries(test_dmg_variants_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_dmg_variants_native PRIVATE XXFC_TEST_NATIVE_DETECTION)
    add_test(NAME test_dmg_variants_native COMMAND test_dmg_variants_native)
    add_executable(test_mpq_codecs_native tests/test_mpq_codecs_native.c)
    target_link_libraries(test_mpq_codecs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_mpq_codecs_native
        COMMAND test_mpq_codecs_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mpq_codecs")
    add_executable(test_libdsk_extra_native tests/test_libdsk_extra_native.c)
    target_link_libraries(test_libdsk_extra_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_libdsk_extra_native COMMAND test_libdsk_extra_native)
    add_executable(test_simh_disk_native tests/test_simh_disk_native.c)
    target_link_libraries(test_simh_disk_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_simh_disk_native COMMAND test_simh_disk_native)
    add_executable(test_snatchit_cp2_native tests/test_snatchit_cp2_native.c)
    target_link_libraries(test_snatchit_cp2_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_snatchit_cp2_native COMMAND test_snatchit_cp2_native)
    add_executable(test_northstar_nsi_native tests/test_northstar_nsi_native.c)
    target_link_libraries(test_northstar_nsi_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_northstar_nsi_native COMMAND test_northstar_nsi_native)
    add_executable(test_thomson_fd_native tests/test_thomson_fd_native.c)
    target_link_libraries(test_thomson_fd_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_thomson_fd_native COMMAND test_thomson_fd_native)
    add_executable(test_cmd_fd_native tests/test_cmd_fd_native.c)
    target_link_libraries(test_cmd_fd_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_cmd_fd_native COMMAND test_cmd_fd_native)
    add_executable(test_pce_pri_native tests/test_pce_pri_native.c)
    target_link_libraries(test_pce_pri_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_pri_native COMMAND test_pce_pri_native)
    add_executable(test_pce_pfi_native tests/test_pce_pfi_native.c)
    target_link_libraries(test_pce_pfi_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_pfi_native COMMAND test_pce_pfi_native)
    add_executable(test_pce_pfdc_native tests/test_pce_pfdc_native.c)
    target_link_libraries(test_pce_pfdc_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_pfdc_native COMMAND test_pce_pfdc_native)
    add_executable(test_pce_pbi_native tests/test_pce_pbi_native.c)
    target_link_libraries(test_pce_pbi_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_pbi_native COMMAND test_pce_pbi_native)
    add_executable(test_pce_pbit_native tests/test_pce_pbit_native.c)
    target_link_libraries(test_pce_pbit_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_pbit_native COMMAND test_pce_pbit_native)
    add_executable(test_pce_tc_native tests/test_pce_tc_native.c)
    target_link_libraries(test_pce_tc_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_tc_native COMMAND test_pce_tc_native)
    add_executable(test_pce_anadisk_native tests/test_pce_anadisk_native.c)
    target_link_libraries(test_pce_anadisk_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_anadisk_native COMMAND test_pce_anadisk_native)
    add_executable(test_pce_xdf_native tests/test_pce_xdf_native.c)
    target_link_libraries(test_pce_xdf_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_pce_xdf_native COMMAND test_pce_xdf_native)
    add_executable(test_hfe_fm_native tests/test_hfe_fm_native.c)
    target_link_libraries(test_hfe_fm_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_hfe_fm_native COMMAND test_hfe_fm_native)
    add_executable(test_hfe_fm_more_native tests/test_hfe_fm_more_native.c)
    target_link_libraries(test_hfe_fm_more_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_hfe_fm_more_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-hfe-fm-more/verify_more_fixtures.py"
            native "$<TARGET_FILE:test_hfe_fm_more_native>")
    add_executable(test_hfe_mfm_stock_native tests/test_hfe_mfm_stock_native.c)
    target_link_libraries(test_hfe_mfm_stock_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_hfe_mfm_stock_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-hfe-mfm-stock/verify_fixtures.py"
            native "$<TARGET_FILE:test_hfe_mfm_stock_native>")
    add_executable(test_hfe_variable_native tests/test_hfe_variable_native.c)
    target_link_libraries(test_hfe_variable_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_hfe_variable_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-hfe-variable/verify_fixtures.py"
            native "$<TARGET_FILE:test_hfe_variable_native>")
    add_executable(test_vhdx_vdi_qemu_native tests/test_vhdx_vdi_qemu_native.c)
    target_link_libraries(test_vhdx_vdi_qemu_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_vhdx_vdi_qemu_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-vhdx-vdi-qemu/verify_fixtures.py"
            native "$<TARGET_FILE:test_vhdx_vdi_qemu_native>")
    add_executable(test_udf_mkudffs_native tests/test_udf_mkudffs_native.c)
    target_link_libraries(test_udf_mkudffs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_udf_mkudffs_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-udf-mkudffs/verify_fixtures.py"
            native "$<TARGET_FILE:test_udf_mkudffs_native>")
    add_executable(test_arcv2_nested_native tests/test_arcv2_nested_native.c)
    target_link_libraries(test_arcv2_nested_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_arcv2_nested_native
        COMMAND test_arcv2_nested_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-arcv2-nested"
            "${CMAKE_CURRENT_BINARY_DIR}/test_arcv2_nested_native_output")
    add_executable(test_qcow2_variants_native tests/test_qcow2_variants_native.c)
    target_link_libraries(test_qcow2_variants_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_qcow2_variants_native
        COMMAND test_qcow2_variants_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-qcow2"
            "${CMAKE_CURRENT_BINARY_DIR}/test_qcow2_variants_native_output")
    if(XXFC_BUILD_SAMPLES)
        add_test(NAME test_arcv2_nested_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arcv2_nested_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-arcv2-nested"
                "${CMAKE_CURRENT_BINARY_DIR}/test_arcv2_nested_cli_output")
    endif()
    add_executable(test_xz_bcj_native tests/test_xz_bcj_native.c)
    target_link_libraries(test_xz_bcj_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_xz_bcj_native
        COMMAND test_xz_bcj_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xz_bcj"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xz_multi_delta")
    if(TARGET xxfc_unpack)
        add_test(NAME test_xz_multi_delta_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xz_multi_delta/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xz_multi_delta"
                "${CMAKE_CURRENT_BINARY_DIR}/test_xz_multi_delta_cli_output")
    endif()
    add_executable(test_avro_codecs_native tests/test_avro_codecs_native.c)
    target_link_libraries(test_avro_codecs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_avro_codecs_native
        COMMAND test_avro_codecs_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-avro-codecs"
            "${CMAKE_CURRENT_BINARY_DIR}/test_avro_codecs_native_output")
    if(TARGET xxfc_unpack)
        add_test(NAME test_avro_codecs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-avro-codecs/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-avro-codecs"
                "${CMAKE_CURRENT_BINARY_DIR}/test_avro_codecs_cli_output")
    endif()
    add_executable(test_romfs_hardlinks_native tests/test_romfs_hardlinks_native.c)
    target_link_libraries(test_romfs_hardlinks_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_romfs_hardlinks_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-romfs-hardlinks/verify_native_romfs_hardlinks.py"
            "$<TARGET_FILE:test_romfs_hardlinks_native>")
    add_executable(test_ext_inline_data_native tests/test_ext_inline_data_native.c)
    target_link_libraries(test_ext_inline_data_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ext_inline_data_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ext-inline-data/verify_native_ext_inline_data.py"
            "$<TARGET_FILE:test_ext_inline_data_native>")
    add_executable(test_ext_64k_dirent_native tests/test_ext_64k_dirent_native.c)
    target_link_libraries(test_ext_64k_dirent_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ext_64k_dirent_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ext-64k-dirent/verify_native_ext_64k_dirent.py"
            "$<TARGET_FILE:test_ext_64k_dirent_native>")
    add_executable(test_erofs_native tests/test_erofs_native.c)
    target_link_libraries(test_erofs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_erofs_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-erofs/verify_native_erofs.py"
            "$<TARGET_FILE:test_erofs_native>")
    add_test(NAME test_erofs_blocksize_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-erofs-blocksize/verify_native_erofs_blocksize.py"
            "$<TARGET_FILE:test_erofs_native>")
    add_executable(test_erofs_lz4_native tests/test_erofs_lz4_native.c)
    target_link_libraries(test_erofs_lz4_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_erofs_lz4_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-erofs-lz4/verify_native_erofs_lz4.py"
            "$<TARGET_FILE:test_erofs_lz4_native>")
    add_test(NAME test_erofs_codecs_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-erofs-codecs/verify_native_erofs_codecs.py"
            "$<TARGET_FILE:test_erofs_lz4_native>")
    add_test(NAME test_erofs_small_compressed_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-erofs-small-compressed/verify_native_erofs_small_compressed.py"
            "$<TARGET_FILE:test_erofs_lz4_native>")
    add_test(NAME test_erofs_small_layouts_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-erofs-small-layouts/verify_native_erofs_small_layouts.py"
            "$<TARGET_FILE:test_erofs_lz4_native>")
    add_executable(test_btrfs_codecs_native tests/test_btrfs_codecs_native.c)
    target_link_libraries(test_btrfs_codecs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_btrfs_codecs_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-btrfs-compression/verify_native_btrfs_compression.py"
            "$<TARGET_FILE:test_btrfs_codecs_native>")
    add_executable(test_ntfs_lznt1_native tests/test_ntfs_lznt1_native.c)
    target_link_libraries(test_ntfs_lznt1_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ntfs_lznt1_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ntfs-lznt1/verify_native_ntfs_lznt1.py"
            "$<TARGET_FILE:test_ntfs_lznt1_native>")
    add_executable(test_iso_zisofs_native tests/test_iso_zisofs_native.c)
    target_link_libraries(test_iso_zisofs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_iso_zisofs_native
        COMMAND test_iso_zisofs_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-iso-zisofs"
            "${CMAKE_CURRENT_BINARY_DIR}/test_iso_zisofs_output")
    add_executable(test_cramfs_xip_reader tests/test_cramfs_xip_reader.c)
    target_link_libraries(test_cramfs_xip_reader PRIVATE xxfclib::xxfclib)
    add_test(NAME test_cramfs_xip_reader
        COMMAND test_cramfs_xip_reader
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cramfs-xip"
            "${CMAKE_CURRENT_BINARY_DIR}/test_cramfs_xip_output")
    add_test(NAME test_cramfs_xip_fixture
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cramfs-xip/verify_fixture.py")
    add_executable(test_uimage_lz4_native tests/test_uimage_lz4_native.c)
    target_link_libraries(test_uimage_lz4_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_uimage_lz4_native
        COMMAND test_uimage_lz4_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-uimage-lz4")
    add_executable(test_uimage_lz4_reader tests/test_uimage_lz4_reader.c)
    target_link_libraries(test_uimage_lz4_reader PRIVATE xxfclib::xxfclib)
    add_test(NAME test_uimage_lz4_reader
        COMMAND test_uimage_lz4_reader
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-uimage-lz4"
            "${CMAKE_CURRENT_BINARY_DIR}/test_uimage_lz4_output")
    if(XXFC_BUILD_SAMPLES)
        add_test(NAME test_iso_zisofs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_iso_zisofs_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-iso-zisofs")
        add_test(NAME test_cramfs_xip_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cramfs_xip_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cramfs-xip")
        add_test(NAME test_uimage_lz4_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_uimage_lz4_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-uimage-lz4")
    endif()
    add_executable(test_uefi_fv_guided_native tests/test_uefi_fv_guided_native.c)
    target_link_libraries(test_uefi_fv_guided_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_uefi_fv_guided_native
        COMMAND test_uefi_fv_guided_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/uefi_fv_codecs")
    add_executable(test_uefi_fv_standard_native tests/test_uefi_fv_standard_native.c)
    target_link_libraries(test_uefi_fv_standard_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_uefi_fv_standard_native
        COMMAND test_uefi_fv_standard_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/uefi_fv_standard")
    add_executable(test_uefi_fv_subtype_native tests/test_uefi_fv_subtype_native.c)
    target_link_libraries(test_uefi_fv_subtype_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_uefi_fv_subtype_native
        COMMAND test_uefi_fv_subtype_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/uefi_fv_subtype")
    add_executable(test_os9_rbf_native tests/test_os9_rbf_native.c)
    target_link_libraries(test_os9_rbf_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_os9_rbf_native COMMAND test_os9_rbf_native)
    add_test(NAME test_os9_rbf_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-os9-rbf/verify_native_os9_rbf.py"
            "$<TARGET_FILE:test_os9_rbf_native>")
    add_executable(test_cpm_presets_native tests/test_cpm_presets_native.c)
    target_link_libraries(test_cpm_presets_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_cpm_presets_native COMMAND test_cpm_presets_native)
    add_test(NAME test_cpm_presets_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cpm-presets/verify_native_cpm_presets.py"
            "$<TARGET_FILE:test_cpm_presets_native>")
    add_test(NAME test_cpm_presets_2_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cpm-presets-2/verify_native_cpm_presets.py"
            "$<TARGET_FILE:test_cpm_presets_native>")
    add_test(NAME test_cpm_presets_3_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cpm-presets-3/verify_native_cpm_diskdefs.py"
            "$<TARGET_FILE:test_cpm_presets_native>"
            "$<TARGET_FILE:xxfc_unpack>")
    add_test(NAME test_cpm_presets_4_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cpm-presets-4/verify_native_cpm_diskdefs.py"
            "$<TARGET_FILE:test_cpm_presets_native>"
            "$<TARGET_FILE:xxfc_unpack>")
    add_executable(test_ldbs_native tests/test_ldbs_native.c)
    target_link_libraries(test_ldbs_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ldbs_native COMMAND test_ldbs_native)
    add_executable(test_cpc_edsk_offset_native tests/test_cpc_edsk_offset_native.c)
    target_link_libraries(test_cpc_edsk_offset_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_cpc_edsk_offset_native COMMAND test_cpc_edsk_offset_native)
    add_executable(test_cpc_duplicate_id_native tests/test_cpc_duplicate_id_native.c)
    target_link_libraries(test_cpc_duplicate_id_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_cpc_duplicate_id_native COMMAND test_cpc_duplicate_id_native)
    add_executable(test_cpc_max_geometry_native tests/test_cpc_max_geometry_native.c)
    target_link_libraries(test_cpc_max_geometry_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_cpc_max_geometry_native COMMAND test_cpc_max_geometry_native)
    if(Python3_EXECUTABLE AND TARGET xxfc_unpack)
        add_test(NAME test_cpc_duplicate_id_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cpc-duplicate-id/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_cpc_max_geometry_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-cpc-max-geometry/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
    endif()
    add_executable(test_woz_variants_native tests/test_woz_variants_native.c)
    target_link_libraries(test_woz_variants_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_woz_variants_native COMMAND test_woz_variants_native)
    add_executable(test_a2r3_native tests/test_a2r3_native.c)
    target_link_libraries(test_a2r3_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_a2r3_native PRIVATE XXFC_TEST_NATIVE_DETECTION)
    add_test(NAME test_a2r3_native COMMAND test_a2r3_native)
    add_executable(test_scp_v24_native tests/test_scp_v24_native.c)
    target_link_libraries(test_scp_v24_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_scp_v24_native COMMAND test_scp_v24_native)
    add_executable(test_ext_meta_bg_native tests/test_ext_meta_bg_native.c)
    target_link_libraries(test_ext_meta_bg_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_ext_meta_bg_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ext-meta-bg/verify_native_ext_meta_bg.py"
            "$<TARGET_FILE:test_ext_meta_bg_native>")
    add_executable(test_ldbst_native tests/test_ldbst_native.c)
    target_link_libraries(test_ldbst_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_ldbst_native PRIVATE XXFC_TEST_NATIVE_DETECTION)
    add_test(NAME test_ldbst_native COMMAND test_ldbst_native)
    add_executable(test_tar_sparse_native tests/test_tar_sparse_native.c)
    target_link_libraries(test_tar_sparse_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_tar_sparse_native COMMAND test_tar_sparse_native)
    add_executable(test_squashfs_lz4_native tests/test_squashfs_lz4_native.c)
    target_link_libraries(test_squashfs_lz4_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_squashfs_lz4_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-squashfs-lz4/verify_native_squashfs_lz4.py"
            "$<TARGET_FILE:test_squashfs_lz4_native>")
    add_executable(test_squashfs_lzo_native tests/test_squashfs_lzo_native.c)
    target_link_libraries(test_squashfs_lzo_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_squashfs_lzo_oracle
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-squashfs-lzo/verify_native_squashfs_lzo.py"
            "$<TARGET_FILE:test_squashfs_lzo_native>")
    add_executable(test_squashfs_layouts_native tests/test_squashfs_layouts_native.c)
    target_link_libraries(test_squashfs_layouts_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_squashfs_layouts_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-squashfs-layouts/verify_native_squashfs_layouts.py"
            "$<TARGET_FILE:test_squashfs_layouts_native>")
    add_executable(test_mpq_more_native tests/test_mpq_more_native.c)
    target_link_libraries(test_mpq_more_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_mpq_more_native
        COMMAND test_mpq_more_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mpq_more")
    add_executable(test_mpq_adpcm_raw_native tests/test_mpq_adpcm_raw_native.c)
    target_link_libraries(test_mpq_adpcm_raw_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_mpq_adpcm_raw_native
        COMMAND test_mpq_adpcm_raw_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mpq_adpcm_raw")
    add_executable(test_mpq_chains_native tests/test_mpq_chains_native.c)
    target_link_libraries(test_mpq_chains_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_mpq_chains_native
        COMMAND test_mpq_chains_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mpq_chains")
    add_executable(test_mpq_versions_native tests/test_mpq_versions_native.c)
    target_link_libraries(test_mpq_versions_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_mpq_versions_native
        COMMAND test_mpq_versions_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mpq_versions")
    add_executable(test_zip_dcl_native tests/test_zip_dcl_native.c)
    target_link_libraries(test_zip_dcl_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_zip_dcl_native
        COMMAND test_zip_dcl_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip_dcl")
    add_executable(test_lz4_mixed_frames_native
        tests/test_lz4_mixed_frames_native.c)
    target_link_libraries(test_lz4_mixed_frames_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_lz4_mixed_frames_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lz4_mixed_frames.py"
            native "$<TARGET_FILE:test_lz4_mixed_frames_native>")
    add_executable(test_7zip_bcj2_aes_native tests/test_7zip_bcj2_aes_native.c)
    target_link_libraries(test_7zip_bcj2_aes_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_7zip_bcj2_aes_native PRIVATE XXFC_STATIC)
    add_test(NAME test_7zip_bcj2_aes_native
        COMMAND test_7zip_bcj2_aes_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj2_aes"
            "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(test_7zip_multi_bcj_native tests/test_7zip_multi_bcj_native.c)
    target_link_libraries(test_7zip_multi_bcj_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_7zip_multi_bcj_native PRIVATE XXFC_STATIC)
    add_test(NAME test_7zip_multi_bcj_native
        COMMAND test_7zip_multi_bcj_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_multi_bcj"
            "${CMAKE_CURRENT_BINARY_DIR}/test_7zip_multi_bcj_native_output")
    if(XXFC_BUILD_SAMPLES)
        add_test(NAME test_7zip_multi_bcj_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_multi_bcj/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_multi_bcj"
                "${CMAKE_CURRENT_BINARY_DIR}/test_7zip_multi_bcj_cli_output")
    endif()
    add_executable(test_7zip_bcj_delta_native tests/test_7zip_bcj_delta_native.c)
    target_link_libraries(test_7zip_bcj_delta_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_7zip_bcj_delta_native PRIVATE XXFC_STATIC)
    add_test(NAME test_7zip_bcj_delta_native
        COMMAND test_7zip_bcj_delta_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj_delta"
            "${CMAKE_CURRENT_BINARY_DIR}/test_7zip_bcj_delta_native_output")
    if(XXFC_BUILD_SAMPLES)
        add_test(NAME test_7zip_bcj_delta_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj_delta/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj_delta"
                "${CMAKE_CURRENT_BINARY_DIR}/test_7zip_bcj_delta_cli_output")
    endif()
    add_executable(test_7zip_bcj2_postfilters_native
        tests/test_7zip_bcj2_postfilters_native.c)
    target_link_libraries(test_7zip_bcj2_postfilters_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_7zip_bcj2_postfilters_native PRIVATE XXFC_STATIC)
    add_test(NAME test_7zip_bcj2_postfilters_native
        COMMAND test_7zip_bcj2_postfilters_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj2_postfilters"
            "${CMAKE_CURRENT_BINARY_DIR}/test_7zip_bcj2_postfilters_native_output")
    if(XXFC_BUILD_SAMPLES)
        add_test(NAME test_7zip_bcj2_postfilters_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj2_postfilters/verify_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/7zip_bcj2_postfilters"
                "${CMAKE_CURRENT_BINARY_DIR}/test_7zip_bcj2_postfilters_cli_output")
    endif()
    add_executable(test_xar_xz_native tests/test_xar_xz_native.c)
    target_link_libraries(test_xar_xz_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_xar_xz_native
        COMMAND test_xar_xz_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/xar_xz")
    add_executable(test_bethesda_ba2_native tests/test_bethesda_ba2_native.c)
    target_link_libraries(test_bethesda_ba2_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_bethesda_ba2_native
        COMMAND test_bethesda_ba2_native
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ba2-variants"
            "${CMAKE_CURRENT_BINARY_DIR}/ba2-test-output")
    add_executable(test_pc98_d88_native tests/test_pc98_d88_native.c)
    target_link_libraries(test_pc98_d88_native PRIVATE xxfclib::xxfclib)
    target_compile_definitions(test_pc98_d88_native PRIVATE XXFC_TEST_NATIVE_DETECTION)
    add_test(NAME test_pc98_d88_native COMMAND test_pc98_d88_native)
    add_executable(test_bytekiller_native tests/test_bytekiller_native.c)
    target_include_directories(test_bytekiller_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(test_bytekiller_native PRIVATE XXFC_STATIC)
    add_test(NAME test_bytekiller_native
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bytekiller_native.py"
            "$<TARGET_FILE:test_bytekiller_native>")
    add_executable(test_bytekiller_public tests/test_bytekiller_public.c)
    target_link_libraries(test_bytekiller_public PRIVATE xxfclib::xxfclib)
    add_test(NAME test_bytekiller_public
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bytekiller_public.py"
            "$<TARGET_FILE:test_bytekiller_public>")
    add_executable(test_freearc_lzp_native tests/test_freearc_lzp_native.c)
    target_link_libraries(test_freearc_lzp_native PRIVATE xxfclib::xxfclib)
    add_test(NAME test_freearc_lzp_native COMMAND test_freearc_lzp_native)
    add_test(NAME test_bzip2_randomized
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bzip2_randomized.py"
            "$<TARGET_FILE:test_bzip2_randomized>")
    add_executable(test_additional_readers
        tests/test_additional_readers.c samples/unpack/xxfc_readers.c)
    target_link_libraries(test_additional_readers PRIVATE xxfclib::xxfclib)
    add_test(NAME test_additional_readers COMMAND test_additional_readers)
    if(XXFC_BUILD_SAMPLES)
        add_test(NAME test_ldbst_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ldbst_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_acorn_adfs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_acorn_adfs_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_aaruformat_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aaruformat_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_freearc_lzp_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_freearc_lzp_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_partimage_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_partimage_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_qlie_pack_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_qlie_pack_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_hfsplus_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hfsplus_cli.py"
                "$<TARGET_FILE:test_hfsplus_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_xva_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xva_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_cpm_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cpm_cli.py"
                "$<TARGET_FILE:test_cpm_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_ufs1_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ufs1_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_nitroplus_npa_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nitroplus_npa_cli.py"
                "$<TARGET_FILE:test_nitroplus_npa_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_sfx_nss_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sfx_nss_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_sfx_ad01_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sfx_ad01_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_nexas_pac_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nexas_pac_cli.py"
                "$<TARGET_FILE:test_nexas_pac_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_hfs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hfs_cli.py"
                "$<TARGET_FILE:test_hfs_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_vmdk_compressed_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vmdk_compressed_cli.py"
                "$<TARGET_FILE:test_vmdk_compressed_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_vmdk_external_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vmdk_external.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_malie_lib_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_malie_lib_cli.py"
                "$<TARGET_FILE:test_malie_lib_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_mfs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_filesystem_cli.py"
                "$<TARGET_FILE:test_mfs_native>"
                "$<TARGET_FILE:xxfc_unpack>" "MFS" "14")
        add_test(NAME test_apple_dos33_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_filesystem_cli.py"
                "$<TARGET_FILE:test_apple_dos33_native>"
                "$<TARGET_FILE:xxfc_unpack>" "Apple DOS3.3" "13")
        add_test(NAME test_catsystem_kif_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_catsystem_kif_cli.py"
                "$<TARGET_FILE:test_catsystem_kif_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_dmk_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dmk_cli.py"
                "$<TARGET_FILE:test_dmk_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_nitroplus_npk2_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nitroplus_npk2_cli.py"
                "$<TARGET_FILE:test_nitroplus_npk2_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_xpk_native_codecs
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_xpk_native_codecs.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_native_expansion_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_expansion_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_disk_layout_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_disk_layout_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_pce_anadisk_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pce_anadisk_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_afio_z_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_afio_z_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_lz4_mixed_frames_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lz4_mixed_frames.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_scp_v24_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scp_v24_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_pce_xdf_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pce_xdf_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_hfe_fm_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hfe_fm_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_hfe_fm_more_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-hfe-fm-more/verify_more_fixtures.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_hfe_mfm_stock_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-hfe-mfm-stock/verify_fixtures.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_hfe_variable_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-hfe-variable/verify_fixtures.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_vhdx_vdi_qemu_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-vhdx-vdi-qemu/verify_fixtures.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_udf_mkudffs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-udf-mkudffs/verify_fixtures.py"
                cli "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_qcow2_variants_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_qcow2_variants_cli.py"
                "$<TARGET_FILE:xxfc_unpack>"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-qcow2"
                "${CMAKE_CURRENT_BINARY_DIR}/test_qcow2_variants_cli_output")
        add_test(NAME test_bethesda_ba2_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bethesda_ba2_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_ampk_latin1_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ampk_latin1_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_btrfs_codecs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-btrfs-compression/verify_native_btrfs_compression.py"
                "$<TARGET_FILE:test_btrfs_codecs_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_ntfs_lznt1_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/native-ntfs-lznt1/verify_native_ntfs_lznt1.py"
                "$<TARGET_FILE:test_ntfs_lznt1_native>"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_erofs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_erofs_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_erofs_lz4_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_erofs_lz4_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_erofs_codecs_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_erofs_codecs_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_erofs_small_compressed_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_erofs_small_compressed_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_erofs_small_layouts_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_erofs_small_layouts_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_squashfs_layouts_cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_squashfs_layouts_cli.py"
                "$<TARGET_FILE:xxfc_unpack>")
        add_test(NAME test_additional_extraction
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_additional_formats.py"
                "$<TARGET_FILE:xxfc_unpack>")
        set(XXFC_ARC_CORPUS_ROOT "" CACHE PATH
            "Optional ARC corpus directory for native extraction regressions")
        if(XXFC_ARC_CORPUS_ROOT)
            add_test(NAME test_arc_extraction_regressions
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arc_regressions.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_sfx_embedded_direct_cli
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sfx_lha_pksfx_cli.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_sfx_nested_direct_cli
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sfx_nested_direct_cli.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_arc_disk_wrappers
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arc_disk_wrappers.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_arc_installus_sfx
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arc_installus_sfx.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_arc_sfx_rsfx
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arc_sfx_rsfx.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_arc_sfx_softpaq4
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arc_sfx_softpaq4.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
            add_test(NAME test_tar_nextstep_dirs_cli
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tar_nextstep_dirs_cli.py"
                    "$<TARGET_FILE:xxfc_unpack>" "${XXFC_ARC_CORPUS_ROOT}")
        endif()
    endif()
endif()

if(XXFC_BUILD_RADARE2_PRIVATE_TESTS)
    enable_testing()
    add_subdirectory(private_tests)
endif()

# SIMD flags apply only to the runtime-dispatched sorting translation units.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sort_simd.cmake")
xxfclib_configure_sort_simd_sources("${CMAKE_CURRENT_SOURCE_DIR}")

# SIMD flags apply only to the runtime-dispatched .NET #US backends.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_dotnet_simd.cmake")
xxfclib_configure_dotnet_simd_sources("${CMAKE_CURRENT_SOURCE_DIR}")

# ARM feature flags apply only to runtime-dispatched CRC, Dot Product and
# AES/SHA crypto translation units. Baseline NEON sources need no extra flag.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_arm_accel.cmake")
xxfclib_configure_arm_accel_sources("${CMAKE_CURRENT_SOURCE_DIR}")

# These tests call private filename decoders from a source translation unit;
# link remaining reader dependencies from the static library.
if(XXFC_BUILD_CODEPAGE_TESTS AND TARGET xxfclib AND NOT BUILD_SHARED_LIBS)
    add_custom_target(xxfc_format_codepage_tests)
    foreach(_cp_format bgi zip cab macbinary hfs vmarc emt arcfs smart_install_maker t64 adf)
        add_executable(xxfc_format_codepage_${_cp_format} tests/test_format_codepages.c)
        target_compile_definitions(xxfc_format_codepage_${_cp_format} PRIVATE XX_TEST_${_cp_format})
        target_include_directories(xxfc_format_codepage_${_cp_format} PRIVATE $<TARGET_PROPERTY:xxfclib,INCLUDE_DIRECTORIES>)
        target_link_libraries(xxfc_format_codepage_${_cp_format} PRIVATE xxfclib::xxfclib)
        add_dependencies(xxfc_format_codepage_tests xxfc_format_codepage_${_cp_format})
        add_test(NAME xxfc_format_codepage_${_cp_format} COMMAND xxfc_format_codepage_${_cp_format})
    endforeach()
    unset(_cp_format)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_eid_crypto_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_eid_reader_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_natata_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_fox_sqz_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_stor_resource_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lyme_sfx_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_installshield_obl_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pftw_pkg_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_autoplay_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sod_installer_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_starforce_ovl_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_stuffit5_sfx_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_self_unstuffer_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_secura_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_patchwise_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_installjammer_tests.cmake")

# Public regex API checks can also be built independently of the interpreter.
option(XXFC_BUILD_REGEX_TESTS "Build focused public regex API tests" OFF)
if(XXFC_BUILD_TESTS OR XXFC_BUILD_REGEX_TESTS)
    enable_testing()
    add_subdirectory(tests/regex)
endif()

# Focused JS regression and performance harness, excluded from normal builds.
option(XXFC_BUILD_JS_TESTS "Build focused JavaScript correctness and performance tests" OFF)
if(XXFC_BUILD_JS_TESTS)
    enable_testing()
    add_subdirectory(tests/js_performance)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_tarma_legacy_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sfxcab_bss_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_kirikiri_xp3_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_ar_legacy_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_aixbff_volume_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_bigf_nameless_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_binhex_name_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_cfl_lzo_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lz4_overlay_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_amigalzx_crlf_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_cramfs_page_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_gob_terminal_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_gst_prefix_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_ibmzpak_short_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_insa_empty_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_isskin_overlay_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_ivt_empty_key_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_ivt_index_only_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_asymetrix_empty_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_instalit_footer_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_jgpak_banner_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_kboom_short_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lofi_memory_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_macbinary_paint_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_macbinary_secondary_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_makeself_nvidia_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_mi10_padding_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_mpq_hash_count_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_mva_volume_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pdb_empty_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pyz_namespace_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_refpack_alignment_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_rnca_order_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_rtpatch_metadata_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sfx7zip_resource_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lzma_context_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sfxarj_resource_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_arq_advisory_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_createinstall_resources_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_jugglor_legacy_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_gentee_empty_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_inno126_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_ej_empty_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_installus_pe_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lha_ne_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_show_footer_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_rar_pe_resource_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_rar_copy50_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_zip_pe_resource_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_silmarils_terminal_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sun_mo_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_sqx_ppmd_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_zoom_terminal_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_arcv2_volume_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_asymetrix_volume_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_beospkg_name_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lha_exec_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_fpak_volume_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_fury_dat_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lha_colon_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_wrq_delimiter_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_ibmpack_name_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_is_positional_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_is3_volume_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_lingvoarc_volume_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pmarc_empty_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pp20_slot_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_quantum_name_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_red_volume_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_resourcefork_name_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_squashfs_noeos_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_arcv4_complete_tests.cmake")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pklite_tests.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pklite_tests.cmake")
elseif(XXFC_BUILD_PKLITE_TESTS)
    message(FATAL_ERROR "PKLITE tests require cmake/xxfclib_pklite_tests.cmake")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pe_rawfile_tests.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_pe_rawfile_tests.cmake")
elseif(XXFC_BUILD_PE_RAWFILE_TESTS)
    message(FATAL_ERROR "PE raw-file tests require cmake/xxfclib_pe_rawfile_tests.cmake")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_larc_pfx_tail_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_larc_pfx_erased_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_larc_pfx21_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_larc_pfx334_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_larc_pfx420_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_spis_lh5_key_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_squeeze1_crlf_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_swag_advisory_footer_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_zoom_labels_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_dfsee_imz_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_dh_tests.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_diskdupe_im_tests.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxfclib_djcomp_tests.cmake")
