/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LucasArts GOB archives. A flat bundle of stored files with one member
 * table; there is no compression anywhere in the format, so "unpacking" is a
 * copy and the only thing that can go wrong is a table that points outside
 * the file.
 *
 * Two flavours share the reader because they share the structure and differ
 * only in the signature and the width of the name field.
 *
 * Dark Forces (signature "GOB" 0x0A):
 *
 *   0x00   4  char    "GOB\n"
 *   0x04   4  u32 LE  offset of the member table
 *   table:
 *     +0   4  u32 LE  member count
 *     then count entries of 21 bytes:
 *       +0  4  u32 LE  member offset
 *       +4  4  u32 LE  member size
 *       +8 13  char    name, NUL terminated and NUL padded
 *
 * Jedi Knight / Mysteries of the Sith (signature "GOB "):
 *
 *   0x00   4  char    "GOB "
 *   0x04   4  u32 LE  version, 0x14 in every sample
 *   0x08   4  u32 LE  offset of the member table
 *   table:
 *     +0   4  u32 LE  member count
 *     then count entries of 136 bytes:
 *       +0   4  u32 LE  member offset
 *       +4   4  u32 LE  member size
 *       +8 128  char    name, NUL terminated and NUL padded; may carry a
 *                       directory prefix with DOS backslashes
 *
 * The Dark Forces flavour puts its table at the end of the file and the Jedi
 * Knight flavour puts it at the front, which is why the table offset is a
 * field rather than a constant in either.
 *
 * Every offset and length in the table is checked against the real file size
 * before it is used, and the table's own extent is checked before the count
 * is allowed to drive an allocation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gob/xx_gob.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef GOB
#define XX_GOB_FILE_TYPE XX_FILE_TYPE_GOB
#else
#define XX_GOB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_GOB_DF_HEADER_SIZE 8
#define XX_GOB_JK_HEADER_SIZE 12
#define XX_GOB_DF_NAME_SIZE 13U
#define XX_GOB_JK_NAME_SIZE 128U
#define XX_GOB_DF_ENTRY_SIZE 21
#define XX_GOB_JK_ENTRY_SIZE 136
#define XX_GOB_JK_VERSION 0x14U
/* Res2.gob, the largest sample, lists 3610 members; the cap is the table's
 * own extent in practice, and this is only a belt-and-braces ceiling. */
#define XX_GOB_MAX_MEMBERS 1048576U
#define XX_GOB_FLAVOUR_DF 0U
#define XX_GOB_FLAVOUR_JK 1U
#define XX_GOB_METHOD_STORE 0U
#define XX_GOB_COPY_CHUNK (64 * 1024)

typedef struct xx_gob_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    bool is_folder;
} xx_gob_member;

typedef struct xx_gob_stream_s {
    xx_gob_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_gob_stream;

static void xx_gob_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_gob_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static uint32_t xx_gob_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_gob_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}

static bool xx_gob_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_gob_stream_free(void *pointer) {
    xx_gob_stream *stream = (xx_gob_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_gob_add(xx_gob_stream *stream, const xx_gob_member *member) {
    xx_gob_member *grown = (xx_gob_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Copy a fixed-width, NUL-padded table name into a path that is safe to
 * create. DOS separators become '/', a leading separator or drive letter is
 * dropped, and a byte no file system would take becomes '_'. A name that
 * ends up empty is a reject, not a placeholder: an unnamed member in a table
 * of named ones means the table was not read correctly. */
static char *xx_gob_make_name(const uint8_t *field, size_t field_size) {
    char *name;
    size_t length = 0U;
    size_t output = 0U;
    size_t index;
    size_t component_start;

    while (length < field_size && field[length] != 0U) ++length;
    if (length == 0U) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    index = 0U;
    /* Drop a drive specification such as "c:". */
    if (length >= 2U && field[1] == ':') index = 2U;
    component_start = 0U;
    while (index < length) {
        uint8_t c = field[index++];
        if (c == '\\' || c == '/') {
            /* Collapse runs of separators and refuse "." and ".." parts. */
            size_t part = output - component_start;
            if (part == 0U) continue;
            if (part == 1U && name[component_start] == '.') {
                output = component_start;
                continue;
            }
            if (part == 2U && name[component_start] == '.' &&
                name[component_start + 1U] == '.') {
                xx_str_free(name);
                return NULL;
            }
            name[output++] = '/';
            component_start = output;
            continue;
        }
        if (c < 0x20U || c == 0x7fU || c == ':' || c == '"' || c == '*' ||
            c == '<' || c == '>' || c == '?' || c == '|') {
            c = (uint8_t)'_';
        }
        name[output++] = (char)c;
    }
    while (output != 0U && (name[output - 1U] == '/' ||
                            name[output - 1U] == ' ' ||
                            name[output - 1U] == '.')) {
        --output;
    }
    name[output] = 0;
    if (output == 0U) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

/* ------------------------------------------------------------- parsing -- */

static xx_gob_stream *xx_gob_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_gob_stream *stream = NULL;
    uint8_t header[XX_GOB_JK_HEADER_SIZE];
    uint8_t count_bytes[4];
    uint8_t *table = NULL;
    int64_t total, span, table_offset, table_size;
    int64_t header_size, entry_size;
    uint32_t count, index;
    size_t name_size;
    unsigned flavour;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_GOB_JK_HEADER_SIZE) return NULL;
    if (!xx_gob_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    if (xx_rt_memcmp(header, "GOB\x0a", 4U) == 0) {
        flavour = XX_GOB_FLAVOUR_DF;
        header_size = XX_GOB_DF_HEADER_SIZE;
        entry_size = XX_GOB_DF_ENTRY_SIZE;
        name_size = XX_GOB_DF_NAME_SIZE;
        table_offset = (int64_t)xx_gob_le32(header + 4);
    } else if (xx_rt_memcmp(header, "GOB ", 4U) == 0) {
        flavour = XX_GOB_FLAVOUR_JK;
        header_size = XX_GOB_JK_HEADER_SIZE;
        entry_size = XX_GOB_JK_ENTRY_SIZE;
        name_size = XX_GOB_JK_NAME_SIZE;
        /* The version word is the only other fixed thing this flavour has;
         * pinning it keeps a four-byte signature from being the whole gate. */
        if (xx_gob_le32(header + 4) != XX_GOB_JK_VERSION) return NULL;
        table_offset = (int64_t)xx_gob_le32(header + 8);
    } else {
        return NULL;
    }
    (void)flavour;

    /* The table pointer is attacker-controlled: bound it, then bound the
     * count it leads to, BEFORE either is allowed to size an allocation. */
    if (table_offset < header_size ||
        !xx_gob_range_within(span, table_offset, 4)) {
        return NULL;
    }
    if (!xx_gob_read_at(self, self->base_address + table_offset, count_bytes,
                        sizeof(count_bytes))) {
        return NULL;
    }
    count = xx_gob_le32(count_bytes);
    if (count == 0U || count > XX_GOB_MAX_MEMBERS) return NULL;
    /* count * entry_size cannot overflow: count is bounded above by 2^20 and
     * entry_size by 136. */
    table_size = (int64_t)count * entry_size;
    if (!xx_gob_range_within(span, table_offset + 4, table_size)) return NULL;

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return NULL;
    if (!xx_gob_read_at(self, self->base_address + table_offset + 4, table,
                        (size_t)table_size)) {
        goto fail;
    }

    stream = (xx_gob_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0U; index < count; ++index) {
        const uint8_t *entry = table + (size_t)index * (size_t)entry_size;
        xx_gob_member member;
        int64_t offset = (int64_t)xx_gob_le32(entry);
        int64_t size = (int64_t)xx_gob_le32(entry + 4);
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* A member may not start inside the fixed header and may not run off
         * the end of the file. */
        if (offset < header_size || !xx_gob_range_within(span, offset, size)) {
            goto fail;
        }
        name = xx_gob_make_name(entry + 8, name_size);
        if (!name) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset =
            self->base_address + table_offset + 4 + (int64_t)index * entry_size;
        member.header_size = entry_size;
        member.data_offset = self->base_address + offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        member.method = XX_GOB_METHOD_STORE;
        member.is_folder = false;
        if (!xx_gob_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }
    stream->archive_size = span;
    xx_mem_free(table);
    return stream;

fail:
    xx_mem_free(table);
    xx_gob_stream_free(stream);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_gob_init(xx_gob *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GOB_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lucasarts-gob");
    xx_format_set_extension(&archive->format, "gob");
    archive->format.check_is_valid = xx_gob_check_is_valid;
    archive->format.handle_base_info = xx_gob_handle_base_info;
    archive->format.get_format_size = xx_gob_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gob_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gob_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gob_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gob_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gob_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gob_free_archive_records_reading;
    archive->format.destroy = xx_gob_vtable_destroy;
}

xx_gob *xx_gob_create(xx_io_device *device, int64_t base_address) {
    xx_gob *archive = (xx_gob *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_gob_init(archive, device, base_address);
    return archive;
}

void xx_gob_destroy(xx_gob *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_gob_free(xx_gob *archive) {
    if (!archive) return;
    xx_gob_destroy(archive);
    xx_mem_free(archive);
}

static void xx_gob_vtable_destroy(Abstractformat *self) {
    xx_gob_destroy((xx_gob *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_gob_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gob_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_gob_parse(self, pd);
    if (!stream) return false;
    xx_gob_stream_free(stream);
    return true;
}

bool xx_gob_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gob *archive = (xx_gob *)self;
    xx_gob_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_gob_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_gob_stream_free(stream);
    return true;
}

int64_t xx_gob_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_gob_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_gob *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_gob_set_record(xx_archive_record *record,
                              const xx_gob_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_gob_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_gob_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_gob_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_gob_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_gob_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_gob_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_gob_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_gob_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_gob_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_gob_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gob_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_gob_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_gob_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_gob_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

/* Members are stored, and a GOB can be tens of megabytes, so extraction
 * streams through a fixed buffer rather than materialising a member. */
static bool xx_gob_copy_member(Abstractformat *self,
                               const xx_gob_member *member,
                               xx_io_device *output, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t remaining = member->uncompressed_size;
    int64_t at = member->data_offset;
    bool result = true;

    buffer = (uint8_t *)xx_mem_alloc(XX_GOB_COPY_CHUNK);
    if (!buffer) return false;
    while (result && remaining > 0) {
        size_t piece = remaining > XX_GOB_COPY_CHUNK ? (size_t)XX_GOB_COPY_CHUNK
                                                     : (size_t)remaining;
        size_t completed = 0U;
        if (pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (!xx_gob_read_at(self, at, buffer, piece)) {
            result = false;
            break;
        }
        while (completed < piece) {
            ssize_t sent =
                xx_io_write(output, buffer + completed, piece - completed);
            if (sent <= 0 || (size_t)sent > piece - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        at += (int64_t)piece;
        remaining -= (int64_t)piece;
    }
    xx_mem_free(buffer);
    return result;
}

/* Reading a member without writing it: the same walk, discarding the bytes,
 * so that "unpack with no destination" still proves the extent is readable. */
static bool xx_gob_verify_member(Abstractformat *self,
                                 const xx_gob_member *member,
                                 xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t remaining = member->uncompressed_size;
    int64_t at = member->data_offset;
    bool result = true;

    if (remaining == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(XX_GOB_COPY_CHUNK);
    if (!buffer) return false;
    while (remaining > 0) {
        size_t piece = remaining > XX_GOB_COPY_CHUNK ? (size_t)XX_GOB_COPY_CHUNK
                                                     : (size_t)remaining;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_gob_read_at(self, at, buffer, piece)) {
            result = false;
            break;
        }
        at += (int64_t)piece;
        remaining -= (int64_t)piece;
    }
    xx_mem_free(buffer);
    return result;
}

bool xx_gob_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_gob_stream *stream;
    const xx_gob_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_gob_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_gob_path_safe(member->name)) return false;

    path_option = xx_gob_get_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return xx_gob_verify_member(self, member, pd);

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

    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;

        result = output != NULL;
        if (result) result = xx_gob_copy_member(self, member, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_gob_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
