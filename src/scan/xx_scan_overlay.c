/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/formats/executable/amigahunk/xx_amigahunk.h"
#include "xxfclib/formats/executable/atarist/xx_atarist.h"
#include "xxfclib/formats/executable/dex/xx_dex.h"
#include "xxfclib/formats/executable/elf/xx_elf.h"
#include "xxfclib/formats/filesystem/iso9660/xx_iso9660.h"
#include "xxfclib/formats/executable/java_class/xx_java_class.h"
#include "xxfclib/formats/image/jpeg/xx_jpeg.h"
#include "xxfclib/formats/executable/le/xx_le.h"
#include "xxfclib/formats/executable/lx/xx_lx.h"
#include "xxfclib/formats/executable/macho/xx_macho.h"
#include "xxfclib/formats/executable/msdos/xx_msdos.h"
#include "xxfclib/formats/executable/ne/xx_ne.h"
#include "xxfclib/formats/installer/npm/xx_npm.h"
#include "xxfclib/formats/document/pdf/xxpdf.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"
#include "xxfclib/formats/image/png/xx_png.h"
#include "xxfclib/formats/archive/rar/xx_rar.h"
#include "xxfclib/formats/archive/zip/xx_zip.h"

/* Read only the parser's overlay fields. Most format sizes are relative, but
 * ZIP stores its absolute archive end, so sizes cannot be used generically. */
#define XX_SCAN_READ_OVERLAY(name, member)              \
    do {                                                \
        xx_##name reader;                               \
        Abstractformat *format;                         \
        xx_##name##_init(&reader, device, 0);           \
        format = &reader.member;                        \
        valid = xx_format_handle_base_info(format, pd); \
        if (valid) {                                    \
            *offset = format->overlay_offset;           \
            *size = format->overlay_size;               \
        }                                               \
        xx_##name##_destroy(&reader);                   \
    } while (0)

/* ---- XFormats::getFileParts(FILEPART_OVERLAY) rules -------------------
 * XScanEngine asks the input's class for its overlay part alone. Several
 * classes then differ from the native readers' overlay fields, and the
 * scan must follow the reference:
 *  - XELF and XMACH have no getFileParts: no overlay part at all.
 *  - XLE (LE, LX) and XAtariST add the header/segment ends only when those
 *    parts are requested too, so the overlay is the whole file.
 *  - XNE, XMSDOS and XZip use their own bounds, below. */

static uint32_t xx_scan_ov_u32(xx_io_device *device, int64_t total, int64_t offset)
{
    uint8_t b[4] = {0};
    if (offset < 0 || offset > total - 4 || !xx_io_read_at(device, offset, b, sizeof(b))) return 0U;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t xx_scan_ov_u16(xx_io_device *device, int64_t total, int64_t offset)
{
    uint8_t b[2] = {0};
    if (offset < 0 || offset > total - 2 || !xx_io_read_at(device, offset, b, sizeof(b))) return 0U;
    return (uint16_t)(b[0] | (b[1] << 8));
}

static bool xx_scan_ov_tail(int64_t covered, int64_t total, int64_t *offset, int64_t *size)
{
    if (covered < 0 || covered >= total) return true;
    *offset = covered;
    *size = total - covered;
    return true;
}

/* XNE::getFileParts: after the 64-byte OS/2 header and the segments. */
static bool xx_scan_overlay_ne(xx_io_device *device, int64_t total, int64_t *offset, int64_t *size)
{
    int64_t header = (int32_t)xx_scan_ov_u32(device, total, 0x3c);
    int64_t covered = 0;
    if (header < 0 || header >= total) header = -1; /* XNE::getImageOS2HeaderOffset */
    if (header >= 0) covered = header + 64 < total ? header + 64 : total;
    if (header >= 0) {
        int64_t table = header + xx_scan_ov_u16(device, total, header + 0x22);
        int64_t count = xx_scan_ov_u16(device, total, header + 0x1c);
        uint16_t shift = xx_scan_ov_u16(device, total, header + 0x32);
        int64_t i;
        if (table >= 0 && table < total) { /* XNE::getSegmentList */
            if (count > (total - table) / 8) count = (total - table) / 8;
            for (i = 0; i < count && shift <= 62; ++i) {
                int64_t file_offset = xx_scan_ov_u16(device, total, table + i * 8);
                int64_t file_size = xx_scan_ov_u16(device, total, table + i * 8 + 2);
                if (!file_size) file_size = 0x10000;
                if (total - 1 > 0 && file_offset > ((total - 1) >> shift)) continue;
                file_offset <<= shift;
                if (file_offset > total) continue;
                if (file_offset > total - file_size) file_size = total - file_offset;
                if (file_offset + file_size > covered) covered = file_offset + file_size;
            }
        }
    }
    return xx_scan_ov_tail(covered, total, offset, size);
}

/* XMSDOS::getFileParts: after the header and the e_cp/e_cblp load image. */
static bool xx_scan_overlay_msdos(xx_io_device *device, int64_t total, int64_t *offset, int64_t *size)
{
    uint16_t pages = xx_scan_ov_u16(device, total, 4);
    uint16_t last = xx_scan_ov_u16(device, total, 2);
    int64_t image = total;
    int64_t header;
    int64_t body;
    if (pages > 0) {
        image = (int64_t)pages * 0x200 - ((-(int64_t)last) & 0x1ff);
        if (image < 0 || image > total) image = total;
    }
    header = (int64_t)xx_scan_ov_u16(device, total, 8) * 16;
    if (header > image) header = image;
    if (header > total) header = total;
    body = (image < total ? image : total) - header;
    if (body < 0) body = 0;
    return xx_scan_ov_tail(header + body, total, offset, size);
}

/* XZip::findECDOffset: the last "PK\5\6" in the final 64 KiB + 22 bytes
 * whose comment fits, that is no ZIP64 or multi-disk record, and whose
 * central directory ends at it. */
static int64_t xx_scan_zip_find_ecd(xx_io_device *device, int64_t total)
{
    int64_t start;
    int64_t current;
    if (total < 22) return -1;
    start = total - (0xFFFF + 22);
    if (start < 0) start = 0;
    for (current = total - 22; current >= start; --current) {
        uint16_t comment, disk, start_disk, disk_records, records;
        uint32_t cd_size, cd_offset;
        if (xx_scan_ov_u32(device, total, current) != UINT32_C(0x06054b50)) continue;
        comment = xx_scan_ov_u16(device, total, current + 20);
        if ((int64_t)comment > total - current - 22) continue;
        disk = xx_scan_ov_u16(device, total, current + 4);
        start_disk = xx_scan_ov_u16(device, total, current + 6);
        disk_records = xx_scan_ov_u16(device, total, current + 8);
        records = xx_scan_ov_u16(device, total, current + 10);
        cd_size = xx_scan_ov_u32(device, total, current + 12);
        cd_offset = xx_scan_ov_u32(device, total, current + 16);
        if (disk || start_disk || disk_records != records || records == 0xFFFFU || cd_size == UINT32_MAX || cd_offset == UINT32_MAX) continue;
        if (!records) {
            if (!cd_size && (int64_t)cd_offset == current) return current;
            continue;
        }
        if ((int64_t)cd_offset > current || (int64_t)cd_size != current - (int64_t)cd_offset) continue;
        if (xx_scan_ov_u32(device, total, cd_offset) != UINT32_C(0x02014b50)) continue;
        return current;
    }
    return -1;
}

/* decodeZipUtf8Strict for a flagged UTF-8 name: well-formed UTF-8 only. */
static bool xx_scan_zip_utf8_ok(xx_io_device *device, int64_t offset, uint32_t length)
{
    uint8_t buffer[512];
    uint32_t done = 0U;
    uint32_t need = 0U;
    uint32_t code = 0U;
    uint32_t minimum = 0U;
    while (done < length) {
        uint32_t chunk = length - done < sizeof(buffer) ? length - done : (uint32_t)sizeof(buffer);
        uint32_t i;
        if (!xx_io_read_at(device, offset + done, buffer, chunk)) return false;
        for (i = 0U; i < chunk; ++i) {
            uint8_t c = buffer[i];
            if (need) {
                if ((c & 0xC0U) != 0x80U) return false;
                code = (code << 6) | (c & 0x3FU);
                if (--need == 0U && (code < minimum || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU))) return false;
            } else if (c < 0x80U) {
                continue;
            } else if ((c & 0xE0U) == 0xC0U) {
                need = 1U, code = c & 0x1FU, minimum = 0x80U;
            } else if ((c & 0xF0U) == 0xE0U) {
                need = 2U, code = c & 0x0FU, minimum = 0x800U;
            } else if ((c & 0xF8U) == 0xF0U) {
                need = 3U, code = c & 0x07U, minimum = 0x10000U;
            } else {
                return false;
            }
        }
        done += chunk;
    }
    return need == 0U;
}

/* XZip::getFileParts: after the end of central directory record and its
 * comment, or, without one, after the run of local file records that
 * XZip::_getNumberOfLocalFileHeaders accepts. */
static bool xx_scan_overlay_zip(xx_io_device *device, int64_t total, int64_t *offset, int64_t *size)
{
    int64_t ecd = xx_scan_zip_find_ecd(device, total);
    int64_t covered = 0;
    if (ecd != -1) {
        if (total >= 22 && ecd <= total - 22) {
            covered = ecd + 22 + (int64_t)xx_scan_ov_u16(device, total, ecd + 20);
            if (covered > total) covered = total;
        }
    } else {
        int64_t current = 0;
        while (total - current >= 30) {
            uint16_t flags = xx_scan_ov_u16(device, total, current + 6);
            uint16_t name = xx_scan_ov_u16(device, total, current + 26);
            uint16_t extra = xx_scan_ov_u16(device, total, current + 28);
            int64_t record = 30 + (int64_t)name + (int64_t)extra + (int64_t)xx_scan_ov_u32(device, total, current + 18);
            if (xx_scan_ov_u32(device, total, current) != UINT32_C(0x04034b50)) break;
            if (record > total - current) break;
            if (flags & 0x0008U) break;
            if ((flags & 0x0800U) && !xx_scan_zip_utf8_ok(device, current + 30, name)) break;
            current += record;
        }
        covered = current;
    }
    return xx_scan_ov_tail(covered, total, offset, size);
}

static bool xx_scan_read_overlay(xx_io_device *device, xx_file_type_t type, int64_t *offset, int64_t *size, xx_pd_struct *pd)
{
    bool valid = false;
    int64_t total = xx_io_total_size(device);
    switch (type) {
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET: XX_SCAN_READ_OVERLAY(pe, format); break;
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64:
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64: valid = true; break;
        case XX_FILE_TYPE_MSDOS: valid = total > 0 && xx_scan_overlay_msdos(device, total, offset, size); break;
        case XX_FILE_TYPE_NE: valid = total > 0 && xx_scan_overlay_ne(device, total, offset, size); break;
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX:
        case XX_FILE_TYPE_ATARIST: valid = total > 0 && xx_scan_ov_tail(0, total, offset, size); break;
        case XX_FILE_TYPE_AMIGAHUNK: XX_SCAN_READ_OVERLAY(amigahunk, format); break;
        case XX_FILE_TYPE_DEX: {
            /* XDEX::getFileParts: data_off + data_size, extended by the map
             * items. Without a readable map (XDEX::getMapItems is empty) that
             * is the whole rule, e.g. a header-only DEX is all overlay;
             * otherwise the native reader's map-based bound is used. */
            uint32_t map_off = xx_scan_ov_u32(device, total, 0x34);
            if (total > 0 && (map_off == 0U || (int64_t)map_off > total - 4)) {
                valid = xx_scan_ov_tail((int64_t)xx_scan_ov_u32(device, total, 0x6c) + (int64_t)xx_scan_ov_u32(device, total, 0x68), total, offset, size);
                break;
            }
            XX_SCAN_READ_OVERLAY(dex, format);
            break;
        }
        case XX_FILE_TYPE_JAVA_CLASS: XX_SCAN_READ_OVERLAY(java_class, format); break;
        case XX_FILE_TYPE_JPEG: XX_SCAN_READ_OVERLAY(jpeg, format); break;
        case XX_FILE_TYPE_PNG: XX_SCAN_READ_OVERLAY(png, format); break;
        case XX_FILE_TYPE_ISO9660: XX_SCAN_READ_OVERLAY(iso9660, format); break;
        case XX_FILE_TYPE_RAR: XX_SCAN_READ_OVERLAY(rar, format); break;
        case XX_FILE_TYPE_ZIP: valid = total > 0 && xx_scan_overlay_zip(device, total, offset, size); break;
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA: XX_SCAN_READ_OVERLAY(zip, format); break;
        case XX_FILE_TYPE_NPM: XX_SCAN_READ_OVERLAY(npm, tar_gz.format); break;
        case XX_FILE_TYPE_PDF: {
            /* The reference reports a PDF overlay from its own structure
             * scan, also for documents the archive reader rejects. */
            xx_pdf reader;
            xx_pdf_init(&reader, device, 0);
            valid = xx_pdf_get_overlay(&reader, offset, size, pd);
            xx_pdf_destroy(&reader);
        } break;
        default: break;
    }
    return valid;
}

#undef XX_SCAN_READ_OVERLAY

static bool xx_scan_has_overlay_reader(xx_file_type_t type)
{
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
        case XX_FILE_TYPE_PDF: return true;
        default: return false;
    }
}

bool xx_scan_get_overlay(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, int64_t *offset, int64_t *size, xx_pd_struct *pd)
{
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
    if (options->offset < 0 || options->size < -1 || (options->size >= 0 && options->size > INT64_MAX - options->offset)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan overlay range");
        return false;
    }
    total_size = xx_io_total_size(device);
    if (total_size >= 0 && (options->offset > total_size || (options->size >= 0 && options->size > total_size - options->offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan overlay range exceeds input");
        return false;
    }
    if (!xx_scan_has_overlay_reader(options->file_type)) return !xx_pd_is_stopped(pd) && (!pd || !pd->last_error);
    if (options->size == -1 && total_size < 0) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot determine scan overlay input size");
        return false;
    }
    range_size = options->size >= 0 ? options->size : total_size - options->offset;
    view = xx_io_sub_open_ro(device, options->offset, range_size);
    if (!view) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan overlay input view");
        return false;
    }
    valid = xx_scan_read_overlay(view, options->file_type, &relative_offset, &overlay_size, pd);
    xx_io_close(view);
    if (xx_pd_is_stopped(pd) || (pd && pd->last_error)) return false;

    /* A weak signature may have selected a type whose structural parser
     * rejects the input. There is then no reliable overlay boundary. */
    if (!valid || relative_offset < 0 || overlay_size <= 0) return true;
    if (relative_offset > range_size || overlay_size > range_size - relative_offset) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid parsed scan overlay range");
        return false;
    }
    *offset = options->offset + relative_offset;
    *size = overlay_size;
    return true;
}
