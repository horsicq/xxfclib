/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Battle Isle .LIB containers (Blue Byte's Battle Isle / History Line engine
 * data libraries; the corpus also carries the same container under .TDY and
 * .EGA names).  The layout below was derived from the 562 samples in
 * F:\ARC\ARC\BattleIsle_FT, not from published documentation - no public
 * description of the container was available.
 *
 *   header, 4 bytes at offset 0:
 *     0x00   4  u32 LE   directory offset == total size of the payload area
 *
 *   optional 8-byte sub-header, present when the four bytes at 0x04 are the
 *   ASCII tag "TCT " (284 of 562 samples):
 *     0x04   4  char[4] "TCT "
 *     0x08   2  u16 LE   member count, ALWAYS equal to the directory count
 *     0x0a   2  bytes    'I', 0 in every sample; not interpreted
 *
 *   payload: member bodies laid out contiguously and in directory order,
 *   starting at 0x04 without the sub-header and at 0x0c with it, and running
 *   up to the directory offset.
 *
 *   directory, at the offset in the header and running to end-of-file, an
 *   array of 12-byte entries:
 *     +0x00  8  char[8] member name, NUL/space padded, NOT NUL-terminated
 *     +0x08  4  u32 LE   ABSOLUTE file offset of the member body
 *
 * There is no stored member size: a member runs to the next entry's offset,
 * and the last one runs to the directory offset.  Member bodies are stored
 * verbatim - the container compresses nothing.
 *
 * THERE IS NO MAGIC.  The only thing that makes this reader safe to run over
 * unrelated files is the arithmetic: the tail must divide exactly into
 * 12-byte entries, the first entry must point at the exact byte the payload
 * area begins at, every offset must land inside the payload area, the offsets
 * must ascend strictly, and - when the "TCT " tag is there - the count in the
 * sub-header must agree with the count the directory size implies.  All 562
 * corpus samples satisfy every one of those.  Loosening any of them turns
 * this reader into a wildcard, so none of them is optional.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/battleisle/xx_battleisle.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef BATTLEISLE
#define XX_BATTLEISLE_FILE_TYPE XX_FILE_TYPE_BATTLEISLE
#else
#define XX_BATTLEISLE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_BATTLEISLE_HEADER_SIZE 4
#define XX_BATTLEISLE_SUB_HEADER_SIZE 12
#define XX_BATTLEISLE_ENTRY_SIZE 12
#define XX_BATTLEISLE_NAME_SIZE 8
#define XX_BATTLEISLE_MIN_SIZE 16
/* 1 Mi entries is 12 MiB of directory; a file that small cannot describe
 * more, and the tail-size arithmetic already bounds the count by the real
 * file size before this cap is even consulted. */
#define XX_BATTLEISLE_MAX_MEMBERS 1048576U
#define XX_BATTLEISLE_METHOD_STORE 0U

typedef struct xx_battleisle_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
} xx_battleisle_member;

typedef struct xx_battleisle_stream_s {
    xx_battleisle_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_battleisle_stream;

static void xx_battleisle_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_battleisle_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_battleisle_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_battleisle_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_battleisle_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The 8 raw name bytes are not a C string and are not even guaranteed to be
 * ASCII: the CHAR*.LIB samples use low control bytes as an index inside an
 * otherwise blank name.  Trailing NUL and space padding is dropped, every
 * byte that a filesystem would object to becomes '_', and a name that ends up
 * empty is replaced rather than rejected - an unreadable name is a naming
 * problem, not a structural one. */
static char *xx_battleisle_make_name(const uint8_t *raw) {
    char text[XX_BATTLEISLE_NAME_SIZE + 1];
    size_t length = XX_BATTLEISLE_NAME_SIZE;
    size_t index;

    while (length != 0U &&
           (raw[length - 1U] == 0x00U || raw[length - 1U] == 0x20U)) {
        --length;
    }
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
    if (length == 0U) {
        text[length++] = '_';
    }
    /* A component may not end in '.' on Windows, and a lone ".." would be a
     * traversal component. */
    while (length != 0U && text[length - 1U] == '.') --length;
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return xx_str_dup(text);
}

static void xx_battleisle_stream_free(void *pointer) {
    xx_battleisle_stream *stream = (xx_battleisle_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_battleisle_stream *xx_battleisle_parse(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    xx_battleisle_stream *stream = NULL;
    uint8_t head[XX_BATTLEISLE_SUB_HEADER_SIZE];
    uint8_t *directory = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t data_start;
    uint64_t count;
    uint64_t index;
    uint32_t previous;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_BATTLEISLE_MIN_SIZE) return NULL;
    if (!xx_battleisle_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }

    /* The directory pointer is the first thing read from the file and the
     * first thing bounded: everything downstream is derived from it. */
    directory_offset = (int64_t)xx_battleisle_le32(head);
    if (directory_offset < XX_BATTLEISLE_HEADER_SIZE ||
        directory_offset >= span) {
        return NULL;
    }
    directory_size = span - directory_offset;
    if (directory_size % XX_BATTLEISLE_ENTRY_SIZE != 0) return NULL;
    count = (uint64_t)(directory_size / XX_BATTLEISLE_ENTRY_SIZE);
    if (count == 0U || count > XX_BATTLEISLE_MAX_MEMBERS) return NULL;

    /* "TCT " moves the payload start by eight bytes AND republishes the
     * member count.  Both facts are checked: a tag whose count disagrees
     * with the directory is not this format. */
    data_start = XX_BATTLEISLE_HEADER_SIZE;
    if (xx_rt_memcmp(head + 4, "TCT ", 4U) == 0) {
        if ((uint64_t)xx_battleisle_le16(head + 8) != count) return NULL;
        data_start = XX_BATTLEISLE_SUB_HEADER_SIZE;
    }
    if (data_start >= directory_offset) return NULL;

    /* directory_size is span minus an offset already proven to be inside the
     * file, so this allocation is bounded by the real file size and by the
     * member cap above - never by a number the header simply claims. */
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_battleisle_read_at(self, self->base_address + directory_offset,
                               directory, (size_t)directory_size)) {
        goto fail;
    }

    stream = (xx_battleisle_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items =
        (xx_battleisle_member *)xx_mem_alloc(sizeof(*stream->items) *
                                             (size_t)count);
    if (!stream->items) goto fail;
    xx_mem_zero(stream->items, sizeof(*stream->items) * (size_t)count);

    previous = 0U;
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry =
            directory + (size_t)(index * XX_BATTLEISLE_ENTRY_SIZE);
        uint32_t offset = xx_battleisle_le32(entry + XX_BATTLEISLE_NAME_SIZE);
        int64_t end;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The first member must begin exactly where the payload area begins;
         * a container that leaves a gap there is not this format. */
        if (index == 0U) {
            if ((int64_t)offset != data_start) goto fail;
        } else if ((int64_t)offset <= (int64_t)previous) {
            /* Strictly ascending: every sample is, and accepting equal
             * offsets would admit zero-length members that no writer of this
             * format produces. */
            goto fail;
        }
        if ((int64_t)offset >= directory_offset) goto fail;
        previous = offset;

        end = (index + 1U < count)
                  ? (int64_t)xx_battleisle_le32(
                        directory +
                        (size_t)((index + 1U) * XX_BATTLEISLE_ENTRY_SIZE) +
                        XX_BATTLEISLE_NAME_SIZE)
                  : directory_offset;
        if (end > directory_offset || end <= (int64_t)offset) goto fail;

        stream->items[index].name = xx_battleisle_make_name(entry);
        if (!stream->items[index].name) goto fail;
        stream->items[index].header_offset =
            self->base_address + directory_offset +
            (int64_t)(index * XX_BATTLEISLE_ENTRY_SIZE);
        stream->items[index].header_size = XX_BATTLEISLE_ENTRY_SIZE;
        stream->items[index].data_offset = self->base_address + (int64_t)offset;
        stream->items[index].size = end - (int64_t)offset;
        ++stream->count;
    }

    xx_mem_free(directory);
    stream->archive_size = span;
    return stream;

fail:
    if (directory) xx_mem_free(directory);
    xx_battleisle_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read.  The length
 * comes from two offsets that parse already proved lie inside the file. */
static bool xx_battleisle_decode(Abstractformat *self,
                                 const xx_battleisle_member *member,
                                 uint8_t **out, size_t *out_size,
                                 xx_pd_struct *pd) {
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->size == 0) return false;
    if ((uint64_t)member->size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    if (!xx_battleisle_read_at(self, member->data_offset, output,
                               (size_t)member->size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_battleisle_init(xx_battleisle *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BATTLEISLE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-battleisle-lib");
    xx_format_set_extension(&archive->format, "lib");
    archive->format.check_is_valid = xx_battleisle_check_is_valid;
    archive->format.handle_base_info = xx_battleisle_handle_base_info;
    archive->format.get_format_size = xx_battleisle_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_battleisle_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_battleisle_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_battleisle_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_battleisle_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_battleisle_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_battleisle_free_archive_records_reading;
    archive->format.destroy = xx_battleisle_vtable_destroy;
}

xx_battleisle *xx_battleisle_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_battleisle *archive = (xx_battleisle *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_battleisle_init(archive, device, base_address);
    return archive;
}

void xx_battleisle_destroy(xx_battleisle *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_battleisle_free(xx_battleisle *archive) {
    if (!archive) return;
    xx_battleisle_destroy(archive);
    xx_mem_free(archive);
}

static void xx_battleisle_vtable_destroy(Abstractformat *self) {
    xx_battleisle_destroy((xx_battleisle *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_battleisle_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_battleisle_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_battleisle_parse(self, pd);
    if (!stream) return false;
    xx_battleisle_stream_free(stream);
    return true;
}

bool xx_battleisle_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_battleisle *archive = (xx_battleisle *)self;
    xx_battleisle_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_battleisle_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_battleisle_stream_free(stream);
    return true;
}

int64_t xx_battleisle_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_battleisle_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_battleisle *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_battleisle_set_record(xx_archive_record *record,
                                     const xx_battleisle_member *member) {
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
                                          XX_BATTLEISLE_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_battleisle_copy_options(xx_list_s *target,
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

static const xx_var *xx_battleisle_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_battleisle_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_battleisle_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_battleisle_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_battleisle_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_battleisle_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_battleisle_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_battleisle_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_battleisle_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_battleisle_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_battleisle_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_battleisle_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_battleisle_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_battleisle_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_battleisle_stream *stream;
    const xx_battleisle_member *member;
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
    stream = (xx_battleisle_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_battleisle_path_safe(member->name)) return false;

    path_option = xx_battleisle_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_battleisle_decode(self, member, &plain, &plain_size, pd);
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
        !xx_battleisle_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_battleisle_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
