/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private bounded record plumbing for the Universal Extractor format additions.
 */
#ifndef XX_UE2_INDEXED_H
#define XX_UE2_INDEXED_H
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#define UE2_INDEX_LIMIT 1000000U
typedef struct ue2_member {
    char *name;
    int64_t offset, size, original_size;
    uint64_t tag;
    bool is_folder;
} ue2_member;
typedef struct ue2_index {
    ue2_member *members;
    size_t count, capacity;
    int64_t size;
} ue2_index;
/* All public format structs have Abstractformat then the owned index pointer. */
typedef struct ue2_format { Abstractformat format; ue2_index *index; } ue2_format;
typedef struct ue2_state { const ue2_index *index; size_t cursor; } ue2_state;

static XXFC_MAYBE_UNUSED uint16_t ue2_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t ue2_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static XXFC_MAYBE_UNUSED uint64_t ue2_u64(const uint8_t *p) {
    return (uint64_t)ue2_u32(p) | (uint64_t)ue2_u32(p + 4) << 32;
}
static bool ue2_range(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}
static bool ue2_read(Abstractformat *f, int64_t offset, void *out, size_t size) {
    size_t done = 0;
    if (!f || !f->device || !ue2_range(xx_io_total_size(f->device), offset, (int64_t)size) ||
        xx_io_seek64(f->device, offset, XX_RT_SEEK_SET) != 0) return false;
    while (done < size) {
        size_t take = size - done;
        ssize_t got;
        if (take > 65536U) take = 65536U;
        got = xx_io_read(f->device, (uint8_t *)out + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}
static void ue2_index_free(ue2_index *index) {
    size_t i;
    if (!index) return;
    for (i = 0; i < index->count; ++i) xx_str_free(index->members[i].name);
    xx_mem_free(index->members);
    xx_mem_free(index);
}
static XXFC_MAYBE_UNUSED bool ue2_add(ue2_index *index, const char *name, int64_t offset, int64_t size, uint64_t tag) {
    ue2_member *member;
    char *owned;
    if (!index || !name || !name[0] || index->count >= UE2_INDEX_LIMIT) return false;
    owned = xx_str_dup(name);
    if (!owned) return false;
    if (index->count == index->capacity) {
        size_t capacity = index->capacity ? index->capacity * 2U : 32U;
        ue2_member *members;
        if (capacity > UE2_INDEX_LIMIT) capacity = UE2_INDEX_LIMIT;
        members = (ue2_member *)xx_mem_realloc(index->members, capacity * sizeof(*members));
        if (!members) { xx_str_free(owned); return false; }
        index->members = members;
        index->capacity = capacity;
    }
    member = &index->members[index->count++];
    member->name = owned; member->offset = offset; member->size = size;
    member->original_size = size; member->tag = tag; member->is_folder = false;
    return true;
}
static bool ue2_record(xx_archive_record_state *state) {
    ue2_state *s = (ue2_state *)state->internal_state;
    const ue2_member *m;
    xx_archive_record *r = &state->current_record;
    xx_archive_record_cleanup(r); xx_archive_record_init(r);
    if (s->cursor >= s->index->count) { state->has_record = false; return false; }
    m = &s->index->members[s->cursor];
    r->data_offset = m->offset; r->compressed_size = m->size;
    state->has_record = true; state->current_index = (int64_t)s->cursor;
    return xx_archive_record_set_original_name(r, m->name) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSED_SIZE, (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)m->original_size) &&
           xx_archive_record_set_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, m->size == m->original_size ? 0 : 1) &&
           xx_archive_record_set_meta_bool(r, XX_META_ID_IS_FOLDER, m->is_folder);
}
static int64_t ue2_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || xx_format_handle_base_info(f, pd)) ? f->format_size : -1;
}
static uint64_t ue2_count(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || xx_format_handle_base_info(f, pd)) ? f->number_of_archive_records : 0;
}
static void ue2_state_free(void *pointer) { xx_mem_free(pointer); }
static xx_archive_record_state *ue2_records(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    ue2_state *s;
    size_t i;
    if (!f || (pd && xx_pd_is_stopped(pd)) ||
        (!f->base_info_handled && !xx_format_handle_base_info(f, pd)) || !((ue2_format *)f)->index) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    s = (ue2_state *)xx_mem_calloc(1, sizeof(*s));
    if (!state || !s) { xx_mem_free(state); xx_mem_free(s); return NULL; }
    xx_archive_record_state_init(state, f);
    state->internal_state = s; state->free_internal = ue2_state_free;
    s->index = ((ue2_format *)f)->index;
    state->total_records = (int64_t)s->index->count;
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta copy;
        if (!meta) continue;
        xx_meta_init(&copy, meta->meta_id);
        if (!xx_var_copy(&copy.var, &meta->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); xx_archive_record_state_free(state); return NULL;
        }
    }
    if (s->index->count && !ue2_record(state)) { xx_archive_record_state_free(state); return NULL; }
    return state;
}
static const xx_archive_record *ue2_current(Abstractformat *f, xx_archive_record_state *state) {
    return f && state && state->format == f && state->has_record ? &state->current_record : NULL;
}
static bool ue2_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!ue2_current(f, state) || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    ++((ue2_state *)state->internal_state)->cursor;
    return ue2_record(state);
}
static void ue2_free_records(Abstractformat *f, xx_archive_record_state *state) {
    (void)f; xx_archive_record_state_free(state);
}
static bool ue2_test_extent(Abstractformat *f, int64_t offset, int64_t size, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0, saved;
    int level;
    bool result = false;
    if (!ue2_range(xx_io_total_size(f->device), offset, size)) return false;
    saved = xx_io_tell(f->device);
    buffer = (uint8_t *)xx_mem_alloc(65536U);
    if (!buffer) return false;
    level = xx_pd_enter_level(pd, (uint64_t)size, "Testing stored member");
    while (done < size) {
        size_t take = size - done > 65536 ? 65536U : (size_t)(size - done);
        if ((pd && xx_pd_is_stopped(pd)) || !ue2_read(f, offset + done, buffer, take)) goto cleanup;
        done += (int64_t)take; xx_pd_set_current(pd, level, (uint64_t)done);
    }
    result = !(pd && xx_pd_is_stopped(pd));
cleanup:
    xx_pd_leave_level(pd, level); xx_mem_free(buffer);
    if (saved >= 0) xx_io_seek64(f->device, saved, XX_RT_SEEK_SET);
    return result;
}
static bool ue2_safe_name(const char *name) {
    const char *start = name, *p;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    for (p = name;; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c && (c < 32 || c == ':' || c == '\\' || c == '"' || c == '<' || c == '>' || c == '|' || c == '*' || c == '?')) return false;
        if (!c || c == '/') {
            size_t n = (size_t)(p - start), base = 0;
            char stem[5]; size_t i;
            if (!n || start[n - 1] == '.' || start[n - 1] == ' ' ||
                (n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.')) return false;
            while (base < n && start[base] != '.') ++base;
            if (base == 3 || base == 4) {
                for (i = 0; i < base; ++i) stem[i] = (char)xx_rt_ascii_tolower((unsigned char)start[i]);
                stem[base] = 0;
                if (!xx_rt_strcmp(stem, "con") || !xx_rt_strcmp(stem, "prn") ||
                    !xx_rt_strcmp(stem, "aux") || !xx_rt_strcmp(stem, "nul") ||
                    (base == 4 && (!xx_rt_strncmp(stem, "com", 3) || !xx_rt_strncmp(stem, "lpt", 3)) && stem[3] >= '1' && stem[3] <= '9')) return false;
            }
            if (!c) return true;
            start = p + 1;
        }
    }
}
static bool ue2_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_archive_record *r = ue2_current(f, state);
    const xx_var *option = NULL;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL;
    bool result = false;
    size_t i;
    if (!r || (pd && xx_pd_is_stopped(pd)) || !ue2_safe_name(xx_archive_record_get_original_name(r)) ||
        !ue2_range(xx_io_total_size(f->device), r->data_offset, r->compressed_size)) return false;
    for (i = 0; i < state->options.count; ++i) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)&state->options, i);
        if (meta && meta->meta_id == XX_META_ID_OPT_UNPACK_PATH) { option = &meta->var; break; }
    }
    if (!option) return ue2_test_extent(f, r->data_offset, r->compressed_size, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); base = owned_base;
    }
    if (base) path = xx_str_concat3(base, "/", xx_archive_record_get_original_name(r));
    if (path) {
        ue2_state *s = (ue2_state *)state->internal_state;
        bool folder = s && s->cursor < s->index->count && s->index->members[s->cursor].is_folder;
        if (xx_store_create_dirs_a(path, folder))
            result = folder || xx_store_unpack_device_to_file(f->device, r->data_offset, r->compressed_size, path, pd);
    }
    xx_str_free(path); xx_str_free(owned_base); return result;
}
static void ue2_destroy_format(Abstractformat *f) {
    ue2_index_free(((ue2_format *)f)->index); ((ue2_format *)f)->index = NULL;
    xx_format_cleanup_extra_parameters(f);
}
static XXFC_MAYBE_UNUSED bool ue2_accept(Abstractformat *f, ue2_index *index) {
    if (!index) return false;
    ue2_index_free(((ue2_format *)f)->index); ((ue2_format *)f)->index = index;
    f->format_size = index->size; f->number_of_archive_records = index->count;
    f->base_info_handled = true; f->is_valid = true; return true;
}
static void ue2_init_format(Abstractformat *f, xx_io_device *device, int64_t base, xx_file_type_t type,
                            const char *extension, const char *mime) {
    xx_format_init(f, device, base); f->file_type = type; f->endian = XX_ENDIAN_LITTLE;
    f->format_type = XX_TYPE_ARCHIVE; f->is_archive = true;
    xx_format_set_extension(f, extension); xx_format_set_mime_type(f, mime);
    f->get_format_size = ue2_size; f->get_number_of_archive_records = ue2_count;
    f->create_archive_records_reading = ue2_records; f->get_current_archive_record = ue2_current;
    f->archive_record_move_to_next = ue2_next; f->free_archive_records_reading = ue2_free_records;
    f->unpack_current_archive_record = ue2_unpack; f->destroy = ue2_destroy_format;
}
#endif
