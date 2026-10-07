/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/formats/amigahunk/xx_amigahunk.h"
#include "xxfclib/formats/atarist/xx_atarist.h"
#include "xxfclib/formats/dex/xx_dex.h"
#include "xxfclib/formats/elf/xx_elf.h"
#include "xxfclib/formats/iso9660/xx_iso9660.h"
#include "xxfclib/formats/java_class/xx_java_class.h"
#include "xxfclib/formats/jpeg/xx_jpeg.h"
#include "xxfclib/formats/le/xx_le.h"
#include "xxfclib/formats/lx/xx_lx.h"
#include "xxfclib/formats/macho/xx_macho.h"
#include "xxfclib/formats/msdos/xx_msdos.h"
#include "xxfclib/formats/ne/xx_ne.h"
#include "xxfclib/formats/npm/xx_npm.h"
#include "xxfclib/formats/pe/xx_pe.h"
#include "xxfclib/formats/png/xx_png.h"
#include "xxfclib/formats/rar/xx_rar.h"
#include "xxfclib/formats/zip/xx_zip.h"

/* Read only the parser's overlay fields. Most format sizes are relative, but
 * ZIP stores its absolute archive end, so sizes cannot be used generically. */
#define XX_SCAN_READ_OVERLAY(name, member) do { \
    xx_##name reader; \
    Abstractformat *format; \
    xx_##name##_init(&reader, device, 0); \
    format = &reader.member; \
    valid = xx_format_handle_base_info(format, pd); \
    if (valid) { \
        *offset = format->overlay_offset; \
        *size = format->overlay_size; \
    } \
    xx_##name##_destroy(&reader); \
} while (0)

static bool xx_scan_read_overlay(xx_io_device *device, xx_file_type_t type,
                                  int64_t *offset, int64_t *size,
                                  xx_pd_struct *pd) {
    bool valid = false;
    switch (type) {
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
            XX_SCAN_READ_OVERLAY(pe, format); break;
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64:
            XX_SCAN_READ_OVERLAY(elf, format); break;
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64:
            XX_SCAN_READ_OVERLAY(macho, format); break;
        case XX_FILE_TYPE_MSDOS:
            XX_SCAN_READ_OVERLAY(msdos, format); break;
        case XX_FILE_TYPE_NE:
            XX_SCAN_READ_OVERLAY(ne, format); break;
        case XX_FILE_TYPE_LE:
            XX_SCAN_READ_OVERLAY(le, format); break;
        case XX_FILE_TYPE_LX:
            XX_SCAN_READ_OVERLAY(lx, format); break;
        case XX_FILE_TYPE_AMIGAHUNK:
            XX_SCAN_READ_OVERLAY(amigahunk, format); break;
        case XX_FILE_TYPE_ATARIST:
            XX_SCAN_READ_OVERLAY(atarist, format); break;
        case XX_FILE_TYPE_DEX:
            XX_SCAN_READ_OVERLAY(dex, format); break;
        case XX_FILE_TYPE_JAVA_CLASS:
            XX_SCAN_READ_OVERLAY(java_class, format); break;
        case XX_FILE_TYPE_JPEG:
            XX_SCAN_READ_OVERLAY(jpeg, format); break;
        case XX_FILE_TYPE_PNG:
            XX_SCAN_READ_OVERLAY(png, format); break;
        case XX_FILE_TYPE_ISO9660:
            XX_SCAN_READ_OVERLAY(iso9660, format); break;
        case XX_FILE_TYPE_RAR:
            XX_SCAN_READ_OVERLAY(rar, format); break;
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA:
            XX_SCAN_READ_OVERLAY(zip, format); break;
        case XX_FILE_TYPE_NPM:
            XX_SCAN_READ_OVERLAY(npm, tar_gz.format); break;
        default: break;
    }
    return valid;
}

#undef XX_SCAN_READ_OVERLAY

static bool xx_scan_has_overlay_reader(xx_file_type_t type) {
    switch (type) {
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64:
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64:
        case XX_FILE_TYPE_MSDOS:
        case XX_FILE_TYPE_NE:
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX:
        case XX_FILE_TYPE_AMIGAHUNK:
        case XX_FILE_TYPE_ATARIST:
        case XX_FILE_TYPE_DEX:
        case XX_FILE_TYPE_JAVA_CLASS:
        case XX_FILE_TYPE_JPEG:
        case XX_FILE_TYPE_PNG:
        case XX_FILE_TYPE_ISO9660:
        case XX_FILE_TYPE_RAR:
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA:
        case XX_FILE_TYPE_NPM:
            return true;
        default:
            return false;
    }
}

bool xx_scan_get_overlay(xx_scan_engine *engine, xx_io_device *device,
                          const xx_scan_options *options, int64_t *offset,
                          int64_t *size, xx_pd_struct *pd) {
    xx_scan_options defaults;
    xx_io_device *view;
    int64_t total_size;
    int64_t range_size;
    int64_t relative_offset = -1;
    int64_t overlay_size = 0;
    bool valid;
    (void)engine;

    if (offset) *offset = -1;
    if (size) *size = 0;
    if (!device || !offset || !size) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing scan overlay argument");
        return false;
    }
    if (xx_pd_is_stopped(pd)) return false;
    if (!options) {
        xx_scan_options_init(&defaults);
        options = &defaults;
    }
    if (options->offset < 0 || options->size < -1 ||
        (options->size >= 0 && options->size > INT64_MAX - options->offset)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan overlay range");
        return false;
    }
    total_size = xx_io_total_size(device);
    if (total_size >= 0 && (options->offset > total_size ||
        (options->size >= 0 && options->size > total_size - options->offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS,
                        "Scan overlay range exceeds input");
        return false;
    }
    if (!xx_scan_has_overlay_reader(options->file_type))
        return !xx_pd_is_stopped(pd) && (!pd || !pd->last_error);
    if (options->size == -1 && total_size < 0) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot determine scan overlay input size");
        return false;
    }
    range_size = options->size >= 0 ? options->size : total_size - options->offset;
    view = xx_io_sub_open_ro(device, options->offset, range_size);
    if (!view) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY,
                        "Cannot create scan overlay input view");
        return false;
    }
    valid = xx_scan_read_overlay(view, options->file_type, &relative_offset,
                                  &overlay_size, pd);
    xx_io_close(view);
    if (xx_pd_is_stopped(pd) || (pd && pd->last_error)) return false;

    /* A weak signature may have selected a type whose structural parser
     * rejects the input. There is then no reliable overlay boundary. */
    if (!valid || relative_offset < 0 || overlay_size <= 0) return true;
    if (relative_offset > range_size || overlay_size > range_size - relative_offset) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS,
                        "Invalid parsed scan overlay range");
        return false;
    }
    *offset = options->offset + relative_offset;
    *size = overlay_size;
    return true;
}
