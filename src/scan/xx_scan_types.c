/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/atarist/xx_atarist.h"
#include "xxfclib/formats/dos16m/xx_dos16m.h"
#include "xxfclib/formats/apk/xx_apk.h"
#include "xxfclib/formats/ipa/xx_ipa.h"
#include "xxfclib/formats/jar/xx_jar.h"
#include "xxfclib/formats/npm/xx_npm.h"
#include "xxfclib/formats/pyc/xx_pyc.h"
#include "xxfclib/formats/dotnet/xx_dotnet.h"

#include <stdio.h>
#include <string.h>

typedef struct xx_scan_type_window {
    xx_io_device view;
    xx_io_device *source;
    int64_t base;
    int64_t length; /* -1 when the source size is unknown. */
    int64_t position;
} xx_scan_type_window;

static ssize_t xx_scan_window_read(xx_io_device *view, void *buffer, size_t size)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    ssize_t count;
    if (!window || (!buffer && size)) return -1;
    if (!size || (window->length >= 0 && window->position >= window->length)) return 0;
    if (size > (size_t)PTRDIFF_MAX) size = (size_t)PTRDIFF_MAX;
    if (window->length < 0) {
        int64_t remaining = INT64_MAX - window->base - window->position;
        if (remaining <= 0) return 0;
        if ((uint64_t)size > (uint64_t)remaining) size = (size_t)remaining;
    }
    if (window->length >= 0 && (uint64_t)size > (uint64_t)(window->length - window->position)) size = (size_t)(window->length - window->position);
    if (xx_io_seek64(window->source, window->base + window->position, SEEK_SET) != 0) return -1;
    count = xx_io_read(window->source, buffer, size);
    if (count > 0 && (uint64_t)count > (uint64_t)size) return -1;
    if (count > 0) window->position += count;
    return count;
}

static int xx_scan_window_seek64(xx_io_device *view, int64_t offset, int whence)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    int64_t origin;
    int64_t maximum;
    if (!window) return -1;
    maximum = window->length >= 0 ? window->length : INT64_MAX - window->base;
    if (whence == SEEK_SET) origin = 0;
    else if (whence == SEEK_CUR) origin = window->position;
    else if (whence == SEEK_END && window->length >= 0) origin = window->length;
    else return -1;
    if (offset < -origin || offset > maximum - origin) return -1;
    window->position = origin + offset;
    return 0;
}

static int xx_scan_window_seek(xx_io_device *view, long offset, int whence)
{
    return xx_scan_window_seek64(view, (int64_t)offset, whence);
}

static int64_t xx_scan_window_size(xx_io_device *view)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    return window ? window->length : -1;
}

static int64_t xx_scan_window_tell(xx_io_device *view)
{
    xx_scan_type_window *window = (xx_scan_type_window *)view->priv;
    return window ? window->position : -1;
}

static bool xx_scan_read_at(xx_io_device *device, int64_t offset, uint8_t *bytes, size_t size)
{
    size_t done = 0;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t count = xx_io_read(device, bytes + done, size - done);
        if (count <= 0) return false;
        done += (size_t)count;
    }
    return true;
}

static size_t xx_scan_read_head(xx_io_device *device, uint8_t head[64])
{
    size_t done = 0;
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return 0;
    while (done < 64U) {
        ssize_t count = xx_io_read(device, head + done, 64U - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    return done;
}

static uint32_t xx_scan_u32le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t xx_scan_u32(const uint8_t *bytes, bool big_endian)
{
    return big_endian ? ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3] : xx_scan_u32le(bytes);
}

static uint64_t xx_scan_u64(const uint8_t *bytes, bool big_endian)
{
    uint32_t first = xx_scan_u32(bytes, big_endian);
    uint32_t second = xx_scan_u32(bytes + 4, big_endian);
    return big_endian ? ((uint64_t)first << 32U) | second : ((uint64_t)second << 32U) | first;
}

static bool xx_scan_is_macho_magic(const uint8_t magic[4])
{
    return (!memcmp(magic, "\xFE\xED\xFA\xCE", 4) || !memcmp(magic, "\xCE\xFA\xED\xFE", 4) || !memcmp(magic, "\xFE\xED\xFA\xCF", 4) ||
            !memcmp(magic, "\xCF\xFA\xED\xFE", 4));
}

/* A valid fat table, including every slice, distinguishes CAFEBABE from a
 * Java class header. Both 32-bit and 64-bit fat tables are supported. */
static bool xx_scan_is_macho_fat(xx_io_device *device, const uint8_t head[64], size_t head_size, int64_t file_size, xx_pd_struct *pd)
{
    bool big_endian;
    bool is_64;
    uint32_t count;
    uint64_t table_end;
    uint32_t index;
    size_t record_size;
    if (head_size < 8 || file_size < 8) return false;
    if (!memcmp(head, "\xCA\xFE\xBA\xBE", 4)) {
        big_endian = true;
        is_64 = false;
    } else if (!memcmp(head, "\xBE\xBA\xFE\xCA", 4)) {
        big_endian = false;
        is_64 = false;
    } else if (!memcmp(head, "\xCA\xFE\xBA\xBF", 4)) {
        big_endian = true;
        is_64 = true;
    } else if (!memcmp(head, "\xBF\xBA\xFE\xCA", 4)) {
        big_endian = false;
        is_64 = true;
    } else {
        return false;
    }
    count = xx_scan_u32(head + 4, big_endian);
    record_size = is_64 ? 32U : 20U;
    if (!count || count > 4096U || (uint64_t)count > ((uint64_t)file_size - 8U) / record_size) return false;
    table_end = 8U + (uint64_t)count * record_size;
    for (index = 0; index < count; ++index) {
        uint8_t record[32];
        uint8_t slice_magic[4];
        uint64_t offset;
        uint64_t size;
        uint32_t align;
        uint64_t mask;
        if (pd && pd->is_stop) return false;
        if (!xx_scan_read_at(device, (int64_t)(8U + (uint64_t)index * record_size), record, record_size)) return false;
        offset = is_64 ? xx_scan_u64(record + 8, big_endian) : xx_scan_u32(record + 8, big_endian);
        size = is_64 ? xx_scan_u64(record + 16, big_endian) : xx_scan_u32(record + 12, big_endian);
        align = xx_scan_u32(record + (is_64 ? 24 : 16), big_endian);
        if (!xx_scan_u32(record, big_endian) || !size || align > 63U || offset < table_end || offset > (uint64_t)file_size || size > (uint64_t)file_size - offset ||
            size < 4U)
            return false;
        mask = align ? (UINT64_C(1) << align) - 1U : 0U;
        if ((offset & mask) != 0U || !xx_scan_read_at(device, (int64_t)offset, slice_magic, 4U) || !xx_scan_is_macho_magic(slice_magic)) return false;
    }
    return true;
}

static bool xx_scan_has_com_suffix(const char *file_name)
{
    size_t length;
    if (!file_name) return false;
    length = strlen(file_name);
    if (length < 4U || file_name[length - 4U] != '.') return false;
    return (file_name[length - 3U] == 'c' || file_name[length - 3U] == 'C') && (file_name[length - 2U] == 'o' || file_name[length - 2U] == 'O') &&
           (file_name[length - 1U] == 'm' || file_name[length - 1U] == 'M');
}

/* PDF scan detection accepts a header after a BOM or other leading bytes when the
 * complete "%PDF-" marker begins within the first 1024 bytes. Its byte-zero
 * fast path also accepts "%PDF" with any fifth byte. The scan window already
 * limits reads to options.offset/size. */
static int64_t xx_scan_pdf_header_offset(xx_io_device *device, int64_t file_size, xx_pd_struct *pd)
{
    uint8_t bytes[1029];
    size_t limit;
    size_t done = 0;
    size_t index;
    if (file_size >= 0 && file_size <= 4) return -1;
    limit = file_size >= 0 && file_size < (int64_t)sizeof(bytes) ? (size_t)file_size : sizeof(bytes);
    if (xx_io_seek64(device, 0, SEEK_SET) != 0) return -1;
    while (done < limit) {
        ssize_t count;
        if (pd && pd->is_stop) return -1;
        count = xx_io_read(device, bytes + done, limit - done);
        if (count <= 0) break;
        done += (size_t)count;
    }
    if (done < 5U) return -1;
    if (!memcmp(bytes, "%PDF", 4U)) return 0;
    for (index = 1U; index <= 1024U && index + 5U <= done; ++index) {
        if (!memcmp(bytes + index, "%PDF-", 5U)) return (int64_t)index;
    }
    return -1;
}

static xx_file_type_t xx_scan_detect_type(xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    uint8_t head[64] = {0};
    size_t head_size = xx_scan_read_head(device, head);
    int64_t size = xx_io_total_size(device);
    if (head_size >= 64U && ((!memcmp(head, "MZ", 2)) || (!memcmp(head, "ZM", 2)))) {
        uint32_t offset = xx_scan_u32le(head + 0x3c);
        uint8_t signature[26];
        if (offset >= 64U && offset <= 1024U * 1024U && (size < 0 || (uint64_t)offset + sizeof(signature) <= (uint64_t)size) &&
            xx_scan_read_at(device, offset, signature, sizeof(signature))) {
            if (!memcmp(signature, "PE\0\0", 4)) {
                uint16_t magic = (uint16_t)signature[24] | ((uint16_t)signature[25] << 8U);
                if (magic == 0x10bU || magic == 0x20bU) {
                    xx_dotnet managed;
                    bool valid_managed;
                    xx_dotnet_init(&managed, device, 0);
                    valid_managed = xx_dotnet_check_is_valid(&managed.pe.format, pd);
                    xx_dotnet_destroy(&managed);
                    if (valid_managed) return XX_FILE_TYPE_DOTNET;
                    return magic == 0x10bU ? XX_FILE_TYPE_PE32 : XX_FILE_TYPE_PE64;
                }
            }
        }
        {
            xx_dos4g dos4g;
            bool valid;
            xx_dos4g_init(&dos4g, device, 0);
            valid = xx_dos4g_check_is_valid(&dos4g.format, NULL);
            xx_dos4g_destroy(&dos4g);
            if (valid) return XX_FILE_TYPE_DOS4G;
        }
        {
            xx_dos16m dos16m;
            bool valid;
            xx_dos16m_init(&dos16m, device, 0);
            valid = xx_dos16m_check_is_valid(&dos16m.format, NULL);
            xx_dos16m_destroy(&dos16m);
            if (valid) return XX_FILE_TYPE_DOS16M;
        }
        if (offset >= 64U && offset <= 1024U * 1024U && (size < 0 || (uint64_t)offset + 2U <= (uint64_t)size) && xx_scan_read_at(device, offset, signature, 2U)) {
            if (!memcmp(signature, "NE", 2)) return XX_FILE_TYPE_NE;
            if (!memcmp(signature, "LE", 2)) return XX_FILE_TYPE_LE;
            if (!memcmp(signature, "LX", 2)) return XX_FILE_TYPE_LX;
        }
        return XX_FILE_TYPE_BINARY;
    }

    if (head_size >= 16U && !memcmp(head,
                                    "\x7f"
                                    "ELF",
                                    4)) {
        if (head[4] == 1U) return XX_FILE_TYPE_ELF32;
        if (head[4] == 2U) return XX_FILE_TYPE_ELF64;
    }
    if (xx_scan_is_macho_fat(device, head, head_size, size, pd)) return XX_FILE_TYPE_MACHOFAT;
    if (head_size >= 24U && !memcmp(head, "\xCA\xFE\xBA\xBE", 4) && (((uint32_t)head[6] << 8U) | head[7]) >= 45U) return XX_FILE_TYPE_JAVA_CLASS;
    if (head_size >= 28U && xx_scan_is_macho_magic(head)) {
        if (head[3] == 0xCEU || head[0] == 0xCEU) return XX_FILE_TYPE_MACHO32;
        if (head_size >= 32U) return XX_FILE_TYPE_MACHO64;
    }
    if (head_size >= 4U && (!memcmp(head, "PK\x03\x04", 4) || !memcmp(head, "PK\x05\x06", 4))) {
        xx_apk apk;
        xx_ipa ipa;
        xx_jar jar;
        bool valid;
        xx_apk_init(&apk, device, 0);
        valid = xx_apk_check_is_valid(&apk.zip.format, NULL);
        xx_apk_destroy(&apk);
        if (valid) return XX_FILE_TYPE_APK;
        xx_ipa_init(&ipa, device, 0);
        valid = xx_ipa_check_is_valid(&ipa.zip.format, NULL);
        xx_ipa_destroy(&ipa);
        if (valid) return XX_FILE_TYPE_IPA;
        xx_jar_init(&jar, device, 0);
        valid = xx_jar_check_is_valid(&jar.zip.format, NULL);
        xx_jar_destroy(&jar);
        if (valid) return XX_FILE_TYPE_JAR;
        return XX_FILE_TYPE_ZIP;
    }
    if (head_size >= 2U && head[0] == 0x1fU && head[1] == 0x8bU) {
        xx_npm npm;
        bool valid;
        xx_npm_init(&npm, device, 0);
        valid = xx_npm_check_is_valid(&npm.tar_gz.format, NULL);
        xx_npm_destroy(&npm);
        if (valid) return XX_FILE_TYPE_NPM;
    }
    if (head_size >= 8U && !memcmp(head, "dex\n", 4) && head[7] == 0U) return XX_FILE_TYPE_DEX;
    if (xx_scan_pdf_header_offset(device, size, pd) >= 0) return XX_FILE_TYPE_PDF;
    if (head_size >= 8U && !memcmp(head, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8)) return XX_FILE_TYPE_CFBF;
    if (head_size >= 8U && !memcmp(head, "\x89PNG\r\n\x1a\n", 8)) return XX_FILE_TYPE_PNG;
    if (head_size >= 3U && head[0] == 0xffU && head[1] == 0xd8U && head[2] == 0xffU) return XX_FILE_TYPE_JPEG;
    if (head_size >= 6U && !memcmp(head, "Rar!\x1a\x07", 6)) return XX_FILE_TYPE_RAR;
    if (head_size >= 4U && !memcmp(head, "\x00\x00\x03\xF3", 4)) return XX_FILE_TYPE_AMIGAHUNK;
    if (head_size >= 2U && head[0] == 0x60U && head[1] == 0x1aU) {
        xx_atarist atari;
        bool valid;
        xx_atarist_init(&atari, device, 0);
        valid = xx_atarist_check_is_valid(&atari.format, NULL);
        xx_atarist_destroy(&atari);
        if (valid) return XX_FILE_TYPE_ATARIST;
    }
    if ((size < 0 || size >= 0x8006) && xx_scan_read_at(device, 0x8001, head, 5U) && !memcmp(head, "CD001", 5U)) return XX_FILE_TYPE_ISO9660;
    if (head_size >= 4U && head[2] == 0x0dU && head[3] == 0x0aU && xx_pyc_is_known_magic((uint16_t)head[0] | ((uint16_t)head[1] << 8U))) return XX_FILE_TYPE_PYC;
    if (size > 0 && size <= 65280 && options && xx_scan_has_com_suffix(options->file_name)) return XX_FILE_TYPE_COM;
    return XX_FILE_TYPE_BINARY;
}

xx_list_t *xx_scan_get_file_types(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_type_window window = {0};
    xx_list_t *types;
    xx_file_type_t binary = XX_FILE_TYPE_BINARY;
    xx_file_type_t preferred;
    int64_t original_position;
    int64_t total_size;
    int64_t offset = options ? options->offset : 0;
    int64_t requested_size = options ? options->size : -1;
    (void)engine;
    if (!device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan device is NULL");
        return NULL;
    }
    if (pd && pd->is_stop) return NULL;
    if (offset < 0 || requested_size < -1 || (requested_size >= 0 && requested_size > INT64_MAX - offset)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan type range");
        return NULL;
    }
    total_size = xx_io_total_size(device);
    if (total_size >= 0 && (offset > total_size || (requested_size >= 0 && requested_size > total_size - offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan type range exceeds input");
        return NULL;
    }
    types = xx_list_create(sizeof(xx_file_type_t), NULL);
    if (!types) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan file-type list");
        return NULL;
    }
    window.source = device;
    window.base = offset;
    window.length = requested_size >= 0 ? requested_size : total_size >= 0 ? total_size - offset : -1;
    window.view.priv = &window;
    window.view.read = xx_scan_window_read;
    window.view.seek = xx_scan_window_seek;
    window.view.seek64 = xx_scan_window_seek64;
    window.view.tell = xx_scan_window_tell;
    window.view.total_size = xx_scan_window_size;
    original_position = xx_io_tell(device);
    preferred = xx_scan_detect_type(&window.view, options, pd);
    if (original_position >= 0) (void)xx_io_seek64(device, original_position, SEEK_SET);
    if (pd && pd->is_stop) {
        xx_list_destroy(types);
        return NULL;
    }
    if (!xx_list_append(types, &binary)) goto oom;
    if (preferred == XX_FILE_TYPE_DOTNET) {
        xx_file_type_t dos = XX_FILE_TYPE_MSDOS;
        xx_file_type_t pe_type;
        uint8_t pe_header[26];
        uint8_t dos_header[64];
        if (!xx_scan_read_at(&window.view, 0, dos_header, sizeof(dos_header)) ||
            !xx_scan_read_at(&window.view, xx_scan_u32le(dos_header + 0x3c), pe_header, sizeof(pe_header))) {
            xx_list_destroy(types);
            return NULL;
        }
        pe_type = pe_header[24] == 0x0b && pe_header[25] == 0x02 ? XX_FILE_TYPE_PE64 : XX_FILE_TYPE_PE32;
        if (original_position >= 0) (void)xx_io_seek64(device, original_position, SEEK_SET);
        if (!xx_list_append(types, &dos) || !xx_list_append(types, &pe_type)) goto oom;
    }
    if (preferred == XX_FILE_TYPE_APK || preferred == XX_FILE_TYPE_JAR || preferred == XX_FILE_TYPE_IPA) {
        xx_file_type_t zip = XX_FILE_TYPE_ZIP;
        if (!xx_list_append(types, &zip)) goto oom;
    }
    if (preferred != XX_FILE_TYPE_BINARY && !xx_list_append(types, &preferred)) goto oom;
    return types;
oom:
    xx_list_destroy(types);
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot append scan file type");
    return NULL;
}
