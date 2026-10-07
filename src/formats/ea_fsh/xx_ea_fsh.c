/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Electronic Arts FSH / SHPI shape packs (.fsh). xx_ea_fsh.h carries the
 * field table.
 *
 * The pack is a flat directory of (tag, offset) pairs with no sizes, so an
 * entry's extent is inferred: it runs to the next HIGHER entry offset, and
 * the highest entry runs to the declared pack size. The reference extractor
 * (gaup_pro.wcx) instead takes the next entry in directory order and drops
 * an entry whose successor lies in front of it; on every pack whose
 * directory is sorted -- which is what EA's tools write -- both rules agree.
 *
 * Member names are built here, not copied: the NUL-padded 4-byte tag, with
 * every byte that is not printable ASCII or not legal in a file name turned
 * into '_', a '_' prefixed to Windows device names, then "." and the entry
 * index as six digits (the reference tool's naming). The index keeps equal
 * tags from overwriting each other.
 *
 * Every offset, count and size is bounded by the device before it is used;
 * the directory is read in fixed-size chunks and the extents are sorted with
 * an in-place heapsort, so nothing recurses and nothing allocates beyond
 * one record per directory entry the file can actually hold.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ea_fsh/xx_ea_fsh.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef EA_FSH
#define XX_EA_FSH_FILE_TYPE XX_FILE_TYPE_EA_FSH
#else
#define XX_EA_FSH_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_EA_FSH_HEADER_SIZE 16
#define XX_EA_FSH_ENTRY_SIZE 8
#define XX_EA_FSH_BITMAP_HEADER 16
/* Indices are printed with six digits; real packs hold at most a few
 * thousand entries. */
#define XX_EA_FSH_MAX_MEMBERS 999999U
#define XX_EA_FSH_DIR_CHUNK 512U

typedef struct xx_ea_fsh_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t size;
    uint32_t index;
} xx_ea_fsh_member;

typedef struct xx_ea_fsh_stream_s {
    xx_ea_fsh_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t declared_size;
    bool truncated;
} xx_ea_fsh_stream;

static void xx_ea_fsh_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ea_fsh_read_from(xx_io_device *dev, int64_t offset,
                                uint8_t *out, size_t size) {
    size_t done = 0U;

    if (!dev || offset < 0 || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t received = xx_io_read(dev, out + done, size - done);
        if (received <= 0 || (size_t)received > size - done) return false;
        done += (size_t)received;
    }
    return true;
}

static bool xx_ea_fsh_is_magic(const uint8_t *h) {
    return h[0] == 'S' && h[1] == 'H' && h[2] == 'P' &&
           (h[3] == 'I' || h[3] == 'P' || h[3] == 'S' || h[3] == 'X');
}

static char xx_ea_fsh_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool xx_ea_fsh_is_device_stem(const char *stem, size_t len) {
    char u[4];
    size_t i;

    if (len != 3U && len != 4U) return false;
    for (i = 0U; i < len; ++i) u[i] = xx_ea_fsh_upper(stem[i]);
    if (len == 3U) {
        return !xx_rt_memcmp(u, "CON", 3U) || !xx_rt_memcmp(u, "PRN", 3U) ||
               !xx_rt_memcmp(u, "AUX", 3U) || !xx_rt_memcmp(u, "NUL", 3U);
    }
    return (!xx_rt_memcmp(u, "COM", 3U) || !xx_rt_memcmp(u, "LPT", 3U)) &&
           u[3] >= '1' && u[3] <= '9';
}

/* Last line of defence: the names built below always pass this. */
static bool xx_ea_fsh_path_safe(const char *name) {
    size_t len, i, stem;

    if (!name || !*name) return false;
    len = xx_str_len(name);
    for (i = 0U; i < len; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32U || c >= 127U || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            return false;
        }
    }
    if ((len == 1U && name[0] == '.') ||
        (len == 2U && name[0] == '.' && name[1] == '.') ||
        name[len - 1U] == '.' || name[len - 1U] == ' ') {
        return false;
    }
    for (stem = 0U; stem < len && name[stem] != '.'; ++stem) {
    }
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    if (xx_ea_fsh_is_device_stem(name, stem)) return false;
    /* CONIN$ / CONOUT$ / CLOCK$ cannot arise from a 4-byte tag with '$'
     * kept, but refuse them anyway. */
    if (stem >= 6U) {
        char u[7];
        size_t k, n = stem < 7U ? stem : 7U;
        for (k = 0U; k < n; ++k) u[k] = xx_ea_fsh_upper(name[k]);
        if ((stem == 6U && (!xx_rt_memcmp(u, "CONIN$", 6U) ||
                            !xx_rt_memcmp(u, "CLOCK$", 6U))) ||
            (stem == 7U && !xx_rt_memcmp(u, "CONOUT$", 7U))) {
            return false;
        }
    }
    return true;
}

static char *xx_ea_fsh_make_name(const uint8_t *tag, uint32_t index) {
    char buffer[32];
    size_t len = 0U, at = 0U, i;
    char clean[5];

    while (len < 4U && tag[len]) ++len;
    for (i = 0U; i < len; ++i) {
        unsigned char c = tag[i];
        if (c < 32U || c >= 127U || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            c = '_';
        }
        clean[i] = (char)c;
    }
    if (len == 0U) {
        clean[0] = '_';
        len = 1U;
    }
    clean[len] = '\0';
    {
        /* A device stem is whatever precedes the first '.', trailing
         * spaces dropped -- "CON" and "CON." both qualify. */
        size_t stem = 0U;
        while (stem < len && clean[stem] != '.') ++stem;
        while (stem > 0U && clean[stem - 1U] == ' ') --stem;
        if (xx_ea_fsh_is_device_stem(clean, stem)) buffer[at++] = '_';
    }
    for (i = 0U; i < len; ++i) buffer[at++] = clean[i];
    (void)xx_rt_snprintf(buffer + at, sizeof(buffer) - at, ".%06u",
                         (unsigned)index);
    return xx_str_dup(buffer);
}

static void xx_ea_fsh_stream_free(void *pointer) {
    xx_ea_fsh_stream *s = (xx_ea_fsh_stream *)pointer;
    size_t i;

    if (!s) return;
    for (i = 0U; i < s->count; ++i) xx_str_free(s->items[i].name);
    xx_mem_free(s->items);
    xx_mem_free(s);
}

/* Heapsort of member indices by (data_offset, index). */
static bool xx_ea_fsh_less(const xx_ea_fsh_member *m, uint32_t a, uint32_t b) {
    return m[a].data_offset < m[b].data_offset ||
           (m[a].data_offset == m[b].data_offset && m[a].index < m[b].index);
}

static void xx_ea_fsh_sift(const xx_ea_fsh_member *m, uint32_t *order,
                           size_t root, size_t n) {
    for (;;) {
        size_t child = root * 2U + 1U;
        uint32_t t;
        if (child >= n) return;
        if (child + 1U < n && xx_ea_fsh_less(m, order[child], order[child + 1U])) {
            ++child;
        }
        if (!xx_ea_fsh_less(m, order[root], order[child])) return;
        t = order[root];
        order[root] = order[child];
        order[child] = t;
        root = child;
    }
}

static void xx_ea_fsh_sort(const xx_ea_fsh_member *m, uint32_t *order,
                           size_t n) {
    size_t i;

    if (n < 2U) return;
    for (i = n / 2U; i > 0U; --i) xx_ea_fsh_sift(m, order, i - 1U, n);
    for (i = n - 1U; i > 0U; --i) {
        uint32_t t = order[0];
        order[0] = order[i];
        order[i] = t;
        xx_ea_fsh_sift(m, order, 0U, i);
    }
}

static xx_ea_fsh_stream *xx_ea_fsh_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    uint8_t header[XX_EA_FSH_HEADER_SIZE];
    uint8_t *chunk = NULL;
    uint32_t *order = NULL;
    xx_ea_fsh_stream *s = NULL;
    int64_t total, span, declared, dir_end, end, next;
    uint32_t count, i;
    size_t k;

    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return NULL;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_EA_FSH_HEADER_SIZE ||
        !xx_ea_fsh_read_from(self->device, self->base_address, header,
                             sizeof(header)) ||
        !xx_ea_fsh_is_magic(header)) {
        return NULL;
    }
    /* The directory id is four printable ASCII characters. */
    for (k = 12U; k < 16U; ++k) {
        if (header[k] < 0x20U || header[k] > 0x7EU) return NULL;
    }
    declared = (int64_t)xx_data_get_u32(header + 4, 4, 0, false);
    count = xx_data_get_u32(header + 8, 4, 0, false);
    if (count > XX_EA_FSH_MAX_MEMBERS) return NULL;
    dir_end = XX_EA_FSH_HEADER_SIZE + (int64_t)count * XX_EA_FSH_ENTRY_SIZE;
    if (declared < dir_end || dir_end > span) return NULL;
    end = declared <= span ? declared : span;
    /* Every entry needs at least its bitmap header inside the pack. */
    if (count != 0U && end - dir_end < XX_EA_FSH_BITMAP_HEADER) return NULL;

    s = (xx_ea_fsh_stream *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_mem_zero(s, sizeof(*s));
    s->declared_size = declared;
    s->truncated = declared > span;
    s->archive_size = end;
    if (count == 0U) return s;

    s->items = (xx_ea_fsh_member *)xx_mem_alloc((size_t)count *
                                                sizeof(xx_ea_fsh_member));
    order = (uint32_t *)xx_mem_alloc((size_t)count * sizeof(uint32_t));
    chunk = (uint8_t *)xx_mem_alloc(XX_EA_FSH_DIR_CHUNK * XX_EA_FSH_ENTRY_SIZE);
    if (!s->items || !order || !chunk) goto fail;
    xx_mem_zero(s->items, (size_t)count * sizeof(xx_ea_fsh_member));

    for (i = 0U; i < count;) {
        uint32_t n = count - i, j;
        if (n > XX_EA_FSH_DIR_CHUNK) n = XX_EA_FSH_DIR_CHUNK;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_ea_fsh_read_from(self->device,
                                 self->base_address + XX_EA_FSH_HEADER_SIZE +
                                     (int64_t)i * XX_EA_FSH_ENTRY_SIZE,
                                 chunk, (size_t)n * XX_EA_FSH_ENTRY_SIZE)) {
            goto fail;
        }
        for (j = 0U; j < n; ++j, ++i) {
            const uint8_t *e = chunk + (size_t)j * XX_EA_FSH_ENTRY_SIZE;
            int64_t offset = (int64_t)xx_data_get_u32(e + 4, 4, 0, false);
            xx_ea_fsh_member *m = &s->items[i];
            if (offset < dir_end || offset > end - XX_EA_FSH_BITMAP_HEADER) {
                goto fail;
            }
            m->name = xx_ea_fsh_make_name(e, i);
            if (!m->name) goto fail;
            s->count = (size_t)i + 1U;
            m->index = i;
            m->header_offset = self->base_address + XX_EA_FSH_HEADER_SIZE +
                               (int64_t)i * XX_EA_FSH_ENTRY_SIZE;
            m->data_offset = offset; /* relative until the sizes are set */
            order[i] = i;
        }
    }

    xx_ea_fsh_sort(s->items, order, count);
    next = end;
    for (k = count; k > 0U; --k) {
        xx_ea_fsh_member *m = &s->items[order[k - 1U]];
        if (k < count &&
            s->items[order[k]].data_offset > m->data_offset) {
            next = s->items[order[k]].data_offset;
        }
        m->size = next - m->data_offset;
    }
    for (k = 0U; k < count; ++k) {
        s->items[k].data_offset += self->base_address;
    }
    xx_mem_free(order);
    xx_mem_free(chunk);
    return s;

fail:
    xx_mem_free(order);
    xx_mem_free(chunk);
    xx_ea_fsh_stream_free(s);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ea_fsh_init(xx_ea_fsh *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_EA_FSH_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ea-fsh");
    xx_format_set_extension(&archive->format, "fsh");
    archive->format.check_is_valid = xx_ea_fsh_check_is_valid;
    archive->format.handle_base_info = xx_ea_fsh_handle_base_info;
    archive->format.get_format_size = xx_ea_fsh_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ea_fsh_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ea_fsh_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ea_fsh_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ea_fsh_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ea_fsh_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ea_fsh_free_archive_records_reading;
    archive->format.destroy = xx_ea_fsh_vtable_destroy;
}

xx_ea_fsh *xx_ea_fsh_create(xx_io_device *device, int64_t base_address) {
    xx_ea_fsh *archive = (xx_ea_fsh *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ea_fsh_init(archive, device, base_address);
    return archive;
}

void xx_ea_fsh_destroy(xx_ea_fsh *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ea_fsh_free(xx_ea_fsh *archive) {
    if (!archive) return;
    xx_ea_fsh_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ea_fsh_vtable_destroy(Abstractformat *self) {
    xx_ea_fsh_destroy((xx_ea_fsh *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ea_fsh_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ea_fsh_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ea_fsh_parse(self, pd);
    if (!stream) return false;
    xx_ea_fsh_stream_free(stream);
    return true;
}

bool xx_ea_fsh_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ea_fsh *archive = (xx_ea_fsh *)self;
    xx_ea_fsh_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ea_fsh_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->declared_size = stream->declared_size;
    archive->truncated = stream->truncated;
    xx_ea_fsh_stream_free(stream);
    return true;
}

int64_t xx_ea_fsh_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ea_fsh_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ea_fsh *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ea_fsh_set_record(xx_archive_record *record,
                                 const xx_ea_fsh_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = XX_EA_FSH_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ea_fsh_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ea_fsh_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_ea_fsh_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ea_fsh_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ea_fsh_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ea_fsh_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ea_fsh_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ea_fsh_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ea_fsh_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ea_fsh_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ea_fsh_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ea_fsh_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ea_fsh_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ea_fsh_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

/* Verification without a destination: read the member through. */
static bool xx_ea_fsh_verify(Abstractformat *self,
                             const xx_ea_fsh_member *member,
                             xx_pd_struct *pd) {
    uint8_t buffer[4096];
    int64_t done = 0;

    while (done < member->size) {
        int64_t left = member->size - done;
        size_t n = left < (int64_t)sizeof(buffer) ? (size_t)left
                                                  : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_ea_fsh_read_from(self->device, member->data_offset + done,
                                 buffer, n)) {
            return false;
        }
        done += (int64_t)n;
    }
    return true;
}

bool xx_ea_fsh_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ea_fsh_stream *stream;
    const xx_ea_fsh_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ea_fsh_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ea_fsh_path_safe(member->name)) return false;

    path_option = xx_ea_fsh_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return xx_ea_fsh_verify(self, member, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    /* xx_store_unpack_device_to_file removes its own output on failure,
     * and only when it created it. */
    result = xx_store_create_dirs_a(target_path, false) &&
             xx_store_unpack_device_to_file(self->device, member->data_offset,
                                            member->size, target_path, pd);
    xx_str_free(target_path);
    return result;
}

void xx_ea_fsh_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
