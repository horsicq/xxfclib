/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xx_format_detect_internal.h"
#if XFU_FORMAT_SUPPORT_FULL
#include "xxfclib/formats/document/zxml/xx_zxml.h"
#include "xxfclib/formats/filesystem/zisofs/xx_zisofs.h"
#include "xxfclib/formats/other/nvp/xx_nvp.h"
#include "xxfclib/formats/archive/sqze/xx_sqze.h"
#include "xxfclib/formats/archive/zip_psc/xx_zip_psc.h"
#include "xxfclib/formats/installer/crx_native/xx_crx_native.h"
#include "xxfclib/formats/archive/dcs/xx_dcs.h"
#include "xxfclib/formats/image/mskn1/xx_mskn1.h"
#include "xxfclib/formats/image/mskn2/xx_mskn2.h"
#include "xxfclib/formats/image/mskn3/xx_mskn3.h"
#include "xxfclib/formats/archive/csq/xx_csq.h"
#include "xxfclib/formats/archive/zoot1/xx_zoot1.h"
#include "xxfclib/formats/archive/rdfz/xx_rdfz.h"
#include "xxfclib/formats/firmware/zbeos/xx_zbeos.h"
#include "xxfclib/formats/installer/solarispkg_zip/xx_solarispkg_zip.h"
#include "xxfclib/formats/game/gta_img/xx_gta_img.h"
#include "xxfclib/formats/game/pbo/xx_pbo.h"
#include "xxfclib/formats/game/pam_pak/xx_pam_pak.h"
#include "xxfclib/formats/game/pfpk/xx_pfpk.h"
#include "xxfclib/formats/game/birdies/xx_birdies.h"
#include "xxfclib/formats/game/xuiz/xx_xuiz.h"
#include "xxfclib/formats/game/titan_quest/xx_titan_quest.h"
#include "xxfclib/formats/game/sbpak/xx_sbpak.h"
#include "xxfclib/formats/game/cgjp/xx_cgjp.h"
#include "xxfclib/formats/game/pirs/xx_pirs.h"
#include "xxfclib/formats/image/px/xx_px.h"
#include "xxfclib/formats/executable/binsh_starkit/xx_binsh_starkit.h"
#include "xxfclib/formats/image/evd/xx_evd.h"
#include "xxfclib/formats/installer/desksoft/xx_desksoft.h"
#include "xxfclib/formats/installer/metaproducts/xx_metaproducts.h"
#include "xxfclib/formats/installer/visualware/xx_visualware.h"
#include "xxfclib/formats/installer/lyme_sfx/xx_lyme_sfx.h"
#include "xxfclib/formats/archive/audials/xx_audials.h"
#include "xxfclib/formats/filesystem/psa_disk/xx_psa_disk.h"
#include "xxfclib/formats/archive/webexe/xx_webexe.h"
#include "xxfclib/formats/archive/asd/xx_asd.h"
#include "xxfclib/formats/filesystem/cfd/xx_cfd.h"
#include "xxfclib/formats/archive/rdc/xx_rdc.h"
#include "xxfclib/formats/archive/fox_sqz/xx_fox_sqz.h"
#include "xxfclib/formats/image/skf/xx_skf.h"
#include "xxfclib/formats/archive/osl2000/xx_osl2000.h"
#include "xxfclib/formats/archive/lds/xx_lds.h"
#include "xxfclib/formats/filesystem/dcp_disk/xx_dcp_disk.h"
#include "xxfclib/formats/firmware/sony_image/xx_sony_image.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/formats/audio/legacy_sound_driver/xx_legacy_sound_driver.h"
#include "xxfclib/formats/audio/sm8/xx_sm8.h"
#include "xxfclib/formats/audio/mdh/xx_mdh.h"
#include "xxfclib/formats/audio/palladix_pma/xx_palladix_pma.h"
#include "xxfclib/formats/installer/apks/xx_apks.h"
#include "xxfclib/formats/executable/machofat/xx_machofat.h"
#include "xxfclib/formats/archive/rzip/xx_rzip.h"
#include "xxfclib/formats/archive/chz/xx_chz.h"
#include "xxfclib/formats/filesystem/ecm/xx_ecm.h"
#include "xxfclib/formats/game/he_tlkb/xx_he_tlkb.h"
#include "xxfclib/formats/archive/sar/xx_sar.h"
#include "xxfclib/formats/archive/is_stored/xx_is_stored.h"
#include "xxfclib/formats/archive/larc_pfx/xx_larc_pfx.h"
#include "xxfclib/formats/game/wii_wad/xx_wii_wad.h"
#include "xxfclib/formats/installer/binsh_sfx/xx_binsh_sfx.h"
#include <string.h>

static xx_file_type_t (*const signature_readers[])(xx_io_device *, int64_t) = {
    xx_legacy_sound_driver_detect,
    xx_sm8_detect,
    xx_mdh_detect,
    xx_palladix_pma_detect,
    xx_machofat_detect,
    xx_rzip_detect,
    xx_chz_detect,
    xx_ecm_detect,
    xx_he_tlkb_detect,
    xx_sar_detect,
    xx_is_stored_detect,
    xx_larc_pfx_detect,
    xx_wii_wad_detect,
    xx_binsh_sfx_detect,
    xx_apks_detect,
    xx_zxml_detect,
    xx_zisofs_detect,
    xx_nvp_detect,
    xx_sqze_detect,
    xx_crx_native_detect,
    xx_zip_psc_detect,
    xx_dcs_detect,
    xx_mskn1_detect,
    xx_mskn2_detect,
    xx_mskn3_detect,
    xx_csq_detect,
    xx_zoot1_detect,
    xx_rdfz_detect,
    xx_zbeos_detect,
    xx_solarispkg_zip_detect,
    xx_gta_img_detect,
    xx_pfpk_detect,
    xx_birdies_detect,
    xx_xuiz_detect,
    xx_pam_pak_detect,
    xx_titan_quest_detect,
    xx_cgjp_detect,
    xx_pirs_detect,
    xx_px_detect,
    xx_sbpak_detect,
    xx_pbo_detect,
    xx_audials_detect,
    xx_psa_disk_detect,
    xx_binsh_starkit_detect,
    xx_evd_detect,
    xx_lyme_sfx_detect,
    xx_desksoft_detect,
    xx_visualware_detect,
    xx_metaproducts_detect,
    xx_webexe_detect,
    xx_asd_detect,
    xx_skf_detect,
    xx_fox_sqz_detect,
    xx_lds_detect,
    xx_osl2000_detect,
    xx_cfd_detect,
    xx_rdc_detect,
    xx_dcp_disk_detect,
    xx_sony_image_detect,
};

xx_file_type_t xx_format_detect_signature_readers(xx_io_device *device, xx_pd_struct *pd)
{
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    int64_t saved;
    size_t i;
    if (!device || (pd && xx_pd_is_stopped(pd))) return type;
    saved = xx_io_tell(device);
    if (saved < 0) return type;
    for (i = 0; i < sizeof(signature_readers) / sizeof(signature_readers[0]); ++i) {
        if (pd && xx_pd_is_stopped(pd)) break;
        type = signature_readers[i](device, 0);
        if (type != XX_FILE_TYPE_UNKNOWN) break;
    }
    if (xx_io_seek64(device, saved, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#else
#include <string.h>

xx_file_type_t xx_format_detect_signature_readers(xx_io_device *device, xx_pd_struct *pd)
{
    xx_file_type_t type;
    if (!device || (pd && xx_pd_is_stopped(pd))) return XX_FILE_TYPE_UNKNOWN;
    type = xx_format_get_file_type_device(device);
    return type == XX_FILE_TYPE_BINARY ? XX_FILE_TYPE_UNKNOWN : type;
}
#endif

static const struct {
    const char *extension;
    xx_file_type_t type;
} fast_extensions[] = {
#if !XFU_FORMAT_SUPPORT_FULL
#ifdef XFU_SUPPORT_FORMAT_7ZIP
    {"7z", XX_FILE_TYPE_7ZIP},
#endif
#ifdef XFU_SUPPORT_FORMAT_AR
    {"a", XX_FILE_TYPE_AR},
    {"ar", XX_FILE_TYPE_AR},
#endif
#ifdef XFU_SUPPORT_FORMAT_BASE64
    {"b64", XX_FILE_TYPE_BASE64},
#endif
#ifdef XFU_SUPPORT_FORMAT_BZ2
    {"bz2", XX_FILE_TYPE_BZ2},
#endif
#ifdef XFU_SUPPORT_FORMAT_CAB
    {"cab", XX_FILE_TYPE_CAB},
#endif
#ifdef XFU_SUPPORT_FORMAT_CPIO
    {"cpio", XX_FILE_TYPE_CPIO},
#endif
#ifdef XFU_SUPPORT_FORMAT_GZ
    {"gz", XX_FILE_TYPE_GZ},
#endif
#ifdef XFU_SUPPORT_FORMAT_ISO9660
    {"iso", XX_FILE_TYPE_ISO9660},
#endif
#ifdef XFU_SUPPORT_FORMAT_AR
    {"lib", XX_FILE_TYPE_AR},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZIP
    {"lz", XX_FILE_TYPE_LZIP},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZ4
    {"lz4", XX_FILE_TYPE_LZ4},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZMA
    {"lzma", XX_FILE_TYPE_LZMA},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZOP
    {"lzo", XX_FILE_TYPE_LZOP},
#endif
#ifdef XFU_SUPPORT_FORMAT_RAR
    {"rar", XX_FILE_TYPE_RAR},
#endif
#ifdef XFU_SUPPORT_FORMAT_TAR
    {"tar", XX_FILE_TYPE_TAR},
#ifdef XFU_SUPPORT_FORMAT_BZ2
    {"tar.bz2", XX_FILE_TYPE_TAR_BZ2},
#endif
#ifdef XFU_SUPPORT_FORMAT_GZ
    {"tar.gz", XX_FILE_TYPE_TAR_GZ},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZIP
    {"tar.lz", XX_FILE_TYPE_TAR_LZIP},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZ4
    {"tar.lz4", XX_FILE_TYPE_TAR_LZ4},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZMA
    {"tar.lzma", XX_FILE_TYPE_TAR_LZMA},
#endif
#ifdef XFU_SUPPORT_FORMAT_LZOP
    {"tar.lzo", XX_FILE_TYPE_TAR_LZOP},
#endif
#ifdef XFU_SUPPORT_FORMAT_XZ
    {"tar.xz", XX_FILE_TYPE_TAR_XZ},
#endif
#ifdef XFU_SUPPORT_FORMAT_UNIX_COMPRESS
    {"tar.z", XX_FILE_TYPE_TAR_COMPRESS},
#endif
#ifdef XFU_SUPPORT_FORMAT_ZSTD
    {"tar.zst", XX_FILE_TYPE_TAR_ZSTD},
#endif
#endif
#ifdef XFU_SUPPORT_FORMAT_UUE
    {"uue", XX_FILE_TYPE_UUE},
#endif
#ifdef XFU_SUPPORT_FORMAT_XZ
    {"xz", XX_FILE_TYPE_XZ},
#endif
#ifdef XFU_SUPPORT_FORMAT_UNIX_COMPRESS
    {"z", XX_FILE_TYPE_UNIX_COMPRESS},
#endif
#ifdef XFU_SUPPORT_FORMAT_ZIP
    {"zip", XX_FILE_TYPE_ZIP},
    {"zip64", XX_FILE_TYPE_ZIP64},
#endif
#ifdef XFU_SUPPORT_FORMAT_ZSTD
    {"zst", XX_FILE_TYPE_ZSTD},
    {"zstd", XX_FILE_TYPE_ZSTD},
#endif
    {NULL, XX_FILE_TYPE_UNKNOWN}
#else
#include "xx_format_fast_extensions.inc"
#endif
};

#if !XFU_FORMAT_SUPPORT_FULL
static const size_t fast_extension_count = (sizeof(fast_extensions) / sizeof(fast_extensions[0])) - 1;
#else
static const size_t fast_extension_count = sizeof(fast_extensions) / sizeof(fast_extensions[0]);
#endif

xx_file_type_t xx_format_get_file_type_extension(const char *source_path)
{
    const char *name, *cursor;
    size_t name_length;
    if (!source_path || !fast_extension_count) return XX_FILE_TYPE_UNKNOWN;
    name = source_path;
    for (cursor = source_path; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    name_length = (size_t)(cursor - name);
    for (cursor = name; *cursor; ++cursor) {
        char suffix[32];
        size_t length, i, low = 0, high = fast_extension_count;
        if (*cursor != '.') continue;
        length = name_length - (size_t)(cursor - name) - 1;
        if (!length || length >= sizeof(suffix)) continue;
        for (i = 0; i < length; ++i) {
            unsigned char c = (unsigned char)cursor[i + 1];
            suffix[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        }
        suffix[length] = '\0';
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            int order = strcmp(suffix, fast_extensions[middle].extension);
            if (!order) return fast_extensions[middle].type;
            if (order < 0) high = middle;
            else low = middle + 1;
        }
    }
    return XX_FILE_TYPE_UNKNOWN;
}

xx_file_type_t xx_format_get_file_type_device_fast(xx_io_device *dev, const char *source_path)
{
    xx_file_type_t type;
    if (!dev) return XX_FILE_TYPE_UNKNOWN;
    type = xx_format_detect_signature_readers(dev, NULL);
    if (type != XX_FILE_TYPE_UNKNOWN) return type;
    if (!source_path) source_path = xx_io_source_path(dev);
    type = xx_format_get_file_type_extension(source_path);
    if (type != XX_FILE_TYPE_UNKNOWN) return type;
    return xx_format_get_file_type_device(dev);
}
