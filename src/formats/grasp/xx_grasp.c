/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GRASP / GL animation libraries (".GL", also ".GLB", ".TLB" and plain ".DAT"
 * in the corpus) as produced by Paul Mace Software's GRASP and by Microsoft
 * Multimedia tooling built on it.  The layout below was cross-checked against
 * Deark's graspgl module (XArchive/Algos/xdearkmodule_grasp_p.cpp) and against
 * the 43 samples in F:\ARC\ARC\GRASP.
 *
 *   header, 2 bytes at offset 0:
 *     0x00   2  u16 LE   index size in bytes, always a non-zero multiple of 17
 *
 *   index, immediately after the header, index_size/17 entries of 17 bytes:
 *     +0x00  4  u32 LE   ABSOLUTE file offset of the member's data block, or 0
 *                        for the end-of-list marker that closes every sample
 *     +0x04 13  char[13] member name, NUL padded
 *
 *   data block, at the offset above:
 *     +0x00  4  u32 LE   member length
 *     +0x04  n  bytes    member body, stored verbatim
 *
 * THERE IS NO MAGIC, so the arithmetic has to carry the whole identification.
 * The reader requires: a non-zero index size that is an exact multiple of 17
 * and fits in the file; a first entry that points at the very byte after the
 * index; every data block bounded by the file; members laid out strictly
 * contiguously, each one starting where the previous one's body ended; and the
 * final member ending exactly at end-of-file.  All 43 corpus samples satisfy
 * every one of those, and Deark's identifier uses the weaker first two alone.
 * Nothing here is optional: without the contiguity and EOF checks a two-byte
 * length prefix would match far too much.
 *
 * The big-endian Amiga variant (the "AG\x01\x00" prefix Deark knows about) is
 * deliberately not accepted - no sample of it exists in the corpus and it
 * cannot be validated here.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/grasp/xx_grasp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef GRASP
#define XX_GRASP_FILE_TYPE XX_FILE_TYPE_GRASP
#else
#define XX_GRASP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_GRASP_METHOD_STORE 0U
#define XX_GRASP_ENTRY_SIZE 17
#define XX_GRASP_NAME_SIZE 13
#define XX_GRASP_MIN_SIZE (2 + XX_GRASP_ENTRY_SIZE + 4)
/* u16 index size bounds the table at 65535/17 = 3855 entries; the cap merely
 * makes that explicit. */
#define XX_GRASP_MAX_MEMBERS 3855U

typedef struct xx_grasp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
} xx_grasp_member;

typedef struct xx_grasp_stream_s {
    xx_grasp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_grasp_stream;

static void xx_grasp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_grasp_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_grasp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_grasp_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_grasp_path_safe(const char *name) {
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

/* The 13 raw name bytes are CP437 text, NUL padded and not guaranteed to be
 * NUL terminated.  Bytes a filesystem would object to become '_'; a name that
 * ends up empty is replaced rather than rejected, because an unreadable name
 * is a naming problem and not a structural one. */
static char *xx_grasp_make_name(const uint8_t *raw) {
    char text[XX_GRASP_NAME_SIZE + 1];
    size_t length = 0U;
    size_t index;

    while (length < XX_GRASP_NAME_SIZE && raw[length] != 0x00U) ++length;
    while (length != 0U && raw[length - 1U] == 0x20U) --length;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            text[index] = '_';
        } else {
            text[index] = (char)c;
        }
    }
    while (length != 0U && text[length - 1U] == '.') --length;
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return xx_str_dup(text);
}

static void xx_grasp_stream_free(void *pointer) {
    xx_grasp_stream *stream = (xx_grasp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_grasp_stream *xx_grasp_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_grasp_stream *stream = NULL;
    uint8_t head[2];
    uint8_t *index = NULL;
    int64_t total;
    int64_t span;
    int64_t index_size;
    int64_t data_start;
    int64_t cursor;
    uint64_t slots;
    uint64_t position;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_GRASP_MIN_SIZE) return NULL;
    if (!xx_grasp_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }

    /* The index size is the first thing read and the first thing bounded;
     * every allocation below is derived from it. */
    index_size = (int64_t)xx_grasp_le16(head);
    if (index_size == 0 || index_size % XX_GRASP_ENTRY_SIZE != 0) return NULL;
    if (index_size > span - 2) return NULL;
    slots = (uint64_t)(index_size / XX_GRASP_ENTRY_SIZE);
    if (slots == 0U || slots > XX_GRASP_MAX_MEMBERS) return NULL;
    data_start = 2 + index_size;

    index = (uint8_t *)xx_mem_alloc((size_t)index_size);
    if (!index) return NULL;
    if (!xx_grasp_read_at(self, self->base_address + 2, index,
                          (size_t)index_size)) {
        goto fail;
    }

    stream = (xx_grasp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_grasp_member *)xx_mem_alloc(sizeof(*stream->items) *
                                                    (size_t)slots);
    if (!stream->items) goto fail;
    xx_mem_zero(stream->items, sizeof(*stream->items) * (size_t)slots);

    cursor = data_start;
    for (position = 0U; position < slots; ++position) {
        const uint8_t *entry =
            index + (size_t)(position * XX_GRASP_ENTRY_SIZE);
        int64_t block = (int64_t)xx_grasp_le32(entry);
        uint8_t length_bytes[4];
        int64_t length;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* A zero offset is the end-of-list marker; every sample carries one,
         * so the loop normally stops here and not at the slot count. */
        if (block == 0) break;
        /* Members are contiguous: the first begins where the index ends and
         * each later one begins where its predecessor's body ended.  A gap or
         * an overlap is not this format. */
        if (block != cursor) goto fail;
        if (block > span - 4) goto fail;
        if (entry[4] == 0x00U) goto fail; /* an entry without a name */
        if (!xx_grasp_read_at(self, self->base_address + block, length_bytes,
                              sizeof(length_bytes))) {
            goto fail;
        }
        length = (int64_t)xx_grasp_le32(length_bytes);
        if (length < 0 || length > span - block - 4) goto fail;

        stream->items[position].name = xx_grasp_make_name(entry + 4);
        if (!stream->items[position].name) goto fail;
        stream->items[position].header_offset =
            self->base_address + 2 +
            (int64_t)(position * XX_GRASP_ENTRY_SIZE);
        stream->items[position].header_size = XX_GRASP_ENTRY_SIZE;
        stream->items[position].data_offset = self->base_address + block + 4;
        stream->items[position].size = length;
        ++stream->count;
        cursor = block + 4 + length;
    }

    /* An index that describes nothing, or one whose members stop short of
     * end-of-file, is not a GL library. */
    if (stream->count == 0U || cursor != span) goto fail;

    xx_mem_free(index);
    stream->archive_size = span;
    return stream;

fail:
    if (index) xx_mem_free(index);
    xx_grasp_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read.  The length
 * comes from offsets that parse already proved lie inside the file. */
static bool xx_grasp_decode(Abstractformat *self,
                            const xx_grasp_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A zero-length member is legitimate - the SINNER corpus carries one -
     * and must extract as an empty file rather than failing the unpack. */
    if (member->size == 0) return true;
    if ((uint64_t)member->size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    if (!xx_grasp_read_at(self, member->data_offset, output,
                          (size_t)member->size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_grasp_init(xx_grasp *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GRASP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-grasp-gl");
    xx_format_set_extension(&archive->format, "gl");
    archive->format.check_is_valid = xx_grasp_check_is_valid;
    archive->format.handle_base_info = xx_grasp_handle_base_info;
    archive->format.get_format_size = xx_grasp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_grasp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_grasp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_grasp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_grasp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_grasp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_grasp_free_archive_records_reading;
    archive->format.destroy = xx_grasp_vtable_destroy;
}

xx_grasp *xx_grasp_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_grasp *archive = (xx_grasp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_grasp_init(archive, device, base_address);
    return archive;
}

void xx_grasp_destroy(xx_grasp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_grasp_free(xx_grasp *archive) {
    if (!archive) return;
    xx_grasp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_grasp_vtable_destroy(Abstractformat *self) {
    xx_grasp_destroy((xx_grasp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_grasp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_grasp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_grasp_parse(self, pd);
    if (!stream) return false;
    xx_grasp_stream_free(stream);
    return true;
}

bool xx_grasp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_grasp *archive = (xx_grasp *)self;
    xx_grasp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_grasp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_grasp_stream_free(stream);
    return true;
}

int64_t xx_grasp_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_grasp_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_grasp *)self)->number_of_records : 0U;
}


/* ------------------------------------------------------------- records -- */

static bool xx_grasp_set_record(xx_archive_record *record,
                                     const xx_grasp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_GRASP_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_grasp_copy_options(xx_list_s *target,
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

static const xx_var *xx_grasp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_grasp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_grasp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_grasp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_grasp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_grasp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_grasp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_grasp_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_grasp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_grasp_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_grasp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_grasp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_grasp_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_grasp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_grasp_stream *stream;
    const xx_grasp_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_grasp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_grasp_path_safe(member->name)) return false;

    path_option = xx_grasp_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_grasp_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_grasp_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_grasp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
