/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * id Software VSWAP chunk files -- the wall/sprite/sound pool shipped with
 * Wolfenstein 3D, Spear of Destiny and the Blake Stone games (VSWAP.WL1,
 * VSWAP.BS1, ...).  Ported from XArchive's games/xwolfvswap.cpp and
 * cross-checked against U3's Wolf_FT handler (class ygb, VMT 00669648).
 *
 *   header, 6 bytes at offset 0:
 *     0x00   2  u16 LE   chunk count
 *     0x02   2  u16 LE   index of the first sprite chunk
 *     0x04   2  u16 LE   index of the first sound chunk
 *
 *   then two parallel tables, both indexed by chunk number:
 *     0x06              count * 4   u32 LE   absolute chunk offset
 *     0x06 + count*4    count * 2   u16 LE   chunk length in bytes
 *
 * A slot with offset 0 and length 0 is an unused hole and is skipped.
 *
 * There is no magic.  Recognition is entirely structural, and deliberately
 * strict: sprite_start <= sound_start <= count, the whole table must fit,
 * every live chunk must start at or after the end of the table, chunks must
 * appear in strictly non-overlapping ascending order, and the file may end
 * with at most 4096 bytes of all-zero sector padding.  Anything else is
 * rejected -- an arbitrary overlay must not be able to turn a table-shaped
 * file into a VSWAP archive.
 *
 * Chunks are stored; the container compresses nothing.  Names are synthesised
 * from the chunk class the two start indices imply (wall / sprite / sound).
 *
 * All 3 corpus samples in F:\ARC\ARC\WOLF_FT parse.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wolfft/xx_wolfft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef WOLFFT
#define XX_WOLFFT_FILE_TYPE XX_FILE_TYPE_WOLFFT
#else
#define XX_WOLFFT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_WOLFFT_METHOD_STORE 0U
#define XX_WOLFFT_HEADER_SIZE 6
/* The 16-bit count is its own ceiling; the table-fits-in-the-file check is
 * what actually bounds the allocation. */
#define XX_WOLFFT_MAX_CHUNKS 0xffffU
/* Original WL1 data sets are sector padded after their last chunk.  Accept a
 * small all-zero tail and nothing else. */
#define XX_WOLFFT_MAX_TAIL 4096

typedef struct xx_wolfft_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_wolfft_member;

typedef struct xx_wolfft_stream_s {
    xx_wolfft_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_wolfft_stream;

static void xx_wolfft_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_wolfft_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_wolfft_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_wolfft_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_wolfft_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_wolfft_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_wolfft_path_safe(const char *path) {
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
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

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_wolfft_make_name(const uint8_t *raw, size_t size,
                                 bool keep_path) {
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U &&
           (text[length - 1U] == ' ' || text[length - 1U] == '.' ||
            text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_wolfft_stream_free(void *pointer) {
    xx_wolfft_stream *stream = (xx_wolfft_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_wolfft_add(xx_wolfft_stream *stream,
                          const xx_wolfft_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_wolfft_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_wolfft_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Chunks carry no names.  The two start indices in the header split the pool
 * into walls, sprites and sounds; the reference implementation files each
 * chunk under its class, and so does this reader. */
static char *xx_wolfft_chunk_name(uint32_t index, uint32_t sprite_start,
                                  uint32_t sound_start) {
    const char *kind = index < sprite_start
                           ? "wall"
                           : (index < sound_start ? "sprite" : "sound");
    char text[48];
    size_t length = 0U;
    size_t at = 0U;
    uint32_t scale = 1000U;

    while (kind[at]) text[length++] = kind[at++];
    text[length++] = '/';
    at = 0U;
    while (kind[at]) text[length++] = kind[at++];
    text[length++] = '_';
    while (index / scale >= 10U && scale <= 100000000U) scale *= 10U;
    for (; scale != 0U; scale /= 10U) {
        text[length++] = (char)('0' + ((index / scale) % 10U));
    }
    text[length++] = '.';
    text[length++] = 'b';
    text[length++] = 'i';
    text[length++] = 'n';
    text[length] = 0;
    return xx_str_dup(text);
}

/* The tail after the last chunk must be all zero or the file is not ours. */
static bool xx_wolfft_tail_is_zero(Abstractformat *self, int64_t offset,
                                   int64_t size) {
    uint8_t buffer[512];

    while (size > 0) {
        size_t chunk = size > (int64_t)sizeof(buffer) ? sizeof(buffer)
                                                      : (size_t)size;
        size_t index;
        if (!xx_wolfft_read_at(self, offset, buffer, chunk)) return false;
        for (index = 0U; index < chunk; ++index) {
            if (buffer[index] != 0x00U) return false;
        }
        offset += (int64_t)chunk;
        size -= (int64_t)chunk;
    }
    return true;
}


/* --------------------------------------------------------------- parse -- */

static xx_wolfft_stream *xx_wolfft_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_wolfft_stream *stream = NULL;
    uint8_t head[XX_WOLFFT_HEADER_SIZE];
    uint8_t *table = NULL;
    const uint8_t *offsets;
    const uint8_t *lengths;
    int64_t total;
    int64_t span;
    int64_t table_size;
    int64_t previous_end;
    uint32_t count;
    uint32_t sprite_start;
    uint32_t sound_start;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_WOLFFT_HEADER_SIZE + 6) return NULL;
    if (!xx_wolfft_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }

    count = xx_wolfft_le16(head);
    sprite_start = xx_wolfft_le16(head + 2);
    sound_start = xx_wolfft_le16(head + 4);
    /* The two class boundaries have to be ordered and inside the pool; that
     * is the only self-consistency the header itself offers. */
    if (count == 0U || count > XX_WOLFFT_MAX_CHUNKS ||
        sprite_start > sound_start || sound_start > count) {
        return NULL;
    }
    /* Bound the table against the real file before allocating it. */
    table_size = XX_WOLFFT_HEADER_SIZE + (int64_t)count * 6;
    if (table_size > span) return NULL;

    table = (uint8_t *)xx_mem_alloc((size_t)count * 6U);
    if (!table) return NULL;
    if (!xx_wolfft_read_at(self, self->base_address + XX_WOLFFT_HEADER_SIZE,
                           table, (size_t)count * 6U)) {
        goto fail;
    }
    offsets = table;
    lengths = table + (size_t)count * 4U;

    stream = (xx_wolfft_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    previous_end = table_size;
    for (index = 0U; index < count; ++index) {
        int64_t offset = (int64_t)xx_wolfft_le32(offsets + (size_t)index * 4U);
        int64_t size = (int64_t)xx_wolfft_le16(lengths + (size_t)index * 2U);
        xx_wolfft_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset == 0 && size == 0) continue; /* unused slot */
        /* Chunks must be inside the file, never overlap the table, and run in
         * ascending order; the ordering check is what keeps an arbitrary
         * table-shaped file from being read as an archive. */
        if (offset < table_size || size <= 0 || offset > span ||
            size > span - offset || offset < previous_end) {
            goto fail;
        }
        previous_end = offset + size;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_wolfft_chunk_name(index, sprite_start, sound_start);
        if (!member.name) goto fail;
        member.header_offset =
            self->base_address + XX_WOLFFT_HEADER_SIZE + (int64_t)index * 4;
        member.header_size = 4;
        member.data_offset = self->base_address + offset;
        member.packed_size = size;
        member.unpacked_size = (uint64_t)size;
        member.method = XX_WOLFFT_METHOD_STORE;
        if (!xx_wolfft_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    if (stream->count == 0U || previous_end > span) goto fail;
    /* Only a small all-zero sector tail is tolerated after the last chunk. */
    if (span - previous_end > XX_WOLFFT_MAX_TAIL ||
        !xx_wolfft_tail_is_zero(self, self->base_address + previous_end,
                                span - previous_end)) {
        goto fail;
    }

    xx_mem_free(table);
    stream->archive_size = span;
    return stream;

fail:
    if (table) xx_mem_free(table);
    xx_wolfft_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read; the length
 * comes from offsets parse already proved lie inside the file. */
static bool xx_wolfft_decode(Abstractformat *self,
                             const xx_wolfft_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {{
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->packed_size == 0) return true;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!output) return false;
    if (!xx_wolfft_read_at(self, member->data_offset, output,
                           (size_t)member->packed_size)) {{
        xx_mem_free(output);
        return false;
    }}
    *out = output;
    *out_size = (size_t)member->packed_size;
    return true;
}}


/* ---------------------------------------------------------- lifecycle --- */

void xx_wolfft_init(xx_wolfft *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WOLFFT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-wolf-vswap");
    xx_format_set_extension(&archive->format, "wl1");
    archive->format.check_is_valid = xx_wolfft_check_is_valid;
    archive->format.handle_base_info = xx_wolfft_handle_base_info;
    archive->format.get_format_size = xx_wolfft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wolfft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wolfft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wolfft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wolfft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wolfft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wolfft_free_archive_records_reading;
    archive->format.destroy = xx_wolfft_vtable_destroy;
}

xx_wolfft *xx_wolfft_create(xx_io_device *device, int64_t base_address) {
    xx_wolfft *archive = (xx_wolfft *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_wolfft_init(archive, device, base_address);
    return archive;
}

void xx_wolfft_destroy(xx_wolfft *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_wolfft_free(xx_wolfft *archive) {
    if (!archive) return;
    xx_wolfft_destroy(archive);
    xx_mem_free(archive);
}

static void xx_wolfft_vtable_destroy(Abstractformat *self) {
    xx_wolfft_destroy((xx_wolfft *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_wolfft_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_wolfft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_wolfft_parse(self, pd);
    if (!stream) return false;
    xx_wolfft_stream_free(stream);
    return true;
}

bool xx_wolfft_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_wolfft *archive = (xx_wolfft *)self;
    xx_wolfft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_wolfft_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_wolfft_stream_free(stream);
    return true;
}

int64_t xx_wolfft_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_wolfft_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_wolfft *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_wolfft_set_record(xx_archive_record *record,
                                 const xx_wolfft_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                        member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_wolfft_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_wolfft_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_wolfft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_wolfft_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_wolfft_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_wolfft_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_wolfft_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_wolfft_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_wolfft_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_wolfft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wolfft_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_wolfft_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_wolfft_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_wolfft_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_wolfft_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_wolfft_stream *stream;
    const xx_wolfft_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_wolfft_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_wolfft_path_safe(member->name)) return false;

    path_option =
        xx_wolfft_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_wolfft_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_wolfft_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_wolfft_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
