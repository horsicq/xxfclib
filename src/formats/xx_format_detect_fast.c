/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/xx_format.h"
#include "xx_format_detect_internal.h"
#include "xxfclib/formats/zxml/xx_zxml.h"
#include "xxfclib/formats/zisofs/xx_zisofs.h"
#include "xxfclib/formats/nvp/xx_nvp.h"
#include "xxfclib/formats/sqze/xx_sqze.h"
#include "xxfclib/formats/zip_psc/xx_zip_psc.h"
#include "xxfclib/formats/crx_native/xx_crx_native.h"
#include "xxfclib/formats/dcs/xx_dcs.h"
#include "xxfclib/formats/mskn1/xx_mskn1.h"
#include "xxfclib/formats/mskn2/xx_mskn2.h"
#include "xxfclib/formats/mskn3/xx_mskn3.h"
#include "xxfclib/formats/csq/xx_csq.h"
#include "xxfclib/formats/zoot1/xx_zoot1.h"
#include "xxfclib/formats/rdfz/xx_rdfz.h"
#include "xxfclib/formats/zbeos/xx_zbeos.h"
#include "xxfclib/formats/solarispkg_zip/xx_solarispkg_zip.h"
#include "xxfclib/formats/gta_img/xx_gta_img.h"
#include "xxfclib/formats/pbo/xx_pbo.h"
#include "xxfclib/formats/pam_pak/xx_pam_pak.h"
#include "xxfclib/formats/pfpk/xx_pfpk.h"
#include "xxfclib/formats/birdies/xx_birdies.h"
#include "xxfclib/formats/xuiz/xx_xuiz.h"
#include "xxfclib/formats/titan_quest/xx_titan_quest.h"
#include "xxfclib/formats/sbpak/xx_sbpak.h"
#include "xxfclib/formats/cgjp/xx_cgjp.h"
#include "xxfclib/formats/pirs/xx_pirs.h"
#include "xxfclib/formats/px/xx_px.h"
#include "xxfclib/formats/binsh_starkit/xx_binsh_starkit.h"
#include "xxfclib/formats/evd/xx_evd.h"
#include "xxfclib/formats/desksoft/xx_desksoft.h"
#include "xxfclib/formats/metaproducts/xx_metaproducts.h"
#include "xxfclib/formats/visualware/xx_visualware.h"
#include "xxfclib/formats/lyme_sfx/xx_lyme_sfx.h"
#include "xxfclib/formats/audials/xx_audials.h"
#include "xxfclib/formats/psa_disk/xx_psa_disk.h"
#include "xxfclib/formats/webexe/xx_webexe.h"
#include "xxfclib/formats/asd/xx_asd.h"
#include "xxfclib/formats/cfd/xx_cfd.h"
#include "xxfclib/formats/rdc/xx_rdc.h"
#include "xxfclib/formats/fox_sqz/xx_fox_sqz.h"
#include "xxfclib/formats/skf/xx_skf.h"
#include "xxfclib/formats/osl2000/xx_osl2000.h"
#include "xxfclib/formats/lds/xx_lds.h"
#include "xxfclib/formats/dcp_disk/xx_dcp_disk.h"
#include "xxfclib/formats/sony_image/xx_sony_image.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/formats/legacy_sound_driver/xx_legacy_sound_driver.h"
#include "xxfclib/formats/sm8/xx_sm8.h"
#include "xxfclib/formats/mdh/xx_mdh.h"
#include "xxfclib/formats/palladix_pma/xx_palladix_pma.h"
#include "xxfclib/formats/apks/xx_apks.h"
#include "xxfclib/formats/machofat/xx_machofat.h"
#include "xxfclib/formats/rzip/xx_rzip.h"
#include "xxfclib/formats/chz/xx_chz.h"
#include "xxfclib/formats/ecm/xx_ecm.h"
#include "xxfclib/formats/he_tlkb/xx_he_tlkb.h"
#include "xxfclib/formats/sar/xx_sar.h"
#include "xxfclib/formats/is_stored/xx_is_stored.h"
#include "xxfclib/formats/larc_pfx/xx_larc_pfx.h"
#include "xxfclib/formats/wii_wad/xx_wii_wad.h"
#include "xxfclib/formats/binsh_sfx/xx_binsh_sfx.h"
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

static const struct {
    const char *extension;
    xx_file_type_t type;
} fast_extensions[] = {
#include "xx_format_fast_extensions.inc"
};

static const size_t fast_extension_count = sizeof(fast_extensions) / sizeof(fast_extensions[0]);

xx_file_type_t xx_format_get_file_type_extension(const char *source_path)
{
    const char *name, *cursor;
    size_t name_length;
    if (!source_path) return XX_FILE_TYPE_UNKNOWN;
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
