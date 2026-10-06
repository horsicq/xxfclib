/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/oberon/xx_oberon.h"
#include "xxfclib/algo/mscompress/xx_mscompress.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>

#ifdef OBERON
#define OBERON_TYPE XX_FILE_TYPE_OBERON
#else
#define OBERON_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define OB_HEADER 136U
#define OB_NAME 64U
#define OB_MAX_MEMBERS 10000U
#define OB_MAX_INPUT UINT64_C(1073741824)
#define OB_MAX_MEMBER UINT32_C(67108864)

typedef struct ob_member_s {
    char *name;
    int64_t header_at, data_at;
    uint32_t compressed, original, flags;
} ob_member;
typedef struct ob_view_s {
    ob_member *items;
    size_t count, capacity, index;
    int64_t end;
} ob_view;

static bool ob_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}
static uint32_t ob_le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool ob_read(xx_io_device *device, int64_t at, void *data,
                    size_t size) {
    int64_t saved;
    size_t done = 0U;
    bool ok = false;
    if (!device || at < 0 || (!data && size)) return false;
    saved = xx_io_tell(device);
    if (saved < 0) return false;
    if (xx_io_seek64(device, at, SEEK_SET) == 0) {
        while (done < size) {
            ssize_t got = xx_io_read(device, (uint8_t *)data + done,
                                     size - done);
            if (got <= 0 || (size_t)got > size - done) break;
            done += (size_t)got;
        }
        ok = done == size;
    }
    if (xx_io_seek64(device, saved, SEEK_SET) != 0) ok = false;
    return ok;
}
static void ob_view_free(void *pointer) {
    ob_view *view = (ob_view *)pointer;
    size_t i;
    if (!view) return;
    for (i = 0U; i < view->count; ++i)
        xx_mem_free(view->items[i].name);
    xx_mem_free(view->items);
    xx_mem_free(view);
}
static char ob_fold(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}
static bool ob_same_name(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b)
        if (ob_fold(*a) != ob_fold(*b)) return false;
    return *a == *b;
}
static bool ob_device_stem(const char *p, size_t size) {
    static const char *const words[] = {
        "con", "prn", "aux", "nul", "conin$", "conout$", "clock$"
    };
    size_t i, j, stem = 0U;
    while (stem < size && p[stem] != '.') ++stem;
    while (stem && p[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(words) / sizeof(words[0]); ++i) {
        for (j = 0U; j < stem && words[i][j] &&
                     ob_fold(p[j]) == words[i][j]; ++j) {}
        if (j == stem && words[i][j] == '\0') return true;
    }
    return stem == 4U &&
           ((ob_fold(p[0]) == 'c' && ob_fold(p[1]) == 'o' &&
             ob_fold(p[2]) == 'm') ||
            (ob_fold(p[0]) == 'l' && ob_fold(p[1]) == 'p' &&
             ob_fold(p[2]) == 't')) && p[3] >= '0' && p[3] <= '9';
}
static bool ob_name(const uint8_t *raw, char **out) {
    size_t length = 0U, i, component = 0U;
    char *name;
    while (length < OB_NAME && raw[length]) ++length;
    if (!length || length == OB_NAME) return false;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return false;
    for (i = 0U; i < length; ++i) {
        uint8_t c = raw[i];
        if (c == '\\') {
            if (i == component || raw[i - 1U] == '.' ||
                raw[i - 1U] == ' ' ||
                ob_device_stem(name + component, i - component))
                goto bad;
            name[i] = '/';
            component = i + 1U;
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*') goto bad;
        name[i] = (char)c;
    }
    if (length == component || name[length - 1U] == '.' ||
        name[length - 1U] == ' ' ||
        ob_device_stem(name + component, length - component)) goto bad;
    name[length] = '\0';
    *out = name;
    return true;
bad:
    xx_mem_free(name);
    return false;
}

/* Every header is 136 bytes. LE32 at +128 counts the entire following SZDD
 * stream, including its 14-byte SZDD header. Zero denotes an empty file.
 * Reaching EOF exactly confirms the record boundaries independently of the
 * file names/descriptions stored in the other header fields. */
static ob_view *ob_parse(Abstractformat *f, xx_pd_struct *pd) {
    ob_view *view;
    int64_t total, at;
    bool any_stream = false;
    if (!f || !f->device || f->base_address < 0 || ob_stopped(pd))
        return NULL;
    total = xx_io_total_size(f->device);
    if (total < f->base_address + OB_HEADER ||
        (uint64_t)(total - f->base_address) > OB_MAX_INPUT) return NULL;
    view = (ob_view *)xx_mem_calloc(1U, sizeof(*view));
    if (!view) return NULL;
    at = f->base_address;
    while (at < total && !ob_stopped(pd)) {
        uint8_t h[OB_HEADER], z[14];
        ob_member entry;
        uint32_t compressed, original = 0U, flags;
        size_t i;
        if (view->count == OB_MAX_MEMBERS || total - at < OB_HEADER ||
            !ob_read(f->device, at, h, sizeof(h))) goto bad;
        xx_mem_zero(&entry, sizeof(entry));
        if (!ob_name(h, &entry.name)) goto bad;
        compressed = ob_le32(h + 128U);
        flags = ob_le32(h + 132U);
        if (flags > 1U || compressed > OB_MAX_MEMBER ||
            compressed > (uint64_t)(total - at - OB_HEADER)) {
            xx_mem_free(entry.name);
            goto bad;
        }
        if (compressed) {
            static const uint8_t magic[9] = {
                'S', 'Z', 'D', 'D', 0x88, 0xf0, 0x27, 0x33, 'A'
            };
            if (compressed < sizeof(z) ||
                !ob_read(f->device, at + OB_HEADER, z, sizeof(z)) ||
                xx_rt_memcmp(z, magic, sizeof(magic))) {
                xx_mem_free(entry.name);
                goto bad;
            }
            original = ob_le32(z + 10U);
            if (original > OB_MAX_MEMBER) {
                xx_mem_free(entry.name);
                goto bad;
            }
            any_stream = true;
        }
        for (i = 0U; i < view->count; ++i) {
            if (ob_same_name(view->items[i].name, entry.name)) {
                xx_mem_free(entry.name);
                goto bad;
            }
        }
        if (view->count == view->capacity) {
            size_t capacity = view->capacity ? view->capacity * 2U : 32U;
            ob_member *grown = (ob_member *)xx_mem_realloc(
                view->items, capacity * sizeof(*view->items));
            if (!grown) { xx_mem_free(entry.name); goto bad; }
            view->items = grown;
            view->capacity = capacity;
        }
        entry.header_at = at;
        entry.data_at = at + OB_HEADER;
        entry.compressed = compressed;
        entry.original = original;
        entry.flags = flags;
        view->items[view->count++] = entry;
        at += OB_HEADER + compressed;
    }
    if (ob_stopped(pd) || at != total || !view->count || !any_stream)
        goto bad;
    view->end = at;
    return view;
bad:
    ob_view_free(view);
    return NULL;
}
static bool ob_copy_options(xx_list_s *dest, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(dest, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}
static bool ob_set_record(xx_archive_record *record, const ob_member *item) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->header_at;
    record->header_size = OB_HEADER;
    record->data_offset = item->data_at;
    record->compressed_size = item->compressed;
    return xx_archive_record_set_original_name(record, item->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          item->original) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSED_SIZE,
                                          item->compressed) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          item->compressed ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}
static XXFC_MAYBE_UNUSED const xx_var *ob_option(const xx_list_s *options, uint32_t id) {
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}
static bool ob_write(xx_io_device *out, const uint8_t *data, size_t size,
                     xx_pd_struct *pd) {
    size_t done = 0U;
    while (done < size) {
        ssize_t got;
        if (ob_stopped(pd)) return false;
        got = xx_io_write(out, data + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return !ob_stopped(pd);
}
static bool ob_decode(Abstractformat *f, const ob_member *item,
                      xx_io_device *out, xx_pd_struct *pd) {
    uint8_t *packed = NULL, *plain = NULL;
    size_t consumed = 0U;
    bool ok = false;
    if (ob_stopped(pd) || out == f->device) return false;
    if (!item->compressed) return true;
    packed = (uint8_t *)xx_mem_alloc(item->compressed);
    plain = (uint8_t *)xx_mem_alloc(item->original ? item->original : 1U);
    if (!packed || !plain ||
        !ob_read(f->device, item->data_at, packed, item->compressed) ||
        !xx_mscompress_lzss_decode(packed + 14U,
                                   item->compressed - 14U, plain,
                                   item->original, 16U, &consumed) ||
        consumed != item->compressed - 14U || ob_stopped(pd)) goto done;
    ok = !out || ob_write(out, plain, item->original, pd);
done:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return ok;
}

void xx_oberon_init(xx_oberon *archive, xx_io_device *device, int64_t base) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = OBERON_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-oberon-setup-z");
    xx_format_set_extension(&archive->format, "z");
    archive->format.check_is_valid = xx_oberon_check_is_valid;
    archive->format.handle_base_info = xx_oberon_handle_base_info;
    archive->format.get_format_size = xx_oberon_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_oberon_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_oberon_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_oberon_get_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_oberon_archive_record_move_to_next;
    archive->format.unpack_current_archive_record =
        xx_oberon_unpack_current_archive_record;
    archive->format.free_archive_records_reading =
        xx_oberon_free_archive_records_reading;
}
xx_oberon *xx_oberon_create(xx_io_device *device, int64_t base) {
    xx_oberon *archive = (xx_oberon *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_oberon_init(archive, device, base);
    return archive;
}
void xx_oberon_destroy(xx_oberon *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_oberon_free(xx_oberon *archive) {
    if (!archive) return;
    xx_oberon_destroy(archive);
    xx_mem_free(archive);
}
bool xx_oberon_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    ob_view *view = ob_parse(f, pd);
    if (!view) return false;
    ob_view_free(view);
    return true;
}
bool xx_oberon_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    ob_view *view = ob_parse(f, pd);
    if (!view) return false;
    ((xx_oberon *)f)->number_of_records = view->count;
    f->number_of_archive_records = view->count;
    f->format_size = view->end - f->base_address;
    f->overlay_offset = -1;
    f->overlay_size = 0;
    f->is_valid = true;
    f->base_info_handled = true;
    ob_view_free(view);
    return true;
}
int64_t xx_oberon_get_format_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_oberon_handle_base_info(f, pd)) ? f->format_size : -1;
}
uint64_t xx_oberon_get_number_of_archive_records(Abstractformat *f,
                                                   xx_pd_struct *pd) {
    return f && (f->base_info_handled ||
                 xx_oberon_handle_base_info(f, pd)) ?
           f->number_of_archive_records : 0U;
}
xx_archive_record_state *xx_oberon_create_archive_records_reading(
    Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    ob_view *view = ob_parse(f, pd);
    xx_archive_record_state *state;
    if (!view) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { ob_view_free(view); return NULL; }
    xx_archive_record_state_init(state, f);
    state->internal_state = view;
    state->free_internal = ob_view_free;
    state->total_records = (int64_t)view->count;
    if (!ob_copy_options(&state->options, options) ||
        !ob_set_record(&state->current_record, &view->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}
const xx_archive_record *xx_oberon_get_current_archive_record(
    Abstractformat *f, xx_archive_record_state *state) {
    return f && state && state->format == f && state->has_record ?
           &state->current_record : NULL;
}
bool xx_oberon_archive_record_move_to_next(Abstractformat *f,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    ob_view *view;
    if (!f || !state || state->format != f || !state->has_record ||
        !(view = (ob_view *)state->internal_state) || ob_stopped(pd))
        return false;
    if (++view->index >= view->count ||
        !ob_set_record(&state->current_record, &view->items[view->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}
bool xx_oberon_unpack_current_archive_record(Abstractformat *f,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    ob_view *view;
    const ob_member *item;
    const xx_var *option;
    const char *base;
    char *wide_base = NULL, *path = NULL, *stage = NULL;
    xx_io_device *output = NULL;
    size_t capacity;
    unsigned attempt;
    bool overwrite, ok = false;
    if (!f || !state || state->format != f || !state->has_record ||
        !(view = (ob_view *)state->internal_state) ||
        view->index >= view->count || ob_stopped(pd)) return false;
    item = &view->items[view->index];
    option = xx_format_resolve_extra_parameter(f, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return ob_decode(f, item, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW)
        base = wide_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
    else return false;
    if (!base) goto done;
    path = base[0] ? xx_str_concat3(base, "/", item->name) :
                     xx_str_dup(item->name);
    option = xx_format_resolve_extra_parameter(f, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    if (!path || (!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false) ||
        xx_str_len(path) > SIZE_MAX - 40U) goto done;
    capacity = xx_str_len(path) + 40U;
    stage = (char *)xx_mem_alloc(capacity);
    if (!stage) goto done;
    for (attempt = 0U; attempt < 128U && !ob_stopped(pd); ++attempt) {
        int n = xx_rt_snprintf(stage, capacity, "%s.xx_oberon.tmp.%u",
                               path, attempt);
        if (n < 0 || (size_t)n >= capacity) goto done;
        output = xx_io_file_open(stage, "wbx");
        if (output) break;
    }
    if (!output) goto done;
    ok = ob_decode(f, item, output, pd);
    if (xx_io_close(output) != 0) ok = false;
    output = NULL;
    if (ok && !ob_stopped(pd))
        ok = xx_io_file_replace_a(stage, path, overwrite);
    else
        ok = false;
    if (!ok) (void)xx_io_file_remove_a(stage);
done:
    if (output) {
        (void)xx_io_close(output);
        (void)xx_io_file_remove_a(stage);
    }
    xx_mem_free(stage);
    xx_str_free(path);
    xx_str_free(wide_base);
    return ok;
}
void xx_oberon_free_archive_records_reading(Abstractformat *f,
                                             xx_archive_record_state *state) {
    (void)f;
    xx_archive_record_state_free(state);
}
