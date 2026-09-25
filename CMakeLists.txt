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
    include/xxfclib/xxfclib.h
    include/xxfclib/xxfc_defs.h
    include/xxfclib/data/xx_pd.h
    include/xxfclib/io/xx_io.h
    include/xxfclib/die_engine/die_engine.h
    include/xxfclib/memory/xx_memory.h
    include/xxfclib/formats/xx_memory_map.h
    include/xxfclib/strings/xx_string.h
    include/xxfclib/data/xx_data.h
    include/xxfclib/formats/xx_data_signature.h
    include/xxfclib/list/xx_list.h
    include/xxfclib/var/xx_var.h
    include/xxfclib/global/xx_global.h
    include/xxfclib/formats/xx_format.h
    include/xxfclib/formats/xx_format_extractor.h
    include/xxfclib/algo/crc/xx_crc.h
    include/xxfclib/algo/hash/xx_hash.h
    include/xxfclib/algo/adler32/xx_adler32.h
    include/xxfclib/algo/lzfse/xx_lzfse.h
    include/xxfclib/algo/blake2/xx_blake2.h
    include/xxfclib/algo/entropy/xx_entropy.h
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
    include/xxfclib/formats/pe/xx_pe.h
    include/xxfclib/formats/elf/xx_elf.h
    include/xxfclib/formats/macho/xx_macho.h
    include/xxfclib/formats/ne/xx_ne.h
    include/xxfclib/formats/le/xx_le.h
    include/xxfclib/formats/lx/xx_lx.h
    include/xxfclib/formats/dex/xx_dex.h
)

set(XXFCLIB_SOURCES
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
    # driver. die_engine_compat.* carries the handful of cdie runtime
    # primitives whose semantics differ from their xx_* counterparts; the rest
    # alias straight onto xx_rt.
    src/die_engine/die_engine_compat.c
    src/die_engine/db.c
    src/die_engine/scan.c
    src/die_engine/result.c
    src/die_engine/api.c
    # The file under scan. What used to sit beside it here -- a second
    # binary-access layer, memory map, signature notation and matcher -- is
    # gone: xx_io, xx_data, xx_data_signature, xx_memory_map, xx_crc,
    # xx_adler32 and xx_hash do that work now.
    src/die_engine/die_engine_bin.c
    # Its binary-format parsers. These answer predicate queries for signature
    # scripts ("is this import present?"), which is a different job from the
    # readers in src/formats/ -- those enumerate structure for extraction and
    # display. The two are not interchangeable; see docs.
    src/die_engine/xdisasm.c
    src/die_engine/inflate.c
    src/formats/iso9660/xiso9660.c
    src/formats/pe/xpe.c
    src/formats/jpeg/xjpeg.c
    src/formats/png/xpng.c
    src/formats/apk/xapk.c
    src/formats/pdf/xpdf.c
    src/formats/elf/xelf.c
    src/formats/dex/xdex.c
    src/formats/macho/xmach.c
    src/formats/pyc/xpyc.c
    src/formats/zip/xzip.c
    src/formats/xft.c
    src/formats/xx_data_signature.c
    src/global/xx_global.c
    src/io/xx_io.c
    src/io/xx_io_file.c
    src/io/xx_io_mem.c
    src/io/xx_io_process.c
    src/memory/xx_memory.c
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
    src/algo/adler32/xx_adler32.c
    src/algo/lzfse/xx_lzfse.c
    src/algo/crc/xx_crc8.c
    src/algo/crc/xx_crc16.c
    src/algo/crc/xx_crc32.c
    src/algo/crc/xx_crc64.c
    src/algo/blake2/xx_blake2.c
    src/algo/entropy/xx_entropy.c
    src/algo/entropy/platforms/xx_entropy_sse2.c
    src/algo/entropy/platforms/xx_entropy_avx2.c
    src/algo/store/xx_store.c
    src/algo/deflate/xx_deflate.c
    src/algo/deflate/xx_zlib_stream.c
    src/algo/deflate/xx_deflate_dec.c
    src/algo/deflate/xx_deflate_enc.c
    src/algo/bzip2/xx_bzip2.c
    src/algo/bzip2/xx_bzip2_dec.c
    src/algo/bzip2/xx_bzip2_enc.c
    src/algo/lzma/xx_lzma.c
    src/algo/lzma/xx_lzma_dec.c
    src/algo/lzma/xx_lzma_enc.c
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
    src/algo/ain/xx_ain.c
    src/algo/ampk/xx_ampk.c
    src/algo/arcfs/xx_arcfs_lzw.c
    src/algo/arcv2/xx_arcv2_lzhuf.c
    src/algo/arcv4/xx_arcv4.c
    src/algo/aldus/xx_aldus_lzw.c
    src/algo/aldus/xx_aldus_lzsh.c
    src/algo/dcl/xx_dcl.c
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
    src/formats/xx_format_extractor.c
    src/formats/xx_memory_map.c
    src/formats/zip/xx_zip.c
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
    src/formats/rar/xx_rar.c
    src/formats/rar/xx_rar_extractor.c
    src/formats/ar/xx_ar.c
    src/formats/ar/xx_ar_extractor.c
    src/formats/bz2/xx_bz2.c
    src/formats/bz2/xx_bz2_extractor.c
    src/formats/gz/xx_gz.c
    src/formats/gz/xx_gz_extractor.c
    src/formats/xz/xx_xz.c
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
)

if(WIN32)
    list(APPEND XXFCLIB_SOURCES
        src/platforms/xx_entry_windows.c
        src/io/platforms/xx_io_windows.c
        src/memory/platforms/xx_memory_windows.c
        src/strings/platforms/xx_string_windows.c
        src/fs/platforms/xx_fs_windows.c
        src/rt/platforms/xx_rt_windows.c
        src/die_engine/platforms/xx_die_engine_windows.c
    )
else()
    list(APPEND XXFCLIB_SOURCES
        src/io/platforms/xx_io_posix.c
        src/memory/platforms/xx_memory_posix.c
        src/strings/platforms/xx_string_posix.c
        src/fs/platforms/xx_fs_posix.c
        src/rt/platforms/xx_rt_posix.c
        src/die_engine/platforms/xx_die_engine_posix.c
    )
endif()

include(GNUInstallDirs)

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Helper to configure target include dirs, warnings, and export/import definitions
function(xxfclib_configure_target target is_shared)
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
        )
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)

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
                target_link_libraries(${target} PRIVATE Threads::Threads)
            else()
                target_link_libraries(${target} INTERFACE Threads::Threads)
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

if(MSVC)
    set_source_files_properties(src/data/platforms/xx_data_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/algo/entropy/platforms/xx_entropy_avx2.c PROPERTIES COMPILE_FLAGS "/arch:AVX2")
endif()


# Optional sample programs
if(XXFC_BUILD_SAMPLES)
    add_subdirectory(samples/console)
    add_subdirectory(samples/unpack)
endif()

# Installation Targets
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
if(XXFC_BUILD_TESTS)
    enable_testing()
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
