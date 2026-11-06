/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Derived from SpecAbstract/modules/nfd_binary.cpp and XBinary's text
 * classifiers. Source snapshot: F:/ownCloud/prepare/qt5/_mylibs (2026-11-06).
 */

#include "xx_nfd_engine_internal.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

typedef struct xx_nfd_prefix_input_s {
    xx_io_device *file;
    const unsigned char *bytes;
    const unsigned char *view;
    int64_t size;
    int64_t base;
    int64_t verified;
    size_t capacity;
    size_t valid;
    bool failed;
} xx_nfd_prefix_input;

static char *nfd_strdup(const char *text)
{
    size_t size;
    char *copy;
    if (!text) text = "";
    size = xx_rt_strlen(text) + 1;
    copy = (char *)xx_rt_malloc(size);
    if (copy) xx_rt_memcpy(copy, text, size);
    return copy;
}

static bool prefix_window(xx_nfd_prefix_input *input, int64_t offset)
{
    size_t count;
    uint64_t left;
    if (!input || offset < 0 || offset >= input->size) return false;
    left = (uint64_t)(input->size - offset);
    count = left < input->capacity ? (size_t)left : input->capacity;
    input->view = xx_io_buffer_view(input->file, offset, &count);
    if (!input->view || !count) {
        input->failed = true;
        return false;
    }
    input->base = offset;
    input->valid = count;
    if (offset <= input->verified && offset + (int64_t)count > input->verified) {
        input->verified = offset + (int64_t)count;
    }
    return true;
}

static unsigned char prefix_byte(xx_nfd_prefix_input *input, int64_t offset)
{
    if (input->bytes) return input->bytes[(size_t)offset];
    if (input->failed) return 0;
    if (!input->view || offset < input->base ||
        (uint64_t)(offset - input->base) >= input->valid) {
        if (!prefix_window(input, offset)) return 0;
    }
    return input->view[(size_t)(offset - input->base)];
}

static int plain_text_data(xx_nfd_prefix_input *input, int64_t size)
{
    int64_t i;
    int64_t control = 0;
    int64_t printable = 0;
    int64_t extended = 0;
    if (size >= 3 && prefix_byte(input, 0) == 0xef &&
        prefix_byte(input, 1) == 0xbb && prefix_byte(input, 2) == 0xbf) return 0;
    if (size >= 2 && ((prefix_byte(input, 0) == 0xff && prefix_byte(input, 1) == 0xfe) ||
                      (prefix_byte(input, 0) == 0xfe && prefix_byte(input, 1) == 0xff))) return 0;
    for (i = 0; i < size; ++i) {
        unsigned char byte = prefix_byte(input, i);
        if (!byte) return 0;
        if (byte < 0x09) ++control;
        else if (byte == 0x09 || byte == 0x0a || byte == 0x0d) ++printable;
        else if (byte >= 0x20 && byte <= 0x7e) ++printable;
        else if (byte >= 0x80) ++extended;
        else ++control;
    }
    if (size) {
        double total = (double)size;
        double printable_ratio = (double)(printable + extended) / total;
        double control_ratio = (double)control / total;
        double extended_ratio = (double)extended / total;
        return printable_ratio >= 0.85 && control_ratio <= 0.05 && extended_ratio <= 0.50;
    }
    return 0;
}

static int utf8_text_data(xx_nfd_prefix_input *input, int64_t size)
{
    int64_t i = 0;
    int64_t start = 0;
    int64_t valid = 0;
    int64_t multibyte = 0;
    int64_t printable = 0;
    bool bom = false;
    if (size >= 3 && prefix_byte(input, 0) == 0xef &&
        prefix_byte(input, 1) == 0xbb && prefix_byte(input, 2) == 0xbf) {
        bom = true;
        start = 3;
    }
    for (i = start; i < size;) {
        unsigned char byte = prefix_byte(input, i);
        if (!byte) return 0;
        if (byte < 0x80) {
            if (byte >= 0x20 || byte == 0x09 || byte == 0x0a || byte == 0x0d) ++printable;
            ++valid;
            ++i;
        } else if ((byte & 0xe0) == 0xc0) {
            if (i + 1 >= size || (prefix_byte(input, i + 1) & 0xc0) != 0x80 || byte < 0xc2) return 0;
            ++multibyte; ++valid; i += 2;
        } else if ((byte & 0xf0) == 0xe0) {
            if (i + 2 >= size || (prefix_byte(input, i + 1) & 0xc0) != 0x80 ||
                (prefix_byte(input, i + 2) & 0xc0) != 0x80 ||
                (byte == 0xe0 && prefix_byte(input, i + 1) < 0xa0)) return 0;
            ++multibyte; ++valid; i += 3;
        } else if ((byte & 0xf8) == 0xf0) {
            if (i + 3 >= size || (prefix_byte(input, i + 1) & 0xc0) != 0x80 ||
                (prefix_byte(input, i + 2) & 0xc0) != 0x80 ||
                (prefix_byte(input, i + 3) & 0xc0) != 0x80 ||
                (byte == 0xf0 && prefix_byte(input, i + 1) < 0x90) || byte > 0xf4 ||
                (byte == 0xf4 && prefix_byte(input, i + 1) > 0x8f)) return 0;
            ++multibyte; ++valid; i += 4;
        } else return 0;
    }
    if (bom) return valid > 0;
    if (valid) return ((double)multibyte / (double)valid) > 0.05 &&
                      ((double)printable / (double)valid) >= 0.70;
    return 0;
}

static int unicode_text_data(xx_nfd_prefix_input *input, int64_t size)
{
    int64_t i;
    int64_t sample;
    int64_t nulls = 0;
    int64_t printable = 0;
    int64_t even_nulls = 0;
    int64_t odd_nulls = 0;
    if (size >= 2) {
        uint16_t marker = (uint16_t)(prefix_byte(input, 0) |
                                     ((uint16_t)prefix_byte(input, 1) << 8));
        if (marker == 0xfffe) return 2;
        if (marker == 0xfeff) return 1;
    }
    if (size < 4) return 0;
    sample = size < 512 ? size : 512;
    for (i = 0; i < sample; ++i) {
        unsigned char byte = prefix_byte(input, i);
        if (!byte) {
            ++nulls;
            if (i & 1) ++odd_nulls; else ++even_nulls;
        } else if (byte >= 0x20 && byte <= 0x7e) ++printable;
    }
    if (nulls > 0 && sample > 4 && (double)nulls / (double)sample >= 0.30 &&
        (double)printable / (double)sample >= 0.30) {
        int64_t le_pairs = 0;
        int64_t be_pairs = 0;
        if (even_nulls > odd_nulls * 2) return 1;
        if (odd_nulls > even_nulls * 2) return 2;
        for (i = 0; i < sample - 1; i += 2) {
            if (!prefix_byte(input, i + 1) && prefix_byte(input, i) >= 0x20 &&
                prefix_byte(input, i) <= 0x7e) ++le_pairs;
            if (!prefix_byte(input, i) && prefix_byte(input, i + 1) >= 0x20 &&
                prefix_byte(input, i + 1) <= 0x7e) ++be_pairs;
        }
        if (le_pairs > be_pairs) return 1;
        if (be_pairs > le_pairs) return 2;
    }
    return 0;
}

static int classify_prefix(xx_io_device *device, int64_t maximum,
                           int (*classify)(xx_nfd_prefix_input *, int64_t))
{
    xx_nfd_prefix_input input;
    unsigned char *copy;
    int result;
    if (!device || xx_io_total_size(device) <= 0) return 0;
    xx_rt_memset(&input, 0, sizeof(input));
    input.file = device;
    input.size = xx_io_total_size(device) < maximum ? xx_io_total_size(device) : maximum;
    copy = (unsigned char *)xx_rt_malloc((size_t)input.size);
    if (!copy) return 0;
    if (!xx_io_read_at(device, 0, copy, (size_t)input.size)) {
        xx_rt_free(copy);
        return 0;
    }
    input.bytes = copy;
    input.verified = input.size;
    result = classify(&input, input.size);
    xx_rt_free(copy);
    return input.failed ? 0 : result;
}

static void pending_free(xx_nfd_pending_record *record)
{
    if (!record) return;
    xx_rt_free(record->name_token);
    xx_rt_free(record->type);
    xx_rt_free(record->name);
    xx_rt_free(record->version);
    xx_rt_free(record->info);
    xx_rt_memset(record, 0, sizeof(*record));
}

bool xx_nfd_pending_put(xx_nfd_binary_state *state, xx_nfd_result_group group,
                        const char *name_token, const char *type, const char *name,
                        const char *version, const char *info, bool heuristic,
                        bool aggressive, bool unknown, xx_pd_struct *pd)
{
    xx_nfd_pending_record record;
    xx_nfd_pending_record *items;
    size_t i;
    if (!state || !name_token) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid NFD record");
        return false;
    }
    for (i = 0; i < state->record_count; ++i) {
        if (state->records[i].group == group &&
            xx_rt_strcmp(state->records[i].name_token, name_token) == 0) {
            pending_free(&state->records[i]);
            break;
        }
    }
    xx_rt_memset(&record, 0, sizeof(record));
    record.group = group;
    record.name_order = xx_nfd_record_name_order(name_token);
    record.name_token = nfd_strdup(name_token);
    record.type = nfd_strdup(type ? type : "Unknown");
    record.name = nfd_strdup(name ? name : "Unknown");
    record.version = nfd_strdup(version);
    record.info = nfd_strdup(info);
    record.is_heuristic = heuristic;
    record.is_aggressive = aggressive;
    record.is_unknown = unknown;
    if (!record.name_token || !record.type || !record.name || !record.version || !record.info) {
        pending_free(&record);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD record");
        return false;
    }
    if (i < state->record_count) {
        state->records[i] = record;
        return true;
    }
    if (state->record_count == state->record_capacity) {
        size_t capacity = state->record_capacity ? state->record_capacity * 2 : 16;
        if (capacity < state->record_capacity || capacity > SIZE_MAX / sizeof(*items)) goto no_memory;
        items = (xx_nfd_pending_record *)xx_rt_realloc(state->records, capacity * sizeof(*items));
        if (!items) goto no_memory;
        state->records = items;
        state->record_capacity = capacity;
    }
    state->records[state->record_count++] = record;
    return true;
no_memory:
    pending_free(&record);
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot grow NFD records");
    return false;
}

static const xx_nfd_signature_row *header_get(const xx_nfd_binary_state *state,
                                               const char *name)
{
    size_t i;
    for (i = 0; i < state->header_count; ++i) {
        if (xx_rt_strcmp(state->headers[i]->basic.record_name, name) == 0) return state->headers[i];
    }
    return NULL;
}

static bool header_add(xx_nfd_binary_state *state, const xx_nfd_signature_row *row,
                       xx_pd_struct *pd)
{
    const xx_nfd_signature_row **items;
    size_t capacity;
    if (header_get(state, row->basic.record_name)) return true;
    if (state->header_count == state->header_capacity) {
        capacity = state->header_capacity ? state->header_capacity * 2 : 16;
        if (capacity < state->header_capacity || capacity > SIZE_MAX / sizeof(*items)) goto no_memory;
        items = (const xx_nfd_signature_row **)xx_rt_realloc(state->headers, capacity * sizeof(*items));
        if (!items) goto no_memory;
        state->headers = items;
        state->header_capacity = capacity;
    }
    state->headers[state->header_count++] = row;
    return true;
no_memory:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot grow NFD header matches");
    return false;
}

static bool scan_header_table(xx_nfd_binary_state *state, const char *table_name,
                              xx_pd_struct *pd)
{
    const xx_nfd_table *table = xx_nfd_find_table(table_name, XX_NFD_TABLE_SIGNATURE);
    const xx_nfd_signature_row *rows;
    size_t i;
    if (!table) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing NFD header table");
        return false;
    }
    rows = (const xx_nfd_signature_row *)table->rows;
    for (i = 0; i < table->row_count; ++i) {
        if (xx_pd_is_stopped(pd)) return false;
        if (!header_get(state, rows[i].basic.record_name) &&
            xx_nfd_signature_match(&rows[i], state->device, 0, NULL, NULL) &&
            !header_add(state, &rows[i], pd)) return false;
    }
    return true;
}

static bool put_basic(xx_nfd_binary_state *state, xx_nfd_result_group group,
                      const xx_nfd_basic_info *basic, const char *version,
                      const char *info, xx_pd_struct *pd)
{
    return xx_nfd_pending_put(state, group, basic->record_name,
                              xx_nfd_record_type_name(basic->record_type),
                              xx_nfd_record_name(basic->record_name),
                              version ? version : basic->version,
                              info ? info : basic->info, false, false, false, pd);
}

static bool put_header(xx_nfd_binary_state *state, xx_nfd_result_group group,
                       const char *name, const char *version, const char *info,
                       xx_pd_struct *pd)
{
    const xx_nfd_signature_row *row = header_get(state, name);
    return !row || put_basic(state, group, &row->basic, version, info, pd);
}

static bool handle_formats(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    const xx_nfd_signature_row *row = NULL;
    static const char *names[] = {
        "MICROSOFTCOMPILEDHTMLHELP", "AUTOIT", "RTF", "LUACOMPILED",
        "MICROSOFTWINHELP", "MP3", "MP4", "WINDOWSMEDIA", "FLASHVIDEO",
        "WAV", "AU", "DEB", "AVI", "WEBP", "TTF", "ANDROIDARSC",
        "ANDROIDXML", "AR"
    };
    size_t i;
    int64_t size = state->region->size;
    if (!size) {
        return xx_nfd_pending_put(state, XX_NFD_GROUP_FORMAT, "EMPTYFILE", "Format",
                                  "Empty file", "", "", false, false, false, pd);
    }
    row = header_get(state, "PDF");
    if (row && size >= 8) {
        char version[7];
        unsigned char bytes[3];
        if (xx_io_read_at(state->device, 5, bytes, sizeof(bytes))) {
            version[0] = (char)bytes[0]; version[1] = (char)bytes[1]; version[2] = (char)bytes[2]; version[3] = 0;
            return put_basic(state, XX_NFD_GROUP_FORMAT, &row->basic, version, NULL, pd);
        }
    }
    row = header_get(state, "MICROSOFTCOMPOUND");
    if (row && size >= 8) {
        uint16_t sub1 = xx_io_get_u16(state->device, 0x200, false);
        uint16_t sub2 = xx_io_get_u16(state->device, 0x1000, false);
        if (!sub1 && sub2 == 0xfffd) {
            return xx_nfd_pending_put(state, XX_NFD_GROUP_FORMAT, "MICROSOFTINSTALLER",
                                      "Installer", "Microsoft Installer", "", "",
                                      false, false, false, pd);
        }
        if (sub1 == 0xa5ec) {
            return xx_nfd_pending_put(state, XX_NFD_GROUP_FORMAT, "MICROSOFTOFFICEWORD",
                                      "Format", "Microsoft Office Word", "97-2003", "",
                                      false, false, false, pd);
        }
        return put_basic(state, XX_NFD_GROUP_FORMAT, &row->basic, NULL, NULL, pd);
    }
    row = header_get(state, "JAVACOMPILEDCLASS");
    if (row && size >= 8) return put_basic(state, XX_NFD_GROUP_FORMAT, &row->basic, NULL, NULL, pd);
    row = header_get(state, "DEX");
    if (row && size >= 8) {
        char version[4];
        version[0] = (char)xx_io_get_u8(state->device, 4);
        version[1] = (char)xx_io_get_u8(state->device, 5);
        version[2] = (char)xx_io_get_u8(state->device, 6);
        version[3] = 0;
        return put_basic(state, XX_NFD_GROUP_FORMAT, &row->basic, version, NULL, pd);
    }
    row = header_get(state, "SWF");
    if (row && size >= 8) {
        char version[12];
        int value = (int)xx_io_get_u8(state->device, 3);
        int count = 0;
        if (value >= 100) version[count++] = (char)('0' + value / 100);
        if (value >= 10) version[count++] = (char)('0' + (value / 10) % 10);
        version[count++] = (char)('0' + value % 10); version[count] = 0;
        return put_basic(state, XX_NFD_GROUP_FORMAT, &row->basic, version, NULL, pd);
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        row = header_get(state, names[i]);
        if (row && size >= 8) return put_basic(state, XX_NFD_GROUP_FORMAT, &row->basic, NULL, NULL, pd);
    }
    if (size >= 0x8010 && xx_io_get_u8(state->device, 0x8000) == 1 &&
        xx_io_get_u8(state->device, 0x8001) == 'C' && xx_io_get_u8(state->device, 0x8002) == 'D' &&
        xx_io_get_u8(state->device, 0x8003) == '0' && xx_io_get_u8(state->device, 0x8004) == '0' &&
        xx_io_get_u8(state->device, 0x8005) == '1' && xx_io_get_u8(state->device, 0x8006) == 1) {
        return xx_nfd_pending_put(state, XX_NFD_GROUP_FORMAT, "ISO9660", "Format", "ISO 9660",
                                  "", "", false, false, false, pd);
    }
    return true;
}

static bool handle_archives(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    static const struct { const char *name; int64_t minimum; } items[] = {
        {"GZIP", 9}, {"XAR", 9}, {"LZFSE", 9}, {"ZLIB", 32},
        {"XZ", 32}, {"BZIP2", 9}, {"7Z", 9}, {"ARJ", 9}, {"LHA", 9}, {"RAR", 7}
    };
    size_t i;
    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        const xx_nfd_signature_row *row = header_get(state, items[i].name);
        if (row && state->region->size >= items[i].minimum) {
            return put_basic(state, XX_NFD_GROUP_ARCHIVE, &row->basic, NULL, NULL, pd);
        }
    }
    return true;
}

static bool handle_images(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    const xx_nfd_signature_row *row;
    int64_t size = state->region->size;
    row = header_get(state, "JPEG");
    if (row && size >= 8) return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, NULL, pd);
    row = header_get(state, "GIF");
    if (row && size >= 8) return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, NULL, pd);
    row = header_get(state, "TIFF");
    if (row && size >= 8) return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, NULL, pd);
    row = header_get(state, "WINDOWSICON");
    if (row && size >= 20) return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, NULL, pd);
    row = header_get(state, "WINDOWSCURSOR");
    if (row && size >= 20) return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, NULL, pd);
    row = header_get(state, "PNG");
    if (row && size >= 24) {
        char info[48];
        uint32_t width = xx_io_get_u32(state->device, 16, true);
        uint32_t height = xx_io_get_u32(state->device, 20, true);
        char *p = info;
        char digits[11];
        int n;
        n = 0; do { digits[n++] = (char)('0' + width % 10); width /= 10; } while (width);
        while (n) *p++ = digits[--n]; *p++ = 'x';
        n = 0; do { digits[n++] = (char)('0' + height % 10); height /= 10; } while (height);
        while (n) *p++ = digits[--n]; *p = 0;
        return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, info, pd);
    }
    row = header_get(state, "DJVU");
    if (row && size >= 8) return put_basic(state, XX_NFD_GROUP_IMAGE, &row->basic, NULL, NULL, pd);
    return true;
}

static bool handle_simple_groups(xx_nfd_binary_state *state, xx_pd_struct *pd)
{
    static const struct { const char *name; xx_nfd_result_group group; int64_t minimum; } items[] = {
        {"WINAUTH", XX_NFD_GROUP_CERTIFICATE, 8},
        {"PDB", XX_NFD_GROUP_DATABASE, 32}, {"MICROSOFTLINKERDATABASE", XX_NFD_GROUP_DATABASE, 32},
        {"MINGW", XX_NFD_GROUP_DEBUG_DATA, 8}, {"PDBFILELINK", XX_NFD_GROUP_DEBUG_DATA, 8},
        {"BORLANDDEBUGINFO", XX_NFD_GROUP_DEBUG_DATA, 16},
        {"INNOSETUP", XX_NFD_GROUP_INSTALLER_DATA, 8}, {"INSTALLANYWHERE", XX_NFD_GROUP_INSTALLER_DATA, 8},
        {"GHOSTINSTALLER", XX_NFD_GROUP_INSTALLER_DATA, 8}, {"NSIS", XX_NFD_GROUP_INSTALLER_DATA, 8},
        {"SIXXPACK", XX_NFD_GROUP_INSTALLER_DATA, 8}, {"THINSTALL", XX_NFD_GROUP_INSTALLER_DATA, 8},
        {"CLICKTEAM", XX_NFD_GROUP_INSTALLER_DATA, 20}, {"QTINSTALLER", XX_NFD_GROUP_INSTALLER_DATA, 20},
        {"ADVANCEDINSTALLER", XX_NFD_GROUP_INSTALLER_DATA, 20}, {"OPERA", XX_NFD_GROUP_INSTALLER_DATA, 20},
        {"GPINSTALL", XX_NFD_GROUP_INSTALLER_DATA, 20}, {"AVASTANTIVIRUS", XX_NFD_GROUP_INSTALLER_DATA, 20},
        {"INSTALLSHIELD", XX_NFD_GROUP_INSTALLER_DATA, 8}, {"SETUPFACTORY", XX_NFD_GROUP_INSTALLER_DATA, 8},
        {"ACTUALINSTALLER", XX_NFD_GROUP_INSTALLER_DATA, 8}, {"INSTALL4J", XX_NFD_GROUP_INSTALLER_DATA, 8},
        {"VMWARE", XX_NFD_GROUP_INSTALLER_DATA, 8}, {"NOSINSTALLER", XX_NFD_GROUP_INSTALLER_DATA, 8},
        {"WINRAR", XX_NFD_GROUP_SFX_DATA, 20}, {"SQUEEZSFX", XX_NFD_GROUP_SFX_DATA, 20},
        {"FISHNET", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"XENOCODE", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"MOLEBOXULTRA", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"1337EXECRYPTER", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"ACTIVEMARK", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"AGAINNATIVITYCRYPTER", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"ARCRYPT", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"NOXCRYPT", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"FASTFILECRYPT", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"LIGHTNINGCRYPTERSCANTIME", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"ZELDACRYPT", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"WOUTHRSEXECRYPTER", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"WLCRYPT", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"DOTNETSHRINK", XX_NFD_GROUP_PROTECTOR_DATA, 8},
        {"SPOONSTUDIO", XX_NFD_GROUP_PROTECTOR_DATA, 8}, {"SERGREENAPPACKER", XX_NFD_GROUP_PROTECTOR_DATA, 30}
    };
    size_t i;
    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        if (state->region->size >= items[i].minimum &&
            !put_header(state, items[i].group, items[i].name, NULL, NULL, pd)) return false;
    }
    return true;
}

static int pending_compare(const xx_nfd_pending_record *left,
                           const xx_nfd_pending_record *right)
{
    if (left->group != right->group) return left->group < right->group ? -1 : 1;
    if (left->name_order != right->name_order) return left->name_order < right->name_order ? -1 : 1;
    return 0;
}

static void pending_sort(xx_nfd_pending_record *records, size_t count)
{
    size_t i;
    for (i = 1; i < count; ++i) {
        xx_nfd_pending_record value = records[i];
        size_t j = i;
        while (j && pending_compare(&value, &records[j - 1]) < 0) {
            records[j] = records[j - 1];
            --j;
        }
        records[j] = value;
    }
}

static bool finish_result(xx_nfd_binary_state *state, xx_scan_result *result,
                          xx_pd_struct *pd)
{
    size_t i;
    if (!state->record_count &&
        !xx_nfd_pending_put(state, XX_NFD_GROUP_FORMAT, "UNKNOWN", "Unknown", "Unknown",
                            "", "", false, false, true, pd)) return false;
    pending_sort(state->records, state->record_count);
    if (state->record_count > (size_t)INT_MAX ||
        state->record_count > SIZE_MAX / sizeof(*result->pRecords)) goto no_memory;
    result->pRecords = (xx_scan_record *)xx_rt_calloc(state->record_count, sizeof(*result->pRecords));
    if (!result->pRecords) goto no_memory;
    result->nCapacity = result->nCount = (int)state->record_count;
    for (i = 0; i < state->record_count; ++i) {
        xx_scan_record *output = &result->pRecords[i];
        xx_nfd_pending_record *input = &state->records[i];
        output->engine_id = XX_SCAN_ENGINE_NFD;
        output->file_type = state->region->file_type;
        output->offset = 0;
        output->size = state->region->size;
        output->type = input->type; input->type = NULL;
        output->name = input->name; input->name = NULL;
        output->version = input->version; input->version = NULL;
        output->info = input->info; input->info = NULL;
        output->priority = xx_scan_type_to_prio(output->type);
        output->is_heuristic = input->is_heuristic;
        output->is_aggressive_heuristic = input->is_aggressive;
        output->is_unknown = input->is_unknown;
        output->scan_id = state->region->scan_id;
        output->parent_id = state->region->parent_id;
        output->scan_part = state->region->part;
        output->archive_offset = state->options ? state->options->archive_offset : 0;
        output->archive_size = state->options ? state->options->archive_size : 0;
        output->archive_compressed = state->options && state->options->archive_compressed;
        output->archive_method = state->options ? state->options->archive_method : NULL;
        xx_rt_free(input->name_token); input->name_token = NULL;
    }
    return true;
no_memory:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD scan result records");
    return false;
}

static void state_cleanup(xx_nfd_binary_state *state)
{
    size_t i;
    if (!state) return;
    for (i = 0; i < state->record_count; ++i) pending_free(&state->records[i]);
    xx_rt_free(state->records);
    xx_rt_free(state->headers);
    xx_rt_free(state->header_text);
    state->records = NULL;
    state->headers = NULL;
    state->header_text = NULL;
}

bool xx_nfd_binary_scan(xx_nfd_binary_state *state, xx_scan_result *result,
                        xx_pd_struct *pd)
{
    bool success = false;
    if (!state || !state->device || !state->region || !result) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing NFD binary scan argument");
        return false;
    }
    if (!scan_header_table(state, "g_binary_records", pd) ||
        !scan_header_table(state, "g_archive_records", pd)) goto done;
    if (state->region->part == XX_SCAN_PART_OVERLAY &&
        !scan_header_table(state, "g_PE_overlay_records", pd)) goto done;
    state->is_plain_text = classify_prefix(state->device, 0x8000, plain_text_data) != 0;
    state->is_utf8 = classify_prefix(state->device, 0x2000, utf8_text_data) != 0;
    state->unicode_type = classify_prefix(state->device, 0x1000, unicode_text_data);
    if (state->unicode_type) {
        state->header_text = xx_str_read_unicode(state->device, 2,
            state->region->size < 0x1000 ? state->region->size : 0x1000,
            state->unicode_type == 2);
    } else if (state->is_utf8) {
        state->header_text = xx_str_read_utf8(state->device, 3,
            state->region->size < 0x1000 ? state->region->size : 0x1000);
    } else if (state->is_plain_text) {
        state->header_text = xx_str_read_ansi(state->device, 0,
            state->region->size < 0x1000 ? state->region->size : 0x1000);
    } else if (state->region->size >= 2 && xx_io_get_u8(state->device, 0) == '#' &&
               xx_io_get_u8(state->device, 1) == '!') {
        state->header_text = xx_str_read_ansi(state->device, 0,
            state->region->size < 0x4000 ? state->region->size : 0x4000);
    } else state->header_text = nfd_strdup("");
    if (!state->header_text) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate NFD text header");
        goto done;
    }
    if (!xx_nfd_text_scan(state, pd) || !handle_formats(state, pd) ||
        !handle_archives(state, pd) || !handle_images(state, pd) ||
        !handle_simple_groups(state, pd) || !finish_result(state, result, pd)) goto done;
    success = true;
done:
    state_cleanup(state);
    return success;
}
