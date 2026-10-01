/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_format_additional.h"
#include "xxfclib/formats/dmk/xx_dmk.h"
#include "xxfclib/formats/mfs/xx_mfs.h"
#include "xxfclib/formats/hfs/xx_hfs.h"
#include "xxfclib/formats/catsystem_kif/xx_catsystem_kif.h"
#include "xxfclib/formats/malie_lib/xx_malie_lib.h"
#include "xxfclib/formats/nexas_pac/xx_nexas_pac.h"
#include "xxfclib/formats/nitroplus_npa/xx_nitroplus_npa.h"
#include "xxfclib/formats/ufs1/xx_ufs1.h"
#include "xxfclib/formats/apple_dos33/xx_apple_dos33.h"
#include "xxfclib/formats/apple_pascal/xx_apple_pascal.h"
#include "xxfclib/formats/nitroplus_npk2/xx_nitroplus_npk2.h"
#include "xxfclib/formats/thomson_sap/xx_thomson_sap.h"
#include "xxfclib/formats/exfat/xx_exfat.h"
#include "xxfclib/formats/majiro/xx_majiro.h"
#include "xxfclib/formats/wux/xx_wux.h"
#include "xxfclib/formats/sdi/xx_sdi.h"
#include "xxfclib/formats/nhd/xx_nhd.h"
#include "xxfclib/formats/virtual98/xx_virtual98.h"
#include "xxfclib/formats/xva/xx_xva.h"
#include "xxfclib/formats/qlie_pack/xx_qlie_pack.h"
#include "xxfclib/formats/hfsplus/xx_hfsplus.h"
#include "xxfclib/formats/partimage/xx_partimage.h"
#include "xxfclib/formats/aaruformat/xx_aaruformat.h"
#include "xxfclib/formats/acorn_adfs/xx_acorn_adfs.h"
#include "xxfclib/formats/gdi/xx_gdi.h"
#include "xxfclib/formats/mds/xx_mds.h"
#include "xxfclib/formats/cbm_d64/xx_cbm_d64.h"
#include "xxfclib/formats/cbm_d71/xx_cbm_d71.h"
#include "xxfclib/formats/cbm_d81/xx_cbm_d81.h"
#include "xxfclib/formats/ccd/xx_ccd.h"
#include "xxfclib/formats/cdrdao_toc/xx_cdrdao_toc.h"
#include "xxfclib/formats/diskcopy42/xx_diskcopy42.h"
#include "xxfclib/formats/acorn_dfs/xx_acorn_dfs.h"
#include "xxfclib/formats/atari_dos2/xx_atari_dos2.h"
#include "xxfclib/formats/apridisk/xx_apridisk.h"
#include "xxfclib/formats/ti99_dsk/xx_ti99_dsk.h"
#include "xxfclib/formats/apple_dos32/xx_apple_dos32.h"
#include "xxfclib/formats/apple_dos33_32/xx_apple_dos33_32.h"
#include "xxfclib/formats/cbm_d8x/xx_cbm_d8x.h"
#include "xxfclib/formats/cbm_d67/xx_cbm_d67.h"
#include "xxfclib/formats/cbm_d90/xx_cbm_d90.h"
#include "xxfclib/formats/snatchit_cp2/xx_snatchit_cp2.h"
#include "xxfclib/formats/pce_pri/xx_pce_pri.h"
#include "xxfclib/formats/pce_pfi/xx_pce_pfi.h"
#include "xxfclib/formats/pce_pfdc/xx_pce_pfdc.h"
#include "xxfclib/formats/pce_pbi/xx_pce_pbi.h"
#include "xxfclib/formats/pce_pbit/xx_pce_pbit.h"
#include "xxfclib/formats/pce_tc/xx_pce_tc.h"
#include "xxfclib/formats/erofs/xx_erofs.h"
#include "xxfclib/formats/ldbs/xx_ldbs.h"
#include "xxfclib/formats/ldbst/xx_ldbst.h"
#include "xxfclib/formats/bytekiller/xx_bytekiller.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include <stdio.h>
#include "xxfclib/formats/pmdiskcopy/xx_pmdiskcopy.h"
#include "xxfclib/formats/qrst/xx_qrst.h"
#include "xxfclib/formats/phar/xx_phar.h"
#include "xxfclib/formats/bzip1/xx_bzip1.h"
#include "xxfclib/formats/freeze/xx_freeze.h"
#include "xxfclib/formats/lpak/xx_lpak.h"
#include "xxfclib/formats/lha/xx_lha.h"
#include "xxfclib/formats/starkit/xx_starkit.h"
#include "xxfclib/formats/gitobject/xx_gitobject.h"
#include "xxfclib/formats/bgi/xx_bgi.h"
#include "xxfclib/formats/bgi2/xx_bgi2.h"
#include "xxfclib/formats/binscii/xx_binscii.h"
#include "xxfclib/formats/blindwrite_5_6_image/xx_blindwrite_5_6_image.h"
#include "xxfclib/formats/btrfs_stream/xx_btrfs_stream.h"
#include "xxfclib/formats/cpk/xx_cpk.h"
#include "xxfclib/formats/crt/xx_crt.h"
#include "xxfclib/formats/d_link_alpha_encimg_v2/xx_d_link_alpha_encimg_v2.h"
#include "xxfclib/formats/d_link_fpkg_cpkg/xx_d_link_fpkg_cpkg.h"
#include "xxfclib/formats/daemon_tools_mdx/xx_daemon_tools_mdx.h"
#include "xxfclib/formats/diet_compression/xx_diet_compression.h"
#include "xxfclib/formats/dxa/xx_dxa.h"
#include "xxfclib/formats/ea_fsh/xx_ea_fsh.h"
#include "xxfclib/formats/ewf2_ex01/xx_ewf2_ex01.h"
#include "xxfclib/formats/ewf2_lx01/xx_ewf2_lx01.h"
#include "xxfclib/formats/ewf_l01/xx_ewf_l01.h"
#include "xxfclib/formats/fmod_sample_bank/xx_fmod_sample_bank.h"
#include "xxfclib/formats/gbi/xx_gbi.h"
#include "xxfclib/formats/hsf/xx_hsf.h"
#include "xxfclib/formats/htc_nbh_rom_image/xx_htc_nbh_rom_image.h"
#include "xxfclib/formats/hxc_hfe_extended/xx_hxc_hfe_extended.h"
#include "xxfclib/formats/hxc_hfe_hddd_a2_variant/xx_hxc_hfe_hddd_a2_variant.h"
#include "xxfclib/formats/hxc_hfe_v3/xx_hxc_hfe_v3.h"
#include "xxfclib/formats/hxs/xx_hxs.h"
#include "xxfclib/formats/jffs2_old/xx_jffs2_old.h"
#include "xxfclib/formats/kgb_archiver/xx_kgb_archiver.h"
#include "xxfclib/formats/livemaker/xx_livemaker.h"
#include "xxfclib/formats/maxis_far_archive/xx_maxis_far_archive.h"
#include "xxfclib/formats/minix/xx_minix.h"
#include "xxfclib/formats/moof/xx_moof.h"
#include "xxfclib/formats/ms_dos_backup2/xx_ms_dos_backup2.h"
#include "xxfclib/formats/noa/xx_noa.h"
#include "xxfclib/formats/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.h"
#include "xxfclib/formats/partclone_image/xx_partclone_image.h"
#include "xxfclib/formats/ppmd/xx_ppmd.h"
#include "xxfclib/formats/rpa/xx_rpa.h"
#include "xxfclib/formats/rpg_maker_rgssad/xx_rpg_maker_rgssad.h"
#include "xxfclib/formats/sfark_compressed_soundfont/xx_sfark_compressed_soundfont.h"
#include "xxfclib/formats/sis/xx_sis.h"
#include "xxfclib/formats/spectrum_udi/xx_spectrum_udi.h"
#include "xxfclib/formats/squashfs_sqlz/xx_squashfs_sqlz.h"
#include "xxfclib/formats/stos_memory_bank/xx_stos_memory_bank.h"
#include "xxfclib/formats/stuffitx/xx_stuffitx.h"
#include "xxfclib/formats/sufs/xx_sufs.h"
#include "xxfclib/formats/nextstep_diskimage/xx_nextstep_diskimage.h"
#include "xxfclib/formats/sfx_vms_dcx/xx_sfx_vms_dcx.h"
#include "xxfclib/formats/oberon/xx_oberon.h"
#include "xxfclib/formats/solitaire_deluxe/xx_solitaire_deluxe.h"
#include "xxfclib/formats/sunvtoc/xx_sunvtoc.h"
#include "xxfclib/formats/telltale_ttarch/xx_telltale_ttarch.h"
#include "xxfclib/formats/ufs2/xx_ufs2.h"
#include "xxfclib/formats/uif/xx_uif.h"
#include "xxfclib/formats/valve_gcf_cache/xx_valve_gcf_cache.h"
#include "xxfclib/formats/valve_xzp/xx_valve_xzp.h"
#include "xxfclib/formats/vmdk_cowd_sparse/xx_vmdk_cowd_sparse.h"
#include "xxfclib/formats/vmdk_sesparse/xx_vmdk_sesparse.h"
#include "xxfclib/formats/xiaomi_hdr1/xx_xiaomi_hdr1.h"
#include "xxfclib/formats/xiaomi_hdr2/xx_xiaomi_hdr2.h"
#include "xxfclib/formats/xp3/xx_xp3.h"
#include "xxfclib/formats/xpk_compressed_file/xx_xpk_compressed_file.h"
#include "xxfclib/formats/yenc_encoded_file/xx_yenc_encoded_file.h"
#include "xxfclib/formats/ypf/xx_ypf.h"
#include "xxfclib/formats/bga/xx_bga.h"

static bool probe_read_exact(xx_io_device *device, unsigned char *buffer, size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t got = xx_io_read(device, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* Validate anchors with bounded reads. Negative offsets are relative to EOF. */
static bool matches(xx_io_device *device, const unsigned char *prefix,
                    size_t prefix_size, int64_t size, int64_t offset,
                    const unsigned char *bytes, size_t count) {
    unsigned char buffer[32];
    if (offset < 0) offset += size;
    if (count > sizeof(buffer) || offset < 0 || offset > size ||
        (int64_t)count > size - offset) return false;
    if (offset <= (int64_t)prefix_size &&
        count <= prefix_size - (size_t)offset)
        return xx_rt_memcmp(prefix + (size_t)offset, bytes, count) == 0;
    return xx_io_seek64(device, offset, SEEK_SET) == 0 &&
           probe_read_exact(device, buffer, count) &&
           xx_rt_memcmp(buffer, bytes, count) == 0;
}

/* The FTSV launcher has more than one executable header.  Its distinctive
 * archive banner may be a few kilobytes into the image, so check a bounded
 * prefix before asking the reader to validate and decode the full stream. */
static bool has_vms_dcx_ftsv_banner(xx_io_device *device, int64_t size) {
    static const unsigned char banner[] =
        "OpenVMS DCX FTSV Compressed File";
    unsigned char block[4096];
    int64_t limit = size < 1024 * 1024 ? size : 1024 * 1024;
    int64_t at = 0;
    size_t i;
    if (!device || limit < (int64_t)(sizeof(banner) - 1U)) return false;
    while (at < limit) {
        size_t take = (size_t)(limit - at);
        if (take > sizeof(block)) take = sizeof(block);
        if (xx_io_seek64(device, at, SEEK_SET) != 0 ||
            !probe_read_exact(device, block, take)) return false;
        for (i = 0U; i + sizeof(banner) - 1U <= take; ++i) {
            if (xx_rt_memcmp(block + i, banner, sizeof(banner) - 1U) == 0)
                return true;
        }
        if (take <= sizeof(banner) - 1U) break;
        at += (int64_t)(take - (sizeof(banner) - 2U));
    }
    return false;
}

xx_file_type_t xx_format_detect_additional(xx_io_device *device) {
    int64_t saved, size;
    unsigned char prefix[64] = {0};
    size_t prefix_size;
    xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    if (!device) return result;
    saved = xx_io_tell(device);
    if (saved < 0) saved = 0;
    size = xx_io_total_size(device);
    if (size <= 0) goto done;
    prefix_size = size < (int64_t)sizeof(prefix) ? (size_t)size : sizeof(prefix);
    if (xx_io_seek64(device, 0, SEEK_SET) != 0 ||
        !probe_read_exact(device, prefix, prefix_size)) goto done;
    if (prefix_size >= 30U &&
        !xx_rt_memcmp(prefix, "SOFTWARE PIRATESRelease 3.02$0", 30U)) {
        xx_snatchit_cp2 *reader = xx_snatchit_cp2_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_snatchit_cp2_free(reader);
        if (valid) { result = XX_FILE_TYPE_SNATCHIT_CP2; goto done; }
    }
    if (prefix_size >= 4U && !xx_rt_memcmp(prefix, "PRI ", 4U)) {
        xx_pce_pri *reader = xx_pce_pri_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_pce_pri_free(reader);
        if (valid) { result = XX_FILE_TYPE_PCE_PRI; goto done; }
    }
    if (prefix_size >= 4U && !xx_rt_memcmp(prefix, "PFI ", 4U)) {
        xx_pce_pfi *reader = xx_pce_pfi_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_pce_pfi_free(reader);
        if (valid) { result = XX_FILE_TYPE_PCE_PFI; goto done; }
    }
    if (prefix_size >= 8U && !xx_rt_memcmp(prefix, "PFDC", 4U)) {
        unsigned version;
        xx_file_type_t type;
        xx_pce_pfdc *reader;
        if (prefix[4] == 0U && prefix[5] == 0U &&
            prefix[6] == 0U && prefix[7] == 0U) {
            version = 0U; type = XX_FILE_TYPE_PCE_PFDC_V0;
        } else if (prefix[4] == 0U && prefix[5] == 1U &&
                   prefix[6] == 0U && prefix[7] == 0U) {
            version = 1U; type = XX_FILE_TYPE_PCE_PFDC_V1;
        } else if (prefix[4] == 0U && prefix[5] == 2U &&
                   prefix[6] == 0U && prefix[7] == 0U) {
            version = 2U; type = XX_FILE_TYPE_PCE_PFDC_V2;
        } else if (prefix[4] == 0U && prefix[5] == 0U &&
                   prefix[6] == 0U && prefix[7] == 4U) {
            version = 4U; type = XX_FILE_TYPE_PCE_PFDC_V4;
        } else goto done;
        reader = xx_pce_pfdc_create(device, 0, version);
        if (reader) {
            bool valid = xx_format_is_valid(&reader->format, NULL);
            xx_pce_pfdc_free(reader);
            if (valid) { result = type; goto done; }
        }
    }
    if (prefix_size >= 4U &&
        (!xx_rt_memcmp(prefix, "PBI ", 4U) ||
         !xx_rt_memcmp(prefix, "PBIn", 4U))) {
        xx_pce_pbi *reader = xx_pce_pbi_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_pce_pbi_free(reader);
        if (valid) { result = XX_FILE_TYPE_PCE_PBI; goto done; }
    }
    if (prefix_size >= 4U && !xx_rt_memcmp(prefix, "PBIT", 4U)) {
        xx_pce_pbit *reader = xx_pce_pbit_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_pce_pbit_free(reader);
        if (valid) { result = XX_FILE_TYPE_PCE_PBIT; goto done; }
    }
    if (prefix_size >= 2U && prefix[0] == 0x5aU && prefix[1] == 0xa5U) {
        xx_pce_tc *reader = xx_pce_tc_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_pce_tc_free(reader);
        if (valid) { result = XX_FILE_TYPE_PCE_TC; goto done; }
    }
    if (size >= 1028) {
        uint8_t magic[4];
        bool found = xx_io_seek64(device, 1024, SEEK_SET) == 0 &&
                     probe_read_exact(device, magic, sizeof(magic)) &&
                     !xx_rt_memcmp(magic, "\xe2\xe1\xf5\xe0", 4U);
        if (xx_io_seek64(device, 0, SEEK_SET) != 0) goto done;
        if (found) {
            xx_erofs *reader = xx_erofs_create(device, 0);
            bool valid = reader && xx_format_is_valid(&reader->format, NULL);
            xx_erofs_free(reader);
            if (valid) { result = XX_FILE_TYPE_EROFS; goto done; }
        }
    }
    if (prefix_size >= 8U &&
        !xx_rt_memcmp(prefix, "LBS\1DSK\2", 8U)) {
        xx_ldbs *reader = xx_ldbs_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_ldbs_free(reader);
        if (valid) { result = XX_FILE_TYPE_LDBS; goto done; }
    }
    if ((prefix_size >= 6U && !xx_rt_memcmp(prefix,"[LDBS]",6U)) ||
        (prefix_size >= 9U && prefix[0] == 0xefU &&
         prefix[1] == 0xbbU && prefix[2] == 0xbfU &&
         !xx_rt_memcmp(prefix+3U,"[LDBS]",6U))) {
        xx_ldbst *reader = xx_ldbst_create(device, 0);
        bool valid = reader && xx_format_is_valid(&reader->format, NULL);
        xx_ldbst_free(reader);
        if (valid) { result = XX_FILE_TYPE_LDBST; goto done; }
    }
    if (xx_apridisk_test_magic(prefix, prefix_size)) {
        xx_apridisk *reader = xx_apridisk_create(device, 0);
        bool valid = reader && xx_apridisk_check_is_valid(&reader->format, NULL);
        xx_apridisk_free(reader);
        if (valid) { result = XX_FILE_TYPE_APRIDISK; goto done; }
    }
    if (prefix_size >= 20U &&
        (size == 92160 || size == 184320 || size == 368640) &&
        !xx_rt_memcmp(prefix + 13U, "DSK", 3U)) {
        xx_ti99_dsk *reader = xx_ti99_dsk_create(device, 0);
        bool valid = reader && xx_ti99_dsk_check_is_valid(&reader->format, NULL);
        xx_ti99_dsk_free(reader);
        if (valid) { result = XX_FILE_TYPE_TI99_DSK; goto done; }
    }
    if (prefix_size >= 16U && prefix[0] == 0x96U && prefix[1] == 0x02U &&
        (size == 92176 || size == 133136 || size == 183952)) {
        xx_atari_dos2 *reader = xx_atari_dos2_create(device, 0);
        bool valid = reader && xx_atari_dos2_check_is_valid(&reader->format, NULL);
        xx_atari_dos2_free(reader);
        if (valid) { result = XX_FILE_TYPE_ATARI_DOS2; goto done; }
    }
    if (prefix_size >= 8U && !xx_rt_memcmp(prefix, "AARUFRMT", 8U)) {
        xx_aaruformat *reader = xx_aaruformat_create(device, 0);
        bool valid = reader && xx_aaruformat_check_is_valid(&reader->format, NULL);
        xx_aaruformat_free(reader);
        if (valid) { result = XX_FILE_TYPE_AARUFORMAT; goto done; }
    }
    if (prefix_size >= 16U &&
        !xx_rt_memcmp(prefix, "PaRtImAgE-VoLuMe", 16U)) {
        xx_partimage *reader = xx_partimage_create(device, 0);
        bool valid = reader && xx_partimage_check_is_valid(&reader->format, NULL);
        xx_partimage_free(reader);
        if (valid) { result = XX_FILE_TYPE_PARTIMAGE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, -28,
                (const unsigned char *)"FilePackVer", 11)) {
        xx_qlie_pack *reader = xx_qlie_pack_create(device, 0);
        bool valid = reader && xx_qlie_pack_check_is_valid(&reader->format, NULL);
        xx_qlie_pack_free(reader);
        if (valid) { result = XX_FILE_TYPE_QLIE_PACK; goto done; }
    }
    /* Xen XVA is a TAR with ova.xml first; choose its disk reader before the
     * generic TAR fallback, but only after full native structural validation. */
    if (prefix_size >= 8U && !xx_rt_memcmp(prefix, "ova.xml\0", 8U)) {
        xx_xva *reader = xx_xva_create(device, 0);
        bool valid = reader && xx_xva_check_is_valid(&reader->format, NULL);
        xx_xva_free(reader);
        if (valid) { result = XX_FILE_TYPE_XVA; goto done; }
    }
    if (prefix_size >= 32 && (prefix[0] == 1 || prefix[0] == 2) &&
        !xx_rt_memcmp(prefix + 1, "SYSTEME D'ARCHIVAGE PUKALL S.A.P.", 31)) {
        xx_thomson_sap *reader = xx_thomson_sap_create(device, 0);
        bool valid = reader && xx_thomson_sap_check_is_valid(&reader->format, NULL);
        xx_thomson_sap_free(reader);
        if (valid) { result = XX_FILE_TYPE_THOMSON_SAP; goto done; }
    }
    if (prefix_size >= 11 && !xx_rt_memcmp(prefix + 3, "EXFAT   ", 8)) {
        xx_exfat *reader = xx_exfat_create(device, 0);
        bool valid = reader && xx_exfat_check_is_valid(&reader->format, NULL);
        xx_exfat_free(reader);
        if (valid) { result = XX_FILE_TYPE_EXFAT; goto done; }
    }
    if (prefix_size >= 7 && !xx_rt_memcmp(prefix, "NPA\x01\0\0\0", 7)) {
        xx_nitroplus_npa *reader = xx_nitroplus_npa_create(device, 0);
        bool valid = reader && xx_nitroplus_npa_check_is_valid(&reader->format, NULL);
        xx_nitroplus_npa_free(reader);
        if (valid) { result = XX_FILE_TYPE_NITROPLUS_NPA; goto done; }
    }
    if (prefix_size >= 4 && !xx_rt_memcmp(prefix, "NPK2", 4)) {
        xx_nitroplus_npk2 *reader = xx_nitroplus_npk2_create(device, 0);
        bool valid = reader && xx_nitroplus_npk2_check_is_valid(&reader->format, NULL);
        xx_nitroplus_npk2_free(reader);
        if (valid) { result = XX_FILE_TYPE_NITROPLUS_NPK2; goto done; }
    }
    if (prefix_size >= 4 && !xx_rt_memcmp(prefix, "KIF\0", 4)) {
        xx_catsystem_kif *reader = xx_catsystem_kif_create(device, 0);
        bool valid = reader && xx_catsystem_kif_check_is_valid(&reader->format, NULL);
        xx_catsystem_kif_free(reader);
        if (valid) { result = XX_FILE_TYPE_CATSYSTEM_KIF; goto done; }
    }
    if (prefix_size >= 12 && !xx_rt_memcmp(prefix, "PAC", 3) && prefix[3] != 'K') {
        xx_nexas_pac *reader = xx_nexas_pac_create(device, 0);
        bool valid = reader && xx_nexas_pac_check_is_valid(&reader->format, NULL);
        xx_nexas_pac_free(reader);
        if (valid) { result = XX_FILE_TYPE_NEXAS_PAC; goto done; }
    }
    if (prefix_size >= 4 &&
        (!xx_rt_memcmp(prefix, "LIB\0", 4) ||
         !xx_rt_memcmp(prefix, "LIBU", 4))) {
        xx_malie_lib *reader = xx_malie_lib_create(device, 0);
        bool valid = reader && xx_malie_lib_check_is_valid(&reader->format, NULL);
        xx_malie_lib_free(reader);
        if (valid) { result = XX_FILE_TYPE_MALIE_LIB; goto done; }
    }
    if (prefix_size >= 16 && !xx_rt_memcmp(prefix, "MajiroArcV", 10) &&
        prefix[10] >= '1' && prefix[10] <= '3' &&
        !xx_rt_memcmp(prefix + 11, ".000\0", 5)) {
        xx_majiro *reader = xx_majiro_create(device, 0);
        bool valid = reader && xx_majiro_check_is_valid(&reader->format, NULL);
        xx_majiro_free(reader);
        if (valid) { result = XX_FILE_TYPE_MAJIRO; goto done; }
    }
    if (prefix_size >= 8 && !xx_rt_memcmp(prefix, "WUX0\x2e\xd0\x99\x10", 8)) {
        xx_wux *reader = xx_wux_create(device, 0);
        bool valid = reader && xx_wux_check_is_valid(&reader->format, NULL);
        xx_wux_free(reader);
        if (valid) { result = XX_FILE_TYPE_WUX; goto done; }
    }
    if (prefix_size >= 8 && !xx_rt_memcmp(prefix, "$SDI0001", 8)) {
        xx_sdi *reader = xx_sdi_create(device, 0);
        bool valid = reader && xx_sdi_check_is_valid(&reader->format, NULL);
        xx_sdi_free(reader);
        if (valid) { result = XX_FILE_TYPE_SDI; goto done; }
    }
    if (prefix_size >= 13 && !xx_rt_memcmp(prefix, "T98HDDIMAGE.R0", 13)) {
        xx_nhd *reader = xx_nhd_create(device, 0);
        bool valid = reader && xx_nhd_check_is_valid(&reader->format, NULL);
        xx_nhd_free(reader);
        if (valid) { result = XX_FILE_TYPE_NHD; goto done; }
    }
    if (prefix_size >= 7 && !xx_rt_memcmp(prefix, "VHD1.00", 7)) {
        xx_virtual98 *reader = xx_virtual98_create(device, 0);
        bool valid = reader && xx_virtual98_check_is_valid(&reader->format, NULL);
        xx_virtual98_free(reader);
        if (valid) { result = XX_FILE_TYPE_VIRTUAL98; goto done; }
    }
    if(prefix_size>=2 && prefix[0]=='M' && prefix[1]=='Z'){
        xx_lha *reader=xx_lha_create(device,0);
        bool valid=reader && xx_lha_check_is_valid(&reader->format,NULL);
        xx_lha_free(reader);if(valid){result=XX_FILE_TYPE_LHA;goto done;}
        {
            xx_starkit *kit=xx_starkit_create(device,0);
            valid=kit && xx_starkit_check_is_valid(&kit->format,NULL);
            xx_starkit_free(kit);if(valid){result=XX_FILE_TYPE_STARKIT;goto done;}
        }
    }
    if(prefix_size>=4 && !xx_rt_memcmp(prefix,"LPAK",4)){
        xx_lpak *reader=xx_lpak_create(device,0);
        bool valid=reader && xx_lpak_check_is_valid(&reader->format,NULL);
        xx_lpak_free(reader);if(valid){result=XX_FILE_TYPE_LPAK;goto done;}
    }
    if(prefix_size>=2 && prefix[0]==0x1f && (prefix[1]==0x9e || prefix[1]==0x9f)) {
        xx_freeze *reader=xx_freeze_create(device,0);
        bool valid=reader && xx_freeze_check_is_valid(&reader->format,NULL);
        xx_freeze_free(reader);if(valid){result=XX_FILE_TYPE_FREEZE;goto done;}
    }
    if(prefix_size>=4 && prefix[0]=='B' && prefix[1]=='Z' && prefix[2]=='0' && prefix[3]>='1' && prefix[3]<='9') {
        xx_bzip1 *reader=xx_bzip1_create(device,0);
        bool valid=reader && xx_bzip1_check_is_valid(&reader->format,NULL);
        xx_bzip1_free(reader);if(valid){result=XX_FILE_TYPE_BZIP1;goto done;}
    }
    if((prefix_size>=2 && (prefix[0]&15)==8 && (prefix[0]>>4)<=7 && (((unsigned)prefix[0]<<8)|prefix[1])%31==0) ||
       (prefix_size>=5 && (!xx_rt_memcmp(prefix,"blob ",5) || !xx_rt_memcmp(prefix,"tree ",5) ||
        !xx_rt_memcmp(prefix,"commit ",7) || !xx_rt_memcmp(prefix,"tag ",4)))) {
        xx_gitobject *reader=xx_gitobject_create(device,0);
        bool valid=reader && xx_gitobject_check_is_valid(&reader->format,NULL);
        xx_gitobject_free(reader);if(valid){result=XX_FILE_TYPE_GIT_OBJECT;goto done;}
    }
    if(prefix_size>=11 && !xx_rt_memcmp(prefix,"PM Diskcopy",11)) {
        xx_pmdiskcopy *reader=xx_pmdiskcopy_create(device,0);
        bool valid=reader && xx_pmdiskcopy_check_is_valid(&reader->format,NULL);
        xx_pmdiskcopy_free(reader);if(valid){result=XX_FILE_TYPE_PMDISKCOPY;goto done;}
    }
    if(prefix_size>=4 && !xx_rt_memcmp(prefix,"QRST",4)) {
        xx_qrst *reader=xx_qrst_create(device,0);
        bool valid=reader && xx_qrst_check_is_valid(&reader->format,NULL);
        xx_qrst_free(reader);if(valid){result=XX_FILE_TYPE_QRST;goto done;}
    }
    if(prefix_size>=5 && (!xx_rt_memcmp(prefix,"<?php",5) || (prefix[0]=='#' && prefix[1]=='!'))) {
        xx_phar *reader=xx_phar_create(device,0);
        bool valid=reader && xx_phar_check_is_valid(&reader->format,NULL);
        xx_phar_free(reader);if(valid){result=XX_FILE_TYPE_PHAR;goto done;}
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x50\x61\x63\x6b\x46\x69\x6c\x65\x20\x20\x20\x20", 12)) {
        xx_bgi *reader = xx_bgi_create(device, 0);
        bool valid = reader && xx_bgi_check_is_valid(&reader->format, NULL);
        xx_bgi_free(reader);
        if (valid) { result = XX_FILE_TYPE_BGI; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x42\x55\x52\x49\x4b\x4f\x20\x41\x52\x43\x32\x30", 12)) {
        xx_bgi2 *reader = xx_bgi2_create(device, 0);
        bool valid = reader && xx_bgi2_check_is_valid(&reader->format, NULL);
        xx_bgi2_free(reader);
        if (valid) { result = XX_FILE_TYPE_BGI2; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x69\x4c\x65\x53\x74\x41\x72\x54\x66\x49\x6c\x45\x73\x54\x61\x52\x74", 18)) {
        xx_binscii *reader = xx_binscii_create(device, 0);
        bool valid = reader && xx_binscii_check_is_valid(&reader->format, NULL);
        xx_binscii_free(reader);
        if (valid) { result = XX_FILE_TYPE_BINSCII; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x42\x57\x54\x35\x20\x53\x54\x52\x45\x41\x4d\x20\x53\x49\x47\x4e", 16)) {
        xx_blindwrite_5_6_image *reader = xx_blindwrite_5_6_image_create(device, 0);
        bool valid = reader && xx_blindwrite_5_6_image_check_is_valid(&reader->format, NULL);
        xx_blindwrite_5_6_image_free(reader);
        if (valid) { result = XX_FILE_TYPE_BLINDWRITE_5_6_IMAGE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x62\x74\x72\x66\x73\x2d\x73\x74\x72\x65\x61\x6d\x00", 13)) {
        xx_btrfs_stream *reader = xx_btrfs_stream_create(device, 0);
        bool valid = reader && xx_btrfs_stream_check_is_valid(&reader->format, NULL);
        xx_btrfs_stream_free(reader);
        if (valid) { result = XX_FILE_TYPE_BTRFS_STREAM; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x43\x50\x4b\x20", 4)) {
        xx_cpk *reader = xx_cpk_create(device, 0);
        bool valid = reader && xx_cpk_check_is_valid(&reader->format, NULL);
        xx_cpk_free(reader);
        if (valid) { result = XX_FILE_TYPE_CPK; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x43\x36\x34\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20\x20\x20", 16) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x43\x31\x32\x38\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20\x20", 16) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x43\x42\x4d\x32\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20\x20", 16) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x56\x49\x43\x32\x30\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20", 16) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x50\x4c\x55\x53\x34\x20\x43\x41\x52\x54\x52\x49\x44\x47\x45\x20", 16)) {
        xx_crt *reader = xx_crt_create(device, 0);
        bool valid = reader && xx_crt_check_is_valid(&reader->format, NULL);
        xx_crt_free(reader);
        if (valid) { result = XX_FILE_TYPE_CRT; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 32, (const unsigned char *)"\x21\x03\x08\x20", 4) ||
         matches(device, prefix, prefix_size, size, 32, (const unsigned char *)"\x20\x08\x03\x21", 4)) {
        xx_d_link_alpha_encimg_v2 *reader = xx_d_link_alpha_encimg_v2_create(device, 0);
        bool valid = reader && xx_d_link_alpha_encimg_v2_check_is_valid(&reader->format, NULL);
        xx_d_link_alpha_encimg_v2_free(reader);
        if (valid) { result = XX_FILE_TYPE_D_LINK_ALPHA_ENCIMG_V2; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x50\x4b\x47", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x43\x50\x4b\x47", 4)) {
        xx_d_link_fpkg_cpkg *reader = xx_d_link_fpkg_cpkg_create(device, 0);
        bool valid = reader && xx_d_link_fpkg_cpkg_check_is_valid(&reader->format, NULL);
        xx_d_link_fpkg_cpkg_free(reader);
        if (valid) { result = XX_FILE_TYPE_D_LINK_FPKG_CPKG; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4d\x45\x44\x49\x41\x20\x44\x45\x53\x43\x52\x49\x50\x54\x4f\x52", 16)) {
        xx_daemon_tools_mdx *reader = xx_daemon_tools_mdx_create(device, 0);
        bool valid = reader && xx_daemon_tools_mdx_check_is_valid(&reader->format, NULL);
        xx_daemon_tools_mdx_free(reader);
        if (valid) { result = XX_FILE_TYPE_DAEMON_TOOLS_MDX; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x9d\x89\x64\x6c\x7a", 5) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\xb4\x4c\xcd\x21\x9d\x89", 6)) {
        xx_diet_compression *reader = xx_diet_compression_create(device, 0);
        bool valid = reader && xx_diet_compression_check_is_valid(&reader->format, NULL);
        xx_diet_compression_free(reader);
        if (valid) { result = XX_FILE_TYPE_DIET_COMPRESSION; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x44\x58", 2)) {
        xx_dxa *reader = xx_dxa_create(device, 0);
        bool valid = reader && xx_dxa_check_is_valid(&reader->format, NULL);
        xx_dxa_free(reader);
        if (valid) { result = XX_FILE_TYPE_DXA; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x53\x48\x50\x49", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x53\x48\x50\x50", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x53\x48\x50\x53", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x53\x48\x50\x58", 4)) {
        xx_ea_fsh *reader = xx_ea_fsh_create(device, 0);
        bool valid = reader && xx_ea_fsh_check_is_valid(&reader->format, NULL);
        xx_ea_fsh_free(reader);
        if (valid) { result = XX_FILE_TYPE_EA_FSH; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x45\x56\x46\x32\x0d\x0a\x81\x00", 8)) {
        xx_ewf2_ex01 *reader = xx_ewf2_ex01_create(device, 0);
        bool valid = reader && xx_ewf2_ex01_check_is_valid(&reader->format, NULL);
        xx_ewf2_ex01_free(reader);
        if (valid) { result = XX_FILE_TYPE_EWF2_EX01; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4c\x45\x46\x32\x0d\x0a\x81\x00", 8)) {
        xx_ewf2_lx01 *reader = xx_ewf2_lx01_create(device, 0);
        bool valid = reader && xx_ewf2_lx01_check_is_valid(&reader->format, NULL);
        xx_ewf2_lx01_free(reader);
        if (valid) { result = XX_FILE_TYPE_EWF2_LX01; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4c\x56\x46\x09\x0d\x0a\xff\x00", 8)) {
        xx_ewf_l01 *reader = xx_ewf_l01_create(device, 0);
        bool valid = reader && xx_ewf_l01_check_is_valid(&reader->format, NULL);
        xx_ewf_l01_free(reader);
        if (valid) { result = XX_FILE_TYPE_EWF_L01; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x53\x42\x31", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x53\x42\x32", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x53\x42\x33", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x53\x42\x34", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x53\x42\x35", 4)) {
        xx_fmod_sample_bank *reader = xx_fmod_sample_bank_create(device, 0);
        bool valid = reader && xx_fmod_sample_bank_check_is_valid(&reader->format, NULL);
        xx_fmod_sample_bank_free(reader);
        if (valid) { result = XX_FILE_TYPE_FMOD_SAMPLE_BANK; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x47\x42\x49", 3)) {
        xx_gbi *reader = xx_gbi_create(device, 0);
        bool valid = reader && xx_gbi_check_is_valid(&reader->format, NULL);
        xx_gbi_free(reader);
        if (valid) { result = XX_FILE_TYPE_GBI; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 32777, (const unsigned char *)"\x43\x44\x52\x4f\x4d", 5)) {
        xx_hsf *reader = xx_hsf_create(device, 0);
        bool valid = reader && xx_hsf_check_is_valid(&reader->format, NULL);
        xx_hsf_free(reader);
        if (valid) { result = XX_FILE_TYPE_HSF; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x52\x30\x30\x30\x46\x46\x0a", 7)) {
        xx_htc_nbh_rom_image *reader = xx_htc_nbh_rom_image_create(device, 0);
        bool valid = reader && xx_htc_nbh_rom_image_check_is_valid(&reader->format, NULL);
        xx_htc_nbh_rom_image_free(reader);
        if (valid) { result = XX_FILE_TYPE_HTC_NBH_ROM_IMAGE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x48\x58\x43\x50\x49\x43\x46\x45\x01", 9)) {
        xx_hxc_hfe_extended *reader = xx_hxc_hfe_extended_create(device, 0);
        bool valid = reader && xx_hxc_hfe_extended_check_is_valid(&reader->format, NULL);
        xx_hxc_hfe_extended_free(reader);
        if (valid) { result = XX_FILE_TYPE_HXC_HFE_EXTENDED; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x48\x58\x43\x50\x49\x43\x46\x45\x00", 9)) {
        xx_hxc_hfe_hddd_a2_variant *reader = xx_hxc_hfe_hddd_a2_variant_create(device, 0);
        bool valid = reader && xx_hxc_hfe_hddd_a2_variant_check_is_valid(&reader->format, NULL);
        xx_hxc_hfe_hddd_a2_variant_free(reader);
        if (valid) { result = XX_FILE_TYPE_HXC_HFE_HDDD_A2_VARIANT; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x48\x58\x43\x48\x46\x45\x56\x33", 8)) {
        xx_hxc_hfe_v3 *reader = xx_hxc_hfe_v3_create(device, 0);
        bool valid = reader && xx_hxc_hfe_v3_check_is_valid(&reader->format, NULL);
        xx_hxc_hfe_v3_free(reader);
        if (valid) { result = XX_FILE_TYPE_HXC_HFE_V3; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x49\x54\x4f\x4c\x49\x54\x4c\x53", 8)) {
        xx_hxs *reader = xx_hxs_create(device, 0);
        bool valid = reader && xx_hxs_check_is_valid(&reader->format, NULL);
        xx_hxs_free(reader);
        if (valid) { result = XX_FILE_TYPE_HXS; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x84\x19", 2) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x19\x84", 2)) {
        xx_jffs2_old *reader = xx_jffs2_old_create(device, 0);
        bool valid = reader && xx_jffs2_old_check_is_valid(&reader->format, NULL);
        xx_jffs2_old_free(reader);
        if (valid) { result = XX_FILE_TYPE_JFFS2_OLD; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4b\x00\x47\x00\x42\x00\x32\x00", 8)) {
        xx_kgb_archiver *reader = xx_kgb_archiver_create(device, 0);
        bool valid = reader && xx_kgb_archiver_check_is_valid(&reader->format, NULL);
        xx_kgb_archiver_free(reader);
        if (valid) { result = XX_FILE_TYPE_KGB_ARCHIVER; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x76\x66\x66\x00", 4)) {
        xx_livemaker *reader = xx_livemaker_create(device, 0);
        bool valid = reader && xx_livemaker_check_is_valid(&reader->format, NULL);
        xx_livemaker_free(reader);
        if (valid) { result = XX_FILE_TYPE_LIVEMAKER; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x46\x41\x52\x21\x62\x79\x41\x5a", 8)) {
        xx_maxis_far_archive *reader = xx_maxis_far_archive_create(device, 0);
        bool valid = reader && xx_maxis_far_archive_check_is_valid(&reader->format, NULL);
        xx_maxis_far_archive_free(reader);
        if (valid) { result = XX_FILE_TYPE_MAXIS_FAR_ARCHIVE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x7f\x13", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x13\x7f", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x8f\x13", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x13\x8f", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x68\x24", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x24\x68", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x78\x24", 2) ||
         matches(device, prefix, prefix_size, size, 1040, (const unsigned char *)"\x24\x78", 2) ||
         matches(device, prefix, prefix_size, size, 1048, (const unsigned char *)"\x5a\x4d", 2) ||
         matches(device, prefix, prefix_size, size, 1048, (const unsigned char *)"\x4d\x5a", 2)) {
        xx_minix *reader = xx_minix_create(device, 0);
        bool valid = reader && xx_minix_check_is_valid(&reader->format, NULL);
        xx_minix_free(reader);
        if (valid) { result = XX_FILE_TYPE_MINIX; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4d\x4f\x4f\x46\xff\x0a\x0d\x0a", 8)) {
        xx_moof *reader = xx_moof_create(device, 0);
        bool valid = reader && xx_moof_check_is_valid(&reader->format, NULL);
        xx_moof_free(reader);
        if (valid) { result = XX_FILE_TYPE_MOOF; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 1, (const unsigned char *)"\x42\x41\x43\x4b\x55\x50\x20\x20", 8)) {
        xx_ms_dos_backup2 *reader = xx_ms_dos_backup2_create(device, 0);
        bool valid = reader && xx_ms_dos_backup2_check_is_valid(&reader->format, NULL);
        xx_ms_dos_backup2_free(reader);
        if (valid) { result = XX_FILE_TYPE_MS_DOS_BACKUP2; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x45\x6e\x74\x69\x73\x1a\x00\x00", 8) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x56\x49\x53\x54\x1a\x00\x00\x00", 8)) {
        xx_noa *reader = xx_noa_create(device, 0);
        bool valid = reader && xx_noa_check_is_valid(&reader->format, NULL);
        xx_noa_free(reader);
        if (valid) { result = XX_FILE_TYPE_NOA; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\xcf\xad\x12\xfe", 4)) {
        xx_outlook_express_dbx_mailbox *reader = xx_outlook_express_dbx_mailbox_create(device, 0);
        bool valid = reader && xx_outlook_express_dbx_mailbox_check_is_valid(&reader->format, NULL);
        xx_outlook_express_dbx_mailbox_free(reader);
        if (valid) { result = XX_FILE_TYPE_OUTLOOK_EXPRESS_DBX_MAILBOX; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x70\x61\x72\x74\x63\x6c\x6f\x6e\x65\x2d\x69\x6d\x61\x67\x65", 15)) {
        xx_partclone_image *reader = xx_partclone_image_create(device, 0);
        bool valid = reader && xx_partclone_image_check_is_valid(&reader->format, NULL);
        xx_partclone_image_free(reader);
        if (valid) { result = XX_FILE_TYPE_PARTCLONE_IMAGE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x8f\xaf\xac\x84", 4)) {
        xx_ppmd *reader = xx_ppmd_create(device, 0);
        bool valid = reader && xx_ppmd_check_is_valid(&reader->format, NULL);
        xx_ppmd_free(reader);
        if (valid) { result = XX_FILE_TYPE_PPMD; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x52\x50\x41\x2d\x32\x2e\x30\x20", 8) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x52\x50\x41\x2d\x33\x2e\x30\x20", 8) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x52\x50\x41\x2d\x33\x2e\x32\x20", 8) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x52\x50\x41\x2d\x34\x2e\x30\x20", 8) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x41\x4c\x54\x2d\x31\x2e\x30\x20", 8)) {
        xx_rpa *reader = xx_rpa_create(device, 0);
        bool valid = reader && xx_rpa_check_is_valid(&reader->format, NULL);
        xx_rpa_free(reader);
        if (valid) { result = XX_FILE_TYPE_RPA; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x52\x47\x53\x53\x41\x44\x00", 7)) {
        xx_rpg_maker_rgssad *reader = xx_rpg_maker_rgssad_create(device, 0);
        bool valid = reader && xx_rpg_maker_rgssad_check_is_valid(&reader->format, NULL);
        xx_rpg_maker_rgssad_free(reader);
        if (valid) { result = XX_FILE_TYPE_RPG_MAKER_RGSSAD; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 26, (const unsigned char *)"\x73\x66\x41\x72\x6b", 5)) {
        xx_sfark_compressed_soundfont *reader = xx_sfark_compressed_soundfont_create(device, 0);
        bool valid = reader && xx_sfark_compressed_soundfont_check_is_valid(&reader->format, NULL);
        xx_sfark_compressed_soundfont_free(reader);
        if (valid) { result = XX_FILE_TYPE_SFARK_COMPRESSED_SOUNDFONT; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x7a\x1a\x20\x10", 4) ||
         matches(device, prefix, prefix_size, size, 4, (const unsigned char *)"\x6d\x00\x00\x10", 4) ||
         matches(device, prefix, prefix_size, size, 4, (const unsigned char *)"\x12\x3a\x00\x10", 4)) {
        xx_sis *reader = xx_sis_create(device, 0);
        bool valid = reader && xx_sis_check_is_valid(&reader->format, NULL);
        xx_sis_free(reader);
        if (valid) { result = XX_FILE_TYPE_SIS; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x55\x44\x49\x21", 4)) {
        xx_spectrum_udi *reader = xx_spectrum_udi_create(device, 0);
        bool valid = reader && xx_spectrum_udi_check_is_valid(&reader->format, NULL);
        xx_spectrum_udi_free(reader);
        if (valid) { result = XX_FILE_TYPE_SPECTRUM_UDI; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x73\x71\x6c\x7a", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x7a\x6c\x71\x73", 4)) {
        xx_squashfs_sqlz *reader = xx_squashfs_sqlz_create(device, 0);
        bool valid = reader && xx_squashfs_sqlz_check_is_valid(&reader->format, NULL);
        xx_squashfs_sqlz_free(reader);
        if (valid) { result = XX_FILE_TYPE_SQUASHFS_SQLZ; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4c\x69\x6f\x6e\x70\x6f\x75\x62\x6e\x6b", 10)) {
        xx_stos_memory_bank *reader = xx_stos_memory_bank_create(device, 0);
        bool valid = reader && xx_stos_memory_bank_check_is_valid(&reader->format, NULL);
        xx_stos_memory_bank_free(reader);
        if (valid) { result = XX_FILE_TYPE_STOS_MEMORY_BANK; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x53\x74\x75\x66\x66\x49\x74\x21", 8)) {
        xx_stuffitx *reader = xx_stuffitx_create(device, 0);
        bool valid = reader && xx_stuffitx_check_is_valid(&reader->format, NULL);
        xx_stuffitx_free(reader);
        if (valid) { result = XX_FILE_TYPE_STUFFITX; goto done; }
    }
    if (size >= 485 && prefix_size >= 17U &&
        matches(device, prefix, prefix_size, size, 0,
                (const unsigned char *)"Solitaire Deluxe.", 17)) {
        xx_solitaire_deluxe *reader =
            xx_solitaire_deluxe_create(device, 0);
        bool valid = reader &&
            xx_solitaire_deluxe_check_is_valid(&reader->format, NULL);
        xx_solitaire_deluxe_free(reader);
        if (valid) { result = XX_FILE_TYPE_SOLITAIRE_DELUXE; goto done; }
    }
    if (size >= 144 && size <= 1024 * 1024 * 1024 &&
        matches(device, prefix, prefix_size, size, 136,
                (const unsigned char *)"SZDD\x88\xf0\x27\x33", 8)) {
        xx_oberon *reader = xx_oberon_create(device, 0);
        bool valid = reader && xx_oberon_check_is_valid(&reader->format, NULL);
        xx_oberon_free(reader);
        if (valid) { result = XX_FILE_TYPE_OBERON; goto done; }
    }
    if (size >= 128 && size <= 128 * 1024 * 1024 &&
        prefix_size >= 8U &&
        ((prefix[0] == 0xb0U && prefix[1] == 0U &&
          prefix[2] == 0x30U && prefix[3] == 0U &&
          prefix[4] == 0x44U && prefix[5] == 0U &&
          prefix[6] == 0x60U && prefix[7] == 0U) ||
         (prefix[0] == 3U && prefix[1] == 0U && prefix[2] == 0U &&
          prefix[3] == 0U && prefix[4] == 0U && prefix[5] == 0U &&
          prefix[6] == 0U && prefix[7] == 0U &&
          has_vms_dcx_ftsv_banner(device, size)))) {
        xx_sfx_vms_dcx *reader = xx_sfx_vms_dcx_create(device, 0);
        bool valid = reader &&
            xx_sfx_vms_dcx_check_is_valid(&reader->format, NULL);
        xx_sfx_vms_dcx_free(reader);
        if (valid) { result = XX_FILE_TYPE_SFX_VMS_DCX; goto done; }
    }
    if (size >= 558 && prefix_size >= 6U &&
        prefix[0] == 0U && prefix[1] == 0U && prefix[2] == 0U &&
        prefix[3] >= 1U && prefix[3] <= 16U &&
        prefix[4] == 2U && prefix[5] == 0U) {
        xx_nextstep_diskimage *reader =
            xx_nextstep_diskimage_create(device, 0);
        bool valid = reader &&
            xx_nextstep_diskimage_check_is_valid(&reader->format, NULL);
        xx_nextstep_diskimage_free(reader);
        if (valid) { result = XX_FILE_TYPE_NEXTSTEP_DISKIMAGE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 9564, (const unsigned char *)"\x54\x19\x01\x00", 4) ||
         matches(device, prefix, prefix_size, size, 9564, (const unsigned char *)"\x00\x01\x19\x54", 4)) {
        xx_sufs *reader = xx_sufs_create(device, 0);
        bool valid = reader && xx_sufs_check_is_valid(&reader->format, NULL);
        xx_sufs_free(reader);
        if (valid) { result = XX_FILE_TYPE_SUFS; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 508, (const unsigned char *)"\xda\xbe", 2) ||
         matches(device, prefix, prefix_size, size, 1020, (const unsigned char *)"\xbe\xda", 2)) {
        xx_sunvtoc *reader = xx_sunvtoc_create(device, 0);
        bool valid = reader && xx_sunvtoc_check_is_valid(&reader->format, NULL);
        xx_sunvtoc_free(reader);
        if (valid) { result = XX_FILE_TYPE_SUNVTOC; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x34\x41\x54\x54", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x33\x41\x54\x54", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x4e\x43\x54\x54", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x5a\x43\x54\x54", 4) ||
         matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x45\x43\x54\x54", 4)) {
        xx_telltale_ttarch *reader = xx_telltale_ttarch_create(device, 0);
        bool valid = reader && xx_telltale_ttarch_check_is_valid(&reader->format, NULL);
        xx_telltale_ttarch_free(reader);
        if (valid) { result = XX_FILE_TYPE_TELLTALE_TTARCH; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 66908, (const unsigned char *)"\x19\x01\x54\x19", 4) ||
         matches(device, prefix, prefix_size, size, 66908, (const unsigned char *)"\x19\x54\x01\x19", 4) ||
         matches(device, prefix, prefix_size, size, 66908, (const unsigned char *)"\x38\x20\x01\x19", 4) ||
         matches(device, prefix, prefix_size, size, 66908, (const unsigned char *)"\x19\x01\x20\x38", 4) ||
         matches(device, prefix, prefix_size, size, 263516, (const unsigned char *)"\x19\x01\x54\x19", 4) ||
         matches(device, prefix, prefix_size, size, 263516, (const unsigned char *)"\x19\x54\x01\x19", 4) ||
         matches(device, prefix, prefix_size, size, 263516, (const unsigned char *)"\x38\x20\x01\x19", 4) ||
         matches(device, prefix, prefix_size, size, 263516, (const unsigned char *)"\x19\x01\x20\x38", 4)) {
        xx_ufs2 *reader = xx_ufs2_create(device, 0);
        bool valid = reader && xx_ufs2_check_is_valid(&reader->format, NULL);
        xx_ufs2_free(reader);
        if (valid) { result = XX_FILE_TYPE_UFS2; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, -64, (const unsigned char *)"\x62\x62\x69\x73", 4)) {
        xx_uif *reader = xx_uif_create(device, 0);
        bool valid = reader && xx_uif_check_is_valid(&reader->format, NULL);
        xx_uif_free(reader);
        if (valid) { result = XX_FILE_TYPE_UIF; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x01\x00\x00\x00\x01\x00\x00\x00", 8)) {
        xx_valve_gcf_cache *reader = xx_valve_gcf_cache_create(device, 0);
        bool valid = reader && xx_valve_gcf_cache_check_is_valid(&reader->format, NULL);
        xx_valve_gcf_cache_free(reader);
        if (valid) { result = XX_FILE_TYPE_VALVE_GCF_CACHE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x70\x69\x5a\x78", 4)) {
        xx_valve_xzp *reader = xx_valve_xzp_create(device, 0);
        bool valid = reader && xx_valve_xzp_check_is_valid(&reader->format, NULL);
        xx_valve_xzp_free(reader);
        if (valid) { result = XX_FILE_TYPE_VALVE_XZP; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x43\x4f\x57\x44", 4)) {
        xx_vmdk_cowd_sparse *reader = xx_vmdk_cowd_sparse_create(device, 0);
        bool valid = reader && xx_vmdk_cowd_sparse_check_is_valid(&reader->format, NULL);
        xx_vmdk_cowd_sparse_free(reader);
        if (valid) { result = XX_FILE_TYPE_VMDK_COWD_SPARSE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\xbe\xba\xfe\xca\x00\x00\x00\x00", 8)) {
        xx_vmdk_sesparse *reader = xx_vmdk_sesparse_create(device, 0);
        bool valid = reader && xx_vmdk_sesparse_check_is_valid(&reader->format, NULL);
        xx_vmdk_sesparse_free(reader);
        if (valid) { result = XX_FILE_TYPE_VMDK_SESPARSE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x48\x44\x52\x31", 4)) {
        xx_xiaomi_hdr1 *reader = xx_xiaomi_hdr1_create(device, 0);
        bool valid = reader && xx_xiaomi_hdr1_check_is_valid(&reader->format, NULL);
        xx_xiaomi_hdr1_free(reader);
        if (valid) { result = XX_FILE_TYPE_XIAOMI_HDR1; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x48\x44\x52\x32", 4)) {
        xx_xiaomi_hdr2 *reader = xx_xiaomi_hdr2_create(device, 0);
        bool valid = reader && xx_xiaomi_hdr2_check_is_valid(&reader->format, NULL);
        xx_xiaomi_hdr2_free(reader);
        if (valid) { result = XX_FILE_TYPE_XIAOMI_HDR2; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x58\x50\x33\x0d\x0a\x20\x0a\x1a\x8b\x67\x01", 11)) {
        xx_xp3 *reader = xx_xp3_create(device, 0);
        bool valid = reader && xx_xp3_check_is_valid(&reader->format, NULL);
        xx_xp3_free(reader);
        if (valid) { result = XX_FILE_TYPE_XP3; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x58\x50\x4b\x46", 4)) {
        xx_xpk_compressed_file *reader = xx_xpk_compressed_file_create(device, 0);
        bool valid = reader && xx_xpk_compressed_file_check_is_valid(&reader->format, NULL);
        xx_xpk_compressed_file_free(reader);
        if (valid) { result = XX_FILE_TYPE_XPK_COMPRESSED_FILE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x3d\x79\x62\x65\x67\x69\x6e\x20", 8)) {
        xx_yenc_encoded_file *reader = xx_yenc_encoded_file_create(device, 0);
        bool valid = reader && xx_yenc_encoded_file_check_is_valid(&reader->format, NULL);
        xx_yenc_encoded_file_free(reader);
        if (valid) { result = XX_FILE_TYPE_YENC_ENCODED_FILE; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 0, (const unsigned char *)"\x59\x50\x46\x00", 4)) {
        xx_ypf *reader = xx_ypf_create(device, 0);
        bool valid = reader && xx_ypf_check_is_valid(&reader->format, NULL);
        xx_ypf_free(reader);
        if (valid) { result = XX_FILE_TYPE_YPF; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 4, (const unsigned char *)"\x47\x5a\x49\x50", 4) ||
         matches(device, prefix, prefix_size, size, 4, (const unsigned char *)"\x42\x5a\x32\x00", 4)) {
        xx_bga *reader = xx_bga_create(device, 0);
        bool valid = reader && xx_bga_check_is_valid(&reader->format, NULL);
        xx_bga_free(reader);
        if (valid) { result = XX_FILE_TYPE_BGA; goto done; }
    }
    if (size == 116480 &&
        matches(device, prefix, prefix_size, size, 17 * 13 * 256 + 3,
                (const unsigned char *)"\x02", 1)) {
        xx_apple_dos32 *reader = xx_apple_dos32_create(device, 0);
        bool valid = reader && xx_apple_dos32_check_is_valid(&reader->format, NULL);
        xx_apple_dos32_free(reader);
        if (valid) { result = XX_FILE_TYPE_APPLE_DOS32; goto done; }
    }
    if (size == 409600 &&
        matches(device, prefix, prefix_size, size, 17 * 32 * 256 + 3,
                (const unsigned char *)"\x03", 1)) {
        xx_apple_dos33_32 *reader = xx_apple_dos33_32_create(device, 0);
        bool valid = reader && xx_apple_dos33_32_check_is_valid(&reader->format, NULL);
        xx_apple_dos33_32_free(reader);
        if (valid) { result = XX_FILE_TYPE_APPLE_DOS33_32; goto done; }
    }
    if (size >= 73728 &&
        matches(device, prefix, prefix_size, size, 69635,
                (const unsigned char *)"\x03", 1) &&
        matches(device, prefix, prefix_size, size, 69671,
                (const unsigned char *)"\x7a", 1) &&
        matches(device, prefix, prefix_size, size, 69685,
                (const unsigned char *)"\x10\x00\x01", 3)) {
        xx_apple_dos33 *reader = xx_apple_dos33_create(device, 0);
        bool valid = reader && xx_apple_dos33_check_is_valid(&reader->format, NULL);
        xx_apple_dos33_free(reader);
        if (valid) { result = XX_FILE_TYPE_APPLE_DOS33; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 1024,
                (const unsigned char *)"\xd2\xd7", 2)) {
        xx_mfs *reader = xx_mfs_create(device, 0);
        bool valid = reader && xx_mfs_check_is_valid(&reader->format, NULL);
        xx_mfs_free(reader);
        if (valid) { result = XX_FILE_TYPE_MFS; goto done; }
    }
    if (size >= 1536 && matches(device, prefix, prefix_size, size, 1024,
                (const unsigned char *)"BD", 2)) {
        xx_hfs *reader = xx_hfs_create(device, 0);
        bool valid = reader && xx_hfs_check_is_valid(&reader->format, NULL);
        xx_hfs_free(reader);
        if (valid) { result = XX_FILE_TYPE_HFS; goto done; }
    }
    if (size >= 1536 &&
        (matches(device, prefix, prefix_size, size, 1024,
                 (const unsigned char *)"H+", 2) ||
         matches(device, prefix, prefix_size, size, 1024,
                 (const unsigned char *)"HX", 2))) {
        xx_hfsplus *reader = xx_hfsplus_create(device, 0);
        bool valid = reader && xx_hfsplus_check_is_valid(&reader->format, NULL);
        xx_hfsplus_free(reader);
        if (valid) { result = XX_FILE_TYPE_HFSPLUS; goto done; }
    }
    if (matches(device, prefix, prefix_size, size, 9564,
                (const unsigned char *)"\x54\x19\x01\0", 4) ||
        matches(device, prefix, prefix_size, size, 9564,
                (const unsigned char *)"\0\x01\x19\x54", 4)) {
        xx_ufs1 *reader = xx_ufs1_create(device, 0);
        bool valid = reader && xx_ufs1_check_is_valid(&reader->format, NULL);
        xx_ufs1_free(reader);
        if (valid) { result = XX_FILE_TYPE_UFS1; goto done; }
    }
    /* DMK has no magic. Only probe its constrained header, then validate
     * every track, sector ID, geometry and CRC before selecting this type. */
    if (prefix_size >= 16 && (prefix[0] == 0 || prefix[0] == 0xff) &&
        prefix[1] && !(prefix[4] & 0xaf) &&
        (unsigned)(prefix[2] | ((unsigned)prefix[3] << 8)) > 128 &&
        (unsigned)(prefix[2] | ((unsigned)prefix[3] << 8)) < 16384 &&
        !xx_rt_memcmp(prefix + 5, "\0\0\0\0\0\0\0\0\0\0\0", 11)) {
        xx_dmk *reader = xx_dmk_create(device, 0);
        bool valid = reader && xx_dmk_check_is_valid(&reader->format, NULL);
        xx_dmk_free(reader);
        if (valid) { result = XX_FILE_TYPE_DMK; goto done; }
    }
    /* A Pascal volume has a fixed directory header in either supported
     * sector order. Require its structural validation after fixed signatures. */
    if (matches(device, prefix, prefix_size, size, 1024,
                (const unsigned char *)"\0\0\x06\0\0\0", 6) ||
        (size >= 143360 && matches(device, prefix, prefix_size, size, 2816,
                (const unsigned char *)"\0\0\x06\0\0\0", 6))) {
        xx_apple_pascal *reader = xx_apple_pascal_create(device, 0);
        bool valid = reader && xx_apple_pascal_check_is_valid(&reader->format, NULL);
        xx_apple_pascal_free(reader);
        if (valid) result = XX_FILE_TYPE_APPLE_PASCAL;
    }
    /* Old-map ADFS has no magic. The exact S/M/L geometry and map's sector
     * count only gate a complete map/directory/ownership validation. L's
     * alternate physical order needs explicit reader selection. */
    if (result == XX_FILE_TYPE_UNKNOWN &&
        ((size == 163840 && matches(device, prefix, prefix_size, size, 252,
                                   (const unsigned char *)"\x80\x02\0", 3)) ||
         (size == 327680 && matches(device, prefix, prefix_size, size, 252,
                                   (const unsigned char *)"\0\x05\0", 3)) ||
         (size == 655360 && matches(device, prefix, prefix_size, size, 252,
                                   (const unsigned char *)"\0\x0a\0", 3)))) {
        xx_acorn_adfs *reader = xx_acorn_adfs_create(device, 0);
        bool valid = reader && xx_acorn_adfs_check_is_valid(&reader->format, NULL);
        xx_acorn_adfs_free(reader);
        if (valid) result = XX_FILE_TYPE_ADFS;
    }
    if (result == XX_FILE_TYPE_UNKNOWN && size <= 65536 &&
        xx_gdi_test_magic(prefix, prefix_size)) {
        xx_gdi *reader = xx_gdi_create(device, 0);
        bool valid = reader && xx_gdi_check_is_valid(&reader->format, NULL);
        xx_gdi_free(reader);
        if (valid) result = XX_FILE_TYPE_GDI;
    }
    if (result == XX_FILE_TYPE_UNKNOWN && size <= 131072 &&
        xx_ccd_test_magic(prefix, prefix_size)) {
        xx_ccd *reader = xx_ccd_create(device, 0);
        bool valid = reader && xx_ccd_check_is_valid(&reader->format, NULL);
        xx_ccd_free(reader);
        if (valid) result = XX_FILE_TYPE_CCD;
    }
    if (result == XX_FILE_TYPE_UNKNOWN && size <= 65536 && prefix_size >= 2U &&
        (prefix[0] == '/' || prefix[0] == 'C' || prefix[0] == ' ' ||
         prefix[0] == '\t' || prefix[0] == '\r' || prefix[0] == '\n')) {
        bool candidate = xx_cdrdao_toc_test_magic(prefix, prefix_size);
        if (!candidate && size > (int64_t)prefix_size) {
            uint8_t *sheet = (uint8_t *)xx_mem_alloc((size_t)size);
            if (sheet) {
                candidate = xx_io_seek64(device, 0, SEEK_SET) == 0 &&
                    probe_read_exact(device, sheet, (size_t)size) &&
                    xx_cdrdao_toc_test_magic(sheet, (size_t)size);
                xx_mem_free(sheet);
            }
        }
        if (candidate) {
            xx_cdrdao_toc *reader = xx_cdrdao_toc_create(device, 0);
            bool valid = reader && xx_cdrdao_toc_check_is_valid(&reader->format, NULL);
            xx_cdrdao_toc_free(reader);
            if (valid) result = XX_FILE_TYPE_CDRDAO_TOC;
        }
    }
    if (result == XX_FILE_TYPE_UNKNOWN && prefix_size >= 18U &&
        !xx_rt_memcmp(prefix, "MEDIA DESCRIPTOR", 16U)) {
        xx_mds *reader = xx_mds_create(device, 0);
        bool valid = reader && xx_mds_check_is_valid(&reader->format, NULL);
        xx_mds_free(reader);
        if (valid) result = XX_FILE_TYPE_MDS;
    }
    if (result == XX_FILE_TYPE_UNKNOWN &&
        size >= 84 + 409600 && size <= 84 + 1474560) {
        uint8_t header[84];
        if (xx_io_seek64(device, 0, SEEK_SET) == 0 &&
            probe_read_exact(device, header, sizeof(header)) &&
            xx_diskcopy42_test_magic(header, sizeof(header))) {
            xx_diskcopy42 *reader = xx_diskcopy42_create(device, 0);
            bool valid = reader && xx_diskcopy42_check_is_valid(&reader->format, NULL);
            xx_diskcopy42_free(reader);
            if (valid) result = XX_FILE_TYPE_DISKCOPY42;
        }
    }
    if (result == XX_FILE_TYPE_UNKNOWN &&
        size >= 102400 && size <= 4194304) {
        uint8_t catalog[8];
        if (xx_io_seek64(device, 256, SEEK_SET) == 0 &&
            probe_read_exact(device, catalog, sizeof(catalog)) &&
            catalog[5] <= 248U && (catalog[5] & 7U) == 0U &&
            ((((unsigned)catalog[6] & 3U) << 8U) | catalog[7]) >= 400U &&
            ((((unsigned)catalog[6] & 3U) << 8U) | catalog[7]) <= 800U) {
            xx_acorn_dfs *reader = xx_acorn_dfs_create(device, 0);
            bool valid = reader && xx_acorn_dfs_check_is_valid(&reader->format, NULL);
            xx_acorn_dfs_free(reader);
            if (valid) result = XX_FILE_TYPE_ACORN_DFS;
        }
    }
    if (result == XX_FILE_TYPE_UNKNOWN && (size == 7520256 || size == 5013504) &&
        matches(device, prefix, prefix_size, size, 0,
                (const unsigned char *)"\x00\x01\x00\xFF\x4C\x0A\x4C\x14\x01\x00", 10)) {
        xx_cbm_d90 *reader = xx_cbm_d90_create_ex(device, 0,
            size == 5013504 ? XX_CBM_D90_9060 : XX_CBM_D90_9090);
        bool valid = reader && xx_cbm_d90_check_is_valid(&reader->format, NULL);
        xx_cbm_d90_free(reader);
        if (valid) result = XX_FILE_TYPE_CBM_D90;
    }
    if (result == XX_FILE_TYPE_UNKNOWN && size == 176640 &&
        matches(device, prefix, prefix_size, size, 91394,
                (const unsigned char *)"\x01", 1)) {
        xx_cbm_d67 *reader = xx_cbm_d67_create(device, 0);
        bool valid = reader && xx_cbm_d67_check_is_valid(&reader->format, NULL);
        xx_cbm_d67_free(reader);
        if (valid) result = XX_FILE_TYPE_CBM_D67;
    }
    if (result == XX_FILE_TYPE_UNKNOWN &&
        (size == 533248 || size == 535331 ||
         size == 1066496 || size == 1070662)) {
        xx_cbm_d8x *reader = xx_cbm_d8x_create(device, 0);
        bool valid = false;
        if (reader) {
            if (size >= 1066496) {
                xx_cbm_d8x_destroy(reader);
                xx_cbm_d8x_init_ex(reader, device, 0, XX_CBM_D8X_8250);
            }
            valid = xx_cbm_d8x_check_is_valid(&reader->format, NULL);
        }
        xx_cbm_d8x_free(reader);
        if (valid) result = XX_FILE_TYPE_CBM_D8X;
    }
    if (result == XX_FILE_TYPE_UNKNOWN &&
        (size == 349696 || size == 351062) &&
        matches(device, prefix, prefix_size, size, 91394,
                (const unsigned char *)"A\x80", 2)) {
        xx_cbm_d71 *reader = xx_cbm_d71_create(device, 0);
        bool valid = reader && xx_cbm_d71_check_is_valid(&reader->format, NULL);
        xx_cbm_d71_free(reader);
        if (valid) result = XX_FILE_TYPE_CBM_D71;
    }
    if (result == XX_FILE_TYPE_UNKNOWN &&
        (size == 819200 || size == 822400) &&
        matches(device, prefix, prefix_size, size, 399360,
                (const unsigned char *)"\x28\x03\x44\0", 4)) {
        xx_cbm_d81 *reader = xx_cbm_d81_create(device, 0);
        bool valid = reader && xx_cbm_d81_check_is_valid(&reader->format, NULL);
        xx_cbm_d81_free(reader);
        if (valid) result = XX_FILE_TYPE_CBM_D81;
    }
    if (result == XX_FILE_TYPE_UNKNOWN &&
        (size == 174848 || size == 175531 || size == 196608 || size == 197376) &&
        matches(device, prefix, prefix_size, size, 91394,
                (const unsigned char *)"A\0", 2)) {
        xx_cbm_d64 *reader = xx_cbm_d64_create(device, 0);
        bool valid = reader && xx_cbm_d64_check_is_valid(&reader->format, NULL);
        if (!valid && reader && size >= 196608) {
            xx_cbm_d64_destroy(reader);
            xx_cbm_d64_init_ex(reader, device, 0, XX_CBM_D64_SPEED40);
            valid = xx_cbm_d64_check_is_valid(&reader->format, NULL);
        }
        if (!valid && reader && size >= 196608) {
            xx_cbm_d64_destroy(reader);
            xx_cbm_d64_init_ex(reader, device, 0, XX_CBM_D64_DOLPHIN40);
            valid = xx_cbm_d64_check_is_valid(&reader->format, NULL);
        }
        xx_cbm_d64_free(reader);
        if (valid) result = XX_FILE_TYPE_CBM_D64;
    }
    if (result == XX_FILE_TYPE_UNKNOWN && size >= 12 && size <= 16777232 &&
        prefix_size >= 4U) {
        uint32_t packed = ((uint32_t)prefix[0] << 24U) |
                          ((uint32_t)prefix[1] << 16U) |
                          ((uint32_t)prefix[2] << 8U) | prefix[3];
        bool candidate = (packed <= (uint32_t)size &&
                          (packed + 12U == (uint32_t)size ||
                           packed + 16U == (uint32_t)size)) ||
            !xx_rt_memcmp(prefix, "ACE!", 4U) ||
            !xx_rt_memcmp(prefix, "FVL0", 4U) ||
            !xx_rt_memcmp(prefix, "GR20", 4U) ||
            !xx_rt_memcmp(prefix, "MD10", 4U) ||
            !xx_rt_memcmp(prefix, "MD11", 4U) ||
            matches(device, prefix, prefix_size, size, -4,
                    (const unsigned char *)"JEK!", 4U);
        if (candidate) {
            xx_bytekiller *reader = xx_bytekiller_create(device, 0);
            bool valid = reader && xx_bytekiller_check_is_valid(&reader->format, NULL);
            xx_bytekiller_free(reader);
            if (valid) result = XX_FILE_TYPE_BYTEKILLER;
        }
    }
done:
    (void)xx_io_seek64(device, saved, SEEK_SET);
    return result;
}
