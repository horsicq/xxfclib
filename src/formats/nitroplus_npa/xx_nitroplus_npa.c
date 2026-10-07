/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Independent native C Nitroplus NPA reader. Primary layout evidence (MIT, morkt):
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/NitroPlus/ArcNPA.cs
 * Caller supplies the final substitution table; no game-key database is included.
 * No upstream implementation is incorporated.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/nitroplus_npa/xx_nitroplus_npa.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

#ifdef NITROPLUS_NPA
#define NA_FILE_TYPE XX_FILE_TYPE_NITROPLUS_NPA
#else
#define NA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define NA_HEADER_SIZE 41U
#define NA_MAX_COUNT 0xFFFFFU
#define NA_MAX_INDEX (64U * 1024U * 1024U)
#define NA_MAX_NAMES (64U * 1024U * 1024U)
#define NA_MAX_NAME 4096U
#define NA_MAX_PACKED (256U * 1024U * 1024U)

typedef struct na_member {
    int64_t offset, header_offset;
    uint32_t size, unpacked_size, header_size, raw_name_size;
    uint8_t file_key;
    char *name;
    bool duplicate;
} na_member;
typedef struct na_layout {
    na_member *members;
    uint32_t count, directories, key1, key2;
    bool compressed, encrypted;
    uint32_t profile;
    uint8_t table[256];
    int64_t format_size;
    size_t index;
} na_layout;
typedef struct na_name_key {
    const char *name;
    uint32_t index;
} na_name_key;

static bool na_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static bool na_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || na_stopped(pd) || xx_io_seek64(device, at, XX_RT_SEEK_SET))
        return false;
    while (done < size) {
        ssize_t got;
        size_t take = size - done;
        if (na_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static void na_layout_free(void *ptr) {
    na_layout *layout = (na_layout *)ptr;
    uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i)
            if (layout->members[i].name) xx_mem_free(layout->members[i].name);
        xx_mem_free(layout->members);
    }
    xx_mem_zero(layout->table, sizeof(layout->table));
    xx_mem_free(layout);
}
static size_t na_escape(char *out, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '%'; out[1] = hex[c >> 4U]; out[2] = hex[c & 15U];
    return 3U;
}
static bool na_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}
static bool na_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}
static char *na_name(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i = 0U, at = 0U;
    if (!result) return NULL;
    while (i < size) {
        uint8_t c = raw[i++];
        if (na_lead(c) && i < size && na_trail(raw[i])) {
            at += na_escape(result + at, c);
            at += na_escape(result + at, raw[i++]);
        } else if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == '%') {
            at += na_escape(result + at, c);
        } else {
            result[at++] = c == '\\' ? '/' : (char)c;
        }
    }
    result[at] = 0;
    return result;
}
static int na_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}
static int na_compare_keys(const void *a, const void *b) {
    const na_name_key *x = (const na_name_key *)a;
    const na_name_key *y = (const na_name_key *)b;
    int order = na_fold_compare(x->name, y->name);
    if (order) return order;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void na_suffix(char *name, uint32_t index) {
    char suffix[14];
    size_t length = xx_str_len(name), component = 0U, dot = length, i;
    int amount = xx_rt_snprintf(suffix, sizeof(suffix), "%%_%u", index);
    for (i = 0U; i < length; ++i) if (name[i] == '/') component = i + 1U;
    for (i = length; i > component + 1U; --i)
        if (name[i - 1U] == '.') { dot = i - 1U; break; }
    if (amount <= 0 || (size_t)amount >= sizeof(suffix)) return;
    xx_rt_memmove(name + dot + (size_t)amount, name + dot, length - dot + 1U);
    xx_rt_memcpy(name + dot, suffix, (size_t)amount);
}


static int na_prefix_compare(const char *name, const char *prefix, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        unsigned char a = (unsigned char)name[i], b = (unsigned char)prefix[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 'a' - 'A');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + 'a' - 'A');
        if (a != b) return a < b ? -1 : 1;
        if (!a) return 0;
    }
    return name[length] ? 1 : 0;
}
/* A leaf named root cannot coexist with the implicit directory root/child.
 * Alias every such leaf before any output; raw '%' is escaped, so generated
 * aliases cannot collide with original archive names. */
static bool na_mark_prefixes(na_layout *layout, const na_name_key *keys, xx_pd_struct *pd) {
    uint32_t i;
    for (i = 0U; i < layout->count; ++i) {
        const char *name = layout->members[i].name;
        size_t n;
        for (n = 0U; name[n]; ++n) if (name[n] == '/' && n) {
            uint32_t first = 0U, end = layout->count;
            if (na_stopped(pd)) return false;
            while (first < end) {
                uint32_t middle = first + (end - first) / 2U;
                if (na_prefix_compare(keys[middle].name, name, n) < 0) first = middle + 1U;
                else end = middle;
            }
            while (first < layout->count && !na_prefix_compare(keys[first].name, name, n))
                layout->members[keys[first++].index].duplicate = true;
        }
    }
    return true;
}


typedef struct na_scheme {
    uint32_t profile, name_key;
    uint8_t table[256];
} na_scheme;
static bool na_permutation(const uint8_t *table) {
    uint8_t seen[256];
    unsigned i;
    if (!table) return false;
    xx_mem_zero(seen, sizeof(seen));
    for (i = 0U; i < 256U; ++i) {
        if (seen[table[i]]) return false;
        seen[table[i]] = 1U;
    }
    return true;
}
static int na_hex(unsigned c) {
    return c >= '0' && c <= '9' ? (int)(c - '0') :
        c >= 'a' && c <= 'f' ? (int)(c - 'a' + 10U) :
        c >= 'A' && c <= 'F' ? (int)(c - 'A' + 10U) : -1;
}
static bool na_resolve_scheme(xx_nitroplus_npa *archive, const xx_list_s *options,
                              na_scheme *scheme) {
    const xx_var *value = xx_format_resolve_extra_parameter(&archive->format, options, XX_META_ID_OPT_PASSWORD);
    size_t i, length = 0U;
    xx_mem_zero(scheme, sizeof(*scheme));
    if (!value) {
        if (!archive->has_scheme) return false;
        scheme->profile = (uint32_t)archive->profile; scheme->name_key = archive->name_key;
        xx_rt_memcpy(scheme->table, archive->decrypt_table, 256U);
    } else if (value->type == XX_VAR_TYPE_BYTES || value->type == XX_VAR_TYPE_BYTES_VIEW) {
        const uint8_t *bytes = (const uint8_t *)xx_var_get_bytes(value, &length);
        if (!bytes || length != 261U) return false;
        scheme->profile = bytes[0]; scheme->name_key = xx_data_get_u32(bytes + 1U, 4, 0, false);
        xx_rt_memcpy(scheme->table, bytes + 5U, 256U);
    } else if (value->type == XX_VAR_TYPE_STRING || value->type == XX_VAR_TYPE_STRING_VIEW) {
        const char *text = xx_var_get_str(value);
        if (!text || xx_str_len(text) != 523U || (text[0] != 's' && text[0] != 'l') ||
            text[1] != ':' || text[10] != ':') return false;
        scheme->profile = text[0] == 'l' ? 1U : 0U;
        for (i = 2U; i < 10U; ++i) {
            int hex = na_hex((unsigned char)text[i]);
            if (hex < 0) return false;
            scheme->name_key = (scheme->name_key << 4U) | (uint32_t)hex;
        }
        for (i = 0U; i < 256U; ++i) {
            int high = na_hex((unsigned char)text[11U + i * 2U]);
            int low = na_hex((unsigned char)text[12U + i * 2U]);
            if (high < 0 || low < 0) return false;
            scheme->table[i] = (uint8_t)((unsigned)high * 16U + (unsigned)low);
        }
    } else return false;
    return scheme->profile <= 1U && na_permutation(scheme->table);
}
static bool na_header(Abstractformat *format, uint8_t header[41], xx_pd_struct *pd) {
    int64_t available, total;
    uint32_t count, folders, files, index_size;
    if (!format || !format->device || format->base_address < 0 || na_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available < 41 || !na_read(format->device, format->base_address, header, 41U, pd) ||
        xx_rt_memcmp(header, "NPA\1\0\0\0", 7U) || header[15] > 1U || header[16] > 1U) return false;
    count = xx_data_get_u32(header + 17U, 4, 0, false); folders = xx_data_get_u32(header + 21U, 4, 0, false); files = xx_data_get_u32(header + 25U, 4, 0, false);
    index_size = xx_data_get_u32(header + 37U, 4, 0, false);
    return count <= NA_MAX_COUNT && folders <= count && files <= count &&
        (uint64_t)folders + files == count && index_size <= NA_MAX_INDEX &&
        (uint64_t)count * 22U <= index_size && (uint64_t)index_size <= (uint64_t)available - 41U;
}
static uint8_t na_name_mask(uint32_t position, uint32_t ordinal, uint32_t archive_key) {
    uint32_t key = 0xfcU * position;
    unsigned shift;
    for (shift = 0U; shift < 32U; shift += 8U)
        key -= (archive_key >> shift) + (ordinal >> shift);
    return (uint8_t)key;
}
static na_layout *na_parse_inner(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[41], *index = NULL, *folders = NULL;
    na_layout *layout = NULL;
    na_name_key *keys = NULL;
    na_scheme scheme;
    uint32_t records, index_size, i, directory_count = 0U, archive_key;
    size_t at = 0U, names = 0U;
    int64_t available;
    bool ok = false;
    xx_mem_zero(&scheme, sizeof(scheme));
    if (!na_header(format, header, pd)) return NULL;
    if (header[16] && !na_resolve_scheme((xx_nitroplus_npa *)format, options, &scheme)) return NULL;
    records = xx_data_get_u32(header + 17U, 4, 0, false); index_size = xx_data_get_u32(header + 37U, 4, 0, false);
    available = xx_io_total_size(format->device) - format->base_address;
    layout = (na_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) goto done;
    layout->count = xx_data_get_u32(header + 25U, 4, 0, false); layout->directories = xx_data_get_u32(header + 21U, 4, 0, false);
    layout->key1 = xx_data_get_u32(header + 7U, 4, 0, false); layout->key2 = xx_data_get_u32(header + 11U, 4, 0, false);
    layout->compressed = header[15] != 0U; layout->encrypted = header[16] != 0U;
    layout->profile = scheme.profile; xx_rt_memcpy(layout->table, scheme.table, 256U);
    layout->format_size = 41 + (int64_t)index_size;
    archive_key = layout->encrypted && scheme.profile == 1U ? layout->key1 + layout->key2 : layout->key1 * layout->key2;
    if ((uint64_t)layout->count * sizeof(*layout->members) > SIZE_MAX ||
        (uint64_t)layout->count * sizeof(*keys) > SIZE_MAX) goto done;
    index = index_size ? (uint8_t *)xx_mem_alloc(index_size) : NULL;
    folders = (uint8_t *)xx_mem_calloc((size_t)layout->directories + 1U, 1U);
    keys = layout->count ? (na_name_key *)xx_mem_alloc((size_t)layout->count * sizeof(*keys)) : NULL;
    layout->members = layout->count ? (na_member *)xx_mem_calloc(layout->count, sizeof(*layout->members)) : NULL;
    if ((index_size && !index) || !folders || (layout->count && (!keys || !layout->members)) ||
        (index_size && !na_read(format->device, format->base_address + 41, index, index_size, pd))) goto done;
    layout->count = 0U;
    for (i = 0U; i < records; ++i) {
        size_t record_at = at;
        uint32_t length, folder_id, relative, packed, plain, sum = 0U, j;
        uint8_t type;
        if (na_stopped(pd) || index_size - at < 4U) goto done;
        length = xx_data_get_u32(index + at, 4, 0, false); at += 4U;
        if (!length || length > NA_MAX_NAME || (uint64_t)length + 17U > index_size - at) goto done;
        for (j = 0U; j < length; ++j) { index[at + j] += na_name_mask(j, i, archive_key); sum += index[at + j]; }
        type = index[at + length]; folder_id = xx_data_get_u32(index + at + length + 1U, 4, 0, false);
        relative = xx_data_get_u32(index + at + length + 5U, 4, 0, false); packed = xx_data_get_u32(index + at + length + 9U, 4, 0, false);
        plain = xx_data_get_u32(index + at + length + 13U, 4, 0, false);
        if (folder_id > layout->directories || (type != 1U && type != 2U)) goto done;
        if (type == 1U) {
            if (!folder_id || folders[folder_id] || packed || plain || relative) goto done;
            folders[folder_id] = 1U; ++directory_count;
        } else {
            na_member *member;
            uint64_t offset = 41U + (uint64_t)index_size + relative;
            if (layout->count >= xx_data_get_u32(header + 25U, 4, 0, false) || offset > (uint64_t)available ||
                packed > (uint64_t)available - offset || (!layout->compressed && packed != plain) ||
                names > NA_MAX_NAMES - (length * 3U + 14U)) goto done;
            member = &layout->members[layout->count];
            member->name = na_name(index + at, length);
            if (!member->name) goto done;
            ++layout->count; names += length * 3U + 14U;
            member->offset = format->base_address + (int64_t)offset;
            member->header_offset = format->base_address + 41 + (int64_t)record_at;
            member->header_size = length + 21U; member->raw_name_size = length;
            member->size = packed; member->unpacked_size = plain;
            {
                uint32_t key = (scheme.name_key - sum) * length;
                if (scheme.profile != 1U) key = (key + archive_key) * plain;
                member->file_key = (uint8_t)key;
            }
            if ((int64_t)offset + packed > layout->format_size) layout->format_size = (int64_t)offset + packed;
            keys[layout->count - 1U].name = member->name; keys[layout->count - 1U].index = layout->count - 1U;
        }
        at += length + 17U;
    }
    if (at != index_size || layout->count != xx_data_get_u32(header + 25U, 4, 0, false) || directory_count != layout->directories) goto done;
    if (layout->count) xx_rt_qsort(keys, layout->count, sizeof(*keys), na_compare_keys);
    for (i = 1U; i < layout->count; ++i)
        if (!na_fold_compare(keys[i - 1U].name, keys[i].name)) layout->members[keys[i].index].duplicate = true;
    if (!na_mark_prefixes(layout, keys, pd)) goto done;
    for (i = 0U; i < layout->count; ++i) {
        if (na_stopped(pd)) goto done;
        if (layout->members[i].duplicate) na_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    if (index) { xx_mem_zero(index, index_size); xx_mem_free(index); }
    if (folders) xx_mem_free(folders);
    if (keys) xx_mem_free(keys);
    xx_mem_zero(&scheme, sizeof(scheme));
    if (!ok) { na_layout_free(layout); layout = NULL; }
    return layout;
}
static na_layout *na_parse(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    na_layout *layout = na_parse_inner(format, options, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) { na_layout_free(layout); layout = NULL; }
    return layout;
}
static void na_destroy_format(Abstractformat *format) {
    xx_nitroplus_npa_destroy((xx_nitroplus_npa *)format);
}
void xx_nitroplus_npa_init(xx_nitroplus_npa *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = NA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "npa");
    xx_format_set_mime_type(&archive->format, "application/x-nitroplus-npa");
    archive->format.destroy = na_destroy_format;
    archive->format.check_is_valid = xx_nitroplus_npa_check_is_valid;
    archive->format.handle_base_info = xx_nitroplus_npa_handle_base_info;
    archive->format.get_format_size = xx_nitroplus_npa_get_format_size;
    archive->format.get_number_of_archive_records = xx_nitroplus_npa_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_nitroplus_npa_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_nitroplus_npa_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_nitroplus_npa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_nitroplus_npa_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_nitroplus_npa_free_archive_records_reading;
}
xx_nitroplus_npa *xx_nitroplus_npa_create(xx_io_device *device, int64_t base) {
    xx_nitroplus_npa *archive = (xx_nitroplus_npa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_nitroplus_npa_init(archive, device, base);
    return archive;
}
void xx_nitroplus_npa_destroy(xx_nitroplus_npa *archive) {
    if (archive) { xx_nitroplus_npa_clear_scheme(archive); xx_format_cleanup_extra_parameters(&archive->format); }
}
void xx_nitroplus_npa_free(xx_nitroplus_npa *archive) {
    if (!archive) return;
    xx_nitroplus_npa_destroy(archive); xx_mem_free(archive);
}

bool xx_nitroplus_npa_set_scheme(xx_nitroplus_npa *archive, xx_nitroplus_npa_profile profile,
                                uint32_t name_key, const uint8_t decrypt_table[256]) {
    uint8_t copy[256];
    if (!archive || (profile != XX_NITROPLUS_NPA_STANDARD && profile != XX_NITROPLUS_NPA_LAMENTO) ||
        !na_permutation(decrypt_table)) return false;
    xx_rt_memcpy(copy, decrypt_table, 256U);
    xx_rt_memcpy(archive->decrypt_table, copy, 256U); xx_mem_zero(copy, sizeof(copy));
    archive->profile = profile; archive->name_key = name_key; archive->has_scheme = true;
    archive->format.base_info_handled = false; archive->format.is_valid = false;
    return true;
}
void xx_nitroplus_npa_clear_scheme(xx_nitroplus_npa *archive) {
    if (!archive) return;
    xx_mem_zero(archive->decrypt_table, sizeof(archive->decrypt_table));
    archive->name_key = 0U; archive->profile = XX_NITROPLUS_NPA_STANDARD; archive->has_scheme = false;
    archive->format.base_info_handled = false; archive->format.is_valid = false;
}
bool xx_nitroplus_npa_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t header[41];
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    bool valid = na_header(format, header, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) valid = false;
    if (valid && (!header[16] || ((xx_nitroplus_npa *)format)->has_scheme ||
        xx_format_find_extra_parameter(format, XX_META_ID_OPT_PASSWORD))) {
        na_layout *layout = na_parse(format, NULL, pd); valid = layout != NULL; na_layout_free(layout);
    }
    return valid;
}
bool xx_nitroplus_npa_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    na_layout *layout;
    xx_nitroplus_npa *archive;
    if (!format || na_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = na_parse(format, NULL, pd);
    if (!layout) return false;
    archive = (xx_nitroplus_npa *)format;
    archive->number_of_records = layout->count; archive->number_of_directories = layout->directories;
    archive->key1 = layout->key1; archive->key2 = layout->key2;
    archive->compressed = layout->compressed; archive->encrypted = layout->encrypted;
    format->number_of_archive_records = layout->count; format->format_size = layout->format_size;
    format->is_valid = true; format->base_info_handled = true;
    na_layout_free(layout); return true;
}
static bool na_payload_mode(Abstractformat *format, const xx_archive_record_state *state,
                             const na_member *member, bool *zlib) {
    const na_layout *layout = (const na_layout *)state->internal_state;
    const xx_var *value = xx_format_resolve_extra_parameter(format, &state->options, XX_NITROPLUS_NPA_OPT_PAYLOAD_MODE);
    uint64_t mode = value ? xx_var_get_u64(value) : 0U;
    if (mode > 2U) return false;
    *zlib = mode == 2U || (!mode && layout->compressed && member->size != member->unpacked_size);
    return *zlib || member->size == member->unpacked_size;
}
int64_t xx_nitroplus_npa_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_nitroplus_npa_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_nitroplus_npa_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_nitroplus_npa_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool na_set_record(Abstractformat *format, xx_archive_record_state *state) {
    na_layout *layout = (na_layout *)state->internal_state;
    const na_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    bool zlib;
    if (!na_payload_mode(format, state, member, &zlib)) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, zlib ? 8U : 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, layout->encrypted);
}
xx_archive_record_state *xx_nitroplus_npa_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    na_layout *layout = na_parse(format, options, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { na_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout;
    state->free_internal = na_layout_free;
    state->total_records = layout->count;
    state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (na_stopped(pd) || !meta) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = layout->count ? na_set_record(format, state) : false;
    if (!layout->count || state->has_record) return state;
fail:
    xx_archive_record_state_free(state);
    return NULL;
}
const xx_archive_record *xx_nitroplus_npa_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
bool xx_nitroplus_npa_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    na_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (na_layout *)state->internal_state)) return false;
    if (na_stopped(pd) || layout->index + 1U >= layout->count) {
        state->has_record = false; return false;
    }
    ++layout->index;
    state->current_index = (int64_t)layout->index;
    state->has_record = na_set_record(format, state);
    return state->has_record;
}
static const xx_var *na_option(Abstractformat *format, const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}

typedef struct na_sink {
    xx_io_device device;
    xx_io_device *destination;
    xx_pd_struct *pd;
    uint8_t *buffer;
    size_t capacity, used;
    uint64_t expected, written;
    uint32_t adler;
} na_sink;
static bool na_sink_flush(na_sink *sink) {
    size_t done = 0U;
    if (na_stopped(sink->pd)) return false;
    while (sink->destination && done < sink->used) {
        ssize_t amount;
        if (na_stopped(sink->pd)) return false;
        amount = xx_io_write(sink->destination, sink->buffer + done, sink->used - done);
        if (amount <= 0 || (size_t)amount > sink->used - done) return false;
        done += (size_t)amount;
    }
    sink->used = 0U; return !na_stopped(sink->pd);
}
static ssize_t na_sink_write(xx_io_device *device, const void *bytes, size_t size) {
    na_sink *sink = (na_sink *)device->priv;
    size_t done = 0U;
    if (na_stopped(sink->pd) || size > sink->expected - sink->written) return -1;
    sink->adler = xx_adler32_update(sink->adler, bytes, size);
    while (done < size) {
        size_t take = size - done;
        if (na_stopped(sink->pd)) return -1;
        if (take > sink->capacity - sink->used) take = sink->capacity - sink->used;
        if (sink->buffer + sink->used != (const uint8_t *)bytes + done)
            xx_rt_memcpy(sink->buffer + sink->used, (const uint8_t *)bytes + done, take);
        sink->used += take; done += take;
        if (sink->used == sink->capacity && !na_sink_flush(sink)) return -1;
    }
    sink->written += size; return (ssize_t)size;
}

static void na_decrypt(na_layout *layout, const na_member *member, uint8_t *bytes,
                        uint32_t offset, size_t size) {
    uint32_t encrypted_length = 4096U + (layout->profile == 1U ? 0U : member->raw_name_size);
    size_t i;
    if (!layout->encrypted || offset >= encrypted_length) return;
    if (size > encrypted_length - offset) size = encrypted_length - offset;
    for (i = 0U; i < size; ++i) {
        uint32_t delta = member->file_key + (layout->profile == 1U ? 0U : offset + (uint32_t)i);
        bytes[i] = (uint8_t)(layout->table[bytes[i]] - delta);
    }
}
bool xx_nitroplus_npa_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    na_layout *layout;
    const na_member *member;
    const xx_var *limit;
    uint8_t *packed = NULL, *buffer = NULL;
    size_t capacity = xx_get_file_buffer_size();
    int64_t cursor, total;
    na_sink sink;
    bool ok = false, zlib;
    if (!format || !format->device || !state || state->format != format || !state->has_record ||
        !(layout = (na_layout *)state->internal_state) || layout->index >= layout->count ||
        destination == format->device || na_stopped(pd)) return false;
    member = &layout->members[layout->index];
    if (!na_payload_mode(format, state, member, &zlib)) return false;
    total = xx_io_total_size(format->device);
    if (member->offset < 0 || member->offset > total || (int64_t)member->size > total - member->offset) return false;
    limit = na_option(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->unpacked_size > xx_var_get_u64(limit)) return false;
    if (zlib && member->size > NA_MAX_PACKED) return false;
    if (!zlib && !member->size) return !na_stopped(pd);
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (capacity > 65536U) capacity = 65536U;
    if (capacity > member->unpacked_size) capacity = member->unpacked_size;
    if (!capacity) capacity = 1U;
    limit = na_option(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit) {
        uint64_t maximum = xx_var_get_u64(limit), needed = zlib ? member->size : 0U;
        if (needed >= maximum) return false;
        if (capacity > maximum - needed) capacity = (size_t)(maximum - needed);
    }
    cursor = xx_io_tell(format->device);
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) goto done;
    xx_mem_zero(&sink, sizeof(sink)); sink.device.priv = &sink; sink.device.write = na_sink_write;
    sink.destination = destination; sink.pd = pd; sink.expected = member->unpacked_size;
    sink.buffer = buffer; sink.capacity = capacity; sink.adler = XX_ADLER32_INIT;
    if (!zlib) {
        uint32_t at = 0U;
        while (at < member->size) {
            size_t take = member->size - at;
            if (take > capacity) take = capacity;
            if (!na_read(format->device, member->offset + at, buffer, take, pd)) goto done;
            na_decrypt(layout, member, buffer, at, take);
            if (na_sink_write(&sink.device, buffer, take) != (ssize_t)take || !na_sink_flush(&sink)) goto done;
            at += (uint32_t)take;
        }
        ok = true;
    } else {
        uint32_t expected_adler;
        size_t consumed = 0U;
        if (member->size < 6U || !(packed = (uint8_t *)xx_mem_alloc(member->size)) ||
            !na_read(format->device, member->offset, packed, member->size, pd)) goto done;
        na_decrypt(layout, member, packed, 0U, member->size);
        if (!xx_zlib_stream_header_is_valid(packed, member->size)) goto done;
        expected_adler = (uint32_t)packed[member->size - 4U] << 24U |
            (uint32_t)packed[member->size - 3U] << 16U |
            (uint32_t)packed[member->size - 2U] << 8U | packed[member->size - 1U];
        ok = xx_deflate_unpack_memory_to_device_ex(packed + 2U, member->size - 6U,
            &sink.device, &consumed, false, pd) && consumed == member->size - 6U && sink.adler == expected_adler;
    }
    ok = ok && sink.written == sink.expected && na_sink_flush(&sink) && !na_stopped(pd);
done:
    if (packed) { xx_mem_zero(packed, member->size); xx_mem_free(packed); }
    if (buffer) { xx_mem_zero(buffer, capacity); xx_mem_free(buffer); }
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}
static bool na_reserved(const char *component, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[9];
    size_t n = 0U, i;
    while (n < length && component[n] != '.') ++n;
    while (n && component[n - 1U] == ' ') --n;
    if (n >= sizeof(stem)) return false;
    for (i = 0U; i < n; ++i) {
        char c = component[i]; stem[i] = c >= 'a' && c <= 'z' ? (char)(c + 'A' - 'a') : c;
    }
    stem[n] = 0;
    if (n == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (!xx_rt_memcmp(stem, "COM", 3U) || !xx_rt_memcmp(stem, "LPT", 3U))) return true;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!xx_str_cmp(stem, names[i])) return true;
    return false;
}
static bool na_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || na_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_nitroplus_npa_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    na_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    size_t prefix = 0U, n;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (na_layout *)state->internal_state) ||
        layout->index >= layout->count || na_stopped(pd)) return false;
    path_option = na_option(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_nitroplus_npa_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!na_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = na_option(format, &state->options, XX_META_ID_OPT_OVERWRITE);
    if (overwrite_option) overwrite = xx_var_get_bool(overwrite_option);
    path = !*base || base[xx_str_len(base) - 1U] == '/' || base[xx_str_len(base) - 1U] == '\\'
        ? xx_str_concat(base, layout->members[layout->index].name)
        : xx_str_concat3(base, "/", layout->members[layout->index].name);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto done;
    for (n = 0U; path[n]; ++n) if (path[n] == '/' || path[n] == '\\') prefix = n + 1U;
    stage_path = (char *)xx_mem_alloc(prefix + 50U);
    if (!stage_path) goto done;
    xx_rt_memcpy(stage_path, path, prefix);
    for (attempt = 0U; attempt < 128U && !na_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path + prefix, 50U,
            ".xxfc-npa-%u-%u.tmp", (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        /* An archive member may legally use the staging component itself. */
        if (!na_fold_compare(stage_path, path)) continue;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_nitroplus_npa_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !na_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
void xx_nitroplus_npa_free_archive_records_reading(Abstractformat *format,
    xx_archive_record_state *state) {
    (void)format; xx_archive_record_state_free(state);
}
