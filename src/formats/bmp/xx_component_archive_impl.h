/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal bounded range-member adapter for the component readers.
 * Every including reader supplies xx_components_build; no file is copied
 * wholesale and every range is checked against its validated format extent.
 */
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/crc/xx_crc.h"

typedef struct xx_component_member_s {
    int64_t offset, size;
    char name[96];
    bool has_crc32;
    uint32_t crc32, crc32_seed;
} xx_component_member;
typedef struct xx_component_stream_s {
    xx_component_member *items;
    size_t count, capacity, index;
} xx_component_stream;

static bool xx_components_build(Abstractformat *, xx_component_stream *, xx_pd_struct *);

static bool xx_component_read(Abstractformat *f, int64_t rel, void *data, size_t size)
{
    size_t done = 0;
    size_t capacity = xx_get_file_buffer_size();
    if (!f || !f->device || rel < 0 || f->base_address < 0 || f->format_size < 0 || (uint64_t)size > (uint64_t)f->format_size || rel > f->format_size - (int64_t)size ||
        xx_io_seek64(f->device, f->base_address + rel, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done < capacity ? size - done : capacity;
        ssize_t got = xx_io_read(f->device, (uint8_t *)data + done, request);
        if (got <= 0 || (size_t)got > request) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_component_add(Abstractformat *f, xx_component_stream *s, int64_t rel, int64_t size, const char *kind)
{
    xx_component_member *m;
    size_t i, n;
    char number[16];
    if (!f || !s || !kind || rel < 0 || size < 0 || size > f->format_size || rel > f->format_size - size || s->count >= 65536U) return false;
    if (s->count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2U : 16U;
        void *p = xx_mem_realloc(s->items, capacity * sizeof(*s->items));
        if (!p) return false;
        s->items = (xx_component_member *)p;
        s->capacity = capacity;
    }
    m = &s->items[s->count];
    xx_mem_zero(m, sizeof(*m));
    m->offset = f->base_address + rel;
    m->size = size;
    n = s->count + 1U;
    for (i = 0; i < 6U; ++i) {
        number[5U - i] = (char)('0' + n % 10U);
        n /= 10U;
    }
    number[6] = '-';
    xx_rt_memcpy(m->name, number, 7U);
    for (i = 0; kind[i] && i < sizeof(m->name) - 12U; ++i) {
        unsigned char c = (unsigned char)kind[i];
        m->name[7U + i] = (char)(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') ? c : '_');
    }
    xx_rt_memcpy(m->name + 7U + i, ".bin", 5U);
    ++s->count;
    return true;
}

static void xx_components_free(void *ptr)
{
    xx_component_stream *s = (xx_component_stream *)ptr;
    if (s) {
        xx_mem_free(s->items);
        xx_mem_free(s);
    }
}

static bool xx_components_record(xx_archive_record_state *state)
{
    xx_component_stream *s = (xx_component_stream *)state->internal_state;
    xx_component_member *m;
    if (!s || s->index >= s->count) return false;
    m = &s->items[s->index];
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->current_record.data_offset = m->offset;
    state->current_record.compressed_size = m->size;
    if (m->has_crc32 && !xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_CRC32, m->crc32)) return false;
    return xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_ORIGINAL_NAME, m->name) &&
           xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)m->size) &&
           xx_archive_record_set_meta_bool(&state->current_record, XX_META_ID_IS_FOLDER, false);
}

static xx_archive_record_state *xx_components_create(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_component_stream *s;
    size_t i;
    if (!f || xx_pd_is_stopped(pd) || (!f->base_info_handled && !xx_format_handle_base_info(f, pd))) return NULL;
    s = (xx_component_stream *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_mem_zero(s, sizeof(*s));
    if (!xx_components_build(f, s, pd) || !s->count) {
        xx_components_free(s);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_components_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, f);
    state->internal_state = s;
    state->free_internal = xx_components_free;
    state->total_records = (int64_t)s->count;
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *in = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        xx_meta out;
        xx_mem_zero(&out, sizeof(out));
        out.meta_id = in->meta_id;
        xx_var_init(&out.var);
        if (!xx_var_copy(&out.var, &in->var) || !xx_list_append(&state->options, &out)) {
            xx_var_cleanup(&out.var);
            xx_archive_record_state_free(state);
            return NULL;
        }
    }
    state->has_record = xx_components_record(state);
    if (!state->has_record) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

static const xx_archive_record *xx_components_current(Abstractformat *f, xx_archive_record_state *state)
{
    return state && state->format == f && state->has_record ? &state->current_record : NULL;
}
static bool xx_components_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_component_stream *s;
    if (!f || !state || state->format != f || xx_pd_is_stopped(pd) || !(s = (xx_component_stream *)state->internal_state)) return false;
    ++s->index;
    ++state->current_index;
    state->has_record = xx_components_record(state);
    return state->has_record;
}
static void xx_components_close(Abstractformat *f, xx_archive_record_state *state)
{
    (void)f;
    xx_archive_record_state_free(state);
}

static bool xx_components_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_component_stream *s;
    xx_component_member *m;
    const xx_var *option;
    const char *base = NULL;
    char *owned = NULL, *path = NULL;
    xx_io_device *out = NULL;
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buffer = NULL;
    int64_t done = 0;
    uint32_t crc;
    bool ok = false, created = false;
    if (!f || !state || state->format != f || !state->has_record || xx_pd_is_stopped(pd) || !(s = (xx_component_stream *)state->internal_state) || s->index >= s->count)
        return false;
    m = &s->items[s->index];
    crc = m->crc32_seed;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)m->size > xx_var_get_u64(option)) return false;
    option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (option) {
        if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
        else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
            owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
            base = owned;
        }
        if (!base) goto done;
        path = xx_str_concat3(base, "/", m->name);
        if (!path || !xx_store_create_dirs_a(path, false)) goto done;
        option = xx_format_resolve_extra_parameter(f, &state->options, XX_META_ID_OPT_OVERWRITE);
        if ((!option || !xx_var_get_bool(option)) && xx_io_file_exists_a(path)) goto done;
        out = xx_io_file_open(path, "wb");
        if (!out) goto done;
        created = true;
    }
    if (m->size > 0 && !(buffer = (uint8_t *)xx_mem_alloc(capacity))) goto done;
    while (done < m->size) {
        size_t size = (uint64_t)(m->size - done) < capacity ? (size_t)(m->size - done) : capacity;
        size_t written = 0;
        if (xx_pd_is_stopped(pd) || !xx_component_read(f, m->offset - f->base_address + done, buffer, size)) goto done;
        if (m->has_crc32) crc = xx_crc32_calc(crc, buffer, size);
        while (out && written < size) {
            ssize_t n = xx_io_write(out, buffer + written, size - written);
            if (n <= 0 || (size_t)n > size - written) goto done;
            written += (size_t)n;
        }
        done += (int64_t)size;
    }
    ok = !xx_pd_is_stopped(pd) && (!m->has_crc32 || crc == m->crc32);
done:
    if (buffer) xx_mem_free(buffer);
    if (out && xx_io_close(out) != 0) ok = false;
    if (!ok && created && path) xx_rt_remove(path);
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}

static void xx_components_install(Abstractformat *f)
{
    f->create_archive_records_reading = xx_components_create;
    f->get_current_archive_record = xx_components_current;
    f->archive_record_move_to_next = xx_components_next;
    f->unpack_current_archive_record = xx_components_unpack;
    f->free_archive_records_reading = xx_components_close;
}
static bool xx_components_finish(Abstractformat *f, xx_pd_struct *pd)
{
    xx_component_stream s;
    bool ok;
    xx_mem_zero(&s, sizeof(s));
    ok = !xx_pd_is_stopped(pd) && xx_components_build(f, &s, pd);
    f->number_of_archive_records = ok ? s.count : 0;
    f->is_archive = ok && s.count != 0;
    if (f->is_archive) f->format_type = XX_TYPE_ARCHIVE;
    xx_mem_free(s.items);
    if (!ok) {
        f->is_valid = false;
        f->base_info_handled = false;
    }
    return ok;
}
