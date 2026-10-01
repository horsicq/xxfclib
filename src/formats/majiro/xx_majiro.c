/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Independent native C implementation of the Majiro archive layout.
 * Format evidence: GARbro ArcFormats/Majiro/ArcMajiro.cs (MIT, morkt,
 * https://github.com/morkt/GARbro/blob/master/ArcFormats/Majiro/ArcMajiro.cs).
 * No upstream code is incorporated; resource hashes are lookup keys, not
 * payload checksums. Their values are preserved in the archive itself.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/majiro/xx_majiro.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef MAJIRO
#define MA_FILE_TYPE XX_FILE_TYPE_MAJIRO
#else
#define MA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define MA_HEADER_SIZE 28U
#define MA_MAX_COUNT 0xFFFFFU
#define MA_MAX_NAME 4096U
#define MA_MAX_NAMES (16U * 1024U * 1024U)
#define MA_MAX_EXPANDED (64U * 1024U * 1024U)

typedef struct ma_member {
    int64_t offset;
    uint32_t size;
    char *name;
    bool duplicate;
} ma_member;
typedef struct ma_layout {
    ma_member *members;
    uint32_t count;
    uint32_t version;
    uint32_t record_size;
    uint32_t names_offset;
    uint32_t data_offset;
    int64_t format_size;
    size_t index;
} ma_layout;
typedef struct ma_name_key {
    const char *name;
    uint32_t index;
} ma_name_key;

static bool ma_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static uint32_t ma_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}
static bool ma_read(xx_io_device *device, int64_t at, void *buffer,
                    size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || at < 0 || xx_io_seek64(device, at, XX_RT_SEEK_SET))
        return false;
    while (done < size) {
        ssize_t got;
        size_t take = size - done;
        if (ma_stopped(pd)) return false;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static void ma_layout_free(void *ptr) {
    ma_layout *layout = (ma_layout *)ptr;
    uint32_t i;
    if (!layout) return;
    if (layout->members) {
        for (i = 0U; i < layout->count; ++i)
            if (layout->members[i].name) xx_mem_free(layout->members[i].name);
        xx_mem_free(layout->members);
    }
    xx_mem_free(layout);
}
static size_t ma_escape(char *out, uint8_t c) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '%'; out[1] = hex[c >> 4U]; out[2] = hex[c & 15U];
    return 3U;
}
static bool ma_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}
static bool ma_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}
static char *ma_name(const uint8_t *raw, size_t size) {
    char *result = (char *)xx_mem_alloc(size * 3U + 14U);
    size_t i = 0U, at = 0U;
    if (!result) return NULL;
    while (i < size) {
        uint8_t c = raw[i++];
        if (ma_lead(c) && i < size && ma_trail(raw[i])) {
            at += ma_escape(result + at, c);
            at += ma_escape(result + at, raw[i++]);
        } else if (c >= 0x80U || c < 0x20U || c == 0x7fU || c == '%') {
            at += ma_escape(result + at, c);
        } else {
            result[at++] = c == '\\' ? '/' : (char)c;
        }
    }
    result[at] = 0;
    return result;
}
static int ma_fold_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}
static int ma_compare_keys(const void *a, const void *b) {
    const ma_name_key *x = (const ma_name_key *)a;
    const ma_name_key *y = (const ma_name_key *)b;
    int order = ma_fold_compare(x->name, y->name);
    if (order) return order;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}
static void ma_suffix(char *name, uint32_t index) {
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

/* Every header/index/name/data range is relative to the caller's base. */
static ma_layout *ma_parse_inner(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t header[MA_HEADER_SIZE], entry[16], next[16];
    ma_layout *layout = NULL;
    ma_name_key *keys = NULL;
    uint8_t *names = NULL;
    uint64_t index_end;
    int64_t available, total;
    size_t name_size, name_at = 0U, expanded = 0U;
    uint32_t i, hash_size;
    bool ok = false;
    if (!format || !format->device || format->base_address < 0 || ma_stopped(pd))
        return NULL;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return NULL;
    available = total - format->base_address;
    if (available < MA_HEADER_SIZE ||
        !ma_read(format->device, format->base_address, header, sizeof(header), pd) ||
        xx_rt_memcmp(header, "MajiroArcV", 10U) ||
        header[10] < '1' || header[10] > '3' ||
        xx_rt_memcmp(header + 11U, ".000\0", 5U)) return NULL;
    layout = (ma_layout *)xx_mem_calloc(1U, sizeof(*layout));
    if (!layout) return NULL;
    layout->count = ma_le32(header + 16U);
    layout->version = (uint32_t)(header[10] - '0');
    layout->record_size = (layout->version + 1U) * 4U;
    layout->names_offset = ma_le32(header + 20U);
    layout->data_offset = ma_le32(header + 24U);
    index_end = MA_HEADER_SIZE +
        ((uint64_t)layout->count + (layout->version == 1U ? 1U : 0U)) *
            layout->record_size;
    if (!layout->count || layout->count > MA_MAX_COUNT ||
        index_end != layout->names_offset ||
        layout->data_offset <= layout->names_offset ||
        (uint64_t)layout->data_offset > (uint64_t)available) goto done;
    name_size = layout->data_offset - layout->names_offset;
    if (name_size > MA_MAX_NAMES || layout->count > name_size ||
        (uint64_t)layout->count * sizeof(*layout->members) > SIZE_MAX ||
        (uint64_t)layout->count * sizeof(*keys) > SIZE_MAX) goto done;
    layout->members = (ma_member *)xx_mem_calloc(layout->count, sizeof(*layout->members));
    keys = (ma_name_key *)xx_mem_alloc((size_t)layout->count * sizeof(*keys));
    names = (uint8_t *)xx_mem_alloc(name_size);
    if (!layout->members || !keys || !names ||
        !ma_read(format->device, format->base_address + layout->names_offset,
                 names, name_size, pd)) goto done;
    hash_size = layout->version == 3U ? 8U : 4U;
    layout->format_size = layout->data_offset;
    for (i = 0U; i < layout->count; ++i) {
        ma_member *member = &layout->members[i];
        size_t start = name_at, length;
        uint32_t offset, size;
        if (ma_stopped(pd)) goto done;
        while (name_at < name_size && names[name_at] && name_at - start <= MA_MAX_NAME)
            ++name_at;
        length = name_at - start;
        if (name_at >= name_size || !length || length > MA_MAX_NAME ||
            expanded > MA_MAX_EXPANDED - (length * 3U + 14U)) goto done;
        ++name_at;
        expanded += length * 3U + 14U;
        member->name = ma_name(names + start, length);
        if (!member->name ||
            !ma_read(format->device, format->base_address + MA_HEADER_SIZE +
                      (int64_t)i * layout->record_size, entry,
                      layout->record_size, pd)) goto done;
        offset = ma_le32(entry + hash_size);
        if (layout->version == 1U) {
            uint32_t end;
            if (!ma_read(format->device, format->base_address + MA_HEADER_SIZE +
                          (int64_t)(i + 1U) * layout->record_size, next,
                          layout->record_size, pd)) goto done;
            end = ma_le32(next + hash_size);
            if (end < offset) goto done;
            size = end - offset;
        } else size = ma_le32(entry + hash_size + 4U);
        if (offset < layout->data_offset || (uint64_t)offset > (uint64_t)available ||
            (uint64_t)size > (uint64_t)available - offset) goto done;
        member->offset = format->base_address + offset;
        member->size = size;
        if ((int64_t)offset + size > layout->format_size)
            layout->format_size = (int64_t)offset + size;
        keys[i].name = member->name;
        keys[i].index = i;
    }
    /* Leave any name-table alignment padding uninterpreted. */
    xx_rt_qsort(keys, layout->count, sizeof(*keys), ma_compare_keys);
    if (ma_stopped(pd)) goto done;
    for (i = 1U; i < layout->count; ++i)
        if (!ma_fold_compare(keys[i - 1U].name, keys[i].name))
            layout->members[keys[i].index].duplicate = true;
    for (i = 0U; i < layout->count; ++i) {
        if (ma_stopped(pd)) goto done;
        if (layout->members[i].duplicate) ma_suffix(layout->members[i].name, i);
    }
    ok = true;
done:
    if (names) xx_mem_free(names);
    if (keys) xx_mem_free(keys);
    if (!ok) { ma_layout_free(layout); layout = NULL; }
    return layout;
}
static ma_layout *ma_parse(Abstractformat *format, xx_pd_struct *pd) {
    int64_t cursor = format && format->device ? xx_io_tell(format->device) : -1;
    ma_layout *layout = ma_parse_inner(format, pd);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) {
        ma_layout_free(layout); layout = NULL;
    }
    return layout;
}
static void ma_destroy_format(Abstractformat *format) {
    xx_majiro_destroy((xx_majiro *)format);
}
void xx_majiro_init(xx_majiro *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = MA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_extension(&archive->format, "arc");
    xx_format_set_mime_type(&archive->format, "application/x-majiro-archive");
    archive->format.destroy = ma_destroy_format;
    archive->format.check_is_valid = xx_majiro_check_is_valid;
    archive->format.handle_base_info = xx_majiro_handle_base_info;
    archive->format.get_format_size = xx_majiro_get_format_size;
    archive->format.get_number_of_archive_records = xx_majiro_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_majiro_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_majiro_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_majiro_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_majiro_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_majiro_free_archive_records_reading;
}
xx_majiro *xx_majiro_create(xx_io_device *device, int64_t base) {
    xx_majiro *archive = (xx_majiro *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_majiro_init(archive, device, base);
    return archive;
}
void xx_majiro_destroy(xx_majiro *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_majiro_free(xx_majiro *archive) {
    if (!archive) return;
    xx_majiro_destroy(archive); xx_mem_free(archive);
}
bool xx_majiro_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ma_layout *layout = ma_parse(format, pd);
    bool valid = layout != NULL;
    ma_layout_free(layout);
    return valid;
}
bool xx_majiro_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ma_layout *layout;
    xx_majiro *archive;
    if (!format || ma_stopped(pd)) return false;
    if (format->base_info_handled) return format->is_valid;
    layout = ma_parse(format, pd);
    if (!layout) return false;
    archive = (xx_majiro *)format;
    archive->number_of_records = layout->count;
    archive->version = layout->version;
    archive->names_offset = layout->names_offset;
    archive->data_offset = layout->data_offset;
    format->number_of_archive_records = layout->count;
    format->format_size = layout->format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ma_layout_free(layout);
    return true;
}
int64_t xx_majiro_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return xx_majiro_handle_base_info(format, pd) ? format->format_size : -1;
}
uint64_t xx_majiro_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd) {
    return xx_majiro_handle_base_info(format, pd) ? format->number_of_archive_records : 0U;
}
static bool ma_set_record(Abstractformat *format, xx_archive_record_state *state) {
    ma_layout *layout = (ma_layout *)state->internal_state;
    const ma_member *member = &layout->members[layout->index];
    xx_archive_record *record = &state->current_record;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + MA_HEADER_SIZE +
                            (int64_t)layout->index * layout->record_size;
    record->header_size = layout->record_size;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}
xx_archive_record_state *xx_majiro_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ma_layout *layout = ma_parse(format, pd);
    xx_archive_record_state *state;
    size_t i;
    if (!layout) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { ma_layout_free(layout); return NULL; }
    xx_archive_record_state_init(state, format);
    state->internal_state = layout;
    state->free_internal = ma_layout_free;
    state->total_records = layout->count;
    state->current_index = 0;
    if (options) for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (ma_stopped(pd) || !meta) goto fail;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto fail;
        }
    }
    state->has_record = ma_set_record(format, state);
    if (state->has_record) return state;
fail:
    xx_archive_record_state_free(state);
    return NULL;
}
const xx_archive_record *xx_majiro_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
        ? &state->current_record : NULL;
}
bool xx_majiro_archive_record_move_to_next(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    ma_layout *layout;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (ma_layout *)state->internal_state)) return false;
    if (ma_stopped(pd) || layout->index + 1U >= layout->count) {
        state->has_record = false; return false;
    }
    ++layout->index;
    state->current_index = (int64_t)layout->index;
    state->has_record = ma_set_record(format, state);
    return state->has_record;
}
static const xx_var *ma_option(Abstractformat *format, const xx_list_s *options, uint32_t id) {
    return xx_format_resolve_extra_parameter(format, options, id);
}
bool xx_majiro_unpack_current_archive_record_to_device(Abstractformat *format,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    ma_layout *layout;
    const ma_member *member;
    const xx_var *limit;
    uint8_t *buffer = NULL;
    size_t capacity = xx_get_file_buffer_size();
    uint32_t done = 0U;
    int64_t cursor, total;
    bool ok = false;
    if (!format || !format->device || !state || state->format != format ||
        !state->has_record || !(layout = (ma_layout *)state->internal_state) ||
        layout->index >= layout->count || destination == format->device || ma_stopped(pd))
        return false;
    member = &layout->members[layout->index];
    total = xx_io_total_size(format->device);
    if (member->offset < 0 || member->offset > total ||
        (int64_t)member->size > total - member->offset) return false;
    limit = ma_option(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && member->size > xx_var_get_u64(limit)) return false;
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (capacity > 1048576U) capacity = 1048576U;
    if (capacity > member->size) capacity = member->size;
    limit = ma_option(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && xx_var_get_u64(limit) < capacity) capacity = (size_t)xx_var_get_u64(limit);
    cursor = xx_io_tell(format->device);
    if (!member->size) { ok = true; goto done; }
    if (!capacity || !(buffer = (uint8_t *)xx_mem_alloc(capacity))) goto done;
    while (done < member->size) {
        size_t take = member->size - done, wrote = 0U;
        if (take > capacity) take = capacity;
        if (!ma_read(format->device, member->offset + done, buffer, take, pd)) goto done;
        while (destination && wrote < take) {
            ssize_t amount;
            if (ma_stopped(pd)) goto done;
            amount = xx_io_write(destination, buffer + wrote, take - wrote);
            if (amount <= 0 || (size_t)amount > take - wrote) goto done;
            wrote += (size_t)amount;
        }
        done += (uint32_t)take;
    }
    ok = !ma_stopped(pd);
done:
    if (buffer) xx_mem_free(buffer);
    if (cursor >= 0 && xx_io_seek64(format->device, cursor, XX_RT_SEEK_SET)) ok = false;
    return ok;
}
static bool ma_reserved(const char *component, size_t length) {
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
static bool ma_safe_name(const char *name) {
    const char *component = name, *at;
    if (!name || !*name || *name == '/') return false;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 127U || (c && c < 32U)) return false;
        if (c == '/' || !c) {
            size_t n = (size_t)(at - component);
            if (!n || component[0] == ' ' || component[n - 1U] == '.' ||
                component[n - 1U] == ' ' || ma_reserved(component, n)) return false;
            if (!c) return true;
            component = at + 1;
        }
    }
}
bool xx_majiro_unpack_current_archive_record(Abstractformat *format,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    ma_layout *layout;
    const xx_var *path_option, *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL, *stage_path = NULL;
    xx_io_device *stage = NULL;
    bool ok = false, overwrite = false;
    unsigned attempt;
    size_t prefix = 0U, n;
    if (!format || !state || state->format != format || !state->has_record ||
        !(layout = (ma_layout *)state->internal_state) ||
        layout->index >= layout->count || ma_stopped(pd)) return false;
    path_option = ma_option(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xx_majiro_unpack_current_archive_record_to_device(format, state, NULL, pd);
    if (!ma_safe_name(layout->members[layout->index].name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option)); base = owned_base;
    }
    if (!base) goto done;
    overwrite_option = ma_option(format, &state->options, XX_META_ID_OPT_OVERWRITE);
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
    for (attempt = 0U; attempt < 128U && !ma_stopped(pd); ++attempt) {
        int wrote = xx_rt_snprintf(stage_path + prefix, 50U,
            ".xxfc-majiro-%u-%u.tmp", (unsigned)layout->index, attempt);
        if (wrote <= 0) goto done;
        /* An archive member may legally use the staging component itself. */
        if (!ma_fold_compare(stage_path, path)) continue;
        stage = xx_io_file_open(stage_path, "wbx");
        if (stage) break;
    }
    if (!stage) goto done;
    ok = xx_majiro_unpack_current_archive_record_to_device(format, state, stage, pd);
    if (xx_io_close(stage)) ok = false;
    stage = NULL;
    if (ok && !ma_stopped(pd)) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    else ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage_path);
done:
    if (stage) { (void)xx_io_close(stage); (void)xx_io_file_remove_a(stage_path); }
    if (stage_path) xx_mem_free(stage_path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}
void xx_majiro_free_archive_records_reading(Abstractformat *format,
    xx_archive_record_state *state) {
    (void)format; xx_archive_record_state_free(state);
}
