/* Copyright (c) 2026 hors <horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Frontend compatibility bridge to the format-owned native reader factories. */
#include "xxfc_readers.h"
#include "xxfclib/formats/archive/tar/xx_tar.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"
#include "xxfclib/formats/archive/uue/xx_uue.h"
#include "xxfclib/formats/archive/sfx_lha/xx_sfx_lha.h"
#include "xxfclib/formats/archive/bigaf/xx_bigaf.h"
#include "xxfclib/formats/archive/chz/xx_chz.h"
#include "xxfclib/formats/archive/sfx_chz/xx_sfx_chz.h"
#include "xxfclib/formats/archive/spis/xx_spis.h"
#include "xxfclib/formats/archive/swag/xx_swag.h"
#include "xxfclib/formats/archive/sfx_swag/xx_sfx_swag.h"
#include "xxfclib/formats/other/base64/xx_base64.h"
#include "xxfclib/formats/xx_legacy_archive.h"
#include <string.h>

typedef struct xxfc_reader_alias { const char *name; xx_file_type_t type; } xxfc_reader_alias;
static const xxfc_reader_alias aliases[] = {
    {"7z", XX_FILE_TYPE_7ZIP}, {"gzip", XX_FILE_TYPE_GZ},
    {"bzip2", XX_FILE_TYPE_BZ2}, {"compress", XX_FILE_TYPE_UNIX_COMPRESS},
    {"txt", XX_FILE_TYPE_TEXT},
    {"executable_overlay", XX_FILE_TYPE_EXECUTABLE_OVERLAY},
    {"scummvm_dxa", XX_FILE_TYPE_SCUMMVM_DXA},
    {"apple_diags", XX_FILE_TYPE_APPLE_DIAGS},
    {"eid", XX_FILE_TYPE_EID},
    {"installjammer", XX_FILE_TYPE_INSTALLJAMMER},
    {"crap02", XX_FILE_TYPE_INSTALLJAMMER},
    {"patchwise", XX_FILE_TYPE_PATCHWISE},
    {"zsb", XX_FILE_TYPE_PATCHWISE},
    {"secura", XX_FILE_TYPE_SECURA},
    {"sac", XX_FILE_TYPE_SECURA},
    {"self_unstuffer", XX_FILE_TYPE_SELF_UNSTUFFER},
    {"unstuffer", XX_FILE_TYPE_SELF_UNSTUFFER},
    {"starforce_ovl", XX_FILE_TYPE_STARFORCE_OVL},
    {"ovl", XX_FILE_TYPE_STARFORCE_OVL},
    {"sod_installer", XX_FILE_TYPE_SOD_INSTALLER},
    {"pe_rawfile", XX_FILE_TYPE_PE_RAWFILE},
    {"pklite", XX_FILE_TYPE_PKLITE},
    {"dfsee", XX_FILE_TYPE_DFSEE_IMZ},
    {"dfsee_imz", XX_FILE_TYPE_DFSEE_IMZ},
    {"dh", XX_FILE_TYPE_DH},
    {"djcomp", XX_FILE_TYPE_DJCOMP},
    {"fury_dat", XX_FILE_TYPE_FURY_DAT},
    {"fury", XX_FILE_TYPE_FURY_DAT},
    {"sod_package", XX_FILE_TYPE_SOD_INSTALLER},
    {"pftw_pkg", XX_FILE_TYPE_PFTW_PKG},
    {"pftw", XX_FILE_TYPE_PFTW_PKG},
    {"obl", XX_FILE_TYPE_INSTALLSHIELD_OBL},
    {"installshield_obl", XX_FILE_TYPE_INSTALLSHIELD_OBL},
    {"stor", XX_FILE_TYPE_STOR_RESOURCE},
    {"stor_resource", XX_FILE_TYPE_STOR_RESOURCE},
    {"natata", XX_FILE_TYPE_NATATA_EBOOK},
    {"natata_ebook", XX_FILE_TYPE_NATATA_EBOOK},
    {"Executable overlay", XX_FILE_TYPE_EXECUTABLE_OVERLAY},
    {"tar_lz", XX_FILE_TYPE_TAR_LZIP}, {"tar_zst", XX_FILE_TYPE_TAR_ZSTD},
#include "xxfc_reader_aliases.inc"
};

static bool same_name(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a || *b) {
        unsigned char x, y;
        while (*a && !((*a >= 'a' && *a <= 'z') || (*a >= 'A' && *a <= 'Z') || (*a >= '0' && *a <= '9'))) ++a;
        while (*b && !((*b >= 'a' && *b <= 'z') || (*b >= 'A' && *b <= 'Z') || (*b >= '0' && *b <= '9'))) ++b;
        x = (unsigned char)*a; y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
        if (!x) return true;
        ++a; ++b;
    }
    return true;
}

static void set_name(xxfc_opened *opened, const char *name)
{
    size_t n = 0;
    if (!name) name = "unknown";
    while (*name && n + 1 < sizeof(opened->name_storage)) {
        unsigned char c = (unsigned char)*name++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        opened->name_storage[n++] = (char)c;
    }
    opened->name_storage[n] = '\0';
    opened->reader_name = opened->name_storage;
}

static xx_file_type_t named_type(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        if (same_name(name, aliases[i].name)) return aliases[i].type;
    return XX_FILE_TYPE_UNKNOWN;
}

bool xxfc_open_type(xxfc_opened *opened, xx_io_device *device, int64_t base, xx_file_type_t type)
{
    Abstractformat *format;
    if (!opened || !device || base < 0) return false;
    format = xx_format_create_reader(type, device, base);
    if (!format) return false;
    if (type == XX_FILE_TYPE_ZIP || type == XX_FILE_TYPE_ZIP64)
        ((xx_zip *)format)->allow_leading_data = true;
    memset(opened, 0, sizeof(*opened));
    opened->format = format;
    opened->type = type;
    set_name(opened, xx_format_file_type_to_string(type));
    return true;
}

bool xxfc_open_named(xxfc_opened *opened, xx_io_device *device, int64_t base, const char *name)
{
    xx_file_type_t type = named_type(name);
    if (type == XX_FILE_TYPE_UNKNOWN || !xxfc_open_type(opened, device, base, type)) return false;
    set_name(opened, name);
    return true;
}

void xxfc_close(xxfc_opened *opened)
{
    if (!opened) return;
    if (opened->format) xx_format_free_reader(opened->format);
    memset(opened, 0, sizeof(*opened));
}

static bool validate(xxfc_opened *opened, const char *password, xx_pd_struct *pd)
{
    Abstractformat *f = opened->format;
    if (!f || (pd && xx_pd_is_stopped(pd))) return false;
    if (password && !xx_format_set_password(f, password)) return false;
    if (!xx_format_is_valid(f, pd) || !xx_format_handle_base_info(f, pd) ||
        !f->create_archive_records_reading || !f->get_current_archive_record ||
        !f->unpack_current_archive_record) return false;
    if (f->file_type != XX_FILE_TYPE_UNKNOWN) opened->type = f->file_type;
    return true;
}

bool xxfc_open_extension_fast(xxfc_opened *opened, xx_io_device *device, int64_t base,
                              const char *path, xx_pd_struct *pd)
{
    xx_file_type_t type = xx_format_get_file_type_extension(path);
    if (type == XX_FILE_TYPE_UNKNOWN || !xxfc_open_type(opened, device, base, type)) return false;
    if (validate(opened, NULL, pd)) return true;
    xxfc_close(opened);
    return false;
}

bool xxfc_open_extension(xxfc_opened *opened, xx_io_device *device, int64_t base,
                         const char *path, xx_pd_struct *pd)
{
    const char *suffix;
    size_t i;
    if (xxfc_open_extension_fast(opened, device, base, path, pd)) return true;
    if (!path || (suffix = strrchr(path, '.')) == NULL) return false;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!same_name(suffix + 1, aliases[i].name) ||
            !xxfc_open_type(opened, device, base, aliases[i].type)) continue;
        if (validate(opened, NULL, pd)) return true;
        xxfc_close(opened);
    }
    return false;
}

bool xxfc_open_fallback(xxfc_opened *opened, xx_io_device *device, int64_t base,
                        const char *path, const char *password, xx_file_type_t previous, xx_pd_struct *pd)
{
    xx_list_t *handles;
    size_t i;
    (void)path;
    if (base != 0 || (pd && xx_pd_is_stopped(pd))) return false;
    handles = xx_format_get_unpack_handle_types_device(device, true);
    if (!handles) return false;
    for (i = 0; i < xx_list_count(handles); ++i) {
        xx_handle_type_t handle = *(const xx_handle_type_t *)xx_list_at(handles, i);
        xx_file_type_t type = xx_format_handle_type_file_type(handle);
        if (type == previous || (pd && xx_pd_is_stopped(pd)) ||
            !xxfc_open_type(opened, device, base, type)) continue;
        if (validate(opened, password, pd)) { xx_list_destroy(handles); return true; }
        xxfc_close(opened);
    }
    xx_list_destroy(handles);
    return false;
}

Abstractformat *xxfc_make_writer(const char *name, xx_io_device *output, xxfc_release_fn *release)
{
    xx_file_type_t type = named_type(name);
    Abstractformat *format;
    if (release) *release = NULL;
    if (type == XX_FILE_TYPE_UNKNOWN || !output) return NULL;
    format = xx_format_create_reader(type, output, 0);
    if (!format) return NULL;
    if (!format->create_archive_records_writing || !format->pack_archive_record) {
        xx_format_free_reader(format); return NULL;
    }
    if (release) *release = xx_format_free_reader;
    return format;
}

void xxfc_attach_source_files(xxfc_opened *opened, const char *path)
{
    /* Native readers borrow their device, which retains the source path and
     * volume set. No helper executable or separate filename factory is used. */
    (void)opened; (void)path;
}

void xxfc_refresh_sevenzip_type(xxfc_opened *opened)
{
    if (opened && opened->format && opened->format->file_type != XX_FILE_TYPE_UNKNOWN)
        opened->type = opened->format->file_type;
}

bool xxfc_reader_uses_helper(const xxfc_opened *opened)
{
    const char *name = opened ? opened->reader_name : NULL;
    return name && (strstr(name, "engine") != NULL || strstr(name, "backend") != NULL);
}

bool xxfc_is_incomplete(const xxfc_opened *opened)
{
    xx_file_type_t type;
    if (!opened || !opened->format) return false;
    type = opened->format->file_type != XX_FILE_TYPE_UNKNOWN ? opened->format->file_type : opened->type;
    if (xx_tar_compressed_is_incomplete(opened->format)) return true;
    /* Destructors can be identical across native formats and folded by the
     * linker. Use the reader's actual enum identity before concrete casts. */
    if (type == XX_FILE_TYPE_ZIP || type == XX_FILE_TYPE_ZIP64)
        return ((const xx_zip *)opened->format)->directory_damaged;
    if (type == XX_FILE_TYPE_BASE64) {
        const xx_base64 *archive = (const xx_base64 *)opened->format;
        /* Bare RFC 4648 text has no envelope terminator to lose. The native
         * reader aggregates termination across its wrapped members. */
        return archive->is_wrapped && !archive->is_terminated;
    }
    if (type == XX_FILE_TYPE_UUE)
        return !((const xx_uue *)opened->format)->is_terminated;
    if (type == XX_FILE_TYPE_SFX_LHA)
        return ((const xx_sfx_lha *)opened->format)->incomplete_tail;
    if (type == XX_FILE_TYPE_BIGAF)
        return !((const xx_bigaf *)opened->format)->is_complete;
    if (type == XX_FILE_TYPE_CHZ)
        return !((const xx_chz *)opened->format)->is_complete;
    if (type == XX_FILE_TYPE_SFX_CHZ)
        return !((const xx_sfx_chz *)opened->format)->is_complete;
    if (type == XX_FILE_TYPE_SPIS)
        return !((const xx_spis *)opened->format)->is_complete;
    if (type == XX_FILE_TYPE_SWAG)
        return !((const xx_swag *)opened->format)->is_complete;
    if (type == XX_FILE_TYPE_SFX_SWAG)
        return !((const xx_sfx_swag *)opened->format)->is_complete;
    switch (type) {
#include "xxfc_legacy_incomplete.inc"
        return ((const xx_legacy_archive_info *)opened->format)->incomplete;
    default: return false;
    }
}
