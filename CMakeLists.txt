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

# C Standard Configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

# Library build options
option(BUILD_SHARED_LIBS "Build xxfclib as a shared/dynamic library instead of static" OFF)
option(XXFC_BUILD_BOTH "Build both static and shared library targets" OFF)
option(XXFC_BUILD_TESTS "Build test suite to verify linkage and functionality" OFF)
option(XXFC_BUILD_DISASM_TESTS "Build the focused DIE disassembly regression test" OFF)
option(XXFC_BUILD_SCAN_TESTS "Build the focused scan and subdevice regression tests" OFF)
option(XXFC_BUILD_ZIP_SUPPORT_TESTS "Build the focused ZIP method regression tests" OFF)
option(XXFC_BUILD_PDF_TESTS "Build the focused native PDF reader and extractor regression tests" OFF)
option(XXFC_BUILD_PYC_TESTS "Build the focused native Python bytecode reader and extractor regression tests" OFF)
option(XXFC_BUILD_FORMAT_EXPANSION_TESTS "Build additional reader registration and extraction tests" OFF)
option(XXFC_BUILD_SAMPLES "Build xxfclib sample programs" OFF)

# Opt-in freestanding STATIC archive (MSVC x64 only).
#
# The default static archive is hosted: its objects carry /DEFAULTLIB:MSVCRT
# and reference __chkstk and __security_check_cookie, so a consumer that links
# with /NODEFAULTLIB gets unresolved externals. With this ON the static objects
# are compiled /GS- /GL- /Zl and the archive carries the clean-room __chkstk
# from src/platforms/xx_chkstk_x64.asm.
#
# XXFC_NO_CRT is deliberately NOT defined here: that macro makes
# src/platforms/xx_entry_windows.c define memset/memcpy/memmove/_fltused, and
# a CRT-free EXE consumer (cdie) already defines those in its own entry object.
# Defining both is LNK2005. The consumer owns the compiler-support routines;
# this option only removes the archive's CRT dependency.
option(XXFC_STATIC_NO_CRT "Build the static archive so it can be linked with /NODEFAULTLIB (MSVC x64)" OFF)
option(XXFC_BUILD_DIE_ENGINE "Also build die_engine, a static library holding only the DIE signature engine" ON)

set(XXFC_MSVC_X64 FALSE)
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    set(XXFC_MSVC_X64 TRUE)
endif()

# CMake before 4.0 has no per-target MSVC runtime-check abstraction. Its
# Debug defaults inject /RTC1 globally, which is itself a CRT dependency.
# In a build containing the freestanding DLL, remove those legacy defaults;
# newer CMake versions are handled per target below.
if(XXFC_MSVC_X64 AND (BUILD_SHARED_LIBS OR XXFC_BUILD_BOTH OR XXFC_STATIC_NO_CRT))
    if(NOT POLICY CMP0184)
        foreach(_xxfc_flags CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
            string(REGEX REPLACE "[/-](RTC[1csu]|GZ)" ""
                ${_xxfc_flags} "${${_xxfc_flags}}")
        endforeach()
    endif()
endif()

# The CRT-free Windows DLL supplies the compiler's stack-probe helper in
# assembly.  Keep MASM out of hosted/static configurations, where the normal
# compiler runtime owns that symbol.
if(XXFC_MSVC_X64 AND
   (BUILD_SHARED_LIBS OR XXFC_BUILD_BOTH OR XXFC_STATIC_NO_CRT))
    enable_language(ASM_MASM)
endif()

# Source files
set(XXFCLIB_PUBLIC_HEADERS
    include/xxfclib/formats/pyc/xx_pyc.h
    include/xxfclib/formats/pdf/xxpdf.h
    include/xxfclib/formats/mozilla_mar/xx_mozilla_mar.h
    include/xxfclib/formats/westwood_pak/xx_westwood_pak.h
    include/xxfclib/formats/fatx/xx_fatx.h
    include/xxfclib/formats/soundfont2/xx_soundfont2.h
    include/xxfclib/formats/ivf/xx_ivf.h
    include/xxfclib/formats/windows_ani/xx_windows_ani.h
    include/xxfclib/formats/interplay_acm/xx_interplay_acm.h
    include/xxfclib/formats/cri_ahx/xx_cri_ahx.h
    include/xxfclib/formats/adobe_director_cxt/xx_adobe_director_cxt.h
    include/xxfclib/formats/olympus_dss/xx_olympus_dss.h
    include/xxfclib/formats/ea_exa/xx_ea_exa.h
    include/xxfclib/formats/ckp/xx_ckp.h
    include/xxfclib/formats/edp/xx_edp.h
    include/xxfclib/formats/parsec_rib/xx_parsec_rib.h
    include/xxfclib/formats/parsec_archive/xx_parsec_archive.h
    include/xxfclib/formats/parsec_pmm/xx_parsec_pmm.h
    include/xxfclib/formats/ptero_bigf/xx_ptero_bigf.h
    include/xxfclib/formats/rvz/xx_rvz.h
    include/xxfclib/formats/audio_nitro_strm/xx_audio_nitro_strm.h
    include/xxfclib/formats/audio_wwise_wem/xx_audio_wwise_wem.h
    include/xxfclib/formats/audio_scumm_sou/xx_audio_scumm_sou.h
    include/xxfclib/formats/audio_riff_ima/xx_audio_riff_ima.h
    include/xxfclib/formats/hmi_midi/xx_hmi_midi.h
    include/xxfclib/formats/ensoniq_paf/xx_ensoniq_paf.h
    include/xxfclib/formats/abylight_strm/xx_abylight_strm.h
    include/xxfclib/formats/lego_alp/xx_lego_alp.h
    include/xxfclib/formats/audio_pvf/xx_audio_pvf.h
    include/xxfclib/formats/audio_rifx_wave/xx_audio_rifx_wave.h
    include/xxfclib/formats/audio_shockwave_swa/xx_audio_shockwave_swa.h
    include/xxfclib/formats/die_music/xx_die_music.h
    include/xxfclib/formats/die_music/xx_die_music_enums.inc
    include/xxfclib/formats/acorn_adfs/xx_acorn_adfs.h
    include/xxfclib/formats/aaruformat/xx_aaruformat.h
    include/xxfclib/formats/gdi/xx_gdi.h
    include/xxfclib/formats/mds/xx_mds.h
    include/xxfclib/formats/cbm_d64/xx_cbm_d64.h
    include/xxfclib/formats/cbm_d71/xx_cbm_d71.h
    include/xxfclib/formats/cbm_d81/xx_cbm_d81.h
    include/xxfclib/formats/cbm_d8x/xx_cbm_d8x.h
    include/xxfclib/formats/cbm_d67/xx_cbm_d67.h
    include/xxfclib/formats/cbm_d90/xx_cbm_d90.h
    include/xxfclib/formats/libdsk_extra/xx_libdsk_extra.h
    include/xxfclib/formats/simh_disk/xx_simh_disk.h
    include/xxfclib/formats/snatchit_cp2/xx_snatchit_cp2.h
    include/xxfclib/formats/northstar_nsi/xx_northstar_nsi.h
    include/xxfclib/formats/thomson_fd/xx_thomson_fd.h
    include/xxfclib/formats/cmd_fd/xx_cmd_fd.h
    include/xxfclib/formats/pce_pri/xx_pce_pri.h
    include/xxfclib/formats/pce_pfi/xx_pce_pfi.h
    include/xxfclib/formats/pce_pfdc/xx_pce_pfdc.h
    include/xxfclib/formats/pce_pbi/xx_pce_pbi.h
    include/xxfclib/formats/pce_pbit/xx_pce_pbit.h
    include/xxfclib/formats/pce_tc/xx_pce_tc.h
    include/xxfclib/formats/pce_anadisk/xx_pce_anadisk.h
    include/xxfclib/formats/pce_xdf/xx_pce_xdf.h
    include/xxfclib/formats/os9_rbf/xx_os9_rbf.h
    include/xxfclib/formats/ldbs/xx_ldbs.h
    include/xxfclib/formats/ldbst/xx_ldbst.h
    include/xxfclib/formats/ccd/xx_ccd.h
    include/xxfclib/formats/cdrdao_toc/xx_cdrdao_toc.h
    include/xxfclib/formats/diskcopy42/xx_diskcopy42.h
    include/xxfclib/formats/acorn_dfs/xx_acorn_dfs.h
    include/xxfclib/formats/fdcopy_cfi/xx_fdcopy_cfi.h
    include/xxfclib/formats/atari_dos2/xx_atari_dos2.h
    include/xxfclib/formats/apridisk/xx_apridisk.h
    include/xxfclib/formats/ti99_dsk/xx_ti99_dsk.h
    include/xxfclib/formats/apple_dos32/xx_apple_dos32.h
    include/xxfclib/formats/apple_dos33_32/xx_apple_dos33_32.h
    include/xxfclib/formats/bytekiller/xx_bytekiller.h
    include/xxfclib/formats/partimage/xx_partimage.h
    include/xxfclib/formats/qlie_pack/xx_qlie_pack.h
    include/xxfclib/formats/hfsplus/xx_hfsplus.h
    include/xxfclib/formats/xva/xx_xva.h
    include/xxfclib/formats/hfs/xx_hfs.h
    include/xxfclib/formats/nexas_pac/xx_nexas_pac.h
    include/xxfclib/formats/nitroplus_npa/xx_nitroplus_npa.h
    include/xxfclib/formats/cpm/xx_cpm.h
    include/xxfclib/formats/cpm/xx_cpm_presets.h
    include/xxfclib/formats/ufs1/xx_ufs1.h
    include/xxfclib/formats/apple_dos33/xx_apple_dos33.h
    include/xxfclib/formats/malie_lib/xx_malie_lib.h
    include/xxfclib/formats/dmk/xx_dmk.h
    include/xxfclib/formats/mfs/xx_mfs.h
    include/xxfclib/formats/catsystem_kif/xx_catsystem_kif.h
    include/xxfclib/formats/apple_pascal/xx_apple_pascal.h
    include/xxfclib/formats/nitroplus_npk2/xx_nitroplus_npk2.h
    include/xxfclib/formats/thomson_sap/xx_thomson_sap.h
    include/xxfclib/formats/anex86_hdi/xx_anex86_hdi.h
    include/xxfclib/formats/exfat/xx_exfat.h
    include/xxfclib/formats/majiro/xx_majiro.h
    include/xxfclib/formats/wux/xx_wux.h
    include/xxfclib/formats/sdi/xx_sdi.h
    include/xxfclib/formats/nhd/xx_nhd.h
    include/xxfclib/formats/virtual98/xx_virtual98.h
    include/xxfclib/formats/lpak/xx_lpak.h
    include/xxfclib/formats/freeze/xx_freeze.h
    include/xxfclib/formats/bzip1/xx_bzip1.h
    include/xxfclib/formats/acorn_atom_disk/xx_acorn_atom_disk.h
    include/xxfclib/formats/apollo_afd/xx_apollo_afd.h
    include/xxfclib/formats/bga/xx_bga.h
    include/xxfclib/formats/bgi/xx_bgi.h
    include/xxfclib/formats/bgi2/xx_bgi2.h
    include/xxfclib/formats/binscii/xx_binscii.h
    include/xxfclib/formats/blindwrite_5_6_image/xx_blindwrite_5_6_image.h
    include/xxfclib/formats/btrfs_stream/xx_btrfs_stream.h
    include/xxfclib/formats/camputers_lynx_ldf/xx_camputers_lynx_ldf.h
    include/xxfclib/formats/cpk/xx_cpk.h
    include/xxfclib/formats/crt/xx_crt.h
    include/xxfclib/formats/d_link_alpha_encimg_v2/xx_d_link_alpha_encimg_v2.h
    include/xxfclib/formats/d_link_fpkg_cpkg/xx_d_link_fpkg_cpkg.h
    include/xxfclib/formats/daemon_tools_mdx/xx_daemon_tools_mdx.h
    include/xxfclib/formats/dart/xx_dart.h
    include/xxfclib/formats/ddd/xx_ddd.h
    include/xxfclib/formats/diet_compression/xx_diet_compression.h
    include/xxfclib/formats/dxa/xx_dxa.h
    include/xxfclib/formats/ea_fsh/xx_ea_fsh.h
    include/xxfclib/formats/ewf2_ex01/xx_ewf2_ex01.h
    include/xxfclib/formats/ewf2_lx01/xx_ewf2_lx01.h
    include/xxfclib/formats/ewf_l01/xx_ewf_l01.h
    include/xxfclib/formats/fmod_sample_bank/xx_fmod_sample_bank.h
    include/xxfclib/formats/gbi/xx_gbi.h
    include/xxfclib/formats/goldsrc_bsp/xx_goldsrc_bsp.h
    include/xxfclib/formats/hsf/xx_hsf.h
    include/xxfclib/formats/htc_nbh_rom_image/xx_htc_nbh_rom_image.h
    include/xxfclib/formats/hxc_hfe_extended/xx_hxc_hfe_extended.h
    include/xxfclib/formats/hxc_hfe_hddd_a2_variant/xx_hxc_hfe_hddd_a2_variant.h
    include/xxfclib/formats/hxc_hfe_v3/xx_hxc_hfe_v3.h
    include/xxfclib/formats/hxs/xx_hxs.h
    include/xxfclib/formats/jffs2_old/xx_jffs2_old.h
    include/xxfclib/formats/jvc/xx_jvc.h
    include/xxfclib/formats/kgb_archiver/xx_kgb_archiver.h
    include/xxfclib/formats/kryoflux_stream/xx_kryoflux_stream.h
    include/xxfclib/formats/livemaker/xx_livemaker.h
    include/xxfclib/formats/lzma86/xx_lzma86.h
    include/xxfclib/formats/maxis_far_archive/xx_maxis_far_archive.h
    include/xxfclib/formats/mgt/xx_mgt.h
    include/xxfclib/formats/minix/xx_minix.h
    include/xxfclib/formats/moof/xx_moof.h
    include/xxfclib/formats/ms_dos_backup2/xx_ms_dos_backup2.h
    include/xxfclib/formats/mub/xx_mub.h
    include/xxfclib/formats/noa/xx_noa.h
    include/xxfclib/formats/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.h
    include/xxfclib/formats/partclone_image/xx_partclone_image.h
    include/xxfclib/formats/ppmd/xx_ppmd.h
    include/xxfclib/formats/quoted_printable_encoded_fil/xx_quoted_printable_encoded_fil.h
    include/xxfclib/formats/risc_os_sprite/xx_risc_os_sprite.h
    include/xxfclib/formats/rpa/xx_rpa.h
    include/xxfclib/formats/rpg_maker_rgssad/xx_rpg_maker_rgssad.h
    include/xxfclib/formats/sfark_compressed_soundfont/xx_sfark_compressed_soundfont.h
    include/xxfclib/formats/sis/xx_sis.h
    include/xxfclib/formats/spectrum_udi/xx_spectrum_udi.h
    include/xxfclib/formats/squashfs_sqlz/xx_squashfs_sqlz.h
    include/xxfclib/formats/stos_memory_bank/xx_stos_memory_bank.h
    include/xxfclib/formats/stuffitx/xx_stuffitx.h
    include/xxfclib/formats/sufs/xx_sufs.h
    include/xxfclib/formats/sunvtoc/xx_sunvtoc.h
    include/xxfclib/formats/telltale_ttarch/xx_telltale_ttarch.h
    include/xxfclib/formats/ufs2/xx_ufs2.h
    include/xxfclib/formats/uif/xx_uif.h
    include/xxfclib/formats/valve_gcf_cache/xx_valve_gcf_cache.h
    include/xxfclib/formats/valve_xzp/xx_valve_xzp.h
    include/xxfclib/formats/vmdk_cowd_sparse/xx_vmdk_cowd_sparse.h
    include/xxfclib/formats/vmdk_sesparse/xx_vmdk_sesparse.h
    include/xxfclib/formats/xiaomi_hdr1/xx_xiaomi_hdr1.h
    include/xxfclib/formats/xiaomi_hdr2/xx_xiaomi_hdr2.h
    include/xxfclib/formats/xp3/xx_xp3.h
    include/xxfclib/formats/xpk_compressed_file/xx_xpk_compressed_file.h
    include/xxfclib/formats/yenc_encoded_file/xx_yenc_encoded_file.h
    include/xxfclib/formats/ypf/xx_ypf.h

    include/xxfclib/xxfclib.h
    include/xxfclib/xxfc_defs.h
    include/xxfclib/data/xx_pd.h
    include/xxfclib/io/xx_io.h
    include/xxfclib/scan/xx_scan.h
    include/xxfclib/die_engine/die_engine.h
    include/xxfclib/memory/xx_memory.h
    include/xxfclib/formats/xx_memory_map.h
    include/xxfclib/strings/xx_string.h
    include/xxfclib/data/xx_data.h
    include/xxfclib/formats/xx_data_signature.h
    include/xxfclib/list/xx_list.h
    include/xxfclib/var/xx_var.h
    include/xxfclib/global/xx_global.h
    include/xxfclib/global/xx_settings_global.h
    include/xxfclib/terminal/xx_terminal.h
    include/xxfclib/settings/xx_settings.h
    include/xxfclib/formats/xx_executable_inspect.h
    include/xxfclib/formats/xx_format_streams.h
    include/xxfclib/formats/xx_format.h
    include/xxfclib/formats/xx_format_extractor.h
    include/xxfclib/algo/crc/xx_crc.h
    include/xxfclib/algo/hash/xx_hash.h
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
    include/xxfclib/formats/zip/xx_zip.h
    include/xxfclib/formats/jar/xx_jar.h
    include/xxfclib/formats/apk/xx_apk.h
    include/xxfclib/formats/ipa/xx_ipa.h
    include/xxfclib/formats/7zip/xx_7zip.h
    include/xxfclib/formats/rar/xx_rar.h
    include/xxfclib/formats/ar/xx_ar.h
    include/xxfclib/formats/bz2/xx_bz2.h
    include/xxfclib/formats/gz/xx_gz.h
    include/xxfclib/formats/xz/xx_xz.h
    include/xxfclib/formats/tar/xx_tar.h
    include/xxfclib/formats/tar_gz/xx_tar_gz.h
    include/xxfclib/formats/npm/xx_npm.h
    include/xxfclib/formats/tar_bz2/xx_tar_bz2.h
    include/xxfclib/formats/tar_xz/xx_tar_xz.h
    include/xxfclib/formats/tar_lz4/xx_tar_lz4.h
    include/xxfclib/formats/lz4/xx_lz4.h
    include/xxfclib/formats/lz5/xx_lz5.h
    include/xxfclib/formats/lizard/xx_lizard.h
    include/xxfclib/formats/brotli/xx_brotli.h
    include/xxfclib/formats/unixpack/xx_unixpack.h
    include/xxfclib/formats/zlib/xx_zlib.h
    include/xxfclib/formats/unixcompress/xx_unixcompress.h
    include/xxfclib/formats/gitobject/xx_gitobject.h
    include/xxfclib/formats/mscompress/xx_mscompress.h
    include/xxfclib/formats/ash0/xx_ash0.h
    include/xxfclib/formats/wiilz77/xx_wiilz77.h
    include/xxfclib/formats/lzv1/xx_lzv1.h
    include/xxfclib/formats/oraclesqueeze/xx_oraclesqueeze.h
    include/xxfclib/formats/softronics/xx_softronics.h
    include/xxfclib/formats/logitechcompress/xx_logitechcompress.h
    include/xxfclib/formats/dmapacked/xx_dmapacked.h
    include/xxfclib/formats/gashuff/xx_gashuff.h
    include/xxfclib/formats/huf/xx_huf.h
    include/xxfclib/formats/lzdiet/xx_lzdiet.h
    include/xxfclib/formats/lzpis2/xx_lzpis2.h
    include/xxfclib/formats/tar_zstd/xx_tar_zstd.h
    include/xxfclib/formats/zstd/xx_zstd.h
    include/xxfclib/formats/cpio/xx_cpio.h
    include/xxfclib/formats/mtree/xx_mtree.h
    include/xxfclib/formats/tar_nextstep/xx_tar_nextstep.h
    include/xxfclib/formats/tar_compress/xx_tar_compress.h
    include/xxfclib/formats/tar_lzip/xx_tar_lzip.h
    include/xxfclib/formats/lzip/xx_lzip.h
    include/xxfclib/formats/lzma/xx_lzma.h
    include/xxfclib/formats/tar_lzma/xx_tar_lzma.h
    include/xxfclib/formats/tar_lzop/xx_tar_lzop.h
    include/xxfclib/formats/tarx1/xx_tarx1.h
    include/xxfclib/formats/tarx2/xx_tarx2.h
    include/xxfclib/formats/iso9660/xx_iso9660.h
    include/xxfclib/formats/trx/xx_trx.h
    include/xxfclib/formats/seama/xx_seama.h
    include/xxfclib/formats/chk/xx_chk.h
    include/xxfclib/formats/packimg/xx_packimg.h
    include/xxfclib/formats/dlob/xx_dlob.h
    include/xxfclib/formats/wince/xx_wince.h
    include/xxfclib/formats/binhdr/xx_binhdr.h
    include/xxfclib/formats/rtk/xx_rtk.h
    include/xxfclib/formats/csman/xx_csman.h
    include/xxfclib/formats/vxworks/xx_vxworks.h
    include/xxfclib/formats/uefi_fv/xx_uefi_fv.h
    include/xxfclib/formats/uefi_capsule/xx_uefi_capsule.h
    include/xxfclib/formats/qcow/xx_qcow.h
    include/xxfclib/formats/qnx6/xx_qnx6.h
    include/xxfclib/formats/luks/xx_luks.h
    include/xxfclib/formats/apfs/xx_apfs.h
    include/xxfclib/formats/btrfs/xx_btrfs.h
    include/xxfclib/formats/logfs/xx_logfs.h
    include/xxfclib/formats/dmg/xx_dmg.h
    include/xxfclib/formats/dms/xx_dms.h
    include/xxfclib/formats/lzop/xx_lzop.h
    include/xxfclib/formats/srec/xx_srec.h
    include/xxfclib/formats/dclraw/xx_dclraw.h
    include/xxfclib/formats/xpak/xx_xpak.h
    include/xxfclib/formats/pdb/xx_pdb.h
    include/xxfclib/formats/infogramesft/xx_infogramesft.h
    include/xxfclib/formats/uboot/xx_uboot.h
    include/xxfclib/formats/twrx/xx_twrx.h
    include/xxfclib/formats/tplink/xx_tplink.h
    include/xxfclib/formats/silmarilsft/xx_silmarilsft.h
    include/xxfclib/formats/shrs/xx_shrs.h
    include/xxfclib/formats/mh01/xx_mh01.h
    include/xxfclib/formats/matter_ota/xx_matter_ota.h
    include/xxfclib/formats/lz4demo/xx_lz4demo.h
    include/xxfclib/formats/lingvoarc/xx_lingvoarc.h
    include/xxfclib/formats/jboot/xx_jboot.h
    include/xxfclib/formats/encrpted_img/xx_encrpted_img.h
    include/xxfclib/formats/encfw/xx_encfw.h
    include/xxfclib/formats/ecos/xx_ecos.h
    include/xxfclib/formats/dlke/xx_dlke.h
    include/xxfclib/formats/dlink_tlv/xx_dlink_tlv.h
    include/xxfclib/formats/dkbs/xx_dkbs.h
    include/xxfclib/formats/autel/xx_autel.h
    include/xxfclib/formats/arcadyan/xx_arcadyan.h
    include/xxfclib/formats/androidboot/xx_androidboot.h
    include/xxfclib/formats/rawstac/xx_rawstac.h
    include/xxfclib/formats/rtpatch/xx_rtpatch.h
    include/xxfclib/formats/vmssaveset/xx_vmssaveset.h
    include/xxfclib/formats/boo/xx_boo.h
    include/xxfclib/formats/wolfft/xx_wolfft.h
    include/xxfclib/formats/settlersft/xx_settlersft.h
    include/xxfclib/formats/teacy/xx_teacy.h
    include/xxfclib/formats/rsc/xx_rsc.h
    include/xxfclib/formats/res/xx_res.h
    include/xxfclib/formats/bsn/xx_bsn.h
    include/xxfclib/formats/wintermutedcp/xx_wintermutedcp.h
    include/xxfclib/formats/volitionvpft/xx_volitionvpft.h
    include/xxfclib/formats/agis/xx_agis.h
    include/xxfclib/formats/hog/xx_hog.h
    include/xxfclib/formats/rnca/xx_rnca.h
    include/xxfclib/formats/paperport/xx_paperport.h
    include/xxfclib/formats/starkit/xx_starkit.h
    include/xxfclib/formats/lspack10/xx_lspack10.h
    include/xxfclib/formats/ixa/xx_ixa.h
    include/xxfclib/formats/mlb_ft/xx_mlb_ft.h
    include/xxfclib/formats/fss/xx_fss.h
    include/xxfclib/formats/epf/xx_epf.h
    include/xxfclib/formats/dfc/xx_dfc.h
    include/xxfclib/formats/sfx_rsfx/xx_sfx_rsfx.h
    include/xxfclib/formats/sfx_softpaq4/xx_sfx_softpaq4.h
    include/xxfclib/formats/ka/xx_ka.h
    include/xxfclib/formats/nextstep_diskimage/xx_nextstep_diskimage.h
    include/xxfclib/formats/dn/xx_dn.h
    include/xxfclib/formats/insa/xx_insa.h
    include/xxfclib/formats/sfx_vms_dcx/xx_sfx_vms_dcx.h
    include/xxfclib/formats/oberon/xx_oberon.h
    include/xxfclib/formats/ppd/xx_ppd.h
    include/xxfclib/formats/sfx_ad01/xx_sfx_ad01.h
    include/xxfclib/formats/sfx_nss/xx_sfx_nss.h
    include/xxfclib/formats/solitaire_deluxe/xx_solitaire_deluxe.h
    include/xxfclib/formats/thebat_msb/xx_thebat_msb.h
    include/xxfclib/formats/sfx_localzip/xx_sfx_localzip.h
    include/xxfclib/formats/lif/xx_lif.h
    include/xxfclib/formats/qip2/xx_qip2.h
    include/xxfclib/formats/emt/xx_emt.h
    include/xxfclib/formats/bagf/xx_bagf.h
    include/xxfclib/formats/wrzl/xx_wrzl.h
    include/xxfclib/formats/spk/xx_spk.h
    include/xxfclib/formats/stac/xx_stac.h
    include/xxfclib/formats/dbz/xx_dbz.h
    include/xxfclib/formats/squeeze2/xx_squeeze2.h
    include/xxfclib/formats/sq/xx_sq.h
    include/xxfclib/formats/phar/xx_phar.h
    include/xxfclib/formats/mpq/xx_mpq.h
    include/xxfclib/formats/sabdu/xx_sabdu.h
    include/xxfclib/formats/apricot/xx_apricot.h
    include/xxfclib/formats/hdcopy/xx_hdcopy.h
    include/xxfclib/formats/copydisk/xx_copydisk.h
    include/xxfclib/formats/ciso/xx_ciso.h
    include/xxfclib/formats/vmdk/xx_vmdk.h
    include/xxfclib/formats/vhddynamic/xx_vhddynamic.h
    include/xxfclib/formats/wim/xx_wim.h
    include/xxfclib/formats/softpaq2/xx_softpaq2.h
    include/xxfclib/formats/aiaff/xx_aiaff.h
    include/xxfclib/formats/gxl/xx_gxl.h
    include/xxfclib/formats/shrinkwrap/xx_shrinkwrap.h
    include/xxfclib/formats/red/xx_red.h
    include/xxfclib/formats/diskexpress/xx_diskexpress.h
    include/xxfclib/formats/cpx/xx_cpx.h
    include/xxfclib/formats/smsipak/xx_smsipak.h
    include/xxfclib/formats/bnd/xx_bnd.h
    include/xxfclib/formats/cat/xx_cat.h
    include/xxfclib/formats/csidos/xx_csidos.h
    include/xxfclib/formats/binder/xx_binder.h
    include/xxfclib/formats/jasc/xx_jasc.h
    include/xxfclib/formats/recognita/xx_recognita.h
    include/xxfclib/formats/scf/xx_scf.h
    include/xxfclib/formats/bcw/xx_bcw.h
    include/xxfclib/formats/bvrp/xx_bvrp.h
    include/xxfclib/formats/cpoint/xx_cpoint.h
    include/xxfclib/formats/ssm/xx_ssm.h
    include/xxfclib/formats/mdcd/xx_mdcd.h
    include/xxfclib/formats/xlas/xx_xlas.h
    include/xxfclib/formats/pain/xx_pain.h
    include/xxfclib/formats/qrst/xx_qrst.h
    include/xxfclib/formats/opc/xx_opc.h
    include/xxfclib/formats/tnef/xx_tnef.h
    include/xxfclib/formats/mcc/xx_mcc.h
    include/xxfclib/formats/notetab/xx_notetab.h
    include/xxfclib/formats/stunts/xx_stunts.h
    include/xxfclib/formats/megatechvol/xx_megatechvol.h
    include/xxfclib/formats/grasp/xx_grasp.h
    include/xxfclib/formats/psn/xx_psn.h
    include/xxfclib/formats/sinner/xx_sinner.h
    include/xxfclib/formats/hog2/xx_hog2.h
    include/xxfclib/formats/pcxlib/xx_pcxlib.h
    include/xxfclib/formats/vmsdb/xx_vmsdb.h
    include/xxfclib/formats/vmspcsi/xx_vmspcsi.h
    include/xxfclib/formats/beospkg/xx_beospkg.h
    include/xxfclib/formats/solarispkg/xx_solarispkg.h
    include/xxfclib/formats/pax/xx_pax.h
    include/xxfclib/formats/copyqmexe/xx_copyqmexe.h
    include/xxfclib/formats/diskjuggler/xx_diskjuggler.h
    include/xxfclib/formats/pmdiskcopy/xx_pmdiskcopy.h
    include/xxfclib/formats/diskdupe/xx_diskdupe.h
    include/xxfclib/formats/imd/xx_imd.h
    include/xxfclib/formats/twoimg/xx_twoimg.h
    include/xxfclib/formats/fdi/xx_fdi.h
    include/xxfclib/formats/hfe/xx_hfe.h
    include/xxfclib/formats/teledisk/xx_teledisk.h
    include/xxfclib/formats/copyqm/xx_copyqm.h
    include/xxfclib/formats/pcinstall/xx_pcinstall.h
    include/xxfclib/formats/gksetup/xx_gksetup.h
    include/xxfclib/formats/is11/xx_is11.h
    include/xxfclib/formats/izpack/xx_izpack.h
    include/xxfclib/formats/squeeze1/xx_squeeze1.h
    include/xxfclib/formats/trdos/xx_trdos.h
    include/xxfclib/formats/lifkd/xx_lifkd.h
    include/xxfclib/formats/arcv/xx_arcv.h
    include/xxfclib/formats/compaqlzh/xx_compaqlzh.h
    include/xxfclib/formats/lzk00/xx_lzk00.h
    include/xxfclib/formats/pma/xx_pma.h
    include/xxfclib/formats/binhex/xx_binhex.h
    include/xxfclib/formats/binaryii/xx_binaryii.h
    include/xxfclib/formats/stuffit/xx_stuffit.h
    include/xxfclib/formats/dclft/xx_dclft.h
    include/xxfclib/formats/debugscr/xx_debugscr.h
    include/xxfclib/formats/gob/xx_gob.h
    include/xxfclib/formats/savedskf/xx_savedskf.h
    include/xxfclib/formats/edilzss/xx_edilzss.h
    include/xxfclib/formats/is7inx/xx_is7inx.h
    include/xxfclib/formats/is5/xx_is5.h
    include/xxfclib/formats/is3/xx_is3.h
    include/xxfclib/formats/psdc/xx_psdc.h
    include/xxfclib/formats/unixcompact/xx_unixcompact.h
    include/xxfclib/formats/sco/xx_sco.h
    include/xxfclib/formats/gpfpack/xx_gpfpack.h
    include/xxfclib/formats/ftcomp/xx_ftcomp.h
    include/xxfclib/formats/winlink/xx_winlink.h
    include/xxfclib/formats/gst/xx_gst.h
    include/xxfclib/formats/finear/xx_finear.h
    include/xxfclib/formats/mwave/xx_mwave.h
    include/xxfclib/formats/xorarchive/xx_xorarchive.h
    include/xxfclib/formats/mxs/xx_mxs.h
    include/xxfclib/formats/edc/xx_edc.h
    include/xxfclib/formats/mrnz/xx_mrnz.h
    include/xxfclib/formats/tpwm/xx_tpwm.h
    include/xxfclib/formats/cazip/xx_cazip.h
    include/xxfclib/formats/ibmpack/xx_ibmpack.h
    include/xxfclib/formats/rnc/xx_rnc.h
    include/xxfclib/formats/shar/xx_shar.h
    include/xxfclib/formats/netware2/xx_netware2.h
    include/xxfclib/formats/mathcad/xx_mathcad.h
    include/xxfclib/formats/perform/xx_perform.h
    include/xxfclib/formats/battleisle/xx_battleisle.h
    include/xxfclib/formats/kpck/xx_kpck.h
    include/xxfclib/formats/beatthehouse/xx_beatthehouse.h
    include/xxfclib/formats/pp20/xx_pp20.h
    include/xxfclib/formats/macbinary/xx_macbinary.h
    include/xxfclib/formats/applesingle/xx_applesingle.h
    include/xxfclib/formats/resourcefork/xx_resourcefork.h
    include/xxfclib/formats/cramfs/xx_cramfs.h
    include/xxfclib/formats/jffs2/xx_jffs2.h
    include/xxfclib/formats/yaffs/xx_yaffs.h
    include/xxfclib/formats/ubi/xx_ubi.h
    include/xxfclib/formats/ubifs/xx_ubifs.h
    include/xxfclib/formats/ext/xx_ext.h
    include/xxfclib/formats/erofs/xx_erofs.h
    include/xxfclib/formats/fat/xx_fat.h
    include/xxfclib/formats/mbr/xx_mbr.h
    include/xxfclib/formats/gpt/xx_gpt.h
    include/xxfclib/formats/sparse/xx_sparse.h
    include/xxfclib/formats/uimage/xx_uimage.h
    include/xxfclib/formats/dtb/xx_dtb.h
    include/xxfclib/formats/squashfs/xx_squashfs.h
    include/xxfclib/formats/ntfs/xx_ntfs.h
    include/xxfclib/formats/udf/xx_udf.h
    include/xxfclib/formats/romfs/xx_romfs.h
    include/xxfclib/formats/sqz/xx_sqz.h
    include/xxfclib/formats/topspeed/xx_topspeed.h
    include/xxfclib/formats/tps/xx_tps.h
    include/xxfclib/formats/ulead/xx_ulead.h
    include/xxfclib/formats/quantum/xx_quantum.h
    include/xxfclib/formats/zxzip/xx_zxzip.h
    include/xxfclib/formats/zoom/xx_zoom.h
    include/xxfclib/formats/sfpack/xx_sfpack.h
    include/xxfclib/formats/claylz/xx_claylz.h
    include/xxfclib/formats/c64wraptor/xx_c64wraptor.h
    include/xxfclib/formats/corelltec/xx_corelltec.h
    include/xxfclib/formats/pcsecure/xx_pcsecure.h
    include/xxfclib/formats/rsvk/xx_rsvk.h
    include/xxfclib/formats/lzw15v/xx_lzw15v.h
    include/xxfclib/formats/saf/xx_saf.h
    include/xxfclib/formats/sls/xx_sls.h
    include/xxfclib/formats/nid/xx_nid.h
    include/xxfclib/formats/gamos/xx_gamos.h
    include/xxfclib/formats/panorama/xx_panorama.h
    include/xxfclib/formats/fpak/xx_fpak.h
    include/xxfclib/formats/ace/xx_ace.h
    include/xxfclib/formats/arj/xx_arj.h
    include/xxfclib/formats/ain/xx_ain.h
    include/xxfclib/formats/aldus/xx_aldus.h
    include/xxfclib/formats/alz/xx_alz.h
    include/xxfclib/formats/ampk/xx_ampk.h
    include/xxfclib/formats/aodos/xx_aodos.h
    include/xxfclib/formats/arcfs/xx_arcfs.h
    include/xxfclib/formats/pdp11ar/xx_pdp11ar.h
    include/xxfclib/formats/artipack/xx_artipack.h
    include/xxfclib/formats/arcv2/xx_arcv2.h
    include/xxfclib/formats/arcv4/xx_arcv4.h
    include/xxfclib/formats/warc/xx_warc.h
    include/xxfclib/formats/aixbff/xx_aixbff.h
    include/xxfclib/formats/arx/xx_arx.h
    include/xxfclib/formats/cab/xx_cab.h
    include/xxfclib/formats/msdos/xx_msdos.h
    include/xxfclib/formats/com/xx_com.h
    include/xxfclib/formats/dos16m/xx_dos16m.h
    include/xxfclib/formats/atarist/xx_atarist.h
    include/xxfclib/formats/amigahunk/xx_amigahunk.h
    include/xxfclib/formats/pe/xx_pe_inspect.h
    include/xxfclib/formats/dotnet/xx_dotnet.h
    include/xxfclib/formats/dotnet/xx_dotnet_inspect.h
    include/xxfclib/formats/pe/xx_pe.h
    include/xxfclib/formats/elf/xx_elf_inspect.h
    include/xxfclib/formats/elf/xx_elf.h
    include/xxfclib/formats/macho/xx_macho_inspect.h
    include/xxfclib/formats/macho/xx_macho.h
    include/xxfclib/formats/ne/xx_ne.h
    include/xxfclib/formats/le/xx_le.h
    include/xxfclib/formats/lx/xx_lx.h
    include/xxfclib/formats/dex/xx_dex.h
    # Readers already registered by xx_format and the public API.
    include/xxfclib/formats/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.h
    include/xxfclib/formats/sfx_krzip/xx_sfx_krzip.h
    include/xxfclib/formats/sfx_warpin_package/xx_sfx_warpin_package.h
    include/xxfclib/formats/sfx_hci_instalit/xx_sfx_hci_instalit.h
    include/xxfclib/formats/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.h
    include/xxfclib/formats/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.h
    include/xxfclib/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.h
    include/xxfclib/formats/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.h
    include/xxfclib/formats/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.h
    include/xxfclib/formats/sfx_sbx_extractor/xx_sfx_sbx_extractor.h
    include/xxfclib/formats/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.h
    include/xxfclib/formats/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.h
    include/xxfclib/formats/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.h
    include/xxfclib/formats/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.h
    include/xxfclib/formats/wise_installation_system/xx_wise_installation_system.h
    include/xxfclib/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf.h
    include/xxfclib/formats/gentee_installer/xx_gentee_installer.h
    include/xxfclib/formats/clickteam_install_creator/xx_clickteam_install_creator.h
    include/xxfclib/formats/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.h
    include/xxfclib/formats/sfxstart/xx_sfxstart.h
    include/xxfclib/formats/modbus_tcp/xx_modbus_tcp.h
    include/xxfclib/formats/someip_message/xx_someip_message.h
    include/xxfclib/formats/dds_rtps/xx_dds_rtps.h
    include/xxfclib/formats/rip_message/xx_rip_message.h
    include/xxfclib/formats/vrrp_message/xx_vrrp_message.h
    include/xxfclib/formats/igmp_message/xx_igmp_message.h
    include/xxfclib/formats/pim_message/xx_pim_message.h
    include/xxfclib/formats/ldp_message/xx_ldp_message.h
    include/xxfclib/formats/gre_packet/xx_gre_packet.h
    include/xxfclib/formats/l2tp_packet/xx_l2tp_packet.h
    include/xxfclib/formats/lldp_message/xx_lldp_message.h
    include/xxfclib/formats/netflow_datagram/xx_netflow_datagram.h
    include/xxfclib/formats/ntlm_message/xx_ntlm_message.h
    include/xxfclib/formats/dcerpc_pdu/xx_dcerpc_pdu.h
    include/xxfclib/formats/ethereum_rlp/xx_ethereum_rlp.h
    include/xxfclib/formats/imagemagick_miff/xx_imagemagick_miff.h
    include/xxfclib/formats/avs_image/xx_avs_image.h
    include/xxfclib/formats/scanalytics_iplab/xx_scanalytics_iplab.h
    include/xxfclib/formats/mtv_image/xx_mtv_image.h
    include/xxfclib/formats/nokia_ota_bitmap/xx_nokia_ota_bitmap.h
    include/xxfclib/formats/apple_pict/xx_apple_pict.h
    include/xxfclib/formats/wordperfect_wpg/xx_wordperfect_wpg.h
    include/xxfclib/formats/nasa_vicar/xx_nasa_vicar.h
    include/xxfclib/formats/khoros_viff/xx_khoros_viff.h
    include/xxfclib/formats/imagemagick_mvg/xx_imagemagick_mvg.h
    include/xxfclib/formats/motif_uil/xx_motif_uil.h
    include/xxfclib/formats/iges_model/xx_iges_model.h
    include/xxfclib/formats/openusd_usda/xx_openusd_usda.h
    include/xxfclib/formats/ufo_glif/xx_ufo_glif.h
    include/xxfclib/formats/unifont_hex/xx_unifont_hex.h
    include/xxfclib/formats/adlib_sop/xx_adlib_sop.h
    include/xxfclib/formats/cudfm_cff/xx_cudfm_cff.h
    include/xxfclib/formats/adlib_jbm/xx_adlib_jbm.h
    include/xxfclib/formats/ceres_msc/xx_ceres_msc.h
    include/xxfclib/formats/adlib_xsm/xx_adlib_xsm.h
    include/xxfclib/formats/ken_ksm/xx_ken_ksm.h
    include/xxfclib/formats/implay_music/xx_implay_music.h
    include/xxfclib/formats/adlib_mtr/xx_adlib_mtr.h
    include/xxfclib/formats/rdos_raw/xx_rdos_raw.h
    include/xxfclib/formats/mad_tracker/xx_mad_tracker.h
    include/xxfclib/formats/vasp_poscar/xx_vasp_poscar.h
    include/xxfclib/formats/quantum_espresso_input/xx_quantum_espresso_input.h
    include/xxfclib/formats/cp2k_input/xx_cp2k_input.h
    include/xxfclib/formats/nwchem_input/xx_nwchem_input.h
    include/xxfclib/formats/gamess_input/xx_gamess_input.h
    include/xxfclib/formats/gaussian_input/xx_gaussian_input.h
    include/xxfclib/formats/abinit_input/xx_abinit_input.h
    include/xxfclib/formats/aims_geometry/xx_aims_geometry.h
    include/xxfclib/formats/orca_input/xx_orca_input.h
    include/xxfclib/formats/demon_input/xx_demon_input.h
    include/xxfclib/formats/ethernet_frame/xx_ethernet_frame.h
    include/xxfclib/formats/ip_packet/xx_ip_packet.h
    include/xxfclib/formats/arp_packet/xx_arp_packet.h
    include/xxfclib/formats/icmp_message/xx_icmp_message.h
    include/xxfclib/formats/sip_message/xx_sip_message.h
    include/xxfclib/formats/rtsp_message/xx_rtsp_message.h
    include/xxfclib/formats/diameter_message/xx_diameter_message.h
    include/xxfclib/formats/tacacs_packet/xx_tacacs_packet.h
    include/xxfclib/formats/gtp_message/xx_gtp_message.h
    include/xxfclib/formats/pfcp_message/xx_pfcp_message.h
    include/xxfclib/formats/pptp_message/xx_pptp_message.h
    include/xxfclib/formats/rsvp_message/xx_rsvp_message.h
    include/xxfclib/formats/age_encrypted/xx_age_encrypted.h
    include/xxfclib/formats/kerberos_ccache/xx_kerberos_ccache.h
    include/xxfclib/formats/jose_jws/xx_jose_jws.h
    include/xxfclib/formats/wbmp_image/xx_wbmp_image.h
    include/xxfclib/formats/dec_sixel/xx_dec_sixel.h
    include/xxfclib/formats/palm_bitmap/xx_palm_bitmap.h
    include/xxfclib/formats/adobe_acv/xx_adobe_acv.h
    include/xxfclib/formats/adobe_act/xx_adobe_act.h
    include/xxfclib/formats/ogre_skeleton/xx_ogre_skeleton.h
    include/xxfclib/formats/cal3d_skeleton/xx_cal3d_skeleton.h
    include/xxfclib/formats/collada_dae/xx_collada_dae.h
    include/xxfclib/formats/lightwave_scene/xx_lightwave_scene.h
    include/xxfclib/formats/dsn6_density/xx_dsn6_density.h
    include/xxfclib/formats/crystallography_mtz/xx_crystallography_mtz.h
    include/xxfclib/formats/amira_mesh/xx_amira_mesh.h
    include/xxfclib/formats/tetgen_mesh/xx_tetgen_mesh.h
    include/xxfclib/formats/jedec_fuse/xx_jedec_fuse.h
    include/xxfclib/formats/qchem_input/xx_qchem_input.h
    include/xxfclib/formats/adlib_bam/xx_adlib_bam.h
    include/xxfclib/formats/adlib_bmf/xx_adlib_bmf.h
    include/xxfclib/formats/creative_cmf/xx_creative_cmf.h
    include/xxfclib/formats/adlib_dfm/xx_adlib_dfm.h
    include/xxfclib/formats/adlib_lds/xx_adlib_lds.h
    include/xxfclib/formats/adlib_mkj/xx_adlib_mkj.h
    include/xxfclib/formats/adlib_rol/xx_adlib_rol.h
    include/xxfclib/formats/adlib_sa2/xx_adlib_sa2.h
    include/xxfclib/formats/faust_fmc/xx_faust_fmc.h
    include/xxfclib/formats/softstar_rix/xx_softstar_rix.h
    include/xxfclib/formats/genomics_bed/xx_genomics_bed.h
    include/xxfclib/formats/genomics_wiggle/xx_genomics_wiggle.h
    include/xxfclib/formats/genomics_gtf/xx_genomics_gtf.h
    include/xxfclib/formats/genomics_agp/xx_genomics_agp.h
    include/xxfclib/formats/sequencing_abif/xx_sequencing_abif.h
    include/xxfclib/formats/sequencing_scf/xx_sequencing_scf.h
    include/xxfclib/formats/genomics_sff/xx_genomics_sff.h
    include/xxfclib/formats/lut_spi1d/xx_lut_spi1d.h
    include/xxfclib/formats/lut_spi3d/xx_lut_spi3d.h
    include/xxfclib/formats/lut_cinespace_csp/xx_lut_cinespace_csp.h
    include/xxfclib/formats/ntp_message/xx_ntp_message.h
    include/xxfclib/formats/rtp_rtcp/xx_rtp_rtcp.h
    include/xxfclib/formats/bgp_messages/xx_bgp_messages.h
    include/xxfclib/formats/ospf_packet/xx_ospf_packet.h
    include/xxfclib/formats/sctp_packet/xx_sctp_packet.h
    include/xxfclib/formats/isakmp_message/xx_isakmp_message.h
    include/xxfclib/formats/ssh_transport/xx_ssh_transport.h
    include/xxfclib/formats/smtp_transcript/xx_smtp_transcript.h
    include/xxfclib/formats/pkcs8_private_key/xx_pkcs8_private_key.h
    include/xxfclib/formats/putty_ppk/xx_putty_ppk.h
    include/xxfclib/formats/openssh_certificate/xx_openssh_certificate.h
    include/xxfclib/formats/safetensors/xx_safetensors.h
    include/xxfclib/formats/gguf/xx_gguf.h
    include/xxfclib/formats/cdb_database/xx_cdb_database.h
    include/xxfclib/formats/stomp_frames/xx_stomp_frames.h
    include/xxfclib/formats/fontforge_sfd/xx_fontforge_sfd.h
    include/xxfclib/formats/grub_pff2/xx_grub_pff2.h
    include/xxfclib/formats/opengex_model/xx_opengex_model.h
    include/xxfclib/formats/bvh_motion/xx_bvh_motion.h
    include/xxfclib/formats/directx_x/xx_directx_x.h
    include/xxfclib/formats/gts_surface/xx_gts_surface.h
    include/xxfclib/formats/medit_mesh/xx_medit_mesh.h
    include/xxfclib/formats/gocad_model/xx_gocad_model.h
    include/xxfclib/formats/nastran_bulk/xx_nastran_bulk.h
    include/xxfclib/formats/abaqus_input/xx_abaqus_input.h
    include/xxfclib/formats/ensight_gold_geometry/xx_ensight_gold_geometry.h
    include/xxfclib/formats/gmv_mesh/xx_gmv_mesh.h
    include/xxfclib/formats/usgs_dem/xx_usgs_dem.h
    include/xxfclib/formats/dted_elevation/xx_dted_elevation.h
    include/xxfclib/formats/mapinfo_mif/xx_mapinfo_mif.h
    include/xxfclib/formats/tracker_coconizer/xx_tracker_coconizer.h
    include/xxfclib/formats/tracker_real/xx_tracker_real.h
    include/xxfclib/formats/tracker_megatracker/xx_tracker_megatracker.h
    include/xxfclib/formats/amos_music_bank/xx_amos_music_bank.h
    include/xxfclib/formats/adlib_rad/xx_adlib_rad.h
    include/xxfclib/formats/adlib_amd/xx_adlib_amd.h
    include/xxfclib/formats/adlib_hsc/xx_adlib_hsc.h
    include/xxfclib/formats/adlib_d00/xx_adlib_d00.h
    include/xxfclib/formats/adlib_bnk/xx_adlib_bnk.h
    include/xxfclib/formats/dosbox_dro/xx_dosbox_dro.h
    include/xxfclib/formats/genomics_genbank/xx_genomics_genbank.h
    include/xxfclib/formats/genomics_embl/xx_genomics_embl.h
    include/xxfclib/formats/genomics_swissprot/xx_genomics_swissprot.h
    include/xxfclib/formats/alignment_clustal/xx_alignment_clustal.h
    include/xxfclib/formats/alignment_stockholm/xx_alignment_stockholm.h
    include/xxfclib/formats/alignment_phylip/xx_alignment_phylip.h
    include/xxfclib/formats/alignment_maf/xx_alignment_maf.h
    include/xxfclib/formats/alignment_mauve/xx_alignment_mauve.h
    include/xxfclib/formats/ucsc_nib/xx_ucsc_nib.h
    include/xxfclib/formats/assembly_gfa/xx_assembly_gfa.h
    include/xxfclib/formats/http1_message/xx_http1_message.h
    include/xxfclib/formats/websocket_frames/xx_websocket_frames.h
    include/xxfclib/formats/coap_message/xx_coap_message.h
    include/xxfclib/formats/stun_message/xx_stun_message.h
    include/xxfclib/formats/dhcp_message/xx_dhcp_message.h
    include/xxfclib/formats/radius_packet/xx_radius_packet.h
    include/xxfclib/formats/snmp_message/xx_snmp_message.h
    include/xxfclib/formats/ldap_message/xx_ldap_message.h
    include/xxfclib/formats/tls_records/xx_tls_records.h
    include/xxfclib/formats/jks_keystore/xx_jks_keystore.h
    include/xxfclib/formats/java_serialization/xx_java_serialization.h
    include/xxfclib/formats/x509_crl/xx_x509_crl.h
    include/xxfclib/formats/ocsp_response/xx_ocsp_response.h
    include/xxfclib/formats/lmdb_data/xx_lmdb_data.h
    include/xxfclib/formats/gdbm_dump/xx_gdbm_dump.h
    include/xxfclib/formats/adobe_acb/xx_adobe_acb.h
    include/xxfclib/formats/jasc_palette/xx_jasc_palette.h
    include/xxfclib/formats/x11_xbm/xx_x11_xbm.h
    include/xxfclib/formats/jpeg2000_pgx/xx_jpeg2000_pgx.h
    include/xxfclib/formats/amiga_diskobject/xx_amiga_diskobject.h
    include/xxfclib/formats/tex_vf/xx_tex_vf.h
    include/xxfclib/formats/esri_ascii_grid/xx_esri_ascii_grid.h
    include/xxfclib/formats/surfer_grid/xx_surfer_grid.h
    include/xxfclib/formats/gxf_grid/xx_gxf_grid.h
    include/xxfclib/formats/ogc_wkt/xx_ogc_wkt.h
    include/xxfclib/formats/step_part21/xx_step_part21.h
    include/xxfclib/formats/gerber_rs274x/xx_gerber_rs274x.h
    include/xxfclib/formats/excellon_drill/xx_excellon_drill.h
    include/xxfclib/formats/vrml_scene/xx_vrml_scene.h
    include/xxfclib/formats/renderman_rib/xx_renderman_rib.h
    include/xxfclib/formats/asylum_amf/xx_asylum_amf.h
    include/xxfclib/formats/tracker_stx/xx_tracker_stx.h
    include/xxfclib/formats/tracker_dtm/xx_tracker_dtm.h
    include/xxfclib/formats/tracker_soundfx/xx_tracker_soundfx.h
    include/xxfclib/formats/tracker_funk/xx_tracker_funk.h
    include/xxfclib/formats/tracker_archimedes/xx_tracker_archimedes.h
    include/xxfclib/formats/pce_psi/xx_pce_psi.h
    include/xxfclib/formats/pc98_d88/xx_pc98_d88.h
    include/xxfclib/formats/hxc_mfm/xx_hxc_mfm.h
    include/xxfclib/formats/yaze_ydsk/xx_yaze_ydsk.h
    include/xxfclib/formats/lammps_data/xx_lammps_data.h
    include/xxfclib/formats/lammps_dump/xx_lammps_dump.h
    include/xxfclib/formats/shelx_res/xx_shelx_res.h
    include/xxfclib/formats/turbomole_coord/xx_turbomole_coord.h
    include/xxfclib/formats/charmm_crd/xx_charmm_crd.h
    include/xxfclib/formats/castep_cell/xx_castep_cell.h
    include/xxfclib/formats/crystal_fort34/xx_crystal_fort34.h
    include/xxfclib/formats/siesta_xv/xx_siesta_xv.h
    include/xxfclib/formats/harwell_boeing/xx_harwell_boeing.h
    include/xxfclib/formats/openfoam_points/xx_openfoam_points.h
    include/xxfclib/formats/minecraft_nbt/xx_minecraft_nbt.h
    include/xxfclib/formats/amazon_ion_binary/xx_amazon_ion_binary.h
    include/xxfclib/formats/leveldb_log/xx_leveldb_log.h
    include/xxfclib/formats/dns_message/xx_dns_message.h
    include/xxfclib/formats/rocksdb_blob/xx_rocksdb_blob.h
    include/xxfclib/formats/mongodb_wire/xx_mongodb_wire.h
    include/xxfclib/formats/redis_resp/xx_redis_resp.h
    include/xxfclib/formats/mqtt_packets/xx_mqtt_packets.h
    include/xxfclib/formats/amqp_frames/xx_amqp_frames.h
    include/xxfclib/formats/thrift_compact/xx_thrift_compact.h
    include/xxfclib/formats/x509_certificate/xx_x509_certificate.h
    include/xxfclib/formats/pkcs10_csr/xx_pkcs10_csr.h
    include/xxfclib/formats/pkcs12_pfx/xx_pkcs12_pfx.h
    include/xxfclib/formats/openssh_private_key/xx_openssh_private_key.h
    include/xxfclib/formats/kerberos_keytab/xx_kerberos_keytab.h
    include/xxfclib/formats/gimp_gpl/xx_gimp_gpl.h
    include/xxfclib/formats/gimp_ggr/xx_gimp_ggr.h
    include/xxfclib/formats/iridas_cube_lut/xx_iridas_cube_lut.h
    include/xxfclib/formats/hpgl_plot/xx_hpgl_plot.h
    include/xxfclib/formats/paintshop_psp/xx_paintshop_psp.h
    include/xxfclib/formats/photoshop_pat/xx_photoshop_pat.h
    include/xxfclib/formats/mmd_pmx/xx_mmd_pmx.h
    include/xxfclib/formats/metasequoia_mqo/xx_metasequoia_mqo.h
    include/xxfclib/formats/calma_gdsii/xx_calma_gdsii.h
    include/xxfclib/formats/autodesk_ase/xx_autodesk_ase.h
    include/xxfclib/formats/freesurfer_surface/xx_freesurfer_surface.h
    include/xxfclib/formats/gmsh_msh/xx_gmsh_msh.h
    include/xxfclib/formats/netgen_vol/xx_netgen_vol.h
    include/xxfclib/formats/font_afm/xx_font_afm.h
    include/xxfclib/formats/tiled_tmx/xx_tiled_tmx.h
    include/xxfclib/formats/nintendo_sdat/xx_nintendo_sdat.h
    include/xxfclib/formats/sony_vab/xx_sony_vab.h
    include/xxfclib/formats/yamaha_ym/xx_yamaha_ym.h
    include/xxfclib/formats/zx_ayemul/xx_zx_ayemul.h
    include/xxfclib/formats/dragon_vdk/xx_dragon_vdk.h
    include/xxfclib/formats/apple_a2r/xx_apple_a2r.h
    include/xxfclib/formats/atari_atr/xx_atari_atr.h
    include/xxfclib/formats/atari_pasti_stx/xx_atari_pasti_stx.h
    include/xxfclib/formats/amiga_ipf/xx_amiga_ipf.h
    include/xxfclib/formats/tracker_dtt/xx_tracker_dtt.h
    include/xxfclib/formats/gaussian_cube/xx_gaussian_cube.h
    include/xxfclib/formats/molecule_xyz/xx_molecule_xyz.h
    include/xxfclib/formats/mdl_molfile/xx_mdl_molfile.h
    include/xxfclib/formats/tripos_mol2/xx_tripos_mol2.h
    include/xxfclib/formats/xcrysden_xsf/xx_xcrysden_xsf.h
    include/xxfclib/formats/amber_prmtop/xx_amber_prmtop.h
    include/xxfclib/formats/amber_restart/xx_amber_restart.h
    include/xxfclib/formats/gaussian_fchk/xx_gaussian_fchk.h
    include/xxfclib/formats/jcamp_dx/xx_jcamp_dx.h
    include/xxfclib/formats/dl_poly_config/xx_dl_poly_config.h
    include/xxfclib/formats/nix_nar/xx_nix_nar.h
    include/xxfclib/formats/redis_rdb/xx_redis_rdb.h
    include/xxfclib/formats/postgres_custom/xx_postgres_custom.h
    include/xxfclib/formats/mysql_binlog/xx_mysql_binlog.h
    include/xxfclib/formats/kafka_record_batch/xx_kafka_record_batch.h
    include/xxfclib/formats/android_binary_xml/xx_android_binary_xml.h
    include/xxfclib/formats/android_resources_arsc/xx_android_resources_arsc.h
    include/xxfclib/formats/msgpack/xx_msgpack.h
    include/xxfclib/formats/ubjson/xx_ubjson.h
    include/xxfclib/formats/bittorrent_metainfo/xx_bittorrent_metainfo.h
    include/xxfclib/formats/erlang_external_term/xx_erlang_external_term.h
    include/xxfclib/formats/capnproto_message/xx_capnproto_message.h
    include/xxfclib/formats/dbus_message/xx_dbus_message.h
    include/xxfclib/formats/windows_shell_link/xx_windows_shell_link.h
    include/xxfclib/formats/pkcs7_cms/xx_pkcs7_cms.h
    include/xxfclib/formats/wavefront_obj/xx_wavefront_obj.h
    include/xxfclib/formats/off_mesh/xx_off_mesh.h
    include/xxfclib/formats/ac3d_model/xx_ac3d_model.h
    include/xxfclib/formats/qubicle_qb/xx_qubicle_qb.h
    include/xxfclib/formats/terragen_ter/xx_terragen_ter.h
    include/xxfclib/formats/gimp_xcf/xx_gimp_xcf.h
    include/xxfclib/formats/photoshop_abr/xx_photoshop_abr.h
    include/xxfclib/formats/softimage_pic/xx_softimage_pic.h
    include/xxfclib/formats/alias_pix/xx_alias_pix.h
    include/xxfclib/formats/qt_qpicture/xx_qt_qpicture.h
    include/xxfclib/formats/font_type1_pfb/xx_font_type1_pfb.h
    include/xxfclib/formats/font_gem_fnt/xx_font_gem_fnt.h
    include/xxfclib/formats/tex_gf/xx_tex_gf.h
    include/xxfclib/formats/bpg_image/xx_bpg_image.h
    include/xxfclib/formats/mng_animation/xx_mng_animation.h
    include/xxfclib/formats/atari_7800_a78/xx_atari_7800_a78.h
    include/xxfclib/formats/commodore_pc64/xx_commodore_pc64.h
    include/xxfclib/formats/atari_cas/xx_atari_cas.h
    include/xxfclib/formats/msx_cas/xx_msx_cas.h
    include/xxfclib/formats/oric_tap/xx_oric_tap.h
    include/xxfclib/formats/dragon_cas/xx_dragon_cas.h
    include/xxfclib/formats/amiga_ahx/xx_amiga_ahx.h
    include/xxfclib/formats/amstrad_cpc_sna/xx_amstrad_cpc_sna.h
    include/xxfclib/formats/vtech_vz/xx_vtech_vz.h
    include/xxfclib/formats/zx_hobeta/xx_zx_hobeta.h
    include/xxfclib/formats/genomics_fasta/xx_genomics_fasta.h
    include/xxfclib/formats/genomics_fastq/xx_genomics_fastq.h
    include/xxfclib/formats/genomics_sam/xx_genomics_sam.h
    include/xxfclib/formats/opendx_field/xx_opendx_field.h
    include/xxfclib/formats/genomics_vcf/xx_genomics_vcf.h
    include/xxfclib/formats/genomics_gff3/xx_genomics_gff3.h
    include/xxfclib/formats/protein_pdb/xx_protein_pdb.h
    include/xxfclib/formats/protein_mmcif/xx_protein_mmcif.h
    include/xxfclib/formats/matrix_market/xx_matrix_market.h
    include/xxfclib/formats/gromacs_gro/xx_gromacs_gro.h
    include/xxfclib/formats/microsoft_msf/xx_microsoft_msf.h
    include/xxfclib/formats/windows_registry_hive/xx_windows_registry_hive.h
    include/xxfclib/formats/windows_evtx/xx_windows_evtx.h
    include/xxfclib/formats/binary_plist/xx_binary_plist.h
    include/xxfclib/formats/mongodb_bson/xx_mongodb_bson.h
    include/xxfclib/formats/cbor/xx_cbor.h
    include/xxfclib/formats/openzim/xx_openzim.h
    include/xxfclib/formats/apache_orc/xx_apache_orc.h
    include/xxfclib/formats/hadoop_sequencefile/xx_hadoop_sequencefile.h
    include/xxfclib/formats/leveldb_sstable/xx_leveldb_sstable.h
    include/xxfclib/formats/snappy_framed/xx_snappy_framed.h
    include/xxfclib/formats/lzf_stream/xx_lzf_stream.h
    include/xxfclib/formats/fastlz_sixpack/xx_fastlz_sixpack.h
    include/xxfclib/formats/linux_btf/xx_linux_btf.h
    include/xxfclib/formats/flatgeobuf/xx_flatgeobuf.h
    include/xxfclib/formats/astc_texture/xx_astc_texture.h
    include/xxfclib/formats/pkm_texture/xx_pkm_texture.h
    include/xxfclib/formats/basis_texture/xx_basis_texture.h
    include/xxfclib/formats/openctm_mesh/xx_openctm_mesh.h
    include/xxfclib/formats/font_bdf/xx_font_bdf.h
    include/xxfclib/formats/font_pcf/xx_font_pcf.h
    include/xxfclib/formats/font_psf/xx_font_psf.h
    include/xxfclib/formats/font_windows_fnt/xx_font_windows_fnt.h
    include/xxfclib/formats/tex_tfm/xx_tex_tfm.h
    include/xxfclib/formats/tex_pk/xx_tex_pk.h
    include/xxfclib/formats/tex_dvi/xx_tex_dvi.h
    include/xxfclib/formats/netpbm_pfm/xx_netpbm_pfm.h
    include/xxfclib/formats/steinberg_vst3preset/xx_steinberg_vst3preset.h
    include/xxfclib/formats/font_bmfont/xx_font_bmfont.h
    include/xxfclib/formats/processing_vlw/xx_processing_vlw.h
    include/xxfclib/formats/snes_spc/xx_snes_spc.h
    include/xxfclib/formats/gameboy_gbs/xx_gameboy_gbs.h
    include/xxfclib/formats/sega_sgc/xx_sega_sgc.h
    include/xxfclib/formats/s98_log/xx_s98_log.h
    include/xxfclib/formats/atari_sap/xx_atari_sap.h
    include/xxfclib/formats/sc68_music/xx_sc68_music.h
    include/xxfclib/formats/zx_spectrum_pzx/xx_zx_spectrum_pzx.h
    include/xxfclib/formats/acorn_uef/xx_acorn_uef.h
    include/xxfclib/formats/nintendo_unif/xx_nintendo_unif.h
    include/xxfclib/formats/nintendo_fds/xx_nintendo_fds.h
    include/xxfclib/formats/ucsc_bigwig/xx_ucsc_bigwig.h
    include/xxfclib/formats/ucsc_bigbed/xx_ucsc_bigbed.h
    include/xxfclib/formats/phylo_nexus/xx_phylo_nexus.h
    include/xxfclib/formats/phylo_newick/xx_phylo_newick.h
    include/xxfclib/formats/sqlite_rollback_journal/xx_sqlite_rollback_journal.h
    include/xxfclib/formats/neuroscan_cnt/xx_neuroscan_cnt.h
    include/xxfclib/formats/axona_tetrode/xx_axona_tetrode.h
    include/xxfclib/formats/python_pickle/xx_python_pickle.h
    include/xxfclib/formats/inivation_aedat/xx_inivation_aedat.h
    include/xxfclib/formats/python_marshal/xx_python_marshal.h
    include/xxfclib/formats/vice_x64/xx_vice_x64.h
    include/xxfclib/formats/vice_snapshot/xx_vice_snapshot.h
    include/xxfclib/formats/commodore_g64/xx_commodore_g64.h
    include/xxfclib/formats/commodore_p64/xx_commodore_p64.h
    include/xxfclib/formats/commodore_tap/xx_commodore_tap.h
    include/xxfclib/formats/zx_spectrum_tzx/xx_zx_spectrum_tzx.h
    include/xxfclib/formats/zx_spectrum_szx/xx_zx_spectrum_szx.h
    include/xxfclib/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.h
    include/xxfclib/formats/atari_st_msa/xx_atari_st_msa.h
    include/xxfclib/formats/supercard_scp/xx_supercard_scp.h
    include/xxfclib/formats/apple_woz/xx_apple_woz.h
    include/xxfclib/formats/nintendo_nsf/xx_nintendo_nsf.h
    include/xxfclib/formats/vgm_log/xx_vgm_log.h
    include/xxfclib/formats/psid_sid/xx_psid_sid.h
    include/xxfclib/formats/hes_sound/xx_hes_sound.h
    include/xxfclib/formats/audio_dolby_ac3/xx_audio_dolby_ac3.h
    include/xxfclib/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3.h
    include/xxfclib/formats/audio_aac_adts/xx_audio_aac_adts.h
    include/xxfclib/formats/audio_monkeys_ape/xx_audio_monkeys_ape.h
    include/xxfclib/formats/mpeg_transport_stream/xx_mpeg_transport_stream.h
    include/xxfclib/formats/mpeg_program_stream/xx_mpeg_program_stream.h
    include/xxfclib/formats/realmedia_rm/xx_realmedia_rm.h
    include/xxfclib/formats/idtech_roq/xx_idtech_roq.h
    include/xxfclib/formats/rad_bink/xx_rad_bink.h
    include/xxfclib/formats/rad_smacker/xx_rad_smacker.h
    include/xxfclib/formats/interplay_mve/xx_interplay_mve.h
    include/xxfclib/formats/westwood_vqa/xx_westwood_vqa.h
    include/xxfclib/formats/autodesk_flic/xx_autodesk_flic.h
    include/xxfclib/formats/idtech_md5anim/xx_idtech_md5anim.h
    include/xxfclib/formats/stereolithography_stl/xx_stereolithography_stl.h
    include/xxfclib/formats/garmin_fit/xx_garmin_fit.h
    include/xxfclib/formats/rosbag1/xx_rosbag1.h
    include/xxfclib/formats/mcap/xx_mcap.h
    include/xxfclib/formats/seismic_sac/xx_seismic_sac.h
    include/xxfclib/formats/seismic_seg2/xx_seismic_seg2.h
    include/xxfclib/formats/ucsc_twobit/xx_ucsc_twobit.h
    include/xxfclib/formats/genomics_bgen/xx_genomics_bgen.h
    include/xxfclib/formats/openephys_continuous/xx_openephys_continuous.h
    include/xxfclib/formats/mountainsort_mda/xx_mountainsort_mda.h
    include/xxfclib/formats/igor_ibw/xx_igor_ibw.h
    include/xxfclib/formats/princeton_spe/xx_princeton_spe.h
    include/xxfclib/formats/microscopy_spider/xx_microscopy_spider.h
    include/xxfclib/formats/wmo_grib/xx_wmo_grib.h
    include/xxfclib/formats/wmo_bufr/xx_wmo_bufr.h
    include/xxfclib/formats/autocad_dxf/xx_autocad_dxf.h
    include/xxfclib/formats/blackrock_nsx/xx_blackrock_nsx.h
    include/xxfclib/formats/blackrock_nev/xx_blackrock_nev.h
    include/xxfclib/formats/lecroy_trc/xx_lecroy_trc.h
    include/xxfclib/formats/tektronix_isf/xx_tektronix_isf.h
    include/xxfclib/formats/ircam_sdif/xx_ircam_sdif.h
    include/xxfclib/formats/tracker_liquid/xx_tracker_liquid.h
    include/xxfclib/formats/tracker_dmf/xx_tracker_dmf.h
    include/xxfclib/formats/tracker_ptm/xx_tracker_ptm.h
    include/xxfclib/formats/tracker_ams/xx_tracker_ams.h
    include/xxfclib/formats/tracker_digi/xx_tracker_digi.h
    include/xxfclib/formats/tracker_emod/xx_tracker_emod.h
    include/xxfclib/formats/tracker_mt2/xx_tracker_mt2.h
    include/xxfclib/formats/audio_dsf/xx_audio_dsf.h
    include/xxfclib/formats/audio_dff/xx_audio_dff.h
    include/xxfclib/formats/audio_wave64/xx_audio_wave64.h
    include/xxfclib/formats/audio_adx/xx_audio_adx.h
    include/xxfclib/formats/audio_ast/xx_audio_ast.h
    include/xxfclib/formats/audio_hca/xx_audio_hca.h
    include/xxfclib/formats/iff_8svx/xx_iff_8svx.h
    include/xxfclib/formats/audio_wavpack/xx_audio_wavpack.h
    include/xxfclib/formats/blender_blend/xx_blender_blend.h
    include/xxfclib/formats/autodesk_fbx/xx_autodesk_fbx.h
    include/xxfclib/formats/autodesk_3ds/xx_autodesk_3ds.h
    include/xxfclib/formats/lightwave_lwo2/xx_lightwave_lwo2.h
    include/xxfclib/formats/lightwave_mdd/xx_lightwave_mdd.h
    include/xxfclib/formats/sony_psp_pbp/xx_sony_psp_pbp.h
    include/xxfclib/formats/flash_video_flv/xx_flash_video_flv.h
    include/xxfclib/formats/nintendo_n64_rom/xx_nintendo_n64_rom.h
    include/xxfclib/formats/nintendo_gb_rom/xx_nintendo_gb_rom.h
    include/xxfclib/formats/nintendo_gba_rom/xx_nintendo_gba_rom.h
    include/xxfclib/formats/sega_megadrive_rom/xx_sega_megadrive_rom.h
    include/xxfclib/formats/spring_s3o/xx_spring_s3o.h
    include/xxfclib/formats/xna_xnb/xx_xna_xnb.h
    include/xxfclib/formats/lua_bytecode51/xx_lua_bytecode51.h
    include/xxfclib/formats/quake_md5mesh/xx_quake_md5mesh.h
    include/xxfclib/formats/tracker_mod/xx_tracker_mod.h
    include/xxfclib/formats/tracker_far/xx_tracker_far.h
    include/xxfclib/formats/tracker_mdl/xx_tracker_mdl.h
    include/xxfclib/formats/tracker_gdm/xx_tracker_gdm.h
    include/xxfclib/formats/tracker_dbm/xx_tracker_dbm.h
    include/xxfclib/formats/tracker_med/xx_tracker_med.h
    include/xxfclib/formats/tracker_imf/xx_tracker_imf.h
    include/xxfclib/formats/tracker_amf/xx_tracker_amf.h
    include/xxfclib/formats/tracker_psm/xx_tracker_psm.h
    include/xxfclib/formats/steinberg_fxb/xx_steinberg_fxb.h
    include/xxfclib/formats/astronomy_ser/xx_astronomy_ser.h
    include/xxfclib/formats/photontiming_ptu/xx_photontiming_ptu.h
    include/xxfclib/formats/photontiming_phu/xx_photontiming_phu.h
    include/xxfclib/formats/charmm_dcd/xx_charmm_dcd.h
    include/xxfclib/formats/gromacs_trr/xx_gromacs_trr.h
    include/xxfclib/formats/microscopy_ics/xx_microscopy_ics.h
    include/xxfclib/formats/tecplot_plt/xx_tecplot_plt.h
    include/xxfclib/formats/fujifilm_raf/xx_fujifilm_raf.h
    include/xxfclib/formats/sigma_x3f/xx_sigma_x3f.h
    include/xxfclib/formats/minolta_mrw/xx_minolta_mrw.h
    include/xxfclib/formats/sfx_imp/xx_sfx_imp.h
    include/xxfclib/formats/sfx_red/xx_sfx_red.h
    include/xxfclib/formats/sfx_ha/xx_sfx_ha.h
    include/xxfclib/formats/sfx_lzx/xx_sfx_lzx.h
    include/xxfclib/formats/sfx_sqx/xx_sfx_sqx.h
    include/xxfclib/formats/sfx_ain/xx_sfx_ain.h
    include/xxfclib/formats/sfx_hap/xx_sfx_hap.h
    include/xxfclib/formats/sfx_zoo/xx_sfx_zoo.h
    include/xxfclib/formats/sfx_cazip/xx_sfx_cazip.h
    include/xxfclib/formats/sfx_tgcf/xx_sfx_tgcf.h
    include/xxfclib/formats/sfx_starkit/xx_sfx_starkit.h
    include/xxfclib/formats/sfx_alz/xx_sfx_alz.h
    include/xxfclib/formats/sfx_chm/xx_sfx_chm.h
    include/xxfclib/formats/egg/xx_egg.h
    include/xxfclib/formats/nufx/xx_nufx.h
    include/xxfclib/formats/nintendo_dol/xx_nintendo_dol.h
    include/xxfclib/formats/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.h
    include/xxfclib/formats/nintendo_j3d_btk/xx_nintendo_j3d_btk.h
    include/xxfclib/formats/nintendo_brstm/xx_nintendo_brstm.h
    include/xxfclib/formats/nintendo_brwav/xx_nintendo_brwav.h
    include/xxfclib/formats/nintendo_brlyt/xx_nintendo_brlyt.h
    include/xxfclib/formats/nintendo_brlan/xx_nintendo_brlan.h
    include/xxfclib/formats/nintendo_bfsha/xx_nintendo_bfsha.h
    include/xxfclib/formats/cri_usm/xx_cri_usm.h
    include/xxfclib/formats/cri_utf/xx_cri_utf.h
    include/xxfclib/formats/idtech_iqm/xx_idtech_iqm.h
    include/xxfclib/formats/unreal_psk/xx_unreal_psk.h
    include/xxfclib/formats/unreal_psa/xx_unreal_psa.h
    include/xxfclib/formats/torque_dts/xx_torque_dts.h
    include/xxfclib/formats/magicavoxel_vox/xx_magicavoxel_vox.h
    include/xxfclib/formats/audio_au/xx_audio_au.h
    include/xxfclib/formats/creative_voc/xx_creative_voc.h
    include/xxfclib/formats/tracker_xm/xx_tracker_xm.h
    include/xxfclib/formats/tracker_s3m/xx_tracker_s3m.h
    include/xxfclib/formats/tracker_it/xx_tracker_it.h
    include/xxfclib/formats/tracker_mtm/xx_tracker_mtm.h
    include/xxfclib/formats/tracker_stm/xx_tracker_stm.h
    include/xxfclib/formats/tracker_669/xx_tracker_669.h
    include/xxfclib/formats/tracker_ult/xx_tracker_ult.h
    include/xxfclib/formats/tracker_okt/xx_tracker_okt.h
    include/xxfclib/formats/nifti2/xx_nifti2.h
    include/xxfclib/formats/lidar_las/xx_lidar_las.h
    include/xxfclib/formats/esri_shp/xx_esri_shp.h
    include/xxfclib/formats/polygon_ply/xx_polygon_ply.h
    include/xxfclib/formats/pointcloud_pcd/xx_pointcloud_pcd.h
    include/xxfclib/formats/matlab_mat4/xx_matlab_mat4.h
    include/xxfclib/formats/seismic_segy/xx_seismic_segy.h
    include/xxfclib/formats/biomedical_bdf/xx_biomedical_bdf.h
    include/xxfclib/formats/erlang_beam/xx_erlang_beam.h
    include/xxfclib/formats/java_jmod/xx_java_jmod.h
    include/xxfclib/formats/sfx_arcv2/xx_sfx_arcv2.h
    include/xxfclib/formats/sfx_chz/xx_sfx_chz.h
    include/xxfclib/formats/sfx_szdd/xx_sfx_szdd.h
    include/xxfclib/formats/sfx_mpq/xx_sfx_mpq.h
    include/xxfclib/formats/sfx_swag/xx_sfx_swag.h
    include/xxfclib/formats/sfx_zpak/xx_sfx_zpak.h
    include/xxfclib/formats/sfx_diskexpress/xx_sfx_diskexpress.h
    include/xxfclib/formats/sfx_bzip2/xx_sfx_bzip2.h
    include/xxfclib/formats/sfx_gzip/xx_sfx_gzip.h
    include/xxfclib/formats/sfx_tar/xx_sfx_tar.h
    include/xxfclib/formats/sfx_cab/xx_sfx_cab.h
    include/xxfclib/formats/pmarc_sfx/xx_pmarc_sfx.h
    include/xxfclib/formats/sfx_7zip/xx_sfx_7zip.h
    include/xxfclib/formats/sfx_ace/xx_sfx_ace.h
    include/xxfclib/formats/sfx_zipcentral/xx_sfx_zipcentral.h
    include/xxfclib/formats/sony_psx_exe/xx_sony_psx_exe.h
    include/xxfclib/formats/sony_psf/xx_sony_psf.h
    include/xxfclib/formats/xbox_xdvdfs/xx_xbox_xdvdfs.h
    include/xxfclib/formats/nintendo_wbfs/xx_nintendo_wbfs.h
    include/xxfclib/formats/godot_ctex/xx_godot_ctex.h
    include/xxfclib/formats/unity_serialized/xx_unity_serialized.h
    include/xxfclib/formats/idtech_mdl/xx_idtech_mdl.h
    include/xxfclib/formats/valve_studio_mdl/xx_valve_studio_mdl.h
    include/xxfclib/formats/blitz3d_b3d/xx_blitz3d_b3d.h
    include/xxfclib/formats/milkshape_ms3d/xx_milkshape_ms3d.h
    include/xxfclib/formats/nintendo_bch/xx_nintendo_bch.h
    include/xxfclib/formats/nintendo_cgfx/xx_nintendo_cgfx.h
    include/xxfclib/formats/nintendo_byaml/xx_nintendo_byaml.h
    include/xxfclib/formats/relic_chunky/xx_relic_chunky.h
    include/xxfclib/formats/ogre_mesh/xx_ogre_mesh.h
    include/xxfclib/formats/adobe_ase/xx_adobe_ase.h
    include/xxfclib/formats/adobe_aco/xx_adobe_aco.h
    include/xxfclib/formats/gimp_gbr/xx_gimp_gbr.h
    include/xxfclib/formats/gimp_gih/xx_gimp_gih.h
    include/xxfclib/formats/gimp_pat/xx_gimp_pat.h
    include/xxfclib/formats/jbig2/xx_jbig2.h
    include/xxfclib/formats/djvu/xx_djvu.h
    include/xxfclib/formats/emf/xx_emf.h
    include/xxfclib/formats/wmf/xx_wmf.h
    include/xxfclib/formats/xfig/xx_xfig.h
    include/xxfclib/formats/nifti1/xx_nifti1.h
    include/xxfclib/formats/nrrd/xx_nrrd.h
    include/xxfclib/formats/mrc/xx_mrc.h
    include/xxfclib/formats/metaimage/xx_metaimage.h
    include/xxfclib/formats/vtk_legacy/xx_vtk_legacy.h
    include/xxfclib/formats/gipl/xx_gipl.h
    include/xxfclib/formats/freesurfer_mgh/xx_freesurfer_mgh.h
    include/xxfclib/formats/edf/xx_edf.h
    include/xxfclib/formats/fcs/xx_fcs.h
    include/xxfclib/formats/tensorflow_tfrecord/xx_tensorflow_tfrecord.h
    include/xxfclib/formats/sfx_arc/xx_sfx_arc.h
    include/xxfclib/formats/sfx_arj/xx_sfx_arj.h
    include/xxfclib/formats/sfx_bsn/xx_sfx_bsn.h
    include/xxfclib/formats/sfx_arq/xx_sfx_arq.h
    include/xxfclib/formats/sfx_gxl/xx_sfx_gxl.h
    include/xxfclib/formats/sfx_asymetrix/xx_sfx_asymetrix.h
    include/xxfclib/formats/sfx_rta/xx_sfx_rta.h
    include/xxfclib/formats/sfx_rtpatch/xx_sfx_rtpatch.h
    include/xxfclib/formats/esp_archive/xx_esp_archive.h
    include/xxfclib/formats/sfx_kwaj/xx_sfx_kwaj.h
    include/xxfclib/formats/gemdos_lha/xx_gemdos_lha.h
    include/xxfclib/formats/winimage_zip/xx_winimage_zip.h
    include/xxfclib/formats/hp3000_wrq/xx_hp3000_wrq.h
    include/xxfclib/formats/icu_data_package/xx_icu_data_package.h
    include/xxfclib/formats/sfx_sqz/xx_sfx_sqz.h
    include/xxfclib/formats/nintendo_bfstm/xx_nintendo_bfstm.h
    include/xxfclib/formats/nintendo_bfwav/xx_nintendo_bfwav.h
    include/xxfclib/formats/nintendo_bcwav/xx_nintendo_bcwav.h
    include/xxfclib/formats/nintendo_bfres/xx_nintendo_bfres.h
    include/xxfclib/formats/nintendo_bflyt/xx_nintendo_bflyt.h
    include/xxfclib/formats/nintendo_bclyt/xx_nintendo_bclyt.h
    include/xxfclib/formats/nintendo_bfnt/xx_nintendo_bfnt.h
    include/xxfclib/formats/nintendo_bcfnt/xx_nintendo_bcfnt.h
    include/xxfclib/formats/nintendo_3dsx/xx_nintendo_3dsx.h
    include/xxfclib/formats/sony_tim2/xx_sony_tim2.h
    include/xxfclib/formats/sony_pamf/xx_sony_pamf.h
    include/xxfclib/formats/sega_gvr/xx_sega_gvr.h
    include/xxfclib/formats/microsoft_xwb/xx_microsoft_xwb.h
    include/xxfclib/formats/microsoft_xsb/xx_microsoft_xsb.h
    include/xxfclib/formats/relic_sga/xx_relic_sga.h
    include/xxfclib/formats/xpm/xx_xpm.h
    include/xxfclib/formats/pcx/xx_pcx.h
    include/xxfclib/formats/iff_ilbm/xx_iff_ilbm.h
    include/xxfclib/formats/utah_rle/xx_utah_rle.h
    include/xxfclib/formats/radiance_hdr/xx_radiance_hdr.h
    include/xxfclib/formats/dpx/xx_dpx.h
    include/xxfclib/formats/cineon/xx_cineon.h
    include/xxfclib/formats/xwd/xx_xwd.h
    include/xxfclib/formats/sgi_rgb/xx_sgi_rgb.h
    include/xxfclib/formats/aseprite/xx_aseprite.h
    include/xxfclib/formats/numpy_npy/xx_numpy_npy.h
    include/xxfclib/formats/matlab_mat5/xx_matlab_mat5.h
    include/xxfclib/formats/netcdf_classic/xx_netcdf_classic.h
    include/xxfclib/formats/hdf4/xx_hdf4.h
    include/xxfclib/formats/dbase_dbf/xx_dbase_dbf.h
    include/xxfclib/formats/sas_xport/xx_sas_xport.h
    include/xxfclib/formats/spss_sav/xx_spss_sav.h
    include/xxfclib/formats/stata_dta/xx_stata_dta.h
    include/xxfclib/formats/apache_arrow_file/xx_apache_arrow_file.h
    include/xxfclib/formats/apache_parquet/xx_apache_parquet.h
    include/xxfclib/formats/makeself/xx_makeself.h
    include/xxfclib/formats/sun_java_binsh/xx_sun_java_binsh.h
    include/xxfclib/formats/installanywhere_unix/xx_installanywhere_unix.h
    include/xxfclib/formats/sfx_packagefortheweb/xx_sfx_packagefortheweb.h
    include/xxfclib/formats/sfx_spis/xx_sfx_spis.h
    include/xxfclib/formats/sfx_lha/xx_sfx_lha.h
    include/xxfclib/formats/lmd_container/xx_lmd_container.h
    include/xxfclib/formats/totalannihilation_hpi/xx_totalannihilation_hpi.h
    include/xxfclib/formats/ravensoft_rff/xx_ravensoft_rff.h
    include/xxfclib/formats/terminalreality_pod/xx_terminalreality_pod.h
    include/xxfclib/formats/volition_vpp/xx_volition_vpp.h
    include/xxfclib/formats/kirikiri_xp3/xx_kirikiri_xp3.h
    include/xxfclib/formats/fromsoftware_binder/xx_fromsoftware_binder.h
    include/xxfclib/formats/mythic_myp/xx_mythic_myp.h
    include/xxfclib/formats/lithtech_rez/xx_lithtech_rez.h
    include/xxfclib/formats/nintendo_ncch/xx_nintendo_ncch.h
    include/xxfclib/formats/nintendo_ncsd/xx_nintendo_ncsd.h
    include/xxfclib/formats/nintendo_cia/xx_nintendo_cia.h
    include/xxfclib/formats/nintendo_nds/xx_nintendo_nds.h
    include/xxfclib/formats/nintendo_gcm/xx_nintendo_gcm.h
    include/xxfclib/formats/nintendo_tpl/xx_nintendo_tpl.h
    include/xxfclib/formats/sony_tim/xx_sony_tim.h
    include/xxfclib/formats/sony_vag/xx_sony_vag.h
    include/xxfclib/formats/larian_lspk/xx_larian_lspk.h
    include/xxfclib/formats/larian_lsf/xx_larian_lsf.h
    include/xxfclib/formats/valve_hpak/xx_valve_hpak.h
    include/xxfclib/formats/renpy_rpa/xx_renpy_rpa.h
    include/xxfclib/formats/unreal_package/xx_unreal_package.h
    include/xxfclib/formats/sega_pvr2/xx_sega_pvr2.h
    include/xxfclib/formats/nintendo_bntx/xx_nintendo_bntx.h
    include/xxfclib/formats/icns/xx_icns.h
    include/xxfclib/formats/xcursor/xx_xcursor.h
    include/xxfclib/formats/icc/xx_icc.h
    include/xxfclib/formats/qoi/xx_qoi.h
    include/xxfclib/formats/farbfeld/xx_farbfeld.h
    include/xxfclib/formats/pnm/xx_pnm.h
    include/xxfclib/formats/tga/xx_tga.h
    include/xxfclib/formats/sun_raster/xx_sun_raster.h
    include/xxfclib/formats/fits/xx_fits.h
    include/xxfclib/formats/dicom/xx_dicom.h
    include/xxfclib/formats/pcap/xx_pcap.h
    include/xxfclib/formats/btsnoop/xx_btsnoop.h
    include/xxfclib/formats/java_class/xx_java_class.h
    include/xxfclib/formats/sfnt_collection/xx_sfnt_collection.h
    include/xxfclib/formats/sqlite3/xx_sqlite3.h
    include/xxfclib/formats/sqlite_wal/xx_sqlite_wal.h
    include/xxfclib/formats/avro_object/xx_avro_object.h
    include/xxfclib/formats/glb/xx_glb.h
    include/xxfclib/formats/spirv/xx_spirv.h
    include/xxfclib/formats/crx/xx_crx.h
    include/xxfclib/formats/bethesda_bsa/xx_bethesda_bsa.h
    include/xxfclib/formats/bethesda_ba2/xx_bethesda_ba2.h
    include/xxfclib/formats/unityfs/xx_unityfs.h
    include/xxfclib/formats/bioware_biff/xx_bioware_biff.h
    include/xxfclib/formats/bioware_erf/xx_bioware_erf.h
    include/xxfclib/formats/bioware_rim/xx_bioware_rim.h
    include/xxfclib/formats/lucas_lab/xx_lucas_lab.h
    include/xxfclib/formats/lucas_bun/xx_lucas_bun.h
    include/xxfclib/formats/idtech_bsp/xx_idtech_bsp.h
    include/xxfclib/formats/valve_bsp/xx_valve_bsp.h
    include/xxfclib/formats/idtech_md2/xx_idtech_md2.h
    include/xxfclib/formats/idtech_md3/xx_idtech_md3.h
    include/xxfclib/formats/idtech_qvm/xx_idtech_qvm.h
    include/xxfclib/formats/mohawk_mhk/xx_mohawk_mhk.h
    include/xxfclib/formats/quake_sprite/xx_quake_sprite.h
    include/xxfclib/formats/nintendo_narc/xx_nintendo_narc.h
    include/xxfclib/formats/nintendo_sarc/xx_nintendo_sarc.h
    include/xxfclib/formats/nintendo_pfs0/xx_nintendo_pfs0.h
    include/xxfclib/formats/nintendo_hfs0/xx_nintendo_hfs0.h
    include/xxfclib/formats/nintendo_brres/xx_nintendo_brres.h
    include/xxfclib/formats/nintendo_bcstm/xx_nintendo_bcstm.h
    include/xxfclib/formats/nintendo_bfsar/xx_nintendo_bfsar.h
    include/xxfclib/formats/nintendo_bcsar/xx_nintendo_bcsar.h
    include/xxfclib/formats/sony_psarc/xx_sony_psarc.h
    include/xxfclib/formats/ktx/xx_ktx.h
    include/xxfclib/formats/ktx2/xx_ktx2.h
    include/xxfclib/formats/dds/xx_dds.h
    include/xxfclib/formats/pvr/xx_pvr.h
    include/xxfclib/formats/valve_vtf/xx_valve_vtf.h
    include/xxfclib/formats/xbox_xbe/xx_xbox_xbe.h
    include/xxfclib/formats/flac/xx_flac.h
    include/xxfclib/formats/ogg/xx_ogg.h
    include/xxfclib/formats/mp4/xx_mp4.h
    include/xxfclib/formats/matroska/xx_matroska.h
    include/xxfclib/formats/aiff/xx_aiff.h
    include/xxfclib/formats/caf/xx_caf.h
    include/xxfclib/formats/photoshop_psd/xx_photoshop_psd.h
    include/xxfclib/formats/tiff/xx_tiff.h
    include/xxfclib/formats/openexr/xx_openexr.h
    include/xxfclib/formats/jpeg2000_jp2/xx_jpeg2000_jp2.h
    include/xxfclib/formats/android_vendor_boot/xx_android_vendor_boot.h
    include/xxfclib/formats/android_dtbo/xx_android_dtbo.h
    include/xxfclib/formats/android_vbmeta/xx_android_vbmeta.h
    include/xxfclib/formats/espressif_image/xx_espressif_image.h
    include/xxfclib/formats/wasm/xx_wasm.h
    include/xxfclib/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.h
    include/xxfclib/formats/dotnet_metadata/xx_dotnet_metadata.h
    include/xxfclib/formats/sfnt/xx_sfnt.h
    include/xxfclib/formats/woff/xx_woff.h
    include/xxfclib/formats/woff2/xx_woff2.h
    include/xxfclib/formats/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.h
    include/xxfclib/formats/adam/xx_adam.h
    include/xxfclib/formats/base16/xx_base16.h
    include/xxfclib/formats/bondwell_2_disk/xx_bondwell_2_disk.h
    include/xxfclib/formats/casio_fz_1_disk/xx_casio_fz_1_disk.h
    include/xxfclib/formats/isz/xx_isz.h
    include/xxfclib/formats/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.h
    include/xxfclib/formats/parallels_hdd/xx_parallels_hdd.h
    include/xxfclib/formats/pc_magazine_flp/xx_pc_magazine_flp.h
    include/xxfclib/formats/pchrom/xx_pchrom.h
    include/xxfclib/formats/pem/xx_pem.h
    include/xxfclib/formats/prodos/xx_prodos.h
    include/xxfclib/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk.h
    include/xxfclib/formats/rawcd/xx_rawcd.h
    include/xxfclib/formats/rsdos_fs/xx_rsdos_fs.h
    include/xxfclib/formats/sar_ns/xx_sar_ns.h
    include/xxfclib/formats/swf/xx_swf.h
    include/xxfclib/formats/t64/xx_t64.h
    include/xxfclib/formats/uue/xx_uue.h
    include/xxfclib/formats/vdi/xx_vdi.h
    include/xxfclib/formats/bmp/xx_bmp.h
    include/xxfclib/formats/cfe/xx_cfe.h
    include/xxfclib/formats/dxbc/xx_dxbc.h
    include/xxfclib/formats/gif/xx_gif.h
    include/xxfclib/formats/jpeg/xx_jpeg.h
    include/xxfclib/formats/linuxarm64/xx_linuxarm64.h
    include/xxfclib/formats/linuxboot/xx_linuxboot.h
    include/xxfclib/formats/linuxzimage/xx_linuxzimage.h
    include/xxfclib/formats/pcapng/xx_pcapng.h
    include/xxfclib/formats/pjl/xx_pjl.h
    include/xxfclib/formats/png/xx_png.h
    include/xxfclib/formats/riff/xx_riff.h
    include/xxfclib/formats/svg/xx_svg.h
    include/xxfclib/formats/quake_pak/xx_quake_pak.h
    include/xxfclib/formats/doom_wad/xx_doom_wad.h
    include/xxfclib/formats/quake_wad2/xx_quake_wad2.h
    include/xxfclib/formats/halflife_wad3/xx_halflife_wad3.h
    include/xxfclib/formats/build_grp/xx_build_grp.h
    include/xxfclib/formats/cri_afs/xx_cri_afs.h
    include/xxfclib/formats/cri_awb/xx_cri_awb.h
    include/xxfclib/formats/valve_vpk/xx_valve_vpk.h
    include/xxfclib/formats/nintendo_u8/xx_nintendo_u8.h
    include/xxfclib/formats/nintendo_rarc/xx_nintendo_rarc.h
    include/xxfclib/formats/android_ab/xx_android_ab.h
    include/xxfclib/formats/nes_rom/xx_nes_rom.h
    include/xxfclib/formats/lynx_lnx/xx_lynx_lnx.h
    include/xxfclib/formats/commodore_crt/xx_commodore_crt.h
    include/xxfclib/formats/uf2/xx_uf2.h
    include/xxfclib/formats/ico/xx_ico.h
    include/xxfclib/formats/midi/xx_midi.h
    include/xxfclib/formats/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.h
    include/xxfclib/formats/ardi_installer/xx_ardi_installer.h
    include/xxfclib/formats/arni_installer_container/xx_arni_installer_container.h
    include/xxfclib/formats/ej_technologies_install/xx_ej_technologies_install.h
    include/xxfclib/formats/finstall/xx_finstall.h
    include/xxfclib/formats/ghost_installer/xx_ghost_installer.h
    include/xxfclib/formats/ibm_zpak_installer/xx_ibm_zpak_installer.h
    include/xxfclib/formats/ifah_installer/xx_ifah_installer.h
    include/xxfclib/formats/inno_setup/xx_inno_setup.h
    include/xxfclib/formats/installer_vise_windows/xx_installer_vise_windows.h
    include/xxfclib/formats/installshield_12_setup/xx_installshield_12_setup.h
    include/xxfclib/formats/installshield_3/xx_installshield_3.h
    include/xxfclib/formats/installshield_7_setup/xx_installshield_7_setup.h
    include/xxfclib/formats/installshield_7_setup2/xx_installshield_7_setup2.h
    include/xxfclib/formats/installshield_developer/xx_installshield_developer.h
    include/xxfclib/formats/installshield_issetupstream/xx_installshield_issetupstream.h
    include/xxfclib/formats/installshield_multiplatform/xx_installshield_multiplatform.h
    include/xxfclib/formats/installshield_skin/xx_installshield_skin.h
    include/xxfclib/formats/microfox_put/xx_microfox_put.h
    include/xxfclib/formats/o_setup/xx_o_setup.h
    include/xxfclib/formats/pc_install_setup/xx_pc_install_setup.h
    include/xxfclib/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable.h
    include/xxfclib/formats/qsetup_installation_suite/xx_qsetup_installation_suite.h
    include/xxfclib/formats/rtpatch_setup_data/xx_rtpatch_setup_data.h
    include/xxfclib/formats/setup_factory/xx_setup_factory.h
    include/xxfclib/formats/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.h
    include/xxfclib/formats/spoon_installer/xx_spoon_installer.h
    include/xxfclib/formats/tarma_installer/xx_tarma_installer.h
    include/xxfclib/formats/adf/xx_adf.h
    include/xxfclib/formats/apm/xx_apm.h
    include/xxfclib/formats/vhdx/xx_vhdx.h
    include/xxfclib/formats/base64/xx_base64.h
    include/xxfclib/formats/btoa/xx_btoa.h
    include/xxfclib/formats/chd/xx_chd.h
    include/xxfclib/formats/chm/xx_chm.h
    include/xxfclib/formats/cloop/xx_cloop.h
    include/xxfclib/formats/cue/xx_cue.h
    include/xxfclib/formats/dahuazip/xx_dahuazip.h
    include/xxfclib/formats/dmsfw/xx_dmsfw.h
    include/xxfclib/formats/ewf/xx_ewf.h
    include/xxfclib/formats/godot_engine_pck/xx_godot_engine_pck.h
    include/xxfclib/formats/gpgsigned/xx_gpgsigned.h
    include/xxfclib/formats/ihex/xx_ihex.h
    include/xxfclib/formats/kwaj/xx_kwaj.h
    include/xxfclib/formats/lbr/xx_lbr.h
    include/xxfclib/formats/lzfsestream/xx_lzfsestream.h
    include/xxfclib/formats/nrg/xx_nrg.h
    include/xxfclib/formats/packit_mac/xx_packit_mac.h
    include/xxfclib/formats/rpm/xx_rpm.h
    include/xxfclib/formats/stuffit5/xx_stuffit5.h
    include/xxfclib/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.h
    include/xxfclib/formats/x68000_dim/xx_x68000_dim.h
    include/xxfclib/formats/visionaire_studio_vis/xx_visionaire_studio_vis.h
    include/xxfclib/formats/uharc/xx_uharc.h
    include/xxfclib/formats/trs_80_jv3/xx_trs_80_jv3.h
    include/xxfclib/formats/trs_80_jv1/xx_trs_80_jv1.h
    include/xxfclib/formats/t98_next_nfd/xx_t98_next_nfd.h
    include/xxfclib/formats/stuffit_split_file/xx_stuffit_split_file.h
    include/xxfclib/formats/rdb/xx_rdb.h
    include/xxfclib/formats/qnap_nas_firmware/xx_qnap_nas_firmware.h
    include/xxfclib/formats/qcow1/xx_qcow1.h
    include/xxfclib/formats/nsa/xx_nsa.h
    include/xxfclib/formats/ns2/xx_ns2.h
    include/xxfclib/formats/nec_pc_98_fdi/xx_nec_pc_98_fdi.h
    include/xxfclib/formats/ms_dos_backup/xx_ms_dos_backup.h
    include/xxfclib/formats/hxc_stream_hfe/xx_hxc_stream_hfe.h
    include/xxfclib/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.h
    include/xxfclib/formats/apple_sparse_bundle/xx_apple_sparse_bundle.h
    include/xxfclib/formats/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.h
    include/xxfclib/formats/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.h
)

set(XXFCLIB_SOURCES
    src/formats/mozilla_mar/xx_mozilla_mar.c
    src/formats/mozilla_mar/xx_mozilla_mar_extractor.c
    src/formats/westwood_pak/xx_westwood_pak.c
    src/formats/westwood_pak/xx_westwood_pak_extractor.c
    src/formats/fatx/xx_fatx.c
    src/formats/fatx/xx_fatx_extractor.c
    src/formats/soundfont2/xx_soundfont2.c
    src/formats/soundfont2/xx_soundfont2_extractor.c
    src/formats/ivf/xx_ivf.c
    src/formats/ivf/xx_ivf_extractor.c
    src/formats/windows_ani/xx_windows_ani.c
    src/formats/windows_ani/xx_windows_ani_extractor.c
    src/formats/interplay_acm/xx_interplay_acm.c
    src/formats/interplay_acm/xx_interplay_acm_extractor.c
    src/formats/cri_ahx/xx_cri_ahx.c
    src/formats/cri_ahx/xx_cri_ahx_extractor.c
    src/formats/adobe_director_cxt/xx_adobe_director_cxt.c
    src/formats/adobe_director_cxt/xx_adobe_director_cxt_extractor.c
    src/formats/olympus_dss/xx_olympus_dss.c
    src/formats/olympus_dss/xx_olympus_dss_extractor.c
    src/formats/ea_exa/xx_ea_exa.c
    src/formats/ea_exa/xx_ea_exa_extractor.c
    src/formats/audio_nitro_strm/xx_audio_nitro_strm.c
    src/formats/audio_nitro_strm/xx_audio_nitro_strm_extractor.c
    src/formats/audio_wwise_wem/xx_audio_wwise_wem.c
    src/formats/audio_wwise_wem/xx_audio_wwise_wem_extractor.c
    src/formats/audio_scumm_sou/xx_audio_scumm_sou.c
    src/formats/audio_scumm_sou/xx_audio_scumm_sou_extractor.c
    src/formats/audio_riff_ima/xx_audio_riff_ima.c
    src/formats/audio_riff_ima/xx_audio_riff_ima_extractor.c
    src/formats/hmi_midi/xx_hmi_midi.c
    src/formats/hmi_midi/xx_hmi_midi_extractor.c
    src/formats/ensoniq_paf/xx_ensoniq_paf.c
    src/formats/ensoniq_paf/xx_ensoniq_paf_extractor.c
    src/formats/abylight_strm/xx_abylight_strm.c
    src/formats/abylight_strm/xx_abylight_strm_extractor.c
    src/formats/lego_alp/xx_lego_alp.c
    src/formats/lego_alp/xx_lego_alp_extractor.c
    src/formats/audio_pvf/xx_audio_pvf.c
    src/formats/audio_pvf/xx_audio_pvf_extractor.c
    src/formats/audio_rifx_wave/xx_audio_rifx_wave.c
    src/formats/audio_rifx_wave/xx_audio_rifx_wave_extractor.c
    src/formats/audio_shockwave_swa/xx_audio_shockwave_swa.c
    src/formats/audio_shockwave_swa/xx_audio_shockwave_swa_extractor.c
    src/formats/die_music/xx_die_music.c
    src/formats/acorn_adfs/xx_acorn_adfs.c
    src/formats/aaruformat/xx_aaruformat.c
    src/formats/gdi/xx_gdi.c
    src/formats/mds/xx_mds.c
    src/formats/cbm_d64/xx_cbm_d64.c
    src/formats/cbm_d71/xx_cbm_d71.c
    src/formats/cbm_d81/xx_cbm_d81.c
    src/formats/cbm_d8x/xx_cbm_d8x.c
    src/formats/cbm_d67/xx_cbm_d67.c
    src/formats/cbm_d90/xx_cbm_d90.c
    src/formats/libdsk_extra/xx_libdsk_extra.c
    src/formats/simh_disk/xx_simh_disk.c
    src/formats/snatchit_cp2/xx_snatchit_cp2.c
    src/formats/northstar_nsi/xx_northstar_nsi.c
    src/formats/thomson_fd/xx_thomson_fd.c
    src/formats/cmd_fd/xx_cmd_fd.c
    src/formats/pce_pri/xx_pce_pri.c
    src/formats/pce_pfi/xx_pce_pfi.c
    src/formats/pce_pfdc/xx_pce_pfdc.c
    src/formats/pce_pbi/xx_pce_pbi.c
    src/formats/pce_pbit/xx_pce_pbit.c
    src/formats/pce_tc/xx_pce_tc.c
    src/formats/pce_anadisk/xx_pce_anadisk.c
    src/formats/pce_xdf/xx_pce_xdf.c
    src/formats/os9_rbf/xx_os9_rbf.c
    src/formats/ldbs/xx_ldbs.c
    src/formats/ldbst/xx_ldbst.c
    src/formats/ccd/xx_ccd.c
    src/formats/cdrdao_toc/xx_cdrdao_toc.c
    src/formats/diskcopy42/xx_diskcopy42.c
    src/formats/acorn_dfs/xx_acorn_dfs.c
    src/formats/fdcopy_cfi/xx_fdcopy_cfi.c
    src/formats/atari_dos2/xx_atari_dos2.c
    src/formats/apridisk/xx_apridisk.c
    src/formats/ti99_dsk/xx_ti99_dsk.c
    src/formats/apple_dos32/xx_apple_dos32.c
    src/formats/apple_dos33_32/xx_apple_dos33_32.c
    src/formats/bytekiller/xx_bytekiller.c
    src/formats/partimage/xx_partimage.c
    src/formats/qlie_pack/xx_qlie_pack.c
    src/formats/hfsplus/xx_hfsplus.c
    src/formats/hfsplus/xx_hfsplus_unicode.h
    src/formats/hfsplus/xx_hfsplus_hostfold.h
    src/formats/xva/xx_xva.c
    src/formats/hfs/xx_hfs.c
    src/formats/nexas_pac/xx_nexas_pac.c
    src/formats/nitroplus_npa/xx_nitroplus_npa.c
    src/formats/cpm/xx_cpm.c
    src/formats/cpm/xx_cpm_presets.c
    src/formats/ufs1/xx_ufs1.c
    src/formats/apple_dos33/xx_apple_dos33.c
    src/formats/malie_lib/xx_malie_lib.c
    src/formats/dmk/xx_dmk.c
    src/formats/mfs/xx_mfs.c
    src/formats/catsystem_kif/xx_catsystem_kif.c
    src/formats/catsystem_kif/xx_kif_crypto_private.h
    src/formats/apple_pascal/xx_apple_pascal.c
    src/formats/nitroplus_npk2/xx_nitroplus_npk2.c
    src/formats/thomson_sap/xx_thomson_sap.c
    src/formats/anex86_hdi/xx_anex86_hdi.c
    src/formats/exfat/xx_exfat.c
    src/formats/majiro/xx_majiro.c
    src/formats/wux/xx_wux.c
    src/formats/wux/xx_disk_containers_native.h
    src/formats/sdi/xx_sdi.c
    src/formats/nhd/xx_nhd.c
    src/formats/virtual98/xx_virtual98.c
    src/algo/bzip2/xx_bzip2_random.h
    src/formats/lpak/xx_lpak.c
    src/formats/freeze/xx_freeze.c
    src/formats/freeze/xx_freeze_codec.c
    src/formats/bzip1/xx_bzip1.c
    src/formats/bzip1/xx_bzip1_codec.c
    src/formats/bsn/xx_bsn_codec.c
    src/formats/ppmd/xx_ppmd_varh.c
    src/formats/ppmd/xx_ppmd_vari.c
    src/formats/xx_format_additional.c
    src/formats/acorn_atom_disk/xx_acorn_atom_disk.c
    src/formats/apollo_afd/xx_apollo_afd.c
    src/formats/bga/xx_bga.c
    src/formats/bgi/xx_bgi.c
    src/formats/bgi2/xx_bgi2.c
    src/formats/binscii/xx_binscii.c
    src/formats/blindwrite_5_6_image/xx_blindwrite_5_6_image.c
    src/formats/btrfs_stream/xx_btrfs_stream.c
    src/formats/camputers_lynx_ldf/xx_camputers_lynx_ldf.c
    src/formats/cpk/xx_cpk.c
    src/formats/crt/xx_crt.c
    src/formats/d_link_alpha_encimg_v2/xx_d_link_alpha_encimg_v2.c
    src/formats/d_link_fpkg_cpkg/xx_d_link_fpkg_cpkg.c
    src/formats/daemon_tools_mdx/xx_daemon_tools_mdx.c
    src/formats/dart/xx_dart.c
    src/formats/ddd/xx_ddd.c
    src/formats/diet_compression/xx_diet_compression.c
    src/formats/dxa/xx_dxa.c
    src/formats/ea_fsh/xx_ea_fsh.c
    src/formats/ewf2_ex01/xx_ewf2_ex01.c
    src/formats/ewf2_lx01/xx_ewf2_lx01.c
    src/formats/ewf_l01/xx_ewf_l01.c
    src/formats/fmod_sample_bank/xx_fmod_sample_bank.c
    src/formats/gbi/xx_gbi.c
    src/formats/goldsrc_bsp/xx_goldsrc_bsp.c
    src/formats/hsf/xx_hsf.c
    src/formats/htc_nbh_rom_image/xx_htc_nbh_rom_image.c
    src/formats/hxc_hfe_extended/xx_hxc_hfe_extended.c
    src/formats/hxc_hfe_hddd_a2_variant/xx_hxc_hfe_hddd_a2_variant.c
    src/formats/hxc_hfe_v3/xx_hxc_hfe_v3.c
    src/formats/hxs/xx_hxs.c
    src/formats/jffs2_old/xx_jffs2_old.c
    src/formats/jvc/xx_jvc.c
    src/formats/kgb_archiver/xx_kgb_archiver.c
    src/formats/kryoflux_stream/xx_kryoflux_stream.c
    src/formats/livemaker/xx_livemaker.c
    src/formats/lzma86/xx_lzma86.c
    src/formats/maxis_far_archive/xx_maxis_far_archive.c
    src/formats/mgt/xx_mgt.c
    src/formats/minix/xx_minix.c
    src/formats/moof/xx_moof.c
    src/formats/ms_dos_backup2/xx_ms_dos_backup2.c
    src/formats/mub/xx_mub.c
    src/formats/noa/xx_noa.c
    src/formats/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.c
    src/formats/partclone_image/xx_partclone_image.c
    src/formats/ppmd/xx_ppmd.c
    src/formats/quoted_printable_encoded_fil/xx_quoted_printable_encoded_fil.c
    src/formats/risc_os_sprite/xx_risc_os_sprite.c
    src/formats/rpa/xx_rpa.c
    src/formats/rpg_maker_rgssad/xx_rpg_maker_rgssad.c
    src/formats/sfark_compressed_soundfont/xx_sfark_compressed_soundfont.c
    src/formats/sis/xx_sis.c
    src/formats/spectrum_udi/xx_spectrum_udi.c
    src/formats/squashfs_sqlz/xx_squashfs_sqlz.c
    src/formats/stos_memory_bank/xx_stos_memory_bank.c
    src/formats/stuffitx/xx_stuffitx.c
    src/formats/sufs/xx_sufs.c
    src/formats/sunvtoc/xx_sunvtoc.c
    src/formats/telltale_ttarch/xx_telltale_ttarch.c
    src/formats/ufs2/xx_ufs2.c
    src/formats/uif/xx_uif.c
    src/formats/valve_gcf_cache/xx_valve_gcf_cache.c
    src/formats/valve_xzp/xx_valve_xzp.c
    src/formats/vmdk_cowd_sparse/xx_vmdk_cowd_sparse.c
    src/formats/vmdk_sesparse/xx_vmdk_sesparse.c
    src/formats/xiaomi_hdr1/xx_xiaomi_hdr1.c
    src/formats/xiaomi_hdr2/xx_xiaomi_hdr2.c
    src/formats/xp3/xx_xp3.c
    src/formats/xpk_compressed_file/xx_xpk_compressed_file.c
    src/formats/xpk_compressed_file/xx_xpk_nuke_native.h
    src/formats/xpk_compressed_file/xx_xpk_lin_native.h
    src/formats/xpk_compressed_file/xx_xpk_lz_native.h
    src/formats/xpk_compressed_file/xx_xpk_zeno_native.h
    src/formats/xpk_compressed_file/xx_xpk_rake_native.h
    src/formats/xpk_compressed_file/xx_xpk_tdcs_native.h
    src/formats/xpk_compressed_file/xx_xpk_cyber_native.h
    src/formats/xpk_compressed_file/xx_xpk_rake_table.h
    src/formats/yenc_encoded_file/xx_yenc_encoded_file.c
    src/formats/ypf/xx_ypf.c

    # CRT compatibility core. Each group keeps its portable half beside the
    # subsystem it serves and its platform half in that subsystem's platforms/
    # directory: process, I/O and formatting in src/rt, runtime memory
    # primitives in src/memory, and narrow/wide string primitives in
    # src/strings. On Windows the platform halves are Win32-backed or
    # hand-rolled, which lets the /NODEFAULTLIB target link without a CRT.
    src/rt/xx_rt.c
    src/rt/xx_rt_fp.c
    src/rt/xx_rt_math.c
    src/buf/xx_buf.c
    src/json/xx_json.c
    src/xml/xx_xml.c
    src/fs/xx_fs.c
    # Common callback interface for scanning engines.
    src/scan/xx_scan.c
    src/scan/xx_scan_types.c
    src/scan/xx_scan_overlay.c
    # Embedded ECMAScript interpreter.
    src/js/xx_js_deps.c
    src/js/xx_js_engine.c
    src/js/xx_js_lex.c
    src/js/xx_js_parse.c
    src/js/xx_js_interp.c
    src/js/xx_js_value.c
    src/js/xx_js_builtins.c
    src/js/xx_js_regexp.c
    src/js/xx_js_bytecode.c
    # Detect It Easy scan engine, moved here from cdie. The signature database,
    # the script API bound onto the JavaScript interpreter above, and the scan
    # driver. xx_die_engine_compat.* carries the handful of cdie runtime
    # primitives whose semantics differ from their xx_* counterparts; the rest
    # alias straight onto xx_rt.
    src/die_engine/xx_die_engine_compat.c
    src/die_engine/xx_die_engine_db.c
    src/die_engine/xx_die_engine_scan.c
    src/die_engine/xx_die_engine_result.c
    src/die_engine/xx_die_engine_api.c
    # The file under scan. What used to sit beside it here -- a second
    # binary-access layer, memory map, signature notation and matcher -- is
    # gone: xx_io, xx_data, xx_data_signature, xx_memory_map, xx_crc,
    # xx_adler32 and xx_hash do that work now.
    src/die_engine/xx_die_engine_bin.c
    src/die_engine/xx_die_engine_literal_search.c
    # Its binary-format parsers. These answer predicate queries for signature
    # scripts ("is this import present?"), which is a different job from the
    # readers in src/formats/ -- those enumerate structure for extraction and
    # display. The two are not interchangeable; see docs.
    src/die_engine/xx_die_engine_xdisasm.c
    src/die_engine/xx_die_engine_inflate.c
    src/formats/pe/xx_pe_inspect.c
    src/formats/pe/xx_pe_stream_symbols.c
    src/formats/pe/xx_pe_stream_coff.c
    src/formats/pe/xx_pe_stream_metadata.c
    src/formats/pe/xx_pe_stream_common.c
    src/formats/dotnet/xx_dotnet.c
    src/formats/dotnet/xx_dotnet_inspect.c
    src/formats/dotnet/xx_dotnet_data.c
    src/formats/dotnet/xx_dotnet_extractor.c
    src/formats/apk/xx_apk_inspect.c
    src/formats/pdf/xxpdf.c
    src/formats/pdf/xxpdf_decode.c
    src/formats/pdf/xxpdf_extractor.c
    src/formats/elf/xx_elf_inspect.c
    src/formats/dex/xx_dex_inspect.c
    src/formats/macho/xx_macho_inspect.c
    src/formats/pyc/xx_pyc_extractor.c
    src/formats/pyc/xx_pyc.c
    src/formats/zip/xx_zip_inspect.c
    src/formats/xx_data_signature.c
    src/global/xx_global.c
    src/global/xx_settings_global.c
    src/terminal/xx_terminal.c
    src/settings/xx_settings.c
    src/settings/xx_settings_ini.c
    src/settings/xx_settings_value.c
    src/io/xx_io.c
    src/io/xx_io_file.c
    src/io/xx_io_mem.c
    src/io/xx_io_memory_only.c
    src/io/xx_io_sub.c
    src/io/xx_io_process.c
    src/memory/xx_memory.c
    src/memory/xx_memory_rt.c
    src/memory/platforms/xx_memory_sse2.c
    src/memory/platforms/xx_memory_avx2.c
    src/strings/xx_string.c
    src/data/xx_pd.c
    src/data/xx_data_raw.c
    src/data/xx_data_io.c
    src/global/platforms/xx_global_cpu.c
    src/data/platforms/xx_data_sse2.c
    src/data/platforms/xx_data_avx2.c
    src/list/xx_list.c
    src/var/xx_var.c
    src/algo/crc/xx_crc.c
    src/algo/hash/xx_hash.c
    src/algo/sha/xx_sha.c
    src/algo/sha/xx_sha_simd.c
    src/algo/adler32/xx_adler32.c
    src/algo/adler32/platforms/xx_adler32_sse2.c
    src/algo/adler32/platforms/xx_adler32_avx2.c
    src/algo/lzfse/xx_lzfse.c
    src/algo/crc/xx_crc8.c
    src/algo/crc/xx_crc16.c
    src/algo/crc/xx_crc32.c
    src/algo/crc/xx_crc64.c
    src/algo/crc/platforms/xx_crc64_sse2.c
    src/algo/crc/platforms/xx_crc64_avx2.c
    src/algo/blake2/xx_blake2.c
    src/algo/entropy/xx_entropy.c
    src/algo/entropy/platforms/xx_entropy_sse2.c
    src/algo/entropy/platforms/xx_entropy_avx2.c
    src/algo/kpa/xx_kpa.c
    src/algo/store/xx_store.c
    src/algo/deflate/xx_deflate.c
    src/algo/deflate/xx_zlib_stream.c
    src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/deflate/platforms/xx_deflate_sse2.c
    src/algo/deflate/platforms/xx_deflate_avx2.c
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
    src/algo/aes/xx_aes_internal.h
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
    src/formats/xx_format_streams.c
    src/formats/xx_format_catalog.c
    src/formats/xx_format_detect_fast.c
    src/formats/xx_format_extractor.c
    src/formats/xx_memory_map.c
    src/formats/zip/xx_zip.c
    src/formats/zip/xx_zip_legacy_encoder.c
    src/formats/zip/xx_zip_extractor.c
    src/formats/jar/xx_jar.c
    src/formats/jar/xx_jar_extractor.c
    src/formats/apk/xx_apk.c
    src/formats/apk/xx_apk_extractor.c
    src/formats/ipa/xx_ipa.c
    src/formats/ipa/xx_ipa_extractor.c
    src/formats/7zip/xx_7zip.c
    src/formats/7zip/xx_7zip_extractor.c
    src/formats/7zip/xx_7zip_branch.c
    src/formats/7zip/xx_7zip_stream.c
    src/formats/7zip/xx_7zip_writer.c
    src/formats/rar/xx_rar.c
    src/formats/rar/xx_rar_extractor.c
    src/formats/ar/xx_ar.c
    src/formats/ar/xx_ar_extractor.c
    src/formats/bz2/xx_bz2.c
    src/formats/bz2/xx_bz2_extractor.c
    src/formats/gz/xx_gz.c
    src/formats/gz/xx_gz_extractor.c
    src/formats/xz/xx_xz.c
    src/formats/xz/xx_xz_riscv_native.h
    src/formats/xz/xx_xz_writer.c
    src/formats/xz/xx_xz_extractor.c
    src/formats/tar/xx_tar.c
    src/formats/tar/xx_tar_extractor.c
    src/formats/tar_common/xx_tar_common.c
    src/formats/tar_gz/xx_tar_gz.c
    src/formats/tar_gz/xx_tar_gz_extractor.c
    src/formats/npm/xx_npm.c
    src/formats/npm/xx_npm_extractor.c
    src/formats/tar_bz2/xx_tar_bz2.c
    src/formats/tar_bz2/xx_tar_bz2_extractor.c
    src/formats/tar_xz/xx_tar_xz.c
    src/formats/tar_xz/xx_tar_xz_extractor.c
    src/formats/tar_lz4/xx_tar_lz4.c
    src/formats/tar_lz4/xx_tar_lz4_extractor.c
    src/formats/lz4/xx_lz4.c
    src/formats/lz4/xx_lz4_extractor.c
    src/formats/lz5/xx_lz5.c
    src/formats/lz5/xx_lz5_extractor.c
    src/formats/lizard/xx_lizard.c
    src/formats/lizard/xx_lizard_extractor.c
    src/formats/brotli/xx_brotli.c
    src/formats/brotli/xx_brotli_extractor.c
    src/formats/unixpack/xx_unixpack.c
    src/formats/unixpack/xx_unixpack_extractor.c
    src/formats/zlib/xx_zlib.c
    src/formats/zlib/xx_zlib_extractor.c
    src/formats/unixcompress/xx_unixcompress.c
    src/formats/unixcompress/xx_unixcompress_extractor.c
    src/formats/gitobject/xx_gitobject.c
    src/formats/gitobject/xx_gitobject_extractor.c
    src/formats/mscompress/xx_mscompress.c
    src/formats/mscompress/xx_mscompress_extractor.c
    src/formats/ash0/xx_ash0.c
    src/formats/ash0/xx_ash0_extractor.c
    src/formats/wiilz77/xx_wiilz77.c
    src/formats/wiilz77/xx_wiilz77_extractor.c
    src/formats/lzv1/xx_lzv1.c
    src/formats/lzv1/xx_lzv1_extractor.c
    src/formats/oraclesqueeze/xx_oraclesqueeze.c
    src/formats/oraclesqueeze/xx_oraclesqueeze_extractor.c
    src/formats/softronics/xx_softronics.c
    src/formats/softronics/xx_softronics_extractor.c
    src/formats/logitechcompress/xx_logitechcompress.c
    src/formats/logitechcompress/xx_logitechcompress_extractor.c
    src/formats/dmapacked/xx_dmapacked.c
    src/formats/dmapacked/xx_dmapacked_extractor.c
    src/formats/gashuff/xx_gashuff.c
    src/formats/gashuff/xx_gashuff_extractor.c
    src/formats/huf/xx_huf.c
    src/formats/huf/xx_huf_extractor.c
    src/formats/lzdiet/xx_lzdiet.c
    src/formats/lzdiet/xx_lzdiet_extractor.c
    src/formats/lzpis2/xx_lzpis2.c
    src/formats/lzpis2/xx_lzpis2_extractor.c
    src/formats/zie/xx_zie.c
    src/formats/zie/xx_zie_extractor.c
    src/formats/xeditpack/xx_xeditpack.c
    src/formats/xeditpack/xx_xeditpack_extractor.c
    src/formats/wpk/xx_wpk.c
    src/formats/wpk/xx_wpk_extractor.c
    src/formats/wintersoft/xx_wintersoft.c
    src/formats/wintersoft/xx_wintersoft_extractor.c
    src/formats/vmarc/xx_vmarc.c
    src/formats/vmarc/xx_vmarc_extractor.c
    src/formats/tivoli/xx_tivoli.c
    src/formats/tivoli/xx_tivoli_extractor.c
    src/formats/ti99arc/xx_ti99arc.c
    src/formats/ti99arc/xx_ti99arc_extractor.c
    src/formats/stylus/xx_stylus.c
    src/formats/stylus/xx_stylus_extractor.c
    src/formats/rtpatch/xx_rtpatch.c
    src/formats/rtpatch/xx_rtpatch_extractor.c
    src/formats/rta/xx_rta.c
    src/formats/rta/xx_rta_extractor.c
    src/formats/rompaq/xx_rompaq.c
    src/formats/rompaq/xx_rompaq_extractor.c
    src/formats/rid/xx_rid.c
    src/formats/rid/xx_rid_extractor.c
    src/formats/qnxbase/xx_qnxbase.c
    src/formats/qnxbase/xx_qnxbase_extractor.c
    src/formats/qda/xx_qda.c
    src/formats/qda/xx_qda_extractor.c
    src/formats/pkt/xx_pkt.c
    src/formats/pkt/xx_pkt_extractor.c
    src/formats/lofi/xx_lofi.c
    src/formats/lofi/xx_lofi_extractor.c
    src/formats/lim/xx_lim.c
    src/formats/lim/xx_lim_extractor.c
    src/formats/kolibrikpack/xx_kolibrikpack.c
    src/formats/kolibrikpack/xx_kolibrikpack_extractor.c
    src/formats/ivt/xx_ivt.c
    src/formats/ivt/xx_ivt_extractor.c
    src/formats/irwinpac/xx_irwinpac.c
    src/formats/irwinpac/xx_irwinpac_extractor.c
    src/formats/hap/xx_hap.c
    src/formats/hap/xx_hap_extractor.c
    src/formats/compactpro/xx_compactpro.c
    src/formats/compactpro/xx_compactpro_extractor.c
    src/formats/imp/xx_imp.c
    src/formats/imp/xx_imp_extractor.c
    src/formats/sqx/xx_sqx.c
    src/formats/sqx/xx_sqx_extractor.c
    src/formats/zoo/xx_zoo.c
    src/formats/zoo/xx_zoo_extractor.c
    src/formats/terse/xx_terse.c
    src/formats/terse/xx_terse_extractor.c
    src/formats/stk/xx_stk.c
    src/formats/stk/xx_stk_extractor.c
    src/formats/pcommos2/xx_pcommos2.c
    src/formats/pcommos2/xx_pcommos2_extractor.c
    src/formats/jbf/xx_jbf.c
    src/formats/jbf/xx_jbf_extractor.c
    src/formats/ibmspack/xx_ibmspack.c
    src/formats/ibmspack/xx_ibmspack_extractor.c
    src/formats/gtu/xx_gtu.c
    src/formats/gtu/xx_gtu_extractor.c
    src/formats/glu/xx_glu.c
    src/formats/glu/xx_glu_extractor.c
    src/formats/ztc/xx_ztc.c
    src/formats/ztc/xx_ztc_extractor.c
    src/formats/netwarepacked/xx_netwarepacked.c
    src/formats/netwarepacked/xx_netwarepacked_extractor.c
    src/formats/zpak/xx_zpak.c
    src/formats/zpak/xx_zpak_extractor.c
    src/formats/zcmp/xx_zcmp.c
    src/formats/zcmp/xx_zcmp_extractor.c
    src/formats/scl/xx_scl.c
    src/formats/scl/xx_scl_extractor.c
    src/formats/pakleo/xx_pakleo.c
    src/formats/pakleo/xx_pakleo_extractor.c
    src/formats/npack/xx_npack.c
    src/formats/npack/xx_npack_extractor.c
    src/formats/mi10/xx_mi10.c
    src/formats/mi10/xx_mi10_extractor.c
    src/formats/lzwd/xx_lzwd.c
    src/formats/lzwd/xx_lzwd_extractor.c
    src/formats/lzhcxp/xx_lzhcxp.c
    src/formats/lzhcxp/xx_lzhcxp_extractor.c
    src/formats/kboom/xx_kboom.c
    src/formats/kboom/xx_kboom_extractor.c
    src/formats/hzl/xx_hzl.c
    src/formats/hzl/xx_hzl_extractor.c
    src/formats/ha/xx_ha.c
    src/formats/ha/xx_ha_extractor.c
    src/formats/genius/xx_genius.c
    src/formats/genius/xx_genius_extractor.c
    src/formats/fls/xx_fls.c
    src/formats/fls/xx_fls_extractor.c
    src/formats/earefpack/xx_earefpack.c
    src/formats/earefpack/xx_earefpack_extractor.c
    src/formats/ealib/xx_ealib.c
    src/formats/ealib/xx_ealib_extractor.c
    src/formats/elm/xx_elm.c
    src/formats/elm/xx_elm_extractor.c
    src/formats/ea/xx_ea.c
    src/formats/ea/xx_ea_extractor.c
    src/formats/diskdoubler/xx_diskdoubler.c
    src/formats/diskdoubler/xx_diskdoubler_extractor.c
    src/formats/cmp/xx_cmp.c
    src/formats/cmp/xx_cmp_extractor.c
    src/formats/clp/xx_clp.c
    src/formats/clp/xx_clp_extractor.c
    src/formats/chieflzmulti/xx_chieflzmulti.c
    src/formats/chieflzmulti/xx_chieflzmulti_extractor.c
    src/formats/chieflz/xx_chieflz.c
    src/formats/chieflz/xx_chieflz_extractor.c
    src/formats/bwcf/xx_bwcf.c
    src/formats/bwcf/xx_bwcf_extractor.c
    src/formats/asymetrix/xx_asymetrix.c
    src/formats/asymetrix/xx_asymetrix_extractor.c
    src/formats/seaarc/xx_seaarc.c
    src/formats/seaarc/xx_seaarc_extractor.c
    src/formats/amigalzx/xx_amigalzx.c
    src/formats/amigalzx/xx_amigalzx_extractor.c
    src/formats/spis/xx_spis.c
    src/formats/spis/xx_spis_extractor.c
    src/formats/lha/xx_lha.c
    src/formats/lha/xx_lha_extractor.c
    src/formats/xar/xx_xar.c
    src/formats/xar/xx_xar_extractor.c
    src/formats/fmc1/xx_fmc1.c
    src/formats/fmc1/xx_fmc1_extractor.c
    src/formats/pyz/xx_pyz.c
    src/formats/pyz/xx_pyz_extractor.c
    src/formats/ascendbackup/xx_ascendbackup.c
    src/formats/ascendbackup/xx_ascendbackup_extractor.c
    src/formats/stork/xx_stork.c
    src/formats/stork/xx_stork_extractor.c
    src/formats/zap/xx_zap.c
    src/formats/zap/xx_zap_extractor.c
    src/formats/bwf/xx_bwf.c
    src/formats/bwf/xx_bwf_extractor.c
    src/formats/qualitas/xx_qualitas.c
    src/formats/qualitas/xx_qualitas_extractor.c
    src/formats/jetbbs/xx_jetbbs.c
    src/formats/jetbbs/xx_jetbbs_extractor.c
    src/formats/ecmpacked/xx_ecmpacked.c
    src/formats/ecmpacked/xx_ecmpacked_extractor.c
    src/formats/borlandpack/xx_borlandpack.c
    src/formats/borlandpack/xx_borlandpack_extractor.c
    src/formats/jgpak/xx_jgpak.c
    src/formats/jgpak/xx_jgpak_extractor.c
    src/formats/zzz/xx_zzz.c
    src/formats/zzz/xx_zzz_extractor.c
    src/formats/zz/xx_zz.c
    src/formats/zz/xx_zz_extractor.c
    src/formats/zlwb/xx_zlwb.c
    src/formats/zlwb/xx_zlwb_extractor.c
    src/formats/trc/xx_trc.c
    src/formats/trc/xx_trc_extractor.c
    src/formats/tgcf/xx_tgcf.c
    src/formats/tgcf/xx_tgcf_extractor.c
    src/formats/swag/xx_swag.c
    src/formats/swag/xx_swag_extractor.c
    src/formats/riversoft/xx_riversoft.c
    src/formats/riversoft/xx_riversoft_extractor.c
    src/formats/rcf/xx_rcf.c
    src/formats/rcf/xx_rcf_extractor.c
    src/formats/quarterdeckqp/xx_quarterdeckqp.c
    src/formats/quarterdeckqp/xx_quarterdeckqp_extractor.c
    src/formats/qip1/xx_qip1.c
    src/formats/qip1/xx_qip1_extractor.c
    src/formats/powerarc/xx_powerarc.c
    src/formats/powerarc/xx_powerarc_extractor.c
    src/formats/povlablzh/xx_povlablzh.c
    src/formats/povlablzh/xx_povlablzh_extractor.c
    src/formats/mva/xx_mva.c
    src/formats/mva/xx_mva_extractor.c
    src/formats/miz/xx_miz.c
    src/formats/miz/xx_miz_extractor.c
    src/formats/lsz/xx_lsz.c
    src/formats/lsz/xx_lsz_extractor.c
    src/formats/jm93/xx_jm93.c
    src/formats/jm93/xx_jm93_extractor.c
    src/formats/inteduft/xx_inteduft.c
    src/formats/inteduft/xx_inteduft_extractor.c
    src/formats/igf2/xx_igf2.c
    src/formats/igf2/xx_igf2_extractor.c
    src/formats/igf1/xx_igf1.c
    src/formats/igf1/xx_igf1_extractor.c
    src/formats/ibmzpak/xx_ibmzpak.c
    src/formats/ibmzpak/xx_ibmzpak_extractor.c
    src/formats/fld/xx_fld.c
    src/formats/fld/xx_fld_extractor.c
    src/formats/fiz/xx_fiz.c
    src/formats/fiz/xx_fiz_extractor.c
    src/formats/dtpacked/xx_dtpacked.c
    src/formats/dtpacked/xx_dtpacked_extractor.c
    src/formats/dsl2/xx_dsl2.c
    src/formats/dsl2/xx_dsl2_extractor.c
    src/formats/dpk/xx_dpk.c
    src/formats/dpk/xx_dpk_extractor.c
    src/formats/cfl/xx_cfl.c
    src/formats/cfl/xx_cfl_extractor.c
    src/formats/trcpak/xx_trcpak.c
    src/formats/trcpak/xx_trcpak_extractor.c
    src/formats/swagpacket/xx_swagpacket.c
    src/formats/swagpacket/xx_swagpacket_extractor.c
    src/formats/sw/xx_sw.c
    src/formats/sw/xx_sw_extractor.c
    src/formats/sos/xx_sos.c
    src/formats/sos/xx_sos_extractor.c
    src/formats/secondnature/xx_secondnature.c
    src/formats/secondnature/xx_secondnature_extractor.c
    src/formats/seadata/xx_seadata.c
    src/formats/seadata/xx_seadata_extractor.c
    src/formats/sci/xx_sci.c
    src/formats/sci/xx_sci_extractor.c
    src/formats/powerboardbbs/xx_powerboardbbs.c
    src/formats/powerboardbbs/xx_powerboardbbs_extractor.c
    src/formats/minidump/xx_minidump.c
    src/formats/minidump/xx_minidump_extractor.c
    src/formats/lbrcobol/xx_lbrcobol.c
    src/formats/lbrcobol/xx_lbrcobol_extractor.c
    src/formats/krml/xx_krml.c
    src/formats/krml/xx_krml_extractor.c
    src/formats/jam/xx_jam.c
    src/formats/jam/xx_jam_extractor.c
    src/formats/irixsa/xx_irixsa.c
    src/formats/irixsa/xx_irixsa_extractor.c
    src/formats/hlb/xx_hlb.c
    src/formats/hlb/xx_hlb_extractor.c
    src/formats/frontpagetheme/xx_frontpagetheme.c
    src/formats/frontpagetheme/xx_frontpagetheme_extractor.c
    src/formats/cru/xx_cru.c
    src/formats/cru/xx_cru_extractor.c
    src/formats/bigaf/xx_bigaf.c
    src/formats/bigaf/xx_bigaf_extractor.c
    src/formats/tws/xx_tws.c
    src/formats/tws/xx_tws_extractor.c
    src/formats/packit/xx_packit.c
    src/formats/packit/xx_packit_extractor.c
    src/formats/zfsf/xx_zfsf.c
    src/formats/zfsf/xx_zfsf_extractor.c
    src/formats/marc/xx_marc.c
    src/formats/marc/xx_marc_extractor.c
    src/formats/bigf/xx_bigf.c
    src/formats/bigf/xx_bigf_extractor.c
    src/formats/ptero_bigf/xx_ptero_bigf.c
    src/formats/ptero_bigf/xx_ptero_bigf_extractor.c
    src/formats/rvz/xx_rvz.c
    src/formats/rvz/xx_rvz_extractor.c
    src/formats/ckpedp/xx_ckpedp.c
    src/formats/ckp/xx_ckp.c
    src/formats/ckp/xx_ckp_extractor.c
    src/formats/edp/xx_edp.c
    src/formats/edp/xx_edp_extractor.c
    src/formats/parsec_rib/xx_parsec_rib.c
    src/formats/parsec_rib/xx_parsec_rib_extractor.c
    src/formats/parsec_archive/xx_parsec_archive.c
    src/formats/parsec_archive/xx_parsec_archive_extractor.c
    src/formats/parsec_pmm/xx_parsec_pmm.c
    src/formats/parsec_pmm/xx_parsec_pmm_extractor.c
    src/formats/ascend/xx_ascend.c
    src/formats/ascend/xx_ascend_extractor.c
    src/formats/asar/xx_asar.c
    src/formats/asar/xx_asar_extractor.c
    src/formats/arq/xx_arq.c
    src/formats/arq/xx_arq_extractor.c
    src/formats/ap4/xx_ap4.c
    src/formats/ap4/xx_ap4_extractor.c
    src/formats/freearc/xx_freearc.c
    src/formats/freearc/xx_freearc_extractor.c
    src/formats/zpaq/xx_zpaq.c
    src/formats/zpaq/xx_zpaq_extractor.c
    src/formats/pea/xx_pea.c
    src/formats/pea/xx_pea_extractor.c
    src/formats/lpaq8/xx_lpaq8.c
    src/formats/lpaq8/xx_lpaq8_extractor.c
    src/formats/bcm/xx_bcm.c
    src/formats/bcm/xx_bcm_extractor.c
    src/formats/tar_zstd/xx_tar_zstd.c
    src/formats/tar_zstd/xx_tar_zstd_extractor.c
    src/formats/zstd/xx_zstd.c
    src/formats/zstd/xx_zstd_extractor.c
    src/formats/cpio/xx_cpio.c
    src/formats/cpio/xx_cpio_extractor.c
    src/formats/mtree/xx_mtree.c
    src/formats/mtree/xx_mtree_extractor.c
    src/formats/tar_nextstep/xx_tar_nextstep.c
    src/formats/tar_nextstep/xx_tar_nextstep_extractor.c
    src/formats/tar_compress/xx_tar_compress.c
    src/formats/tar_compress/xx_tar_compress_extractor.c
    src/formats/tar_lzip/xx_tar_lzip.c
    src/formats/tar_lzip/xx_tar_lzip_extractor.c
    src/formats/lzip/xx_lzip.c
    src/formats/lzip/xx_lzip_extractor.c
    src/formats/lzma/xx_lzma.c
    src/formats/lzma/xx_lzma_writer.c
    src/formats/lzma/xx_lzma_extractor.c
    src/formats/tar_lzma/xx_tar_lzma.c
    src/formats/tar_lzma/xx_tar_lzma_extractor.c
    src/formats/tar_lzop/xx_tar_lzop.c
    src/formats/tar_lzop/xx_tar_lzop_extractor.c
    src/formats/tarx1/xx_tarx1.c
    src/formats/tarx1/xx_tarx1_extractor.c
    src/formats/tarx2/xx_tarx2.c
    src/formats/tarx2/xx_tarx2_extractor.c
    src/formats/iso9660/xx_iso9660.c
    src/formats/iso9660/xx_iso_zisofs_native.c
    src/formats/iso9660/xx_iso9660_extractor.c
    src/formats/trx/xx_trx.c
    src/formats/trx/xx_trx_extractor.c
    src/formats/seama/xx_seama.c
    src/formats/seama/xx_seama_extractor.c
    src/formats/chk/xx_chk.c
    src/formats/chk/xx_chk_extractor.c
    src/formats/packimg/xx_packimg.c
    src/formats/packimg/xx_packimg_extractor.c
    src/formats/dlob/xx_dlob.c
    src/formats/dlob/xx_dlob_extractor.c
    src/formats/wince/xx_wince.c
    src/formats/wince/xx_wince_extractor.c
    src/formats/binhdr/xx_binhdr.c
    src/formats/binhdr/xx_binhdr_extractor.c
    src/formats/rtk/xx_rtk.c
    src/formats/rtk/xx_rtk_extractor.c
    src/formats/csman/xx_csman.c
    src/formats/csman/xx_csman_extractor.c
    src/formats/vxworks/xx_vxworks.c
    src/formats/vxworks/xx_vxworks_extractor.c
    src/formats/uefi_fv/xx_uefi_fv.c
    src/formats/uefi_fv/xx_uefi_fv_extractor.c
    src/formats/uefi_capsule/xx_uefi_capsule.c
    src/formats/uefi_capsule/xx_uefi_capsule_extractor.c
    src/formats/qcow/xx_qcow.c
    src/formats/qcow/xx_qcow_extractor.c
    src/formats/qnx6/xx_qnx6.c
    src/formats/qnx6/xx_qnx6_extractor.c
    src/formats/luks/xx_luks.c
    src/formats/luks/xx_luks_extractor.c
    src/formats/apfs/xx_apfs.c
    src/formats/apfs/xx_apfs_extractor.c
    src/formats/btrfs/xx_btrfs.c
    src/formats/btrfs/xx_btrfs_extractor.c
    src/formats/logfs/xx_logfs.c
    src/formats/logfs/xx_logfs_extractor.c
    src/formats/dmg/xx_dmg.c
    src/formats/dmg/xx_dmg_extractor.c
    src/formats/dms/xx_dms.c
    src/formats/lzop/xx_lzop.c
    src/formats/lzop/xx_lzop_extractor.c
    src/formats/srec/xx_srec.c
    src/formats/srec/xx_srec_extractor.c
    src/formats/dclraw/xx_dclraw.c
    src/formats/dclraw/xx_dclraw_extractor.c
    src/formats/xpak/xx_xpak.c
    src/formats/xpak/xx_xpak_extractor.c
    src/formats/pdb/xx_pdb.c
    src/formats/pdb/xx_pdb_extractor.c
    src/formats/infogramesft/xx_infogramesft.c
    src/formats/infogramesft/xx_infogramesft_extractor.c
    src/formats/uboot/xx_uboot.c
    src/formats/uboot/xx_uboot_extractor.c
    src/formats/twrx/xx_twrx.c
    src/formats/twrx/xx_twrx_extractor.c
    src/formats/tplink/xx_tplink.c
    src/formats/tplink/xx_tplink_extractor.c
    src/formats/silmarilsft/xx_silmarilsft.c
    src/formats/silmarilsft/xx_silmarilsft_extractor.c
    src/formats/shrs/xx_shrs.c
    src/formats/shrs/xx_shrs_extractor.c
    src/formats/mh01/xx_mh01.c
    src/formats/mh01/xx_mh01_extractor.c
    src/formats/matter_ota/xx_matter_ota.c
    src/formats/matter_ota/xx_matter_ota_extractor.c
    src/formats/lz4demo/xx_lz4demo.c
    src/formats/lz4demo/xx_lz4demo_extractor.c
    src/formats/lingvoarc/xx_lingvoarc.c
    src/formats/lingvoarc/xx_lingvoarc_extractor.c
    src/formats/jboot/xx_jboot.c
    src/formats/jboot/xx_jboot_extractor.c
    src/formats/encrpted_img/xx_encrpted_img.c
    src/formats/encrpted_img/xx_encrpted_img_extractor.c
    src/formats/encfw/xx_encfw.c
    src/formats/encfw/xx_encfw_extractor.c
    src/formats/ecos/xx_ecos.c
    src/formats/ecos/xx_ecos_extractor.c
    src/formats/dlke/xx_dlke.c
    src/formats/dlke/xx_dlke_extractor.c
    src/formats/dlink_tlv/xx_dlink_tlv.c
    src/formats/dlink_tlv/xx_dlink_tlv_extractor.c
    src/formats/dkbs/xx_dkbs.c
    src/formats/dkbs/xx_dkbs_extractor.c
    src/formats/autel/xx_autel.c
    src/formats/autel/xx_autel_extractor.c
    src/formats/arcadyan/xx_arcadyan.c
    src/formats/arcadyan/xx_arcadyan_extractor.c
    src/formats/androidboot/xx_androidboot.c
    src/formats/androidboot/xx_androidboot_extractor.c
    src/formats/dms/xx_dms_extractor.c
    src/formats/rawstac/xx_rawstac.c
    src/formats/rawstac/xx_rawstac_extractor.c
    src/formats/vmssaveset/xx_vmssaveset.c
    src/formats/vmssaveset/xx_vmssaveset_extractor.c
    src/formats/boo/xx_boo.c
    src/formats/boo/xx_boo_extractor.c
    src/formats/wolfft/xx_wolfft.c
    src/formats/wolfft/xx_wolfft_extractor.c
    src/formats/settlersft/xx_settlersft.c
    src/formats/settlersft/xx_settlersft_extractor.c
    src/formats/teacy/xx_teacy.c
    src/formats/teacy/xx_teacy_extractor.c
    src/formats/rsc/xx_rsc.c
    src/formats/rsc/xx_rsc_extractor.c
    src/formats/res/xx_res.c
    src/formats/res/xx_res_extractor.c
    src/formats/bsn/xx_bsn.c
    src/formats/bsn/xx_bsn_extractor.c
    src/formats/wintermutedcp/xx_wintermutedcp.c
    src/formats/wintermutedcp/xx_wintermutedcp_extractor.c
    src/formats/volitionvpft/xx_volitionvpft.c
    src/formats/volitionvpft/xx_volitionvpft_extractor.c
    src/formats/agis/xx_agis.c
    src/formats/agis/xx_agis_extractor.c
    src/formats/hog/xx_hog.c
    src/formats/hog/xx_hog_extractor.c
    src/formats/rnca/xx_rnca.c
    src/formats/rnca/xx_rnca_extractor.c
    src/formats/paperport/xx_paperport.c
    src/formats/paperport/xx_paperport_extractor.c
    src/formats/starkit/xx_starkit.c
    src/formats/starkit/xx_starkit_extractor.c
    src/formats/lspack10/xx_lspack10.c
    src/formats/lspack10/xx_lspack10_extractor.c
    src/formats/ixa/xx_ixa.c
    src/formats/mlb_ft/xx_mlb_ft.c
    src/formats/fss/xx_fss.c
    src/formats/epf/xx_epf.c
    src/formats/epf/xx_epf_extractor.c
    src/formats/dfc/xx_dfc.c
    src/formats/dfc/xx_dfc_extractor.c
    src/formats/sfx_rsfx/xx_sfx_rsfx.c
    src/formats/sfx_rsfx/xx_sfx_rsfx_extractor.c
    src/formats/sfx_softpaq4/xx_sfx_softpaq4.c
    src/formats/sfx_softpaq4/xx_sfx_softpaq4_extractor.c
    src/formats/ka/xx_ka.c
    src/formats/ka/xx_ka_extractor.c
    src/formats/nextstep_diskimage/xx_nextstep_diskimage.c
    src/formats/nextstep_diskimage/xx_nextstep_diskimage_extractor.c
    src/formats/dn/xx_dn.c
    src/formats/dn/xx_dn_extractor.c
    src/formats/insa/xx_insa.c
    src/formats/insa/xx_insa_extractor.c
    src/formats/sfx_vms_dcx/xx_sfx_vms_dcx.c
    src/formats/sfx_vms_dcx/xx_sfx_vms_dcx_extractor.c
    src/formats/oberon/xx_oberon.c
    src/formats/oberon/xx_oberon_extractor.c
    src/formats/ppd/xx_ppd.c
    src/formats/ppd/xx_ppd_extractor.c
    src/formats/sfx_ad01/xx_sfx_ad01.c
    src/formats/sfx_ad01/xx_sfx_ad01_extractor.c
    src/formats/sfx_nss/xx_sfx_nss.c
    src/formats/sfx_nss/xx_sfx_nss_extractor.c
    src/formats/xx_corpus_extra_extractors.c
    src/formats/solitaire_deluxe/xx_solitaire_deluxe.c
    src/formats/solitaire_deluxe/xx_solitaire_deluxe_extractor.c
    src/formats/thebat_msb/xx_thebat_msb.c
    src/formats/sfx_localzip/xx_sfx_localzip.c
    src/formats/ixa/xx_ixa_extractor.c
    src/formats/lif/xx_lif.c
    src/formats/lif/xx_lif_extractor.c
    src/formats/qip2/xx_qip2.c
    src/formats/qip2/xx_qip2_extractor.c
    src/formats/emt/xx_emt.c
    src/formats/emt/xx_emt_extractor.c
    src/formats/bagf/xx_bagf.c
    src/formats/bagf/xx_bagf_extractor.c
    src/formats/wrzl/xx_wrzl.c
    src/formats/wrzl/xx_wrzl_extractor.c
    src/formats/spk/xx_spk.c
    src/formats/spk/xx_spk_extractor.c
    src/formats/stac/xx_stac.c
    src/formats/stac/xx_stac_extractor.c
    src/formats/dbz/xx_dbz.c
    src/formats/dbz/xx_dbz_extractor.c
    src/formats/squeeze2/xx_squeeze2.c
    src/formats/squeeze2/xx_squeeze2_extractor.c
    src/formats/sq/xx_sq.c
    src/formats/sq/xx_sq_extractor.c
    src/formats/phar/xx_phar.c
    src/formats/phar/xx_phar_extractor.c
    src/formats/mpq/xx_mpq.c
    src/formats/mpq/xx_mpq_extractor.c
    src/formats/sabdu/xx_sabdu.c
    src/formats/sabdu/xx_sabdu_extractor.c
    src/formats/apricot/xx_apricot.c
    src/formats/apricot/xx_apricot_extractor.c
    src/formats/hdcopy/xx_hdcopy.c
    src/formats/hdcopy/xx_hdcopy_extractor.c
    src/formats/copydisk/xx_copydisk.c
    src/formats/copydisk/xx_copydisk_extractor.c
    src/formats/ciso/xx_ciso.c
    src/formats/ciso/xx_ciso_extractor.c
    src/formats/vmdk/xx_vmdk.c
    src/formats/vmdk/xx_vmdk_extractor.c
    src/formats/vhddynamic/xx_vhddynamic.c
    src/formats/vhddynamic/xx_vhddynamic_extractor.c
    src/formats/wim/xx_wim.c
    src/formats/wim/xx_wim_codec.c
    src/formats/wim/xx_wim_extractor.c
    src/formats/softpaq2/xx_softpaq2.c
    src/formats/softpaq2/xx_softpaq2_extractor.c
    src/formats/aiaff/xx_aiaff.c
    src/formats/aiaff/xx_aiaff_extractor.c
    src/formats/gxl/xx_gxl.c
    src/formats/gxl/xx_gxl_extractor.c
    src/formats/shrinkwrap/xx_shrinkwrap.c
    src/formats/shrinkwrap/xx_shrinkwrap_extractor.c
    src/formats/red/xx_red.c
    src/formats/red/xx_red_extractor.c
    src/formats/diskexpress/xx_diskexpress.c
    src/formats/diskexpress/xx_diskexpress_extractor.c
    src/formats/cpx/xx_cpx.c
    src/formats/cpx/xx_cpx_extractor.c
    src/formats/smsipak/xx_smsipak.c
    src/formats/smsipak/xx_smsipak_extractor.c
    src/formats/bnd/xx_bnd.c
    src/formats/bnd/xx_bnd_extractor.c
    src/formats/cat/xx_cat.c
    src/formats/cat/xx_cat_extractor.c
    src/formats/csidos/xx_csidos.c
    src/formats/csidos/xx_csidos_extractor.c
    src/formats/binder/xx_binder.c
    src/formats/binder/xx_binder_extractor.c
    src/formats/jasc/xx_jasc.c
    src/formats/jasc/xx_jasc_extractor.c
    src/formats/recognita/xx_recognita.c
    src/formats/recognita/xx_recognita_extractor.c
    src/formats/scf/xx_scf.c
    src/formats/scf/xx_scf_extractor.c
    src/formats/bcw/xx_bcw.c
    src/formats/bcw/xx_bcw_extractor.c
    src/formats/bvrp/xx_bvrp.c
    src/formats/bvrp/xx_bvrp_extractor.c
    src/formats/cpoint/xx_cpoint.c
    src/formats/ssm/xx_ssm.c
    src/formats/ssm/xx_ssm_extractor.c
    src/formats/mdcd/xx_mdcd.c
    src/formats/mdcd/xx_mdcd_extractor.c
    src/formats/xlas/xx_xlas.c
    src/formats/xlas/xx_xlas_extractor.c
    src/formats/pain/xx_pain.c
    src/formats/pain/xx_pain_extractor.c
    src/formats/qrst/xx_qrst.c
    src/formats/qrst/xx_qrst_extractor.c
    src/formats/opc/xx_opc.c
    src/formats/opc/xx_opc_extractor.c
    src/formats/tnef/xx_tnef.c
    src/formats/tnef/xx_tnef_extractor.c
    src/formats/mcc/xx_mcc.c
    src/formats/mcc/xx_mcc_extractor.c
    src/formats/notetab/xx_notetab.c
    src/formats/notetab/xx_notetab_extractor.c
    src/formats/stunts/xx_stunts.c
    src/formats/stunts/xx_stunts_extractor.c
    src/formats/megatechvol/xx_megatechvol.c
    src/formats/megatechvol/xx_megatechvol_extractor.c
    src/formats/grasp/xx_grasp.c
    src/formats/grasp/xx_grasp_extractor.c
    src/formats/psn/xx_psn.c
    src/formats/psn/xx_psn_extractor.c
    src/formats/sinner/xx_sinner.c
    src/formats/sinner/xx_sinner_extractor.c
    src/formats/hog2/xx_hog2.c
    src/formats/hog2/xx_hog2_extractor.c
    src/formats/pcxlib/xx_pcxlib.c
    src/formats/pcxlib/xx_pcxlib_extractor.c
    src/formats/vmsdb/xx_vmsdb.c
    src/formats/vmsdb/xx_vmsdb_extractor.c
    src/formats/vmspcsi/xx_vmspcsi.c
    src/formats/vmspcsi/xx_vmspcsi_extractor.c
    src/formats/beospkg/xx_beospkg.c
    src/formats/beospkg/xx_beospkg_extractor.c
    src/formats/solarispkg/xx_solarispkg.c
    src/formats/solarispkg/xx_solarispkg_extractor.c
    src/formats/pax/xx_pax.c
    src/formats/pax/xx_pax_extractor.c
    src/formats/copyqmexe/xx_copyqmexe.c
    src/formats/copyqmexe/xx_copyqmexe_extractor.c
    src/formats/diskjuggler/xx_diskjuggler.c
    src/formats/diskjuggler/xx_diskjuggler_extractor.c
    src/formats/pmdiskcopy/xx_pmdiskcopy.c
    src/formats/pmdiskcopy/xx_pmdiskcopy_extractor.c
    src/formats/diskdupe/xx_diskdupe.c
    src/formats/diskdupe/xx_diskdupe_extractor.c
    src/formats/imd/xx_imd.c
    src/formats/imd/xx_imd_extractor.c
    src/formats/twoimg/xx_twoimg.c
    src/formats/twoimg/xx_twoimg_extractor.c
    src/formats/fdi/xx_fdi.c
    src/formats/fdi/xx_fdi_extractor.c
    src/formats/hfe/xx_hfe.c
    src/formats/hfe/xx_hfe_extractor.c
    src/formats/teledisk/xx_teledisk.c
    src/formats/teledisk/xx_teledisk_extractor.c
    src/formats/copyqm/xx_copyqm.c
    src/formats/copyqm/xx_copyqm_extractor.c
    src/formats/pcinstall/xx_pcinstall.c
    src/formats/pcinstall/xx_pcinstall_extractor.c
    src/formats/gksetup/xx_gksetup.c
    src/formats/gksetup/xx_gksetup_extractor.c
    src/formats/is11/xx_is11.c
    src/formats/is11/xx_is11_extractor.c
    src/formats/izpack/xx_izpack.c
    src/formats/izpack/xx_izpack_extractor.c
    src/formats/squeeze1/xx_squeeze1.c
    src/formats/squeeze1/xx_squeeze1_extractor.c
    src/formats/trdos/xx_trdos.c
    src/formats/trdos/xx_trdos_extractor.c
    src/formats/lifkd/xx_lifkd.c
    src/formats/lifkd/xx_lifkd_extractor.c
    src/formats/arcv/xx_arcv.c
    src/formats/arcv/xx_arcv_extractor.c
    src/formats/compaqlzh/xx_compaqlzh.c
    src/formats/compaqlzh/xx_compaqlzh_extractor.c
    src/formats/lzk00/xx_lzk00.c
    src/formats/lzk00/xx_lzk00_extractor.c
    src/formats/pma/xx_pma.c
    src/formats/pma/xx_pma_extractor.c
    src/formats/binhex/xx_binhex.c
    src/formats/binhex/xx_binhex_extractor.c
    src/formats/binaryii/xx_binaryii.c
    src/formats/binaryii/xx_binaryii_extractor.c
    src/formats/stuffit/xx_stuffit.c
    src/formats/stuffit/xx_stuffit_extractor.c
    src/formats/dclft/xx_dclft.c
    src/formats/dclft/xx_dclft_extractor.c
    src/formats/debugscr/xx_debugscr.c
    src/formats/debugscr/xx_debugscr_extractor.c
    src/formats/gob/xx_gob.c
    src/formats/gob/xx_gob_extractor.c
    src/formats/savedskf/xx_savedskf.c
    src/formats/savedskf/xx_savedskf_extractor.c
    src/formats/edilzss/xx_edilzss.c
    src/formats/edilzss/xx_edilzss_extractor.c
    src/formats/is7inx/xx_is7inx.c
    src/formats/is7inx/xx_is7inx_extractor.c
    src/formats/is5/xx_is5.c
    src/formats/is5/xx_is5_extractor.c
    src/formats/is3/xx_is3.c
    src/formats/is3/xx_is3_extractor.c
    src/formats/psdc/xx_psdc.c
    src/formats/psdc/xx_psdc_extractor.c
    src/formats/unixcompact/xx_unixcompact.c
    src/formats/unixcompact/xx_unixcompact_extractor.c
    src/formats/sco/xx_sco.c
    src/formats/sco/xx_sco_extractor.c
    src/formats/gpfpack/xx_gpfpack.c
    src/formats/gpfpack/xx_gpfpack_extractor.c
    src/formats/ftcomp/xx_ftcomp.c
    src/formats/ftcomp/xx_ftcomp_extractor.c
    src/formats/winlink/xx_winlink.c
    src/formats/winlink/xx_winlink_extractor.c
    src/formats/gst/xx_gst.c
    src/formats/gst/xx_gst_extractor.c
    src/formats/finear/xx_finear.c
    src/formats/finear/xx_finear_extractor.c
    src/formats/mwave/xx_mwave.c
    src/formats/mwave/xx_mwave_extractor.c
    src/formats/xorarchive/xx_xorarchive.c
    src/formats/xorarchive/xx_xorarchive_extractor.c
    src/formats/mxs/xx_mxs.c
    src/formats/mxs/xx_mxs_extractor.c
    src/formats/edc/xx_edc.c
    src/formats/edc/xx_edc_extractor.c
    src/formats/mrnz/xx_mrnz.c
    src/formats/mrnz/xx_mrnz_extractor.c
    src/formats/tpwm/xx_tpwm.c
    src/formats/tpwm/xx_tpwm_extractor.c
    src/formats/cazip/xx_cazip.c
    src/formats/cazip/xx_cazip_extractor.c
    src/formats/ibmpack/xx_ibmpack.c
    src/formats/ibmpack/xx_ibmpack_extractor.c
    src/formats/rnc/xx_rnc.c
    src/formats/rnc/xx_rnc_extractor.c
    src/formats/shar/xx_shar.c
    src/formats/shar/xx_shar_extractor.c
    src/formats/netware2/xx_netware2.c
    src/formats/netware2/xx_netware2_extractor.c
    src/formats/mathcad/xx_mathcad.c
    src/formats/mathcad/xx_mathcad_extractor.c
    src/formats/perform/xx_perform.c
    src/formats/perform/xx_perform_extractor.c
    src/formats/battleisle/xx_battleisle.c
    src/formats/battleisle/xx_battleisle_extractor.c
    src/formats/kpck/xx_kpck.c
    src/formats/kpck/xx_kpck_extractor.c
    src/formats/beatthehouse/xx_beatthehouse.c
    src/formats/beatthehouse/xx_beatthehouse_extractor.c
    src/formats/pp20/xx_pp20.c
    src/formats/pp20/xx_pp20_extractor.c
    src/formats/macbinary/xx_macbinary.c
    src/formats/macbinary/xx_macbinary_extractor.c
    src/formats/applesingle/xx_applesingle.c
    src/formats/applesingle/xx_applesingle_extractor.c
    src/formats/resourcefork/xx_resourcefork.c
    src/formats/resourcefork/xx_resourcefork_extractor.c
    src/formats/cramfs/xx_cramfs.c
    src/formats/cramfs/xx_cramfs_direct_native.c
    src/formats/cramfs/xx_cramfs_extractor.c
    src/formats/jffs2/xx_jffs2.c
    src/formats/jffs2/xx_jffs2_extractor.c
    src/formats/yaffs/xx_yaffs.c
    src/formats/yaffs/xx_yaffs_extractor.c
    src/formats/ubi/xx_ubi.c
    src/formats/ubi/xx_ubi_extractor.c
    src/formats/ubifs/xx_ubifs.c
    src/formats/ubifs/xx_ubifs_extractor.c
    src/formats/ext/xx_ext.c
    src/formats/erofs/xx_erofs.c
    src/formats/ext/xx_ext_extractor.c
    src/formats/fat/xx_fat.c
    src/formats/fat/xx_fat_extractor.c
    src/formats/mbr/xx_mbr.c
    src/formats/mbr/xx_mbr_extractor.c
    src/formats/gpt/xx_gpt.c
    src/formats/gpt/xx_gpt_extractor.c
    src/formats/sparse/xx_sparse.c
    src/formats/sparse/xx_sparse_extractor.c
    src/formats/uimage/xx_uimage.c
    src/formats/uimage/xx_uimage_lz4_native.c
    src/formats/uimage/xx_uimage_extractor.c
    src/formats/dtb/xx_dtb.c
    src/formats/dtb/xx_dtb_extractor.c
    src/formats/squashfs/xx_squashfs.c
    src/formats/squashfs/xx_squashfs_extractor.c
    src/formats/ntfs/xx_ntfs.c
    src/formats/ntfs/xx_ntfs_extractor.c
    src/formats/udf/xx_udf.c
    src/formats/udf/xx_udf_extractor.c
    src/formats/romfs/xx_romfs.c
    src/formats/romfs/xx_romfs_extractor.c
    src/formats/sqz/xx_sqz.c
    src/formats/sqz/xx_sqz_extractor.c
    src/formats/topspeed/xx_topspeed.c
    src/formats/topspeed/xx_topspeed_extractor.c
    src/formats/tps/xx_tps.c
    src/formats/tps/xx_tps_extractor.c
    src/formats/ulead/xx_ulead.c
    src/formats/ulead/xx_ulead_extractor.c
    src/formats/quantum/xx_quantum.c
    src/formats/quantum/xx_quantum_extractor.c
    src/formats/zxzip/xx_zxzip.c
    src/formats/zxzip/xx_zxzip_extractor.c
    src/formats/zoom/xx_zoom.c
    src/formats/zoom/xx_zoom_extractor.c
    src/formats/sfpack/xx_sfpack.c
    src/formats/sfpack/xx_sfpack_extractor.c
    src/formats/claylz/xx_claylz.c
    src/formats/claylz/xx_claylz_extractor.c
    src/formats/c64wraptor/xx_c64wraptor.c
    src/formats/c64wraptor/xx_c64wraptor_extractor.c
    src/formats/corelltec/xx_corelltec.c
    src/formats/corelltec/xx_corelltec_extractor.c
    src/formats/pcsecure/xx_pcsecure.c
    src/formats/pcsecure/xx_pcsecure_extractor.c
    src/formats/rsvk/xx_rsvk.c
    src/formats/rsvk/xx_rsvk_extractor.c
    src/formats/lzw15v/xx_lzw15v.c
    src/formats/lzw15v/xx_lzw15v_extractor.c
    src/formats/saf/xx_saf.c
    src/formats/saf/xx_saf_extractor.c
    src/formats/sls/xx_sls.c
    src/formats/sls/xx_sls_extractor.c
    src/formats/nid/xx_nid.c
    src/formats/nid/xx_nid_extractor.c
    src/formats/gamos/xx_gamos.c
    src/formats/gamos/xx_gamos_extractor.c
    src/formats/panorama/xx_panorama.c
    src/formats/panorama/xx_panorama_extractor.c
    src/formats/fpak/xx_fpak.c
    src/formats/fpak/xx_fpak_extractor.c
    src/formats/ace/xx_ace.c
    src/formats/ace/xx_ace_extractor.c
    src/formats/ace/xx_ace_dec.c
    src/formats/arj/xx_arj.c
    src/formats/arj/xx_arj_extractor.c
    src/formats/ain/xx_ain.c
    src/formats/ain/xx_ain_extractor.c
    src/formats/aldus/xx_aldus.c
    src/formats/aldus/xx_aldus_extractor.c
    src/formats/alz/xx_alz.c
    src/formats/alz/xx_alz_extractor.c
    src/formats/ampk/xx_ampk.c
    src/formats/ampk/xx_ampk_extractor.c
    src/formats/aodos/xx_aodos.c
    src/formats/aodos/xx_aodos_extractor.c
    src/formats/arcfs/xx_arcfs.c
    src/formats/arcfs/xx_arcfs_extractor.c
    src/formats/pdp11ar/xx_pdp11ar.c
    src/formats/pdp11ar/xx_pdp11ar_extractor.c
    src/formats/artipack/xx_artipack.c
    src/formats/artipack/xx_artipack_extractor.c
    src/formats/arcv2/xx_arcv2.c
    src/formats/arcv2/xx_arcv2_extractor.c
    src/formats/arcv4/xx_arcv4.c
    src/formats/arcv4/xx_arcv4_extractor.c
    src/formats/warc/xx_warc.c
    src/formats/warc/xx_warc_extractor.c
    src/formats/aixbff/xx_aixbff.c
    src/formats/aixbff/xx_aixbff_extractor.c
    src/formats/arx/xx_arx.c
    src/formats/arx/xx_arx_extractor.c
    src/formats/cab/xx_cab.c
    src/formats/cab/xx_cab_extractor.c
    src/formats/msdos/xx_msdos.c
    src/formats/msdos/xx_msdos_extractor.c
    src/formats/com/xx_com.c
    src/formats/com/xx_com_extractor.c
    src/formats/dos16m/xx_dos16m.c
    src/formats/dos16m/xx_dos16m_extractor.c
    src/formats/atarist/xx_atarist.c
    src/formats/atarist/xx_atarist_extractor.c
    src/formats/amigahunk/xx_amigahunk.c
    src/formats/amigahunk/xx_amigahunk_extractor.c
    src/formats/pe/xx_pe.c
    src/formats/pe/xx_pe_extractor.c
    src/formats/pe/xx_pe_data.c
    src/formats/elf/xx_elf.c
    src/formats/elf/xx_elf_extractor.c
    src/formats/elf/xx_elf_data.c
    src/formats/macho/xx_macho.c
    src/formats/macho/xx_macho_extractor.c
    src/formats/macho/xx_macho_data.c
    src/formats/ne/xx_ne.c
    src/formats/ne/xx_ne_extractor.c
    src/formats/le/xx_le.c
    src/formats/le/xx_le_extractor.c
    src/formats/lx/xx_lx.c
    src/formats/lx/xx_lx_extractor.c
    src/formats/dex/xx_dex.c
    src/formats/dex/xx_dex_extractor.c
    # Readers already registered by xx_format and the public API.
    src/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.c
    src/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly_extractor.c
    src/formats/x68000_dim/xx_x68000_dim.c
    src/formats/x68000_dim/xx_x68000_dim_extractor.c
    src/formats/visionaire_studio_vis/xx_visionaire_studio_vis.c
    src/formats/visionaire_studio_vis/xx_visionaire_studio_vis_extractor.c
    src/formats/uharc/xx_uharc.c
    src/formats/uharc/xx_uharc_extractor.c
    src/formats/trs_80_jv3/xx_trs_80_jv3.c
    src/formats/trs_80_jv3/xx_trs_80_jv3_extractor.c
    src/formats/trs_80_jv1/xx_trs_80_jv1.c
    src/formats/trs_80_jv1/xx_trs_80_jv1_extractor.c
    src/formats/t98_next_nfd/xx_t98_next_nfd.c
    src/formats/t98_next_nfd/xx_t98_next_nfd_extractor.c
    src/formats/stuffit_split_file/xx_stuffit_split_file.c
    src/formats/stuffit_split_file/xx_stuffit_split_file_extractor.c
    src/formats/rdb/xx_rdb.c
    src/formats/rdb/xx_rdb_extractor.c
    src/formats/qnap_nas_firmware/xx_qnap_nas_firmware.c
    src/formats/qnap_nas_firmware/xx_qnap_nas_firmware_extractor.c
    src/formats/qcow1/xx_qcow1.c
    src/formats/qcow1/xx_qcow1_extractor.c
    src/formats/nsa/xx_nsa.c
    src/formats/nsa/xx_nsa_extractor.c
    src/formats/ns2/xx_ns2.c
    src/formats/ns2/xx_ns2_extractor.c
    src/formats/nec_pc_98_fdi/xx_nec_pc_98_fdi.c
    src/formats/nec_pc_98_fdi/xx_nec_pc_98_fdi_extractor.c
    src/formats/ms_dos_backup/xx_ms_dos_backup.c
    src/formats/ms_dos_backup/xx_ms_dos_backup_extractor.c
    src/formats/hxc_stream_hfe/xx_hxc_stream_hfe.c
    src/formats/hxc_stream_hfe/xx_hxc_stream_hfe_extractor.c
    src/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.c
    src/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image_extractor.c
    src/formats/apple_sparse_bundle/xx_apple_sparse_bundle.c
    src/formats/apple_sparse_bundle/xx_apple_sparse_bundle_extractor.c
    src/formats/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.c
    src/formats/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image_extractor.c
    src/formats/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.c
    src/formats/raw_deflate_compressed_data/xx_raw_deflate_compressed_data_extractor.c
    src/formats/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs.c
    src/formats/sfx_analogx_emucore_ffs/xx_sfx_analogx_emucore_ffs_extractor.c
    src/formats/sfx_krzip/xx_sfx_krzip.c
    src/formats/sfx_krzip/xx_sfx_krzip_extractor.c
    src/formats/sfx_warpin_package/xx_sfx_warpin_package.c
    src/formats/sfx_warpin_package/xx_sfx_warpin_package_extractor.c
    src/formats/sfx_hci_instalit/xx_sfx_hci_instalit.c
    src/formats/sfx_hci_instalit/xx_sfx_hci_instalit_extractor.c
    src/formats/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.c
    src/formats/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion_extractor.c
    src/formats/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.c
    src/formats/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects_extractor.c
    src/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.c
    src/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor_extractor.c
    src/formats/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.c
    src/formats/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package_extractor.c
    src/formats/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image.c
    src/formats/sfx_ardi_diskette_image/xx_sfx_ardi_diskette_image_extractor.c
    src/formats/sfx_sbx_extractor/xx_sfx_sbx_extractor.c
    src/formats/sfx_sbx_extractor/xx_sfx_sbx_extractor_extractor.c
    src/formats/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.c
    src/formats/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp_extractor.c
    src/formats/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image.c
    src/formats/sfx_sydex_diskette_image/xx_sfx_sydex_diskette_image_extractor.c
    src/formats/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.c
    src/formats/sfx_compaq_softpaq/xx_sfx_compaq_softpaq_extractor.c
    src/formats/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto.c
    src/formats/sfx_wasp_windows_auto/xx_sfx_wasp_windows_auto_extractor.c
    src/formats/wise_installation_system/xx_wise_installation_system.c
    src/formats/wise_installation_system/xx_wise_installation_system_extractor.c
    src/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf.c
    src/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf_extractor.c
    src/formats/gentee_installer/xx_gentee_installer.c
    src/formats/gentee_installer/xx_gentee_installer_extractor.c
    src/formats/clickteam_install_creator/xx_clickteam_install_creator.c
    src/formats/clickteam_install_creator/xx_clickteam_install_creator_extractor.c
    src/formats/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.c
    src/formats/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor_extractor.c
    src/formats/sfxstart/xx_sfxstart.c
    src/formats/sfxstart/xx_sfxstart_extractor.c
    src/formats/modbus_tcp/xx_modbus_tcp.c
    src/formats/modbus_tcp/xx_modbus_tcp_extractor.c
    src/formats/someip_message/xx_someip_message.c
    src/formats/someip_message/xx_someip_message_extractor.c
    src/formats/dds_rtps/xx_dds_rtps.c
    src/formats/dds_rtps/xx_dds_rtps_extractor.c
    src/formats/rip_message/xx_rip_message.c
    src/formats/rip_message/xx_rip_message_extractor.c
    src/formats/vrrp_message/xx_vrrp_message.c
    src/formats/vrrp_message/xx_vrrp_message_extractor.c
    src/formats/igmp_message/xx_igmp_message.c
    src/formats/igmp_message/xx_igmp_message_extractor.c
    src/formats/pim_message/xx_pim_message.c
    src/formats/pim_message/xx_pim_message_extractor.c
    src/formats/ldp_message/xx_ldp_message.c
    src/formats/ldp_message/xx_ldp_message_extractor.c
    src/formats/gre_packet/xx_gre_packet.c
    src/formats/gre_packet/xx_gre_packet_extractor.c
    src/formats/l2tp_packet/xx_l2tp_packet.c
    src/formats/l2tp_packet/xx_l2tp_packet_extractor.c
    src/formats/lldp_message/xx_lldp_message.c
    src/formats/lldp_message/xx_lldp_message_extractor.c
    src/formats/netflow_datagram/xx_netflow_datagram.c
    src/formats/netflow_datagram/xx_netflow_datagram_extractor.c
    src/formats/ntlm_message/xx_ntlm_message.c
    src/formats/ntlm_message/xx_ntlm_message_extractor.c
    src/formats/dcerpc_pdu/xx_dcerpc_pdu.c
    src/formats/dcerpc_pdu/xx_dcerpc_pdu_extractor.c
    src/formats/ethereum_rlp/xx_ethereum_rlp.c
    src/formats/ethereum_rlp/xx_ethereum_rlp_extractor.c
    src/formats/imagemagick_miff/xx_imagemagick_miff.c
    src/formats/imagemagick_miff/xx_imagemagick_miff_extractor.c
    src/formats/avs_image/xx_avs_image.c
    src/formats/avs_image/xx_avs_image_extractor.c
    src/formats/scanalytics_iplab/xx_scanalytics_iplab.c
    src/formats/scanalytics_iplab/xx_scanalytics_iplab_extractor.c
    src/formats/mtv_image/xx_mtv_image.c
    src/formats/mtv_image/xx_mtv_image_extractor.c
    src/formats/nokia_ota_bitmap/xx_nokia_ota_bitmap.c
    src/formats/nokia_ota_bitmap/xx_nokia_ota_bitmap_extractor.c
    src/formats/apple_pict/xx_apple_pict.c
    src/formats/apple_pict/xx_apple_pict_extractor.c
    src/formats/wordperfect_wpg/xx_wordperfect_wpg.c
    src/formats/wordperfect_wpg/xx_wordperfect_wpg_extractor.c
    src/formats/nasa_vicar/xx_nasa_vicar.c
    src/formats/nasa_vicar/xx_nasa_vicar_extractor.c
    src/formats/khoros_viff/xx_khoros_viff.c
    src/formats/khoros_viff/xx_khoros_viff_extractor.c
    src/formats/imagemagick_mvg/xx_imagemagick_mvg.c
    src/formats/imagemagick_mvg/xx_imagemagick_mvg_extractor.c
    src/formats/motif_uil/xx_motif_uil.c
    src/formats/motif_uil/xx_motif_uil_extractor.c
    src/formats/iges_model/xx_iges_model.c
    src/formats/iges_model/xx_iges_model_extractor.c
    src/formats/openusd_usda/xx_openusd_usda.c
    src/formats/openusd_usda/xx_openusd_usda_extractor.c
    src/formats/ufo_glif/xx_ufo_glif.c
    src/formats/ufo_glif/xx_ufo_glif_extractor.c
    src/formats/unifont_hex/xx_unifont_hex.c
    src/formats/unifont_hex/xx_unifont_hex_extractor.c
    src/formats/adlib_sop/xx_adlib_sop.c
    src/formats/adlib_sop/xx_adlib_sop_extractor.c
    src/formats/cudfm_cff/xx_cudfm_cff.c
    src/formats/cudfm_cff/xx_cudfm_cff_extractor.c
    src/formats/adlib_jbm/xx_adlib_jbm.c
    src/formats/adlib_jbm/xx_adlib_jbm_extractor.c
    src/formats/ceres_msc/xx_ceres_msc.c
    src/formats/ceres_msc/xx_ceres_msc_extractor.c
    src/formats/adlib_xsm/xx_adlib_xsm.c
    src/formats/adlib_xsm/xx_adlib_xsm_extractor.c
    src/formats/ken_ksm/xx_ken_ksm.c
    src/formats/ken_ksm/xx_ken_ksm_extractor.c
    src/formats/implay_music/xx_implay_music.c
    src/formats/implay_music/xx_implay_music_extractor.c
    src/formats/adlib_mtr/xx_adlib_mtr.c
    src/formats/adlib_mtr/xx_adlib_mtr_extractor.c
    src/formats/rdos_raw/xx_rdos_raw.c
    src/formats/rdos_raw/xx_rdos_raw_extractor.c
    src/formats/mad_tracker/xx_mad_tracker.c
    src/formats/mad_tracker/xx_mad_tracker_extractor.c
    src/formats/vasp_poscar/xx_vasp_poscar.c
    src/formats/vasp_poscar/xx_vasp_poscar_extractor.c
    src/formats/quantum_espresso_input/xx_quantum_espresso_input.c
    src/formats/quantum_espresso_input/xx_quantum_espresso_input_extractor.c
    src/formats/cp2k_input/xx_cp2k_input.c
    src/formats/cp2k_input/xx_cp2k_input_extractor.c
    src/formats/nwchem_input/xx_nwchem_input.c
    src/formats/nwchem_input/xx_nwchem_input_extractor.c
    src/formats/gamess_input/xx_gamess_input.c
    src/formats/gamess_input/xx_gamess_input_extractor.c
    src/formats/gaussian_input/xx_gaussian_input.c
    src/formats/gaussian_input/xx_gaussian_input_extractor.c
    src/formats/abinit_input/xx_abinit_input.c
    src/formats/abinit_input/xx_abinit_input_extractor.c
    src/formats/aims_geometry/xx_aims_geometry.c
    src/formats/aims_geometry/xx_aims_geometry_extractor.c
    src/formats/orca_input/xx_orca_input.c
    src/formats/orca_input/xx_orca_input_extractor.c
    src/formats/demon_input/xx_demon_input.c
    src/formats/demon_input/xx_demon_input_extractor.c
    src/formats/ethernet_frame/xx_ethernet_frame.c
    src/formats/ethernet_frame/xx_ethernet_frame_extractor.c
    src/formats/ip_packet/xx_ip_packet.c
    src/formats/ip_packet/xx_ip_packet_extractor.c
    src/formats/arp_packet/xx_arp_packet.c
    src/formats/arp_packet/xx_arp_packet_extractor.c
    src/formats/icmp_message/xx_icmp_message.c
    src/formats/icmp_message/xx_icmp_message_extractor.c
    src/formats/sip_message/xx_sip_message.c
    src/formats/sip_message/xx_sip_message_extractor.c
    src/formats/rtsp_message/xx_rtsp_message.c
    src/formats/rtsp_message/xx_rtsp_message_extractor.c
    src/formats/diameter_message/xx_diameter_message.c
    src/formats/diameter_message/xx_diameter_message_extractor.c
    src/formats/tacacs_packet/xx_tacacs_packet.c
    src/formats/tacacs_packet/xx_tacacs_packet_extractor.c
    src/formats/gtp_message/xx_gtp_message.c
    src/formats/gtp_message/xx_gtp_message_extractor.c
    src/formats/pfcp_message/xx_pfcp_message.c
    src/formats/pfcp_message/xx_pfcp_message_extractor.c
    src/formats/pptp_message/xx_pptp_message.c
    src/formats/pptp_message/xx_pptp_message_extractor.c
    src/formats/rsvp_message/xx_rsvp_message.c
    src/formats/rsvp_message/xx_rsvp_message_extractor.c
    src/formats/age_encrypted/xx_age_encrypted.c
    src/formats/age_encrypted/xx_age_encrypted_extractor.c
    src/formats/kerberos_ccache/xx_kerberos_ccache.c
    src/formats/kerberos_ccache/xx_kerberos_ccache_extractor.c
    src/formats/jose_jws/xx_jose_jws.c
    src/formats/jose_jws/xx_jose_jws_extractor.c
    src/formats/wbmp_image/xx_wbmp_image.c
    src/formats/wbmp_image/xx_wbmp_image_extractor.c
    src/formats/dec_sixel/xx_dec_sixel.c
    src/formats/dec_sixel/xx_dec_sixel_extractor.c
    src/formats/palm_bitmap/xx_palm_bitmap.c
    src/formats/palm_bitmap/xx_palm_bitmap_extractor.c
    src/formats/adobe_acv/xx_adobe_acv.c
    src/formats/adobe_acv/xx_adobe_acv_extractor.c
    src/formats/adobe_act/xx_adobe_act.c
    src/formats/adobe_act/xx_adobe_act_extractor.c
    src/formats/ogre_skeleton/xx_ogre_skeleton.c
    src/formats/ogre_skeleton/xx_ogre_skeleton_extractor.c
    src/formats/cal3d_skeleton/xx_cal3d_skeleton.c
    src/formats/cal3d_skeleton/xx_cal3d_skeleton_extractor.c
    src/formats/collada_dae/xx_collada_dae.c
    src/formats/collada_dae/xx_collada_dae_extractor.c
    src/formats/lightwave_scene/xx_lightwave_scene.c
    src/formats/lightwave_scene/xx_lightwave_scene_extractor.c
    src/formats/dsn6_density/xx_dsn6_density.c
    src/formats/dsn6_density/xx_dsn6_density_extractor.c
    src/formats/crystallography_mtz/xx_crystallography_mtz.c
    src/formats/crystallography_mtz/xx_crystallography_mtz_extractor.c
    src/formats/amira_mesh/xx_amira_mesh.c
    src/formats/amira_mesh/xx_amira_mesh_extractor.c
    src/formats/tetgen_mesh/xx_tetgen_mesh.c
    src/formats/tetgen_mesh/xx_tetgen_mesh_extractor.c
    src/formats/jedec_fuse/xx_jedec_fuse.c
    src/formats/jedec_fuse/xx_jedec_fuse_extractor.c
    src/formats/qchem_input/xx_qchem_input.c
    src/formats/qchem_input/xx_qchem_input_extractor.c
    src/formats/adlib_bam/xx_adlib_bam.c
    src/formats/adlib_bam/xx_adlib_bam_extractor.c
    src/formats/adlib_bmf/xx_adlib_bmf.c
    src/formats/adlib_bmf/xx_adlib_bmf_extractor.c
    src/formats/creative_cmf/xx_creative_cmf.c
    src/formats/creative_cmf/xx_creative_cmf_extractor.c
    src/formats/adlib_dfm/xx_adlib_dfm.c
    src/formats/adlib_dfm/xx_adlib_dfm_extractor.c
    src/formats/adlib_lds/xx_adlib_lds.c
    src/formats/adlib_lds/xx_adlib_lds_extractor.c
    src/formats/adlib_mkj/xx_adlib_mkj.c
    src/formats/adlib_mkj/xx_adlib_mkj_extractor.c
    src/formats/adlib_rol/xx_adlib_rol.c
    src/formats/adlib_rol/xx_adlib_rol_extractor.c
    src/formats/adlib_sa2/xx_adlib_sa2.c
    src/formats/adlib_sa2/xx_adlib_sa2_extractor.c
    src/formats/faust_fmc/xx_faust_fmc.c
    src/formats/faust_fmc/xx_faust_fmc_extractor.c
    src/formats/softstar_rix/xx_softstar_rix.c
    src/formats/softstar_rix/xx_softstar_rix_extractor.c
    src/formats/genomics_bed/xx_genomics_bed.c
    src/formats/genomics_bed/xx_genomics_bed_extractor.c
    src/formats/genomics_wiggle/xx_genomics_wiggle.c
    src/formats/genomics_wiggle/xx_genomics_wiggle_extractor.c
    src/formats/genomics_gtf/xx_genomics_gtf.c
    src/formats/genomics_gtf/xx_genomics_gtf_extractor.c
    src/formats/genomics_agp/xx_genomics_agp.c
    src/formats/genomics_agp/xx_genomics_agp_extractor.c
    src/formats/sequencing_abif/xx_sequencing_abif.c
    src/formats/sequencing_abif/xx_sequencing_abif_extractor.c
    src/formats/sequencing_scf/xx_sequencing_scf.c
    src/formats/sequencing_scf/xx_sequencing_scf_extractor.c
    src/formats/genomics_sff/xx_genomics_sff.c
    src/formats/genomics_sff/xx_genomics_sff_extractor.c
    src/formats/lut_spi1d/xx_lut_spi1d.c
    src/formats/lut_spi1d/xx_lut_spi1d_extractor.c
    src/formats/lut_spi3d/xx_lut_spi3d.c
    src/formats/lut_spi3d/xx_lut_spi3d_extractor.c
    src/formats/lut_cinespace_csp/xx_lut_cinespace_csp.c
    src/formats/lut_cinespace_csp/xx_lut_cinespace_csp_extractor.c
    src/formats/ntp_message/xx_ntp_message.c
    src/formats/ntp_message/xx_ntp_message_extractor.c
    src/formats/rtp_rtcp/xx_rtp_rtcp.c
    src/formats/rtp_rtcp/xx_rtp_rtcp_extractor.c
    src/formats/bgp_messages/xx_bgp_messages.c
    src/formats/bgp_messages/xx_bgp_messages_extractor.c
    src/formats/ospf_packet/xx_ospf_packet.c
    src/formats/ospf_packet/xx_ospf_packet_extractor.c
    src/formats/sctp_packet/xx_sctp_packet.c
    src/formats/sctp_packet/xx_sctp_packet_extractor.c
    src/formats/isakmp_message/xx_isakmp_message.c
    src/formats/isakmp_message/xx_isakmp_message_extractor.c
    src/formats/ssh_transport/xx_ssh_transport.c
    src/formats/ssh_transport/xx_ssh_transport_extractor.c
    src/formats/smtp_transcript/xx_smtp_transcript.c
    src/formats/smtp_transcript/xx_smtp_transcript_extractor.c
    src/formats/pkcs8_private_key/xx_pkcs8_private_key.c
    src/formats/pkcs8_private_key/xx_pkcs8_private_key_extractor.c
    src/formats/putty_ppk/xx_putty_ppk.c
    src/formats/putty_ppk/xx_putty_ppk_extractor.c
    src/formats/openssh_certificate/xx_openssh_certificate.c
    src/formats/openssh_certificate/xx_openssh_certificate_extractor.c
    src/formats/safetensors/xx_safetensors.c
    src/formats/safetensors/xx_safetensors_extractor.c
    src/formats/gguf/xx_gguf.c
    src/formats/gguf/xx_gguf_extractor.c
    src/formats/cdb_database/xx_cdb_database.c
    src/formats/cdb_database/xx_cdb_database_extractor.c
    src/formats/stomp_frames/xx_stomp_frames.c
    src/formats/stomp_frames/xx_stomp_frames_extractor.c
    src/formats/fontforge_sfd/xx_fontforge_sfd.c
    src/formats/fontforge_sfd/xx_fontforge_sfd_extractor.c
    src/formats/grub_pff2/xx_grub_pff2.c
    src/formats/grub_pff2/xx_grub_pff2_extractor.c
    src/formats/opengex_model/xx_opengex_model.c
    src/formats/opengex_model/xx_opengex_model_extractor.c
    src/formats/bvh_motion/xx_bvh_motion.c
    src/formats/bvh_motion/xx_bvh_motion_extractor.c
    src/formats/directx_x/xx_directx_x.c
    src/formats/directx_x/xx_directx_x_extractor.c
    src/formats/gts_surface/xx_gts_surface.c
    src/formats/gts_surface/xx_gts_surface_extractor.c
    src/formats/medit_mesh/xx_medit_mesh.c
    src/formats/medit_mesh/xx_medit_mesh_extractor.c
    src/formats/gocad_model/xx_gocad_model.c
    src/formats/gocad_model/xx_gocad_model_extractor.c
    src/formats/nastran_bulk/xx_nastran_bulk.c
    src/formats/nastran_bulk/xx_nastran_bulk_extractor.c
    src/formats/abaqus_input/xx_abaqus_input.c
    src/formats/abaqus_input/xx_abaqus_input_extractor.c
    src/formats/ensight_gold_geometry/xx_ensight_gold_geometry.c
    src/formats/ensight_gold_geometry/xx_ensight_gold_geometry_extractor.c
    src/formats/gmv_mesh/xx_gmv_mesh.c
    src/formats/gmv_mesh/xx_gmv_mesh_extractor.c
    src/formats/usgs_dem/xx_usgs_dem.c
    src/formats/usgs_dem/xx_usgs_dem_extractor.c
    src/formats/dted_elevation/xx_dted_elevation.c
    src/formats/dted_elevation/xx_dted_elevation_extractor.c
    src/formats/mapinfo_mif/xx_mapinfo_mif.c
    src/formats/mapinfo_mif/xx_mapinfo_mif_extractor.c
    src/formats/tracker_coconizer/xx_tracker_coconizer.c
    src/formats/tracker_coconizer/xx_tracker_coconizer_extractor.c
    src/formats/tracker_real/xx_tracker_real.c
    src/formats/tracker_real/xx_tracker_real_extractor.c
    src/formats/tracker_megatracker/xx_tracker_megatracker.c
    src/formats/tracker_megatracker/xx_tracker_megatracker_extractor.c
    src/formats/amos_music_bank/xx_amos_music_bank.c
    src/formats/amos_music_bank/xx_amos_music_bank_extractor.c
    src/formats/adlib_rad/xx_adlib_rad.c
    src/formats/adlib_rad/xx_adlib_rad_extractor.c
    src/formats/adlib_amd/xx_adlib_amd.c
    src/formats/adlib_amd/xx_adlib_amd_extractor.c
    src/formats/adlib_hsc/xx_adlib_hsc.c
    src/formats/adlib_hsc/xx_adlib_hsc_extractor.c
    src/formats/adlib_d00/xx_adlib_d00.c
    src/formats/adlib_d00/xx_adlib_d00_extractor.c
    src/formats/adlib_bnk/xx_adlib_bnk.c
    src/formats/adlib_bnk/xx_adlib_bnk_extractor.c
    src/formats/dosbox_dro/xx_dosbox_dro.c
    src/formats/dosbox_dro/xx_dosbox_dro_extractor.c
    src/formats/genomics_genbank/xx_genomics_genbank.c
    src/formats/genomics_genbank/xx_genomics_genbank_extractor.c
    src/formats/genomics_embl/xx_genomics_embl.c
    src/formats/genomics_embl/xx_genomics_embl_extractor.c
    src/formats/genomics_swissprot/xx_genomics_swissprot.c
    src/formats/genomics_swissprot/xx_genomics_swissprot_extractor.c
    src/formats/alignment_clustal/xx_alignment_clustal.c
    src/formats/alignment_clustal/xx_alignment_clustal_extractor.c
    src/formats/alignment_stockholm/xx_alignment_stockholm.c
    src/formats/alignment_stockholm/xx_alignment_stockholm_extractor.c
    src/formats/alignment_phylip/xx_alignment_phylip.c
    src/formats/alignment_phylip/xx_alignment_phylip_extractor.c
    src/formats/alignment_maf/xx_alignment_maf.c
    src/formats/alignment_maf/xx_alignment_maf_extractor.c
    src/formats/alignment_mauve/xx_alignment_mauve.c
    src/formats/alignment_mauve/xx_alignment_mauve_extractor.c
    src/formats/ucsc_nib/xx_ucsc_nib.c
    src/formats/ucsc_nib/xx_ucsc_nib_extractor.c
    src/formats/assembly_gfa/xx_assembly_gfa.c
    src/formats/assembly_gfa/xx_assembly_gfa_extractor.c
    src/formats/http1_message/xx_http1_message.c
    src/formats/http1_message/xx_http1_message_extractor.c
    src/formats/websocket_frames/xx_websocket_frames.c
    src/formats/websocket_frames/xx_websocket_frames_extractor.c
    src/formats/coap_message/xx_coap_message.c
    src/formats/coap_message/xx_coap_message_extractor.c
    src/formats/stun_message/xx_stun_message.c
    src/formats/stun_message/xx_stun_message_extractor.c
    src/formats/dhcp_message/xx_dhcp_message.c
    src/formats/dhcp_message/xx_dhcp_message_extractor.c
    src/formats/radius_packet/xx_radius_packet.c
    src/formats/radius_packet/xx_radius_packet_extractor.c
    src/formats/snmp_message/xx_snmp_message.c
    src/formats/snmp_message/xx_snmp_message_extractor.c
    src/formats/ldap_message/xx_ldap_message.c
    src/formats/ldap_message/xx_ldap_message_extractor.c
    src/formats/tls_records/xx_tls_records.c
    src/formats/tls_records/xx_tls_records_extractor.c
    src/formats/jks_keystore/xx_jks_keystore.c
    src/formats/jks_keystore/xx_jks_keystore_extractor.c
    src/formats/java_serialization/xx_java_serialization.c
    src/formats/java_serialization/xx_java_serialization_extractor.c
    src/formats/x509_crl/xx_x509_crl.c
    src/formats/x509_crl/xx_x509_crl_extractor.c
    src/formats/ocsp_response/xx_ocsp_response.c
    src/formats/ocsp_response/xx_ocsp_response_extractor.c
    src/formats/lmdb_data/xx_lmdb_data.c
    src/formats/lmdb_data/xx_lmdb_data_extractor.c
    src/formats/gdbm_dump/xx_gdbm_dump.c
    src/formats/gdbm_dump/xx_gdbm_dump_extractor.c
    src/formats/adobe_acb/xx_adobe_acb.c
    src/formats/adobe_acb/xx_adobe_acb_extractor.c
    src/formats/jasc_palette/xx_jasc_palette.c
    src/formats/jasc_palette/xx_jasc_palette_extractor.c
    src/formats/x11_xbm/xx_x11_xbm.c
    src/formats/x11_xbm/xx_x11_xbm_extractor.c
    src/formats/jpeg2000_pgx/xx_jpeg2000_pgx.c
    src/formats/jpeg2000_pgx/xx_jpeg2000_pgx_extractor.c
    src/formats/amiga_diskobject/xx_amiga_diskobject.c
    src/formats/amiga_diskobject/xx_amiga_diskobject_extractor.c
    src/formats/tex_vf/xx_tex_vf.c
    src/formats/tex_vf/xx_tex_vf_extractor.c
    src/formats/esri_ascii_grid/xx_esri_ascii_grid.c
    src/formats/esri_ascii_grid/xx_esri_ascii_grid_extractor.c
    src/formats/surfer_grid/xx_surfer_grid.c
    src/formats/surfer_grid/xx_surfer_grid_extractor.c
    src/formats/gxf_grid/xx_gxf_grid.c
    src/formats/gxf_grid/xx_gxf_grid_extractor.c
    src/formats/ogc_wkt/xx_ogc_wkt.c
    src/formats/ogc_wkt/xx_ogc_wkt_extractor.c
    src/formats/step_part21/xx_step_part21.c
    src/formats/step_part21/xx_step_part21_extractor.c
    src/formats/gerber_rs274x/xx_gerber_rs274x.c
    src/formats/gerber_rs274x/xx_gerber_rs274x_extractor.c
    src/formats/excellon_drill/xx_excellon_drill.c
    src/formats/excellon_drill/xx_excellon_drill_extractor.c
    src/formats/vrml_scene/xx_vrml_scene.c
    src/formats/vrml_scene/xx_vrml_scene_extractor.c
    src/formats/renderman_rib/xx_renderman_rib.c
    src/formats/renderman_rib/xx_renderman_rib_extractor.c
    src/formats/asylum_amf/xx_asylum_amf.c
    src/formats/asylum_amf/xx_asylum_amf_extractor.c
    src/formats/tracker_stx/xx_tracker_stx.c
    src/formats/tracker_stx/xx_tracker_stx_extractor.c
    src/formats/tracker_dtm/xx_tracker_dtm.c
    src/formats/tracker_dtm/xx_tracker_dtm_extractor.c
    src/formats/tracker_soundfx/xx_tracker_soundfx.c
    src/formats/tracker_soundfx/xx_tracker_soundfx_extractor.c
    src/formats/tracker_funk/xx_tracker_funk.c
    src/formats/tracker_funk/xx_tracker_funk_extractor.c
    src/formats/tracker_archimedes/xx_tracker_archimedes.c
    src/formats/tracker_archimedes/xx_tracker_archimedes_extractor.c
    src/formats/pce_psi/xx_pce_psi.c
    src/formats/pce_psi/xx_pce_psi_extractor.c
    src/formats/pc98_d88/xx_pc98_d88.c
    src/formats/pc98_d88/xx_pc98_d88_extractor.c
    src/formats/hxc_mfm/xx_hxc_mfm.c
    src/formats/hxc_mfm/xx_hxc_mfm_extractor.c
    src/formats/yaze_ydsk/xx_yaze_ydsk.c
    src/formats/yaze_ydsk/xx_yaze_ydsk_extractor.c
    src/formats/lammps_data/xx_lammps_data.c
    src/formats/lammps_data/xx_lammps_data_extractor.c
    src/formats/lammps_dump/xx_lammps_dump.c
    src/formats/lammps_dump/xx_lammps_dump_extractor.c
    src/formats/shelx_res/xx_shelx_res.c
    src/formats/shelx_res/xx_shelx_res_extractor.c
    src/formats/turbomole_coord/xx_turbomole_coord.c
    src/formats/turbomole_coord/xx_turbomole_coord_extractor.c
    src/formats/charmm_crd/xx_charmm_crd.c
    src/formats/charmm_crd/xx_charmm_crd_extractor.c
    src/formats/castep_cell/xx_castep_cell.c
    src/formats/castep_cell/xx_castep_cell_extractor.c
    src/formats/crystal_fort34/xx_crystal_fort34.c
    src/formats/crystal_fort34/xx_crystal_fort34_extractor.c
    src/formats/siesta_xv/xx_siesta_xv.c
    src/formats/siesta_xv/xx_siesta_xv_extractor.c
    src/formats/harwell_boeing/xx_harwell_boeing.c
    src/formats/harwell_boeing/xx_harwell_boeing_extractor.c
    src/formats/openfoam_points/xx_openfoam_points.c
    src/formats/openfoam_points/xx_openfoam_points_extractor.c
    src/formats/minecraft_nbt/xx_minecraft_nbt.c
    src/formats/minecraft_nbt/xx_minecraft_nbt_extractor.c
    src/formats/amazon_ion_binary/xx_amazon_ion_binary.c
    src/formats/amazon_ion_binary/xx_amazon_ion_binary_extractor.c
    src/formats/leveldb_log/xx_leveldb_log.c
    src/formats/leveldb_log/xx_leveldb_log_extractor.c
    src/formats/dns_message/xx_dns_message.c
    src/formats/dns_message/xx_dns_message_extractor.c
    src/formats/rocksdb_blob/xx_rocksdb_blob.c
    src/formats/rocksdb_blob/xx_rocksdb_blob_extractor.c
    src/formats/mongodb_wire/xx_mongodb_wire.c
    src/formats/mongodb_wire/xx_mongodb_wire_extractor.c
    src/formats/redis_resp/xx_redis_resp.c
    src/formats/redis_resp/xx_redis_resp_extractor.c
    src/formats/mqtt_packets/xx_mqtt_packets.c
    src/formats/mqtt_packets/xx_mqtt_packets_extractor.c
    src/formats/amqp_frames/xx_amqp_frames.c
    src/formats/amqp_frames/xx_amqp_frames_extractor.c
    src/formats/thrift_compact/xx_thrift_compact.c
    src/formats/thrift_compact/xx_thrift_compact_extractor.c
    src/formats/x509_certificate/xx_x509_certificate.c
    src/formats/x509_certificate/xx_x509_certificate_extractor.c
    src/formats/pkcs10_csr/xx_pkcs10_csr.c
    src/formats/pkcs10_csr/xx_pkcs10_csr_extractor.c
    src/formats/pkcs12_pfx/xx_pkcs12_pfx.c
    src/formats/pkcs12_pfx/xx_pkcs12_pfx_extractor.c
    src/formats/openssh_private_key/xx_openssh_private_key.c
    src/formats/openssh_private_key/xx_openssh_private_key_extractor.c
    src/formats/kerberos_keytab/xx_kerberos_keytab.c
    src/formats/kerberos_keytab/xx_kerberos_keytab_extractor.c
    src/formats/gimp_gpl/xx_gimp_gpl.c
    src/formats/gimp_gpl/xx_gimp_gpl_extractor.c
    src/formats/gimp_ggr/xx_gimp_ggr.c
    src/formats/gimp_ggr/xx_gimp_ggr_extractor.c
    src/formats/iridas_cube_lut/xx_iridas_cube_lut.c
    src/formats/iridas_cube_lut/xx_iridas_cube_lut_extractor.c
    src/formats/hpgl_plot/xx_hpgl_plot.c
    src/formats/hpgl_plot/xx_hpgl_plot_extractor.c
    src/formats/paintshop_psp/xx_paintshop_psp.c
    src/formats/paintshop_psp/xx_paintshop_psp_extractor.c
    src/formats/photoshop_pat/xx_photoshop_pat.c
    src/formats/photoshop_pat/xx_photoshop_pat_extractor.c
    src/formats/mmd_pmx/xx_mmd_pmx.c
    src/formats/mmd_pmx/xx_mmd_pmx_extractor.c
    src/formats/metasequoia_mqo/xx_metasequoia_mqo.c
    src/formats/metasequoia_mqo/xx_metasequoia_mqo_extractor.c
    src/formats/calma_gdsii/xx_calma_gdsii.c
    src/formats/calma_gdsii/xx_calma_gdsii_extractor.c
    src/formats/autodesk_ase/xx_autodesk_ase.c
    src/formats/autodesk_ase/xx_autodesk_ase_extractor.c
    src/formats/freesurfer_surface/xx_freesurfer_surface.c
    src/formats/freesurfer_surface/xx_freesurfer_surface_extractor.c
    src/formats/gmsh_msh/xx_gmsh_msh.c
    src/formats/gmsh_msh/xx_gmsh_msh_extractor.c
    src/formats/netgen_vol/xx_netgen_vol.c
    src/formats/netgen_vol/xx_netgen_vol_extractor.c
    src/formats/font_afm/xx_font_afm.c
    src/formats/font_afm/xx_font_afm_extractor.c
    src/formats/tiled_tmx/xx_tiled_tmx.c
    src/formats/tiled_tmx/xx_tiled_tmx_extractor.c
    src/formats/nintendo_sdat/xx_nintendo_sdat.c
    src/formats/nintendo_sdat/xx_nintendo_sdat_extractor.c
    src/formats/sony_vab/xx_sony_vab.c
    src/formats/sony_vab/xx_sony_vab_extractor.c
    src/formats/yamaha_ym/xx_yamaha_ym.c
    src/formats/yamaha_ym/xx_yamaha_ym_extractor.c
    src/formats/zx_ayemul/xx_zx_ayemul.c
    src/formats/zx_ayemul/xx_zx_ayemul_extractor.c
    src/formats/dragon_vdk/xx_dragon_vdk.c
    src/formats/dragon_vdk/xx_dragon_vdk_extractor.c
    src/formats/apple_a2r/xx_apple_a2r.c
    src/formats/apple_a2r/xx_apple_a2r_extractor.c
    src/formats/atari_atr/xx_atari_atr.c
    src/formats/atari_atr/xx_atari_atr_extractor.c
    src/formats/atari_pasti_stx/xx_atari_pasti_stx.c
    src/formats/atari_pasti_stx/xx_atari_pasti_stx_extractor.c
    src/formats/amiga_ipf/xx_amiga_ipf.c
    src/formats/amiga_ipf/xx_amiga_ipf_extractor.c
    src/formats/tracker_dtt/xx_tracker_dtt.c
    src/formats/tracker_dtt/xx_tracker_dtt_extractor.c
    src/formats/gaussian_cube/xx_gaussian_cube.c
    src/formats/gaussian_cube/xx_gaussian_cube_extractor.c
    src/formats/molecule_xyz/xx_molecule_xyz.c
    src/formats/molecule_xyz/xx_molecule_xyz_extractor.c
    src/formats/mdl_molfile/xx_mdl_molfile.c
    src/formats/mdl_molfile/xx_mdl_molfile_extractor.c
    src/formats/tripos_mol2/xx_tripos_mol2.c
    src/formats/tripos_mol2/xx_tripos_mol2_extractor.c
    src/formats/xcrysden_xsf/xx_xcrysden_xsf.c
    src/formats/xcrysden_xsf/xx_xcrysden_xsf_extractor.c
    src/formats/amber_prmtop/xx_amber_prmtop.c
    src/formats/amber_prmtop/xx_amber_prmtop_extractor.c
    src/formats/amber_restart/xx_amber_restart.c
    src/formats/amber_restart/xx_amber_restart_extractor.c
    src/formats/gaussian_fchk/xx_gaussian_fchk.c
    src/formats/gaussian_fchk/xx_gaussian_fchk_extractor.c
    src/formats/jcamp_dx/xx_jcamp_dx.c
    src/formats/jcamp_dx/xx_jcamp_dx_extractor.c
    src/formats/dl_poly_config/xx_dl_poly_config.c
    src/formats/dl_poly_config/xx_dl_poly_config_extractor.c
    src/formats/nix_nar/xx_nix_nar.c
    src/formats/nix_nar/xx_nix_nar_extractor.c
    src/formats/redis_rdb/xx_redis_rdb.c
    src/formats/redis_rdb/xx_redis_rdb_extractor.c
    src/formats/postgres_custom/xx_postgres_custom.c
    src/formats/postgres_custom/xx_postgres_custom_extractor.c
    src/formats/mysql_binlog/xx_mysql_binlog.c
    src/formats/mysql_binlog/xx_mysql_binlog_extractor.c
    src/formats/kafka_record_batch/xx_kafka_record_batch.c
    src/formats/kafka_record_batch/xx_kafka_record_batch_extractor.c
    src/formats/android_binary_xml/xx_android_binary_xml.c
    src/formats/android_binary_xml/xx_android_binary_xml_extractor.c
    src/formats/android_resources_arsc/xx_android_resources_arsc.c
    src/formats/android_resources_arsc/xx_android_resources_arsc_extractor.c
    src/formats/msgpack/xx_msgpack.c
    src/formats/msgpack/xx_msgpack_extractor.c
    src/formats/ubjson/xx_ubjson.c
    src/formats/ubjson/xx_ubjson_extractor.c
    src/formats/bittorrent_metainfo/xx_bittorrent_metainfo.c
    src/formats/bittorrent_metainfo/xx_bittorrent_metainfo_extractor.c
    src/formats/erlang_external_term/xx_erlang_external_term.c
    src/formats/erlang_external_term/xx_erlang_external_term_extractor.c
    src/formats/capnproto_message/xx_capnproto_message.c
    src/formats/capnproto_message/xx_capnproto_message_extractor.c
    src/formats/dbus_message/xx_dbus_message.c
    src/formats/dbus_message/xx_dbus_message_extractor.c
    src/formats/windows_shell_link/xx_windows_shell_link.c
    src/formats/windows_shell_link/xx_windows_shell_link_extractor.c
    src/formats/pkcs7_cms/xx_pkcs7_cms.c
    src/formats/pkcs7_cms/xx_pkcs7_cms_extractor.c
    src/formats/wavefront_obj/xx_wavefront_obj.c
    src/formats/wavefront_obj/xx_wavefront_obj_extractor.c
    src/formats/off_mesh/xx_off_mesh.c
    src/formats/off_mesh/xx_off_mesh_extractor.c
    src/formats/ac3d_model/xx_ac3d_model.c
    src/formats/ac3d_model/xx_ac3d_model_extractor.c
    src/formats/qubicle_qb/xx_qubicle_qb.c
    src/formats/qubicle_qb/xx_qubicle_qb_extractor.c
    src/formats/terragen_ter/xx_terragen_ter.c
    src/formats/terragen_ter/xx_terragen_ter_extractor.c
    src/formats/gimp_xcf/xx_gimp_xcf.c
    src/formats/gimp_xcf/xx_gimp_xcf_extractor.c
    src/formats/photoshop_abr/xx_photoshop_abr.c
    src/formats/photoshop_abr/xx_photoshop_abr_extractor.c
    src/formats/softimage_pic/xx_softimage_pic.c
    src/formats/softimage_pic/xx_softimage_pic_extractor.c
    src/formats/alias_pix/xx_alias_pix.c
    src/formats/alias_pix/xx_alias_pix_extractor.c
    src/formats/qt_qpicture/xx_qt_qpicture.c
    src/formats/qt_qpicture/xx_qt_qpicture_extractor.c
    src/formats/font_type1_pfb/xx_font_type1_pfb.c
    src/formats/font_type1_pfb/xx_font_type1_pfb_extractor.c
    src/formats/font_gem_fnt/xx_font_gem_fnt.c
    src/formats/font_gem_fnt/xx_font_gem_fnt_extractor.c
    src/formats/tex_gf/xx_tex_gf.c
    src/formats/tex_gf/xx_tex_gf_extractor.c
    src/formats/bpg_image/xx_bpg_image.c
    src/formats/bpg_image/xx_bpg_image_extractor.c
    src/formats/mng_animation/xx_mng_animation.c
    src/formats/mng_animation/xx_mng_animation_extractor.c
    src/formats/atari_7800_a78/xx_atari_7800_a78.c
    src/formats/atari_7800_a78/xx_atari_7800_a78_extractor.c
    src/formats/commodore_pc64/xx_commodore_pc64.c
    src/formats/commodore_pc64/xx_commodore_pc64_extractor.c
    src/formats/atari_cas/xx_atari_cas.c
    src/formats/atari_cas/xx_atari_cas_extractor.c
    src/formats/msx_cas/xx_msx_cas.c
    src/formats/msx_cas/xx_msx_cas_extractor.c
    src/formats/oric_tap/xx_oric_tap.c
    src/formats/oric_tap/xx_oric_tap_extractor.c
    src/formats/dragon_cas/xx_dragon_cas.c
    src/formats/dragon_cas/xx_dragon_cas_extractor.c
    src/formats/amiga_ahx/xx_amiga_ahx.c
    src/formats/amiga_ahx/xx_amiga_ahx_extractor.c
    src/formats/amstrad_cpc_sna/xx_amstrad_cpc_sna.c
    src/formats/amstrad_cpc_sna/xx_amstrad_cpc_sna_extractor.c
    src/formats/vtech_vz/xx_vtech_vz.c
    src/formats/vtech_vz/xx_vtech_vz_extractor.c
    src/formats/zx_hobeta/xx_zx_hobeta.c
    src/formats/zx_hobeta/xx_zx_hobeta_extractor.c
    src/formats/genomics_fasta/xx_genomics_fasta.c
    src/formats/genomics_fasta/xx_genomics_fasta_extractor.c
    src/formats/genomics_fastq/xx_genomics_fastq.c
    src/formats/genomics_fastq/xx_genomics_fastq_extractor.c
    src/formats/genomics_sam/xx_genomics_sam.c
    src/formats/genomics_sam/xx_genomics_sam_extractor.c
    src/formats/opendx_field/xx_opendx_field.c
    src/formats/opendx_field/xx_opendx_field_extractor.c
    src/formats/genomics_vcf/xx_genomics_vcf.c
    src/formats/genomics_vcf/xx_genomics_vcf_extractor.c
    src/formats/genomics_gff3/xx_genomics_gff3.c
    src/formats/genomics_gff3/xx_genomics_gff3_extractor.c
    src/formats/protein_pdb/xx_protein_pdb.c
    src/formats/protein_pdb/xx_protein_pdb_extractor.c
    src/formats/protein_mmcif/xx_protein_mmcif.c
    src/formats/protein_mmcif/xx_protein_mmcif_extractor.c
    src/formats/matrix_market/xx_matrix_market.c
    src/formats/matrix_market/xx_matrix_market_extractor.c
    src/formats/gromacs_gro/xx_gromacs_gro.c
    src/formats/gromacs_gro/xx_gromacs_gro_extractor.c
    src/formats/microsoft_msf/xx_microsoft_msf.c
    src/formats/microsoft_msf/xx_microsoft_msf_extractor.c
    src/formats/windows_registry_hive/xx_windows_registry_hive.c
    src/formats/windows_registry_hive/xx_windows_registry_hive_extractor.c
    src/formats/windows_evtx/xx_windows_evtx.c
    src/formats/windows_evtx/xx_windows_evtx_extractor.c
    src/formats/binary_plist/xx_binary_plist.c
    src/formats/binary_plist/xx_binary_plist_extractor.c
    src/formats/mongodb_bson/xx_mongodb_bson.c
    src/formats/mongodb_bson/xx_mongodb_bson_extractor.c
    src/formats/cbor/xx_cbor.c
    src/formats/cbor/xx_cbor_extractor.c
    src/formats/openzim/xx_openzim.c
    src/formats/openzim/xx_openzim_extractor.c
    src/formats/apache_orc/xx_apache_orc.c
    src/formats/apache_orc/xx_apache_orc_extractor.c
    src/formats/hadoop_sequencefile/xx_hadoop_sequencefile.c
    src/formats/hadoop_sequencefile/xx_hadoop_sequencefile_extractor.c
    src/formats/leveldb_sstable/xx_leveldb_sstable.c
    src/formats/leveldb_sstable/xx_leveldb_sstable_extractor.c
    src/formats/snappy_framed/xx_snappy_framed.c
    src/formats/snappy_framed/xx_snappy_framed_extractor.c
    src/formats/lzf_stream/xx_lzf_stream.c
    src/formats/lzf_stream/xx_lzf_stream_extractor.c
    src/formats/fastlz_sixpack/xx_fastlz_sixpack.c
    src/formats/fastlz_sixpack/xx_fastlz_sixpack_extractor.c
    src/formats/linux_btf/xx_linux_btf.c
    src/formats/linux_btf/xx_linux_btf_extractor.c
    src/formats/flatgeobuf/xx_flatgeobuf.c
    src/formats/flatgeobuf/xx_flatgeobuf_extractor.c
    src/formats/astc_texture/xx_astc_texture.c
    src/formats/astc_texture/xx_astc_texture_extractor.c
    src/formats/pkm_texture/xx_pkm_texture.c
    src/formats/pkm_texture/xx_pkm_texture_extractor.c
    src/formats/basis_texture/xx_basis_texture.c
    src/formats/basis_texture/xx_basis_texture_extractor.c
    src/formats/openctm_mesh/xx_openctm_mesh.c
    src/formats/openctm_mesh/xx_openctm_mesh_extractor.c
    src/formats/font_bdf/xx_font_bdf.c
    src/formats/font_bdf/xx_font_bdf_extractor.c
    src/formats/font_pcf/xx_font_pcf.c
    src/formats/font_pcf/xx_font_pcf_extractor.c
    src/formats/font_psf/xx_font_psf.c
    src/formats/font_psf/xx_font_psf_extractor.c
    src/formats/font_windows_fnt/xx_font_windows_fnt.c
    src/formats/font_windows_fnt/xx_font_windows_fnt_extractor.c
    src/formats/tex_tfm/xx_tex_tfm.c
    src/formats/tex_tfm/xx_tex_tfm_extractor.c
    src/formats/tex_pk/xx_tex_pk.c
    src/formats/tex_pk/xx_tex_pk_extractor.c
    src/formats/tex_dvi/xx_tex_dvi.c
    src/formats/tex_dvi/xx_tex_dvi_extractor.c
    src/formats/netpbm_pfm/xx_netpbm_pfm.c
    src/formats/netpbm_pfm/xx_netpbm_pfm_extractor.c
    src/formats/steinberg_vst3preset/xx_steinberg_vst3preset.c
    src/formats/steinberg_vst3preset/xx_steinberg_vst3preset_extractor.c
    src/formats/font_bmfont/xx_font_bmfont.c
    src/formats/font_bmfont/xx_font_bmfont_extractor.c
    src/formats/processing_vlw/xx_processing_vlw.c
    src/formats/processing_vlw/xx_processing_vlw_extractor.c
    src/formats/snes_spc/xx_snes_spc.c
    src/formats/snes_spc/xx_snes_spc_extractor.c
    src/formats/gameboy_gbs/xx_gameboy_gbs.c
    src/formats/gameboy_gbs/xx_gameboy_gbs_extractor.c
    src/formats/sega_sgc/xx_sega_sgc.c
    src/formats/sega_sgc/xx_sega_sgc_extractor.c
    src/formats/s98_log/xx_s98_log.c
    src/formats/s98_log/xx_s98_log_extractor.c
    src/formats/atari_sap/xx_atari_sap.c
    src/formats/atari_sap/xx_atari_sap_extractor.c
    src/formats/sc68_music/xx_sc68_music.c
    src/formats/sc68_music/xx_sc68_music_extractor.c
    src/formats/zx_spectrum_pzx/xx_zx_spectrum_pzx.c
    src/formats/zx_spectrum_pzx/xx_zx_spectrum_pzx_extractor.c
    src/formats/acorn_uef/xx_acorn_uef.c
    src/formats/acorn_uef/xx_acorn_uef_extractor.c
    src/formats/nintendo_unif/xx_nintendo_unif.c
    src/formats/nintendo_unif/xx_nintendo_unif_extractor.c
    src/formats/nintendo_fds/xx_nintendo_fds.c
    src/formats/nintendo_fds/xx_nintendo_fds_extractor.c
    src/formats/ucsc_bigwig/xx_ucsc_bigwig.c
    src/formats/ucsc_bigwig/xx_ucsc_bigwig_extractor.c
    src/formats/ucsc_bigbed/xx_ucsc_bigbed.c
    src/formats/ucsc_bigbed/xx_ucsc_bigbed_extractor.c
    src/formats/phylo_nexus/xx_phylo_nexus.c
    src/formats/phylo_nexus/xx_phylo_nexus_extractor.c
    src/formats/phylo_newick/xx_phylo_newick.c
    src/formats/phylo_newick/xx_phylo_newick_extractor.c
    src/formats/sqlite_rollback_journal/xx_sqlite_rollback_journal.c
    src/formats/sqlite_rollback_journal/xx_sqlite_rollback_journal_extractor.c
    src/formats/neuroscan_cnt/xx_neuroscan_cnt.c
    src/formats/neuroscan_cnt/xx_neuroscan_cnt_extractor.c
    src/formats/axona_tetrode/xx_axona_tetrode.c
    src/formats/axona_tetrode/xx_axona_tetrode_extractor.c
    src/formats/python_pickle/xx_python_pickle.c
    src/formats/python_pickle/xx_python_pickle_extractor.c
    src/formats/inivation_aedat/xx_inivation_aedat.c
    src/formats/inivation_aedat/xx_inivation_aedat_extractor.c
    src/formats/python_marshal/xx_python_marshal.c
    src/formats/python_marshal/xx_python_marshal_extractor.c
    src/formats/vice_x64/xx_vice_x64.c
    src/formats/vice_x64/xx_vice_x64_extractor.c
    src/formats/vice_snapshot/xx_vice_snapshot.c
    src/formats/vice_snapshot/xx_vice_snapshot_extractor.c
    src/formats/commodore_g64/xx_commodore_g64.c
    src/formats/commodore_g64/xx_commodore_g64_extractor.c
    src/formats/commodore_p64/xx_commodore_p64.c
    src/formats/commodore_p64/xx_commodore_p64_extractor.c
    src/formats/commodore_tap/xx_commodore_tap.c
    src/formats/commodore_tap/xx_commodore_tap_extractor.c
    src/formats/zx_spectrum_tzx/xx_zx_spectrum_tzx.c
    src/formats/zx_spectrum_tzx/xx_zx_spectrum_tzx_extractor.c
    src/formats/zx_spectrum_szx/xx_zx_spectrum_szx.c
    src/formats/zx_spectrum_szx/xx_zx_spectrum_szx_extractor.c
    src/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.c
    src/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk_extractor.c
    src/formats/atari_st_msa/xx_atari_st_msa.c
    src/formats/atari_st_msa/xx_atari_st_msa_extractor.c
    src/formats/supercard_scp/xx_supercard_scp.c
    src/formats/supercard_scp/xx_supercard_scp_extractor.c
    src/formats/apple_woz/xx_apple_woz.c
    src/formats/apple_woz/xx_apple_woz_extractor.c
    src/formats/nintendo_nsf/xx_nintendo_nsf.c
    src/formats/nintendo_nsf/xx_nintendo_nsf_extractor.c
    src/formats/vgm_log/xx_vgm_log.c
    src/formats/vgm_log/xx_vgm_log_extractor.c
    src/formats/psid_sid/xx_psid_sid.c
    src/formats/psid_sid/xx_psid_sid_extractor.c
    src/formats/hes_sound/xx_hes_sound.c
    src/formats/hes_sound/xx_hes_sound_extractor.c
    src/formats/audio_dolby_ac3/xx_audio_dolby_ac3.c
    src/formats/audio_dolby_ac3/xx_audio_dolby_ac3_extractor.c
    src/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3.c
    src/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3_extractor.c
    src/formats/audio_aac_adts/xx_audio_aac_adts.c
    src/formats/audio_aac_adts/xx_audio_aac_adts_extractor.c
    src/formats/audio_monkeys_ape/xx_audio_monkeys_ape.c
    src/formats/audio_monkeys_ape/xx_audio_monkeys_ape_extractor.c
    src/formats/mpeg_transport_stream/xx_mpeg_transport_stream.c
    src/formats/mpeg_transport_stream/xx_mpeg_transport_stream_extractor.c
    src/formats/mpeg_program_stream/xx_mpeg_program_stream.c
    src/formats/mpeg_program_stream/xx_mpeg_program_stream_extractor.c
    src/formats/realmedia_rm/xx_realmedia_rm.c
    src/formats/realmedia_rm/xx_realmedia_rm_extractor.c
    src/formats/idtech_roq/xx_idtech_roq.c
    src/formats/idtech_roq/xx_idtech_roq_extractor.c
    src/formats/rad_bink/xx_rad_bink.c
    src/formats/rad_bink/xx_rad_bink_extractor.c
    src/formats/rad_smacker/xx_rad_smacker.c
    src/formats/rad_smacker/xx_rad_smacker_extractor.c
    src/formats/interplay_mve/xx_interplay_mve.c
    src/formats/interplay_mve/xx_interplay_mve_extractor.c
    src/formats/westwood_vqa/xx_westwood_vqa.c
    src/formats/westwood_vqa/xx_westwood_vqa_extractor.c
    src/formats/autodesk_flic/xx_autodesk_flic.c
    src/formats/autodesk_flic/xx_autodesk_flic_extractor.c
    src/formats/idtech_md5anim/xx_idtech_md5anim.c
    src/formats/idtech_md5anim/xx_idtech_md5anim_extractor.c
    src/formats/stereolithography_stl/xx_stereolithography_stl.c
    src/formats/stereolithography_stl/xx_stereolithography_stl_extractor.c
    src/formats/garmin_fit/xx_garmin_fit.c
    src/formats/garmin_fit/xx_garmin_fit_extractor.c
    src/formats/rosbag1/xx_rosbag1.c
    src/formats/rosbag1/xx_rosbag1_extractor.c
    src/formats/mcap/xx_mcap.c
    src/formats/mcap/xx_mcap_extractor.c
    src/formats/seismic_sac/xx_seismic_sac.c
    src/formats/seismic_sac/xx_seismic_sac_extractor.c
    src/formats/seismic_seg2/xx_seismic_seg2.c
    src/formats/seismic_seg2/xx_seismic_seg2_extractor.c
    src/formats/ucsc_twobit/xx_ucsc_twobit.c
    src/formats/ucsc_twobit/xx_ucsc_twobit_extractor.c
    src/formats/genomics_bgen/xx_genomics_bgen.c
    src/formats/genomics_bgen/xx_genomics_bgen_extractor.c
    src/formats/openephys_continuous/xx_openephys_continuous.c
    src/formats/openephys_continuous/xx_openephys_continuous_extractor.c
    src/formats/mountainsort_mda/xx_mountainsort_mda.c
    src/formats/mountainsort_mda/xx_mountainsort_mda_extractor.c
    src/formats/igor_ibw/xx_igor_ibw.c
    src/formats/igor_ibw/xx_igor_ibw_extractor.c
    src/formats/princeton_spe/xx_princeton_spe.c
    src/formats/princeton_spe/xx_princeton_spe_extractor.c
    src/formats/microscopy_spider/xx_microscopy_spider.c
    src/formats/microscopy_spider/xx_microscopy_spider_extractor.c
    src/formats/wmo_grib/xx_wmo_grib.c
    src/formats/wmo_grib/xx_wmo_grib_extractor.c
    src/formats/wmo_bufr/xx_wmo_bufr.c
    src/formats/wmo_bufr/xx_wmo_bufr_extractor.c
    src/formats/autocad_dxf/xx_autocad_dxf.c
    src/formats/autocad_dxf/xx_autocad_dxf_extractor.c
    src/formats/blackrock_nsx/xx_blackrock_nsx.c
    src/formats/blackrock_nsx/xx_blackrock_nsx_extractor.c
    src/formats/blackrock_nev/xx_blackrock_nev.c
    src/formats/blackrock_nev/xx_blackrock_nev_extractor.c
    src/formats/lecroy_trc/xx_lecroy_trc.c
    src/formats/lecroy_trc/xx_lecroy_trc_extractor.c
    src/formats/tektronix_isf/xx_tektronix_isf.c
    src/formats/tektronix_isf/xx_tektronix_isf_extractor.c
    src/formats/ircam_sdif/xx_ircam_sdif.c
    src/formats/ircam_sdif/xx_ircam_sdif_extractor.c
    src/formats/tracker_liquid/xx_tracker_liquid.c
    src/formats/tracker_liquid/xx_tracker_liquid_extractor.c
    src/formats/tracker_dmf/xx_tracker_dmf.c
    src/formats/tracker_dmf/xx_tracker_dmf_extractor.c
    src/formats/tracker_ptm/xx_tracker_ptm.c
    src/formats/tracker_ptm/xx_tracker_ptm_extractor.c
    src/formats/tracker_ams/xx_tracker_ams.c
    src/formats/tracker_ams/xx_tracker_ams_extractor.c
    src/formats/tracker_digi/xx_tracker_digi.c
    src/formats/tracker_digi/xx_tracker_digi_extractor.c
    src/formats/tracker_emod/xx_tracker_emod.c
    src/formats/tracker_emod/xx_tracker_emod_extractor.c
    src/formats/tracker_mt2/xx_tracker_mt2.c
    src/formats/tracker_mt2/xx_tracker_mt2_extractor.c
    src/formats/audio_dsf/xx_audio_dsf.c
    src/formats/audio_dsf/xx_audio_dsf_extractor.c
    src/formats/audio_dff/xx_audio_dff.c
    src/formats/audio_dff/xx_audio_dff_extractor.c
    src/formats/audio_wave64/xx_audio_wave64.c
    src/formats/audio_wave64/xx_audio_wave64_extractor.c
    src/formats/audio_adx/xx_audio_adx.c
    src/formats/audio_adx/xx_audio_adx_extractor.c
    src/formats/audio_ast/xx_audio_ast.c
    src/formats/audio_ast/xx_audio_ast_extractor.c
    src/formats/audio_hca/xx_audio_hca.c
    src/formats/audio_hca/xx_audio_hca_extractor.c
    src/formats/iff_8svx/xx_iff_8svx.c
    src/formats/iff_8svx/xx_iff_8svx_extractor.c
    src/formats/audio_wavpack/xx_audio_wavpack.c
    src/formats/audio_wavpack/xx_audio_wavpack_extractor.c
    src/formats/blender_blend/xx_blender_blend.c
    src/formats/blender_blend/xx_blender_blend_extractor.c
    src/formats/autodesk_fbx/xx_autodesk_fbx.c
    src/formats/autodesk_fbx/xx_autodesk_fbx_extractor.c
    src/formats/autodesk_3ds/xx_autodesk_3ds.c
    src/formats/autodesk_3ds/xx_autodesk_3ds_extractor.c
    src/formats/lightwave_lwo2/xx_lightwave_lwo2.c
    src/formats/lightwave_lwo2/xx_lightwave_lwo2_extractor.c
    src/formats/lightwave_mdd/xx_lightwave_mdd.c
    src/formats/lightwave_mdd/xx_lightwave_mdd_extractor.c
    src/formats/sony_psp_pbp/xx_sony_psp_pbp.c
    src/formats/sony_psp_pbp/xx_sony_psp_pbp_extractor.c
    src/formats/flash_video_flv/xx_flash_video_flv.c
    src/formats/flash_video_flv/xx_flash_video_flv_extractor.c
    src/formats/nintendo_n64_rom/xx_nintendo_n64_rom.c
    src/formats/nintendo_n64_rom/xx_nintendo_n64_rom_extractor.c
    src/formats/nintendo_gb_rom/xx_nintendo_gb_rom.c
    src/formats/nintendo_gb_rom/xx_nintendo_gb_rom_extractor.c
    src/formats/nintendo_gba_rom/xx_nintendo_gba_rom.c
    src/formats/nintendo_gba_rom/xx_nintendo_gba_rom_extractor.c
    src/formats/sega_megadrive_rom/xx_sega_megadrive_rom.c
    src/formats/sega_megadrive_rom/xx_sega_megadrive_rom_extractor.c
    src/formats/spring_s3o/xx_spring_s3o.c
    src/formats/spring_s3o/xx_spring_s3o_extractor.c
    src/formats/xna_xnb/xx_xna_xnb.c
    src/formats/xna_xnb/xx_xna_xnb_extractor.c
    src/formats/lua_bytecode51/xx_lua_bytecode51.c
    src/formats/lua_bytecode51/xx_lua_bytecode51_extractor.c
    src/formats/quake_md5mesh/xx_quake_md5mesh.c
    src/formats/quake_md5mesh/xx_quake_md5mesh_extractor.c
    src/formats/tracker_mod/xx_tracker_mod.c
    src/formats/tracker_mod/xx_tracker_mod_extractor.c
    src/formats/tracker_far/xx_tracker_far.c
    src/formats/tracker_far/xx_tracker_far_extractor.c
    src/formats/tracker_mdl/xx_tracker_mdl.c
    src/formats/tracker_mdl/xx_tracker_mdl_extractor.c
    src/formats/tracker_gdm/xx_tracker_gdm.c
    src/formats/tracker_gdm/xx_tracker_gdm_extractor.c
    src/formats/tracker_dbm/xx_tracker_dbm.c
    src/formats/tracker_dbm/xx_tracker_dbm_extractor.c
    src/formats/tracker_med/xx_tracker_med.c
    src/formats/tracker_med/xx_tracker_med_extractor.c
    src/formats/tracker_imf/xx_tracker_imf.c
    src/formats/tracker_imf/xx_tracker_imf_extractor.c
    src/formats/tracker_amf/xx_tracker_amf.c
    src/formats/tracker_amf/xx_tracker_amf_extractor.c
    src/formats/tracker_psm/xx_tracker_psm.c
    src/formats/tracker_psm/xx_tracker_psm_extractor.c
    src/formats/steinberg_fxb/xx_steinberg_fxb.c
    src/formats/steinberg_fxb/xx_steinberg_fxb_extractor.c
    src/formats/astronomy_ser/xx_astronomy_ser.c
    src/formats/astronomy_ser/xx_astronomy_ser_extractor.c
    src/formats/photontiming_ptu/xx_photontiming_ptu.c
    src/formats/photontiming_ptu/xx_photontiming_ptu_extractor.c
    src/formats/photontiming_phu/xx_photontiming_phu.c
    src/formats/photontiming_phu/xx_photontiming_phu_extractor.c
    src/formats/charmm_dcd/xx_charmm_dcd.c
    src/formats/charmm_dcd/xx_charmm_dcd_extractor.c
    src/formats/gromacs_trr/xx_gromacs_trr.c
    src/formats/gromacs_trr/xx_gromacs_trr_extractor.c
    src/formats/microscopy_ics/xx_microscopy_ics.c
    src/formats/microscopy_ics/xx_microscopy_ics_extractor.c
    src/formats/tecplot_plt/xx_tecplot_plt.c
    src/formats/tecplot_plt/xx_tecplot_plt_extractor.c
    src/formats/fujifilm_raf/xx_fujifilm_raf.c
    src/formats/fujifilm_raf/xx_fujifilm_raf_extractor.c
    src/formats/sigma_x3f/xx_sigma_x3f.c
    src/formats/sigma_x3f/xx_sigma_x3f_extractor.c
    src/formats/minolta_mrw/xx_minolta_mrw.c
    src/formats/minolta_mrw/xx_minolta_mrw_extractor.c
    src/formats/sfx_imp/xx_sfx_imp.c
    src/formats/sfx_imp/xx_sfx_imp_extractor.c
    src/formats/sfx_red/xx_sfx_red.c
    src/formats/sfx_red/xx_sfx_red_extractor.c
    src/formats/sfx_ha/xx_sfx_ha.c
    src/formats/sfx_ha/xx_sfx_ha_extractor.c
    src/formats/sfx_lzx/xx_sfx_lzx.c
    src/formats/sfx_lzx/xx_sfx_lzx_extractor.c
    src/formats/sfx_sqx/xx_sfx_sqx.c
    src/formats/sfx_sqx/xx_sfx_sqx_extractor.c
    src/formats/sfx_ain/xx_sfx_ain.c
    src/formats/sfx_ain/xx_sfx_ain_extractor.c
    src/formats/sfx_hap/xx_sfx_hap.c
    src/formats/sfx_hap/xx_sfx_hap_extractor.c
    src/formats/sfx_zoo/xx_sfx_zoo.c
    src/formats/sfx_zoo/xx_sfx_zoo_extractor.c
    src/formats/sfx_cazip/xx_sfx_cazip.c
    src/formats/sfx_cazip/xx_sfx_cazip_extractor.c
    src/formats/sfx_tgcf/xx_sfx_tgcf.c
    src/formats/sfx_tgcf/xx_sfx_tgcf_extractor.c
    src/formats/sfx_starkit/xx_sfx_starkit.c
    src/formats/sfx_starkit/xx_sfx_starkit_extractor.c
    src/formats/sfx_alz/xx_sfx_alz.c
    src/formats/sfx_alz/xx_sfx_alz_extractor.c
    src/formats/sfx_chm/xx_sfx_chm.c
    src/formats/sfx_chm/xx_sfx_chm_extractor.c
    src/formats/egg/xx_egg.c
    src/formats/egg/xx_egg_extractor.c
    src/formats/nufx/xx_nufx.c
    src/formats/nufx/xx_nufx_extractor.c
    src/formats/nintendo_dol/xx_nintendo_dol.c
    src/formats/nintendo_dol/xx_nintendo_dol_extractor.c
    src/formats/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.c
    src/formats/nintendo_j3d_bmd/xx_nintendo_j3d_bmd_extractor.c
    src/formats/nintendo_j3d_btk/xx_nintendo_j3d_btk.c
    src/formats/nintendo_j3d_btk/xx_nintendo_j3d_btk_extractor.c
    src/formats/nintendo_brstm/xx_nintendo_brstm.c
    src/formats/nintendo_brstm/xx_nintendo_brstm_extractor.c
    src/formats/nintendo_brwav/xx_nintendo_brwav.c
    src/formats/nintendo_brwav/xx_nintendo_brwav_extractor.c
    src/formats/nintendo_brlyt/xx_nintendo_brlyt.c
    src/formats/nintendo_brlyt/xx_nintendo_brlyt_extractor.c
    src/formats/nintendo_brlan/xx_nintendo_brlan.c
    src/formats/nintendo_brlan/xx_nintendo_brlan_extractor.c
    src/formats/nintendo_bfsha/xx_nintendo_bfsha.c
    src/formats/nintendo_bfsha/xx_nintendo_bfsha_extractor.c
    src/formats/cri_usm/xx_cri_usm.c
    src/formats/cri_usm/xx_cri_usm_extractor.c
    src/formats/cri_utf/xx_cri_utf.c
    src/formats/cri_utf/xx_cri_utf_extractor.c
    src/formats/idtech_iqm/xx_idtech_iqm.c
    src/formats/idtech_iqm/xx_idtech_iqm_extractor.c
    src/formats/unreal_psk/xx_unreal_psk.c
    src/formats/unreal_psk/xx_unreal_psk_extractor.c
    src/formats/unreal_psa/xx_unreal_psa.c
    src/formats/unreal_psa/xx_unreal_psa_extractor.c
    src/formats/torque_dts/xx_torque_dts.c
    src/formats/torque_dts/xx_torque_dts_extractor.c
    src/formats/magicavoxel_vox/xx_magicavoxel_vox.c
    src/formats/magicavoxel_vox/xx_magicavoxel_vox_extractor.c
    src/formats/audio_au/xx_audio_au.c
    src/formats/audio_au/xx_audio_au_extractor.c
    src/formats/creative_voc/xx_creative_voc.c
    src/formats/creative_voc/xx_creative_voc_extractor.c
    src/formats/tracker_xm/xx_tracker_xm.c
    src/formats/tracker_xm/xx_tracker_xm_extractor.c
    src/formats/tracker_s3m/xx_tracker_s3m.c
    src/formats/tracker_s3m/xx_tracker_s3m_extractor.c
    src/formats/tracker_it/xx_tracker_it.c
    src/formats/tracker_it/xx_tracker_it_extractor.c
    src/formats/tracker_mtm/xx_tracker_mtm.c
    src/formats/tracker_mtm/xx_tracker_mtm_extractor.c
    src/formats/tracker_stm/xx_tracker_stm.c
    src/formats/tracker_stm/xx_tracker_stm_extractor.c
    src/formats/tracker_669/xx_tracker_669.c
    src/formats/tracker_669/xx_tracker_669_extractor.c
    src/formats/tracker_ult/xx_tracker_ult.c
    src/formats/tracker_ult/xx_tracker_ult_extractor.c
    src/formats/tracker_okt/xx_tracker_okt.c
    src/formats/tracker_okt/xx_tracker_okt_extractor.c
    src/formats/nifti2/xx_nifti2.c
    src/formats/nifti2/xx_nifti2_extractor.c
    src/formats/lidar_las/xx_lidar_las.c
    src/formats/lidar_las/xx_lidar_las_extractor.c
    src/formats/esri_shp/xx_esri_shp.c
    src/formats/esri_shp/xx_esri_shp_extractor.c
    src/formats/polygon_ply/xx_polygon_ply.c
    src/formats/polygon_ply/xx_polygon_ply_extractor.c
    src/formats/pointcloud_pcd/xx_pointcloud_pcd.c
    src/formats/pointcloud_pcd/xx_pointcloud_pcd_extractor.c
    src/formats/matlab_mat4/xx_matlab_mat4.c
    src/formats/matlab_mat4/xx_matlab_mat4_extractor.c
    src/formats/seismic_segy/xx_seismic_segy.c
    src/formats/seismic_segy/xx_seismic_segy_extractor.c
    src/formats/biomedical_bdf/xx_biomedical_bdf.c
    src/formats/biomedical_bdf/xx_biomedical_bdf_extractor.c
    src/formats/erlang_beam/xx_erlang_beam.c
    src/formats/erlang_beam/xx_erlang_beam_extractor.c
    src/formats/java_jmod/xx_java_jmod.c
    src/formats/java_jmod/xx_java_jmod_extractor.c
    src/formats/sfx_arcv2/xx_sfx_arcv2.c
    src/formats/sfx_arcv2/xx_sfx_arcv2_extractor.c
    src/formats/sfx_chz/xx_sfx_chz.c
    src/formats/sfx_chz/xx_sfx_chz_extractor.c
    src/formats/sfx_szdd/xx_sfx_szdd.c
    src/formats/sfx_szdd/xx_sfx_szdd_extractor.c
    src/formats/sfx_mpq/xx_sfx_mpq.c
    src/formats/sfx_mpq/xx_sfx_mpq_extractor.c
    src/formats/sfx_swag/xx_sfx_swag.c
    src/formats/sfx_swag/xx_sfx_swag_extractor.c
    src/formats/sfx_zpak/xx_sfx_zpak.c
    src/formats/sfx_zpak/xx_sfx_zpak_extractor.c
    src/formats/sfx_diskexpress/xx_sfx_diskexpress.c
    src/formats/sfx_diskexpress/xx_sfx_diskexpress_extractor.c
    src/formats/sfx_bzip2/xx_sfx_bzip2.c
    src/formats/sfx_bzip2/xx_sfx_bzip2_extractor.c
    src/formats/sfx_gzip/xx_sfx_gzip.c
    src/formats/sfx_gzip/xx_sfx_gzip_extractor.c
    src/formats/sfx_tar/xx_sfx_tar.c
    src/formats/sfx_tar/xx_sfx_tar_extractor.c
    src/formats/sfx_cab/xx_sfx_cab.c
    src/formats/sfx_cab/xx_sfx_cab_extractor.c
    src/formats/pmarc_sfx/xx_pmarc_sfx.c
    src/formats/pmarc_sfx/xx_pmarc_sfx_extractor.c
    src/formats/sfx_7zip/xx_sfx_7zip.c
    src/formats/sfx_7zip/xx_sfx_7zip_extractor.c
    src/formats/sfx_ace/xx_sfx_ace.c
    src/formats/sfx_ace/xx_sfx_ace_extractor.c
    src/formats/sfx_zipcentral/xx_sfx_zipcentral.c
    src/formats/sfx_zipcentral/xx_sfx_zipcentral_extractor.c
    src/formats/sony_psx_exe/xx_sony_psx_exe.c
    src/formats/sony_psx_exe/xx_sony_psx_exe_extractor.c
    src/formats/sony_psf/xx_sony_psf.c
    src/formats/sony_psf/xx_sony_psf_extractor.c
    src/formats/xbox_xdvdfs/xx_xbox_xdvdfs.c
    src/formats/xbox_xdvdfs/xx_xbox_xdvdfs_extractor.c
    src/formats/nintendo_wbfs/xx_nintendo_wbfs.c
    src/formats/nintendo_wbfs/xx_nintendo_wbfs_extractor.c
    src/formats/godot_ctex/xx_godot_ctex.c
    src/formats/godot_ctex/xx_godot_ctex_extractor.c
    src/formats/unity_serialized/xx_unity_serialized.c
    src/formats/unity_serialized/xx_unity_serialized_extractor.c
    src/formats/idtech_mdl/xx_idtech_mdl.c
    src/formats/idtech_mdl/xx_idtech_mdl_extractor.c
    src/formats/valve_studio_mdl/xx_valve_studio_mdl.c
    src/formats/valve_studio_mdl/xx_valve_studio_mdl_extractor.c
    src/formats/blitz3d_b3d/xx_blitz3d_b3d.c
    src/formats/blitz3d_b3d/xx_blitz3d_b3d_extractor.c
    src/formats/milkshape_ms3d/xx_milkshape_ms3d.c
    src/formats/milkshape_ms3d/xx_milkshape_ms3d_extractor.c
    src/formats/nintendo_bch/xx_nintendo_bch.c
    src/formats/nintendo_bch/xx_nintendo_bch_extractor.c
    src/formats/nintendo_cgfx/xx_nintendo_cgfx.c
    src/formats/nintendo_cgfx/xx_nintendo_cgfx_extractor.c
    src/formats/nintendo_byaml/xx_nintendo_byaml.c
    src/formats/nintendo_byaml/xx_nintendo_byaml_extractor.c
    src/formats/relic_chunky/xx_relic_chunky.c
    src/formats/relic_chunky/xx_relic_chunky_extractor.c
    src/formats/ogre_mesh/xx_ogre_mesh.c
    src/formats/ogre_mesh/xx_ogre_mesh_extractor.c
    src/formats/adobe_ase/xx_adobe_ase.c
    src/formats/adobe_ase/xx_adobe_ase_extractor.c
    src/formats/adobe_aco/xx_adobe_aco.c
    src/formats/adobe_aco/xx_adobe_aco_extractor.c
    src/formats/gimp_gbr/xx_gimp_gbr.c
    src/formats/gimp_gbr/xx_gimp_gbr_extractor.c
    src/formats/gimp_gih/xx_gimp_gih.c
    src/formats/gimp_gih/xx_gimp_gih_extractor.c
    src/formats/gimp_pat/xx_gimp_pat.c
    src/formats/gimp_pat/xx_gimp_pat_extractor.c
    src/formats/jbig2/xx_jbig2.c
    src/formats/jbig2/xx_jbig2_extractor.c
    src/formats/djvu/xx_djvu.c
    src/formats/djvu/xx_djvu_extractor.c
    src/formats/emf/xx_emf.c
    src/formats/emf/xx_emf_extractor.c
    src/formats/wmf/xx_wmf.c
    src/formats/wmf/xx_wmf_extractor.c
    src/formats/xfig/xx_xfig.c
    src/formats/xfig/xx_xfig_extractor.c
    src/formats/nifti1/xx_nifti1.c
    src/formats/nifti1/xx_nifti1_extractor.c
    src/formats/nrrd/xx_nrrd.c
    src/formats/nrrd/xx_nrrd_extractor.c
    src/formats/mrc/xx_mrc.c
    src/formats/mrc/xx_mrc_extractor.c
    src/formats/metaimage/xx_metaimage.c
    src/formats/metaimage/xx_metaimage_extractor.c
    src/formats/vtk_legacy/xx_vtk_legacy.c
    src/formats/vtk_legacy/xx_vtk_legacy_extractor.c
    src/formats/gipl/xx_gipl.c
    src/formats/gipl/xx_gipl_extractor.c
    src/formats/freesurfer_mgh/xx_freesurfer_mgh.c
    src/formats/freesurfer_mgh/xx_freesurfer_mgh_extractor.c
    src/formats/edf/xx_edf.c
    src/formats/edf/xx_edf_extractor.c
    src/formats/fcs/xx_fcs.c
    src/formats/fcs/xx_fcs_extractor.c
    src/formats/tensorflow_tfrecord/xx_tensorflow_tfrecord.c
    src/formats/tensorflow_tfrecord/xx_tensorflow_tfrecord_extractor.c
    src/formats/sfx_arc/xx_sfx_arc.c
    src/formats/sfx_arc/xx_sfx_arc_extractor.c
    src/formats/sfx_arj/xx_sfx_arj.c
    src/formats/sfx_arj/xx_sfx_arj_extractor.c
    src/formats/sfx_bsn/xx_sfx_bsn.c
    src/formats/sfx_bsn/xx_sfx_bsn_extractor.c
    src/formats/sfx_arq/xx_sfx_arq.c
    src/formats/sfx_arq/xx_sfx_arq_extractor.c
    src/formats/sfx_gxl/xx_sfx_gxl.c
    src/formats/sfx_gxl/xx_sfx_gxl_extractor.c
    src/formats/sfx_asymetrix/xx_sfx_asymetrix.c
    src/formats/sfx_asymetrix/xx_sfx_asymetrix_extractor.c
    src/formats/sfx_rta/xx_sfx_rta.c
    src/formats/sfx_rta/xx_sfx_rta_extractor.c
    src/formats/sfx_rtpatch/xx_sfx_rtpatch.c
    src/formats/sfx_rtpatch/xx_sfx_rtpatch_extractor.c
    src/formats/esp_archive/xx_esp_archive.c
    src/formats/esp_archive/xx_esp_archive_extractor.c
    src/formats/sfx_kwaj/xx_sfx_kwaj.c
    src/formats/sfx_kwaj/xx_sfx_kwaj_extractor.c
    src/formats/gemdos_lha/xx_gemdos_lha.c
    src/formats/gemdos_lha/xx_gemdos_lha_extractor.c
    src/formats/winimage_zip/xx_winimage_zip.c
    src/formats/winimage_zip/xx_winimage_zip_extractor.c
    src/formats/hp3000_wrq/xx_hp3000_wrq.c
    src/formats/hp3000_wrq/xx_hp3000_wrq_extractor.c
    src/formats/icu_data_package/xx_icu_data_package.c
    src/formats/icu_data_package/xx_icu_data_package_extractor.c
    src/formats/sfx_sqz/xx_sfx_sqz.c
    src/formats/sfx_sqz/xx_sfx_sqz_extractor.c
    src/formats/nintendo_bfstm/xx_nintendo_bfstm.c
    src/formats/nintendo_bfstm/xx_nintendo_bfstm_extractor.c
    src/formats/nintendo_bfwav/xx_nintendo_bfwav.c
    src/formats/nintendo_bfwav/xx_nintendo_bfwav_extractor.c
    src/formats/nintendo_bcwav/xx_nintendo_bcwav.c
    src/formats/nintendo_bcwav/xx_nintendo_bcwav_extractor.c
    src/formats/nintendo_bfres/xx_nintendo_bfres.c
    src/formats/nintendo_bfres/xx_nintendo_bfres_extractor.c
    src/formats/nintendo_bflyt/xx_nintendo_bflyt.c
    src/formats/nintendo_bflyt/xx_nintendo_bflyt_extractor.c
    src/formats/nintendo_bclyt/xx_nintendo_bclyt.c
    src/formats/nintendo_bclyt/xx_nintendo_bclyt_extractor.c
    src/formats/nintendo_bfnt/xx_nintendo_bfnt.c
    src/formats/nintendo_bfnt/xx_nintendo_bfnt_extractor.c
    src/formats/nintendo_bcfnt/xx_nintendo_bcfnt.c
    src/formats/nintendo_bcfnt/xx_nintendo_bcfnt_extractor.c
    src/formats/nintendo_3dsx/xx_nintendo_3dsx.c
    src/formats/nintendo_3dsx/xx_nintendo_3dsx_extractor.c
    src/formats/sony_tim2/xx_sony_tim2.c
    src/formats/sony_tim2/xx_sony_tim2_extractor.c
    src/formats/sony_pamf/xx_sony_pamf.c
    src/formats/sony_pamf/xx_sony_pamf_extractor.c
    src/formats/sega_gvr/xx_sega_gvr.c
    src/formats/sega_gvr/xx_sega_gvr_extractor.c
    src/formats/microsoft_xwb/xx_microsoft_xwb.c
    src/formats/microsoft_xwb/xx_microsoft_xwb_extractor.c
    src/formats/microsoft_xsb/xx_microsoft_xsb.c
    src/formats/microsoft_xsb/xx_microsoft_xsb_extractor.c
    src/formats/relic_sga/xx_relic_sga.c
    src/formats/relic_sga/xx_relic_sga_extractor.c
    src/formats/xpm/xx_xpm.c
    src/formats/xpm/xx_xpm_extractor.c
    src/formats/pcx/xx_pcx.c
    src/formats/pcx/xx_pcx_extractor.c
    src/formats/iff_ilbm/xx_iff_ilbm.c
    src/formats/iff_ilbm/xx_iff_ilbm_extractor.c
    src/formats/utah_rle/xx_utah_rle.c
    src/formats/utah_rle/xx_utah_rle_extractor.c
    src/formats/radiance_hdr/xx_radiance_hdr.c
    src/formats/radiance_hdr/xx_radiance_hdr_extractor.c
    src/formats/dpx/xx_dpx.c
    src/formats/dpx/xx_dpx_extractor.c
    src/formats/cineon/xx_cineon.c
    src/formats/cineon/xx_cineon_extractor.c
    src/formats/xwd/xx_xwd.c
    src/formats/xwd/xx_xwd_extractor.c
    src/formats/sgi_rgb/xx_sgi_rgb.c
    src/formats/sgi_rgb/xx_sgi_rgb_extractor.c
    src/formats/aseprite/xx_aseprite.c
    src/formats/aseprite/xx_aseprite_extractor.c
    src/formats/numpy_npy/xx_numpy_npy.c
    src/formats/numpy_npy/xx_numpy_npy_extractor.c
    src/formats/matlab_mat5/xx_matlab_mat5.c
    src/formats/matlab_mat5/xx_matlab_mat5_extractor.c
    src/formats/netcdf_classic/xx_netcdf_classic.c
    src/formats/netcdf_classic/xx_netcdf_classic_extractor.c
    src/formats/hdf4/xx_hdf4.c
    src/formats/hdf4/xx_hdf4_extractor.c
    src/formats/dbase_dbf/xx_dbase_dbf.c
    src/formats/dbase_dbf/xx_dbase_dbf_extractor.c
    src/formats/sas_xport/xx_sas_xport.c
    src/formats/sas_xport/xx_sas_xport_extractor.c
    src/formats/spss_sav/xx_spss_sav.c
    src/formats/spss_sav/xx_spss_sav_extractor.c
    src/formats/stata_dta/xx_stata_dta.c
    src/formats/stata_dta/xx_stata_dta_extractor.c
    src/formats/apache_arrow_file/xx_apache_arrow_file.c
    src/formats/apache_arrow_file/xx_apache_arrow_file_extractor.c
    src/formats/apache_parquet/xx_apache_parquet.c
    src/formats/apache_parquet/xx_apache_parquet_extractor.c
    src/formats/makeself/xx_makeself.c
    src/formats/makeself/xx_makeself_extractor.c
    src/formats/sun_java_binsh/xx_sun_java_binsh.c
    src/formats/sun_java_binsh/xx_sun_java_binsh_extractor.c
    src/formats/installanywhere_unix/xx_installanywhere_unix.c
    src/formats/installanywhere_unix/xx_installanywhere_unix_extractor.c
    src/formats/sfx_packagefortheweb/xx_sfx_packagefortheweb.c
    src/formats/sfx_packagefortheweb/xx_sfx_packagefortheweb_extractor.c
    src/formats/sfx_spis/xx_sfx_spis.c
    src/formats/sfx_spis/xx_sfx_spis_extractor.c
    src/formats/sfx_lha/xx_sfx_lha.c
    src/formats/sfx_lha/xx_sfx_lha_extractor.c
    src/formats/lmd_container/xx_lmd_container.c
    src/formats/lmd_container/xx_lmd_container_extractor.c
    src/formats/totalannihilation_hpi/xx_totalannihilation_hpi.c
    src/formats/totalannihilation_hpi/xx_totalannihilation_hpi_extractor.c
    src/formats/ravensoft_rff/xx_ravensoft_rff.c
    src/formats/ravensoft_rff/xx_ravensoft_rff_extractor.c
    src/formats/terminalreality_pod/xx_terminalreality_pod.c
    src/formats/terminalreality_pod/xx_terminalreality_pod_extractor.c
    src/formats/volition_vpp/xx_volition_vpp.c
    src/formats/volition_vpp/xx_volition_vpp_extractor.c
    src/formats/kirikiri_xp3/xx_kirikiri_xp3.c
    src/formats/kirikiri_xp3/xx_kirikiri_xp3_extractor.c
    src/formats/fromsoftware_binder/xx_fromsoftware_binder.c
    src/formats/fromsoftware_binder/xx_fromsoftware_binder_extractor.c
    src/formats/mythic_myp/xx_mythic_myp.c
    src/formats/mythic_myp/xx_mythic_myp_extractor.c
    src/formats/lithtech_rez/xx_lithtech_rez.c
    src/formats/lithtech_rez/xx_lithtech_rez_extractor.c
    src/formats/nintendo_ncch/xx_nintendo_ncch.c
    src/formats/nintendo_ncch/xx_nintendo_ncch_extractor.c
    src/formats/nintendo_ncsd/xx_nintendo_ncsd.c
    src/formats/nintendo_ncsd/xx_nintendo_ncsd_extractor.c
    src/formats/nintendo_cia/xx_nintendo_cia.c
    src/formats/nintendo_cia/xx_nintendo_cia_extractor.c
    src/formats/nintendo_nds/xx_nintendo_nds.c
    src/formats/nintendo_nds/xx_nintendo_nds_extractor.c
    src/formats/nintendo_gcm/xx_nintendo_gcm.c
    src/formats/nintendo_gcm/xx_nintendo_gcm_extractor.c
    src/formats/nintendo_tpl/xx_nintendo_tpl.c
    src/formats/nintendo_tpl/xx_nintendo_tpl_extractor.c
    src/formats/sony_tim/xx_sony_tim.c
    src/formats/sony_tim/xx_sony_tim_extractor.c
    src/formats/sony_vag/xx_sony_vag.c
    src/formats/sony_vag/xx_sony_vag_extractor.c
    src/formats/larian_lspk/xx_larian_lspk.c
    src/formats/larian_lspk/xx_larian_lspk_extractor.c
    src/formats/larian_lsf/xx_larian_lsf.c
    src/formats/larian_lsf/xx_larian_lsf_extractor.c
    src/formats/valve_hpak/xx_valve_hpak.c
    src/formats/valve_hpak/xx_valve_hpak_extractor.c
    src/formats/renpy_rpa/xx_renpy_rpa.c
    src/formats/renpy_rpa/xx_renpy_rpa_extractor.c
    src/formats/unreal_package/xx_unreal_package.c
    src/formats/unreal_package/xx_unreal_package_extractor.c
    src/formats/sega_pvr2/xx_sega_pvr2.c
    src/formats/sega_pvr2/xx_sega_pvr2_extractor.c
    src/formats/nintendo_bntx/xx_nintendo_bntx.c
    src/formats/nintendo_bntx/xx_nintendo_bntx_extractor.c
    src/formats/icns/xx_icns.c
    src/formats/icns/xx_icns_extractor.c
    src/formats/xcursor/xx_xcursor.c
    src/formats/xcursor/xx_xcursor_extractor.c
    src/formats/icc/xx_icc.c
    src/formats/icc/xx_icc_extractor.c
    src/formats/qoi/xx_qoi.c
    src/formats/qoi/xx_qoi_extractor.c
    src/formats/farbfeld/xx_farbfeld.c
    src/formats/farbfeld/xx_farbfeld_extractor.c
    src/formats/pnm/xx_pnm.c
    src/formats/pnm/xx_pnm_extractor.c
    src/formats/tga/xx_tga.c
    src/formats/tga/xx_tga_extractor.c
    src/formats/sun_raster/xx_sun_raster.c
    src/formats/sun_raster/xx_sun_raster_extractor.c
    src/formats/fits/xx_fits.c
    src/formats/fits/xx_fits_extractor.c
    src/formats/dicom/xx_dicom.c
    src/formats/dicom/xx_dicom_extractor.c
    src/formats/pcap/xx_pcap.c
    src/formats/pcap/xx_pcap_extractor.c
    src/formats/btsnoop/xx_btsnoop.c
    src/formats/btsnoop/xx_btsnoop_extractor.c
    src/formats/java_class/xx_java_class.c
    src/formats/java_class/xx_java_class_extractor.c
    src/formats/sfnt_collection/xx_sfnt_collection.c
    src/formats/sfnt_collection/xx_sfnt_collection_extractor.c
    src/formats/sqlite3/xx_sqlite3.c
    src/formats/sqlite3/xx_sqlite3_extractor.c
    src/formats/sqlite_wal/xx_sqlite_wal.c
    src/formats/sqlite_wal/xx_sqlite_wal_extractor.c
    src/formats/avro_object/xx_avro_object.c
    src/formats/avro_object/xx_avro_object_extractor.c
    src/formats/glb/xx_glb.c
    src/formats/glb/xx_glb_extractor.c
    src/formats/spirv/xx_spirv.c
    src/formats/spirv/xx_spirv_extractor.c
    src/formats/crx/xx_crx.c
    src/formats/crx/xx_crx_extractor.c
    src/formats/bethesda_bsa/xx_bethesda_bsa.c
    src/formats/bethesda_bsa/xx_bethesda_bsa_extractor.c
    src/formats/bethesda_ba2/xx_bethesda_ba2.c
    src/formats/bethesda_ba2/xx_bethesda_ba2_extractor.c
    src/formats/unityfs/xx_unityfs.c
    src/formats/unityfs/xx_unityfs_extractor.c
    src/formats/bioware_biff/xx_bioware_biff.c
    src/formats/bioware_biff/xx_bioware_biff_extractor.c
    src/formats/bioware_erf/xx_bioware_erf.c
    src/formats/bioware_erf/xx_bioware_erf_extractor.c
    src/formats/bioware_rim/xx_bioware_rim.c
    src/formats/bioware_rim/xx_bioware_rim_extractor.c
    src/formats/lucas_lab/xx_lucas_lab.c
    src/formats/lucas_lab/xx_lucas_lab_extractor.c
    src/formats/lucas_bun/xx_lucas_bun.c
    src/formats/lucas_bun/xx_lucas_bun_extractor.c
    src/formats/idtech_bsp/xx_idtech_bsp.c
    src/formats/idtech_bsp/xx_idtech_bsp_extractor.c
    src/formats/valve_bsp/xx_valve_bsp.c
    src/formats/valve_bsp/xx_valve_bsp_extractor.c
    src/formats/idtech_md2/xx_idtech_md2.c
    src/formats/idtech_md2/xx_idtech_md2_extractor.c
    src/formats/idtech_md3/xx_idtech_md3.c
    src/formats/idtech_md3/xx_idtech_md3_extractor.c
    src/formats/idtech_qvm/xx_idtech_qvm.c
    src/formats/idtech_qvm/xx_idtech_qvm_extractor.c
    src/formats/mohawk_mhk/xx_mohawk_mhk.c
    src/formats/mohawk_mhk/xx_mohawk_mhk_extractor.c
    src/formats/quake_sprite/xx_quake_sprite.c
    src/formats/quake_sprite/xx_quake_sprite_extractor.c
    src/formats/nintendo_narc/xx_nintendo_narc.c
    src/formats/nintendo_narc/xx_nintendo_narc_extractor.c
    src/formats/nintendo_sarc/xx_nintendo_sarc.c
    src/formats/nintendo_sarc/xx_nintendo_sarc_extractor.c
    src/formats/nintendo_pfs0/xx_nintendo_pfs0.c
    src/formats/nintendo_pfs0/xx_nintendo_pfs0_extractor.c
    src/formats/nintendo_hfs0/xx_nintendo_hfs0.c
    src/formats/nintendo_hfs0/xx_nintendo_hfs0_extractor.c
    src/formats/nintendo_brres/xx_nintendo_brres.c
    src/formats/nintendo_brres/xx_nintendo_brres_extractor.c
    src/formats/nintendo_bcstm/xx_nintendo_bcstm.c
    src/formats/nintendo_bcstm/xx_nintendo_bcstm_extractor.c
    src/formats/nintendo_bfsar/xx_nintendo_bfsar.c
    src/formats/nintendo_bfsar/xx_nintendo_bfsar_extractor.c
    src/formats/nintendo_bcsar/xx_nintendo_bcsar.c
    src/formats/nintendo_bcsar/xx_nintendo_bcsar_extractor.c
    src/formats/sony_psarc/xx_sony_psarc.c
    src/formats/sony_psarc/xx_sony_psarc_extractor.c
    src/formats/ktx/xx_ktx.c
    src/formats/ktx/xx_ktx_extractor.c
    src/formats/ktx2/xx_ktx2.c
    src/formats/ktx2/xx_ktx2_extractor.c
    src/formats/dds/xx_dds.c
    src/formats/dds/xx_dds_extractor.c
    src/formats/pvr/xx_pvr.c
    src/formats/pvr/xx_pvr_extractor.c
    src/formats/valve_vtf/xx_valve_vtf.c
    src/formats/valve_vtf/xx_valve_vtf_extractor.c
    src/formats/xbox_xbe/xx_xbox_xbe.c
    src/formats/xbox_xbe/xx_xbox_xbe_extractor.c
    src/formats/flac/xx_flac.c
    src/formats/flac/xx_flac_extractor.c
    src/formats/ogg/xx_ogg.c
    src/formats/ogg/xx_ogg_extractor.c
    src/formats/mp4/xx_mp4.c
    src/formats/mp4/xx_mp4_extractor.c
    src/formats/matroska/xx_matroska.c
    src/formats/matroska/xx_matroska_extractor.c
    src/formats/aiff/xx_aiff.c
    src/formats/aiff/xx_aiff_extractor.c
    src/formats/caf/xx_caf.c
    src/formats/caf/xx_caf_extractor.c
    src/formats/photoshop_psd/xx_photoshop_psd.c
    src/formats/photoshop_psd/xx_photoshop_psd_extractor.c
    src/formats/tiff/xx_tiff.c
    src/formats/tiff/xx_tiff_extractor.c
    src/formats/openexr/xx_openexr.c
    src/formats/openexr/xx_openexr_extractor.c
    src/formats/jpeg2000_jp2/xx_jpeg2000_jp2.c
    src/formats/jpeg2000_jp2/xx_jpeg2000_jp2_extractor.c
    src/formats/android_vendor_boot/xx_android_vendor_boot.c
    src/formats/android_vendor_boot/xx_android_vendor_boot_extractor.c
    src/formats/android_dtbo/xx_android_dtbo.c
    src/formats/android_dtbo/xx_android_dtbo_extractor.c
    src/formats/android_vbmeta/xx_android_vbmeta.c
    src/formats/android_vbmeta/xx_android_vbmeta_extractor.c
    src/formats/espressif_image/xx_espressif_image.c
    src/formats/espressif_image/xx_espressif_image_extractor.c
    src/formats/wasm/xx_wasm.c
    src/formats/wasm/xx_wasm_extractor.c
    src/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.c
    src/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper_extractor.c
    src/formats/dotnet_metadata/xx_dotnet_metadata.c
    src/formats/dotnet_metadata/xx_dotnet_metadata_extractor.c
    src/formats/sfnt/xx_sfnt.c
    src/formats/sfnt/xx_sfnt_extractor.c
    src/formats/woff/xx_woff.c
    src/formats/woff/xx_woff_extractor.c
    src/formats/woff2/xx_woff2.c
    src/formats/woff2/xx_woff2_extractor.c
    src/formats/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw.c
    src/formats/act_apricot_pc_xi_raw/xx_act_apricot_pc_xi_raw_extractor.c
    src/formats/adam/xx_adam.c
    src/formats/adam/xx_adam_extractor.c
    src/formats/base16/xx_base16.c
    src/formats/base16/xx_base16_extractor.c
    src/formats/bondwell_2_disk/xx_bondwell_2_disk.c
    src/formats/bondwell_2_disk/xx_bondwell_2_disk_extractor.c
    src/formats/casio_fz_1_disk/xx_casio_fz_1_disk.c
    src/formats/casio_fz_1_disk/xx_casio_fz_1_disk_extractor.c
    src/formats/isz/xx_isz.c
    src/formats/isz/xx_isz_extractor.c
    src/formats/mame_floppy_image_mfi/xx_mame_floppy_image_mfi.c
    src/formats/mame_floppy_image_mfi/xx_mame_floppy_image_mfi_extractor.c
    src/formats/parallels_hdd/xx_parallels_hdd.c
    src/formats/parallels_hdd/xx_parallels_hdd_extractor.c
    src/formats/pc_magazine_flp/xx_pc_magazine_flp.c
    src/formats/pc_magazine_flp/xx_pc_magazine_flp_extractor.c
    src/formats/pchrom/xx_pchrom.c
    src/formats/pchrom/xx_pchrom_extractor.c
    src/formats/pem/xx_pem.c
    src/formats/pem/xx_pem_extractor.c
    src/formats/prodos/xx_prodos.c
    src/formats/prodos/xx_prodos_extractor.c
    src/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk.c
    src/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk_extractor.c
    src/formats/rawcd/xx_rawcd.c
    src/formats/rawcd/xx_rawcd_extractor.c
    src/formats/rsdos_fs/xx_rsdos_fs.c
    src/formats/rsdos_fs/xx_rsdos_fs_extractor.c
    src/formats/sar_ns/xx_sar_ns.c
    src/formats/sar_ns/xx_sar_ns_extractor.c
    src/formats/swf/xx_swf.c
    src/formats/swf/xx_swf_extractor.c
    src/formats/t64/xx_t64.c
    src/formats/t64/xx_t64_extractor.c
    src/formats/uue/xx_uue.c
    src/formats/uue/xx_uue_extractor.c
    src/formats/vdi/xx_vdi.c
    src/formats/vdi/xx_vdi_extractor.c
    src/formats/bmp/xx_bmp.c
    src/formats/bmp/xx_bmp_extractor.c
    src/formats/cfe/xx_cfe.c
    src/formats/cfe/xx_cfe_extractor.c
    src/formats/dxbc/xx_dxbc.c
    src/formats/dxbc/xx_dxbc_extractor.c
    src/formats/gif/xx_gif.c
    src/formats/gif/xx_gif_extractor.c
    src/formats/jpeg/xx_jpeg.c
    src/formats/jpeg/xx_jpeg_extractor.c
    src/formats/linuxarm64/xx_linuxarm64.c
    src/formats/linuxarm64/xx_linuxarm64_extractor.c
    src/formats/linuxboot/xx_linuxboot.c
    src/formats/linuxboot/xx_linuxboot_extractor.c
    src/formats/linuxzimage/xx_linuxzimage.c
    src/formats/linuxzimage/xx_linuxzimage_extractor.c
    src/formats/pcapng/xx_pcapng.c
    src/formats/pcapng/xx_pcapng_extractor.c
    src/formats/pjl/xx_pjl.c
    src/formats/pjl/xx_pjl_extractor.c
    src/formats/png/xx_png.c
    src/formats/png/xx_png_extractor.c
    src/formats/riff/xx_riff.c
    src/formats/riff/xx_riff_extractor.c
    src/formats/svg/xx_svg.c
    src/formats/svg/xx_svg_extractor.c
    src/formats/quake_pak/xx_quake_pak.c
    src/formats/quake_pak/xx_quake_pak_extractor.c
    src/formats/doom_wad/xx_doom_wad.c
    src/formats/doom_wad/xx_doom_wad_extractor.c
    src/formats/quake_wad2/xx_quake_wad2.c
    src/formats/quake_wad2/xx_quake_wad2_extractor.c
    src/formats/halflife_wad3/xx_halflife_wad3.c
    src/formats/halflife_wad3/xx_halflife_wad3_extractor.c
    src/formats/build_grp/xx_build_grp.c
    src/formats/build_grp/xx_build_grp_extractor.c
    src/formats/cri_afs/xx_cri_afs.c
    src/formats/cri_afs/xx_cri_afs_extractor.c
    src/formats/cri_awb/xx_cri_awb.c
    src/formats/cri_awb/xx_cri_awb_extractor.c
    src/formats/valve_vpk/xx_valve_vpk.c
    src/formats/valve_vpk/xx_valve_vpk_extractor.c
    src/formats/nintendo_u8/xx_nintendo_u8.c
    src/formats/nintendo_u8/xx_nintendo_u8_extractor.c
    src/formats/nintendo_rarc/xx_nintendo_rarc.c
    src/formats/nintendo_rarc/xx_nintendo_rarc_extractor.c
    src/formats/android_ab/xx_android_ab.c
    src/formats/android_ab/xx_android_ab_extractor.c
    src/formats/nes_rom/xx_nes_rom.c
    src/formats/nes_rom/xx_nes_rom_extractor.c
    src/formats/lynx_lnx/xx_lynx_lnx.c
    src/formats/lynx_lnx/xx_lynx_lnx_extractor.c
    src/formats/commodore_crt/xx_commodore_crt.c
    src/formats/commodore_crt/xx_commodore_crt_extractor.c
    src/formats/uf2/xx_uf2.c
    src/formats/uf2/xx_uf2_extractor.c
    src/formats/ico/xx_ico.c
    src/formats/ico/xx_ico_extractor.c
    src/formats/midi/xx_midi.c
    src/formats/midi/xx_midi_extractor.c
    src/formats/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper.c
    src/formats/advanced_installer_bootstrapper/xx_advanced_installer_bootstrapper_extractor.c
    src/formats/ardi_installer/xx_ardi_installer.c
    src/formats/ardi_installer/xx_ardi_installer_extractor.c
    src/formats/arni_installer_container/xx_arni_installer_container.c
    src/formats/arni_installer_container/xx_arni_installer_container_extractor.c
    src/formats/ej_technologies_install/xx_ej_technologies_install.c
    src/formats/ej_technologies_install/xx_ej_technologies_install_extractor.c
    src/formats/finstall/xx_finstall.c
    src/formats/finstall/xx_finstall_extractor.c
    src/formats/ghost_installer/xx_ghost_installer.c
    src/formats/ghost_installer/xx_ghost_installer_extractor.c
    src/formats/ibm_zpak_installer/xx_ibm_zpak_installer.c
    src/formats/ibm_zpak_installer/xx_ibm_zpak_installer_extractor.c
    src/formats/ifah_installer/xx_ifah_installer.c
    src/formats/ifah_installer/xx_ifah_installer_extractor.c
    src/formats/inno_setup/xx_inno_setup.c
    src/formats/inno_setup/xx_inno_setup_extractor.c
    src/formats/installer_vise_windows/xx_installer_vise_windows.c
    src/formats/installer_vise_windows/xx_installer_vise_windows_extractor.c
    src/formats/installshield_12_setup/xx_installshield_12_setup.c
    src/formats/installshield_12_setup/xx_installshield_12_setup_extractor.c
    src/formats/installshield_3/xx_installshield_3.c
    src/formats/installshield_3/xx_installshield_3_extractor.c
    src/formats/installshield_7_setup/xx_installshield_7_setup.c
    src/formats/installshield_7_setup/xx_installshield_7_setup_extractor.c
    src/formats/installshield_7_setup2/xx_installshield_7_setup2.c
    src/formats/installshield_7_setup2/xx_installshield_7_setup2_extractor.c
    src/formats/installshield_developer/xx_installshield_developer.c
    src/formats/installshield_developer/xx_installshield_developer_extractor.c
    src/formats/installshield_issetupstream/xx_installshield_issetupstream.c
    src/formats/installshield_issetupstream/xx_installshield_issetupstream_extractor.c
    src/formats/installshield_multiplatform/xx_installshield_multiplatform.c
    src/formats/installshield_multiplatform/xx_installshield_multiplatform_extractor.c
    src/formats/installshield_skin/xx_installshield_skin.c
    src/formats/installshield_skin/xx_installshield_skin_extractor.c
    src/formats/microfox_put/xx_microfox_put.c
    src/formats/microfox_put/xx_microfox_put_extractor.c
    src/formats/o_setup/xx_o_setup.c
    src/formats/o_setup/xx_o_setup_extractor.c
    src/formats/pc_install_setup/xx_pc_install_setup.c
    src/formats/pc_install_setup/xx_pc_install_setup_extractor.c
    src/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable.c
    src/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable_extractor.c
    src/formats/qsetup_installation_suite/xx_qsetup_installation_suite.c
    src/formats/qsetup_installation_suite/xx_qsetup_installation_suite_extractor.c
    src/formats/rtpatch_setup_data/xx_rtpatch_setup_data.c
    src/formats/rtpatch_setup_data/xx_rtpatch_setup_data_extractor.c
    src/formats/setup_factory/xx_setup_factory.c
    src/formats/setup_factory/xx_setup_factory_extractor.c
    src/formats/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables.c
    src/formats/sfx_ebook_compiler_executables/xx_sfx_ebook_compiler_executables_extractor.c
    src/formats/spoon_installer/xx_spoon_installer.c
    src/formats/spoon_installer/xx_spoon_installer_extractor.c
    src/formats/tarma_installer/xx_tarma_installer.c
    src/formats/tarma_installer/xx_tarma_installer_extractor.c
    src/formats/adf/xx_adf.c
    src/formats/adf/xx_adf_extractor.c
    src/formats/apm/xx_apm.c
    src/formats/apm/xx_apm_extractor.c
    src/formats/vhdx/xx_vhdx.c
    src/formats/vhdx/xx_vhdx_extractor.c
    src/formats/base64/xx_base64.c
    src/formats/base64/xx_base64_extractor.c
    src/formats/btoa/xx_btoa.c
    src/formats/btoa/xx_btoa_extractor.c
    src/formats/chd/xx_chd.c
    src/formats/chd/xx_chd_extractor.c
    src/formats/chm/xx_chm.c
    src/formats/chm/xx_chm_extractor.c
    src/formats/cloop/xx_cloop.c
    src/formats/cloop/xx_cloop_extractor.c
    src/formats/cue/xx_cue.c
    src/formats/cue/xx_cue_extractor.c
    src/formats/dahuazip/xx_dahuazip.c
    src/formats/dahuazip/xx_dahuazip_extractor.c
    src/formats/dmsfw/xx_dmsfw.c
    src/formats/dmsfw/xx_dmsfw_extractor.c
    src/formats/ewf/xx_ewf.c
    src/formats/ewf/xx_ewf_extractor.c
    src/formats/godot_engine_pck/xx_godot_engine_pck.c
    src/formats/godot_engine_pck/xx_godot_engine_pck_extractor.c
    src/formats/gpgsigned/xx_gpgsigned.c
    src/formats/gpgsigned/xx_gpgsigned_extractor.c
    src/formats/ihex/xx_ihex.c
    src/formats/ihex/xx_ihex_extractor.c
    src/formats/kwaj/xx_kwaj.c
    src/formats/kwaj/xx_kwaj_extractor.c
    src/formats/lbr/xx_lbr.c
    src/formats/lbr/xx_lbr_extractor.c
    src/formats/lzfsestream/xx_lzfsestream.c
    src/formats/lzfsestream/xx_lzfsestream_extractor.c
    src/formats/nrg/xx_nrg.c
    src/formats/nrg/xx_nrg_extractor.c
    src/formats/packit_mac/xx_packit_mac.c
    src/formats/packit_mac/xx_packit_mac_extractor.c
    src/formats/rpm/xx_rpm.c
    src/formats/rpm/xx_rpm_extractor.c
    src/formats/stuffit5/xx_stuffit5.c
    src/formats/stuffit5/xx_stuffit5_extractor.c
)

# One generated C reader in its own subfolder for each DIE music family.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/formats/die_music/xx_die_music_sources.cmake")
list(APPEND XXFCLIB_SOURCES ${XX_DIE_MUSIC_SOURCES})
include(src/formats/xx_native_format_sources.cmake)
list(APPEND XXFCLIB_SOURCES
    src/formats/ue2_documents/xx_ue2_documents.c
    src/formats/microsoft_lit/xx_microsoft_lit.c
    src/formats/ue2_games/xx_ue2_games.c
    src/formats/sqlite_sql/xx_sqlite_sql.c
    src/formats/sqlite_sql/xx_sqlite_amalgamation.c
    src/formats/chromium_pak/xx_chromium_pak.c
    src/formats/windows_thumbnail_cache/xx_windows_thumbnail_cache.c)
list(APPEND XXFCLIB_SOURCES src/formats/enigma_virtual_box/xx_enigma_virtual_box.c src/formats/media_engine/xx_media_engine.c)
list(APPEND XXFCLIB_SOURCES src/formats/bitrock/xx_bitrock.c)
list(APPEND XXFCLIB_SOURCES src/formats/smart_install_maker/xx_smart_install_maker.c)
list(APPEND XXFCLIB_SOURCES src/formats/upx_engine/xx_upx_engine.c)
list(APPEND XXFCLIB_SOURCES src/formats/molebox/xx_molebox.c)
list(APPEND XXFCLIB_SOURCES src/formats/superdat/xx_superdat.c)
list(APPEND XXFCLIB_SOURCES
    src/formats/fead/xx_fead.c src/formats/fead/xx_fead_restore.c
    src/formats/fead/xx_fead_bcj2.c
    src/formats/fead/xx_fead_zlib_encoder.c
    src/formats/fead/encoder/deflate.c src/formats/fead/encoder/trees.c
    src/formats/fead/encoder/adler32.c src/formats/fead/encoder/crc32.c
    src/formats/fead/encoder/compress.c)
list(APPEND XXFCLIB_SOURCES src/formats/excelsior/xx_excelsior.c src/formats/legacy_archive_engine/xx_legacy_archive_engine.c)
list(APPEND XXFCLIB_SOURCES
    src/formats/dgca/xx_dgca_decode.c
    src/formats/dgca/xx_dgca_payload.c
    src/formats/dgca/xx_dgca_entropy.c
    src/formats/dgca/xx_dgca_lzp.c
    src/formats/dgca/xx_dgca_range.c
    src/formats/dgca/xx_dgca_cipher.c
    src/formats/dgca/xx_dgca_transform.c
    src/formats/dgca/xx_dgca_adapter.c)

if(WIN32)
    list(APPEND XXFCLIB_SOURCES
        src/platforms/xx_entry_windows.c
        src/io/platforms/xx_io_windows.c
        src/memory/platforms/xx_memory_windows.c
        src/strings/platforms/xx_string_windows.c
        src/fs/platforms/xx_fs_windows.c
        src/rt/platforms/xx_rt_windows.c
        src/global/platforms/xx_global_windows.c
        src/terminal/platforms/xx_terminal_windows.c
        src/settings/platforms/xx_settings_file_windows.c
        src/settings/platforms/xx_settings_windows.c
        src/die_engine/platforms/xx_die_engine_windows.c
    )
else()
    list(APPEND XXFCLIB_SOURCES
        src/io/platforms/xx_io_posix.c
        src/memory/platforms/xx_memory_posix.c
        src/strings/platforms/xx_string_posix.c
        src/fs/platforms/xx_fs_posix.c
        src/rt/platforms/xx_rt_posix.c
        src/global/platforms/xx_global_posix.c
        src/terminal/platforms/xx_terminal_posix.c
        src/settings/platforms/xx_settings_file_posix.c
        src/die_engine/platforms/xx_die_engine_posix.c
    )
endif()

include(GNUInstallDirs)

if(APPLE)
    list(APPEND XXFCLIB_SOURCES src/settings/platforms/xx_settings_macos.c)
elseif(NOT WIN32)
    list(APPEND XXFCLIB_SOURCES src/settings/platforms/xx_settings_posix.c)
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Helper to configure target include dirs, warnings, and export/import definitions
include(src/die_engine/xx_die_engine_cdisasm.cmake)

function(xxfclib_configure_target target is_shared)
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
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/zip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/jar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/apk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ipa
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/7zip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/bz2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/xz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_common
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_gz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/npm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_bz2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_xz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_lz4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lz4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lz5
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lizard
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/brotli
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/unixpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/zlib
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/unixcompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gitobject
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mscompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ash0
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/wiilz77
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzv1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/oraclesqueeze
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/softronics
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/logitechcompress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dmapacked
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gashuff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/huf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzdiet
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzpis2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_zstd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/zstd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/cpio
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mtree
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_nextstep
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_compress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_lzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_lzma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tar_lzop
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tarx1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tarx2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/iso9660
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/trx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/seama
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/chk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/packimg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dlob
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/wince
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/binhdr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rtk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/csman
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/vxworks
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/uefi_fv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/uefi_capsule
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/qcow
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/qnx6
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/luks
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/apfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/btrfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/logfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dmg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dms
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzop
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/srec
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dclraw
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/xpak
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pdb
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/infogramesft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/uboot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/twrx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tplink
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/silmarilsft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/shrs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mh01
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/matter_ota
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lz4demo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lingvoarc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/jboot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/encrpted_img
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/encfw
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ecos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dlke
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dlink_tlv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dkbs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/autel
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/arcadyan
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/androidboot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rawstac
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rtpatch
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/softronics
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/vmssaveset
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/boo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/wolfft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/settlersft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/teacy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rsc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/res
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/bsn
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/wintermutedcp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/volitionvpft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/agis
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/hog
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzpis2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rnca
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/paperport
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/starkit
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lspack10
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ixa
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lif
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/qip2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/emt
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/bagf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/wrzl
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/spk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/stac
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dbz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/squeeze2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sq
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/phar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mpq
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sabdu
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/apricot
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/hdcopy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/copydisk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ciso
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/vmdk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/vhddynamic
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/wim
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/softpaq2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/aiaff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gxl
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/shrinkwrap
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/red
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/diskexpress
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/cpx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/smsipak
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/bnd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/cat
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/csidos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/binder
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/jasc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/recognita
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/scf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/bcw
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/bvrp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ssm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mdcd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/xlas
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pain
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/qrst
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/opc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tnef
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mcc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/notetab
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/stunts
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/megatechvol
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/grasp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/psn
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sinner
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/hog2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pcxlib
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/vmsdb
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/vmspcsi
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/beospkg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/solarispkg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pax
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/copyqmexe
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/diskjuggler
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pmdiskcopy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/diskdupe
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/imd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/twoimg
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/fdi
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/hfe
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/teledisk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/copyqm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pcinstall
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gksetup
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/is11
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/izpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/squeeze1
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/trdos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lifkd
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/arcv
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/compaqlzh
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzk00
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pma
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/binhex
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/binaryii
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/stuffit
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dclft
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/debugscr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gob
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/savedskf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/edilzss
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/is7inx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/is5
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/is3
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/psdc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/unixcompact
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sco
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gpfpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ftcomp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/winlink
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gst
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/finear
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mwave
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/xorarchive
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mxs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/edc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mrnz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tpwm
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/cazip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ibmpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rnc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/shar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/netware2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mathcad
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/perform
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/battleisle
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/kpck
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/beatthehouse
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pp20
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/macbinary
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/applesingle
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/resourcefork
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/cramfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/jffs2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/yaffs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ubi
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ubifs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ext
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/fat
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/mbr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gpt
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sparse
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/uimage
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/dtb
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/squashfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ntfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/udf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/romfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sqz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/topspeed
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/tps
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ulead
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/quantum
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/zxzip
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/zoom
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sfpack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/claylz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/c64wraptor
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/corelltec
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pcsecure
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/rsvk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/lzw15v
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/saf
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/sls
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/nid
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/gamos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/panorama
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/fpak
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ace
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ain
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/aldus
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/alz
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/ampk
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/aodos
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/arcfs
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/pdp11ar
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/artipack
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/arcv2
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/arcv4
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/warc
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/aixbff
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/arx
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/cab
            ${CMAKE_CURRENT_SOURCE_DIR}/src/formats/msdos
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
            # A parallel Ninja build otherwise has many cl.exe instances
            # writing one target PDB, intermittently failing with C1041.
            # Keep Debug information in each object instead.
            $<$<CONFIG:Debug>:/Z7>
            $<$<CONFIG:RelWithDebInfo>:/Z7>
        )
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(XXFC_MSVC_X64 AND (is_shared OR XXFC_STATIC_NO_CRT))
            # SQLite's hosted amalgamation must not introduce CRT imports into
            # the existing freestanding builds. The raw SQLite page reader is
            # still available there; hosted builds provide the SQL export.
            target_compile_definitions(${target} PRIVATE XXFC_SQLITE_HOSTED_DISABLED)
        endif()

        if(is_shared AND XXFC_MSVC_X64)
            # This configuration links without the CRT, so xx_entry_windows.c
            # must supply memset/memcpy/memmove/_fltused itself. The static
            # target keeps the CRT and must NOT get them, or the link fails
            # with LNK2005 against LIBCMT.
            target_compile_definitions(${target} PRIVATE XXFC_NO_CRT)
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:/GS->
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

            if(XXFC_STATIC_NO_CRT AND XXFC_MSVC_X64)
                # Freestanding archive. /Zl drops the /DEFAULTLIB records, /GS-
                # removes the __security_check_cookie references and the asm
                # supplies __chkstk, which together are the three things that
                # stop a /NODEFAULTLIB consumer from linking this archive.
                # No XXFC_NO_CRT: see the option's comment -- the consumer's
                # own entry object owns memset/memcpy/memmove/_fltused.
                target_compile_options(${target} PRIVATE
                    $<$<COMPILE_LANGUAGE:C>:/GS->
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
            endif()
        else()
            # MSVC x86 emits a larger family of arithmetic, floating-point,
            # RTC and stack helpers. Keep the documented Win32 DLL build
            # hosted instead of pretending that a lone stack probe is a
            # complete freestanding compiler runtime.
            target_link_libraries(${target} PRIVATE kernel32.lib)
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
    add_library(die_engine STATIC ${XXFC_DIE_ENGINE_SOURCES})
    xxfclib_configure_target(die_engine FALSE)
    add_library(xxfclib::die_engine ALIAS die_engine)
    list(APPEND XXFCLIB_INSTALL_TARGETS die_engine)
endif()

if(MSVC)
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
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/xpk_compressed_file/LICENSE.ancient"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/ancient")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/die_engine/audio_db/LICENSE.DIE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xxfclib/licenses/die-audio")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/hfsplus/LICENSE.NetBSD"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/formats/hfsplus/LICENSE.Unicode"
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

# Regression checks for the additional archive, disk-image and filesystem readers.
if(XXFC_BUILD_FORMAT_EXPANSION_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
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
