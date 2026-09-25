/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ULEAD ("U_LEAD CORP.") containers.
 *
 *   header, 0x2c bytes at the base address:
 *     +0x00  "U_LEAD CORP."
 *     +0x0c  u32  MUST be zero
 *     +0x10  u16  MUST be 1
 *     +0x12  u16  MUST be 1
 *     +0x14  u32  MUST be zero
 *     +0x18  16   the file name (LAYOUT 2 ONLY; layout 1 has the block table
 *                 here instead)
 *     +0x18  u16  block count      \  layout 1
 *     +0x1a  u16  last block size   /
 *     +0x28  u16  block count      \  layout 2
 *     +0x2a  u16  last block size   /
 *
 * There are TWO LAYOUTS overlapping the same bytes, and which one applies is
 * decided by which of the two count/size pairs is self-consistent against the
 * file size -- and, when both are, by whether the shorter header's count is
 * still plausible.  xx_ulead_parse_header() owns that decision; this reader
 * does not second-guess it.
 *
 * The member is cut into 0x4000-byte blocks, each an independent 12-bit LZW
 * stream, and a u16 per block gives its PACKED size.  A block whose packed
 * size equals its plain size is STORED -- that is the only marker.  The
 * plaintext length is (blockCount - 1) * 0x4000 + lastBlockSize, so nothing
 * has to be measured.
 *
 * THE WHOLE FILE IS THE STREAM.  The block table lives inside the header, so
 * the codec is handed the file from offset zero, not the payload region.  The
 * single record's data offset is therefore the base address, with the header
 * overlapping it; that is the format, not an oversight.
 *
 * Layout 1 stores NO NAME, and the reference falls back to the archive's own
 * file name, which xx_io_device does not expose.  A fixed name is used
 * instead.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ulead/xx_ulead.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/ulead/xx_ulead.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

typedef struct xx_ulead_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ulead_member;

typedef struct xx_ulead_stream_s {
    xx_ulead_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t layout;
    uint32_t block_count;
} xx_ulead_stream;

static void xx_ulead_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ulead_read_at(Abstractformat *self, int64_t offset,
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

/* Refuse anything that would escape the extraction directory. */
static bool xx_ulead_path_safe(const char *name) {
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

static void xx_ulead_stream_free(void *pointer) {
    xx_ulead_stream *stream = (xx_ulead_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_ulead_add(xx_ulead_stream *stream,
                         const xx_ulead_member *member) {
    xx_ulead_member *grown = (xx_ulead_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define XX_ULEAD_HEADER_SIZE 0x2c
/* The codec is handed the whole file; this bounds what that may cost. */
#define XX_ULEAD_MAX_SIZE ((int64_t)256 * 1024 * 1024)
/* Bounds what the header's block count may ask an extraction to allocate. */
#define XX_ULEAD_MAX_OUTPUT ((int64_t)256 * 1024 * 1024)
/* Used when the header stores no usable name; see the file comment. */
#define XX_ULEAD_MEMBER_NAME "ulead"
/* One method, published so a listing has something to show. */
#define XX_ULEAD_METHOD_LZW 1U

/* The stored name is a fixed 16-byte Latin-1 field with no path structure.
 * A name carrying a separator or a control byte is not something this reader
 * should hand on, but it is also not a reason to refuse the archive: the
 * member is published under the fallback name instead, exactly as it would be
 * for the layout that stores no name at all. */
static bool xx_ulead_name_usable(const char *name) {
    size_t index;

    if (!name || name[0] == '\0') return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        unsigned char value = (unsigned char)name[index];

        if (value < 0x20U || value == 0x7FU) return false;
        if (value == (unsigned char)'/' || value == (unsigned char)'\\' ||
            value == (unsigned char)':') {
            return false;
        }
    }
    if (name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
        return false;
    }
    return true;
}

static xx_ulead_stream *xx_ulead_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_ulead_stream *stream;
    xx_ulead_header header;
    xx_ulead_member member;
    uint8_t raw[XX_ULEAD_HEADER_SIZE];
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ULEAD_HEADER_SIZE || span > XX_ULEAD_MAX_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_ulead_read_at(self, self->base_address, raw, sizeof(raw))) {
        return NULL;
    }

    xx_mem_zero(&header, sizeof(header));
    /* The layout decision needs the file size, not just the bytes read: both
     * candidate block tables are tested against what the file can actually
     * hold. */
    if (!xx_ulead_parse_header(raw, sizeof(raw), (uint64_t)span, &header)) {
        return NULL;
    }
    if (header.uncompressed_size == 0U ||
        header.uncompressed_size > (uint64_t)XX_ULEAD_MAX_OUTPUT) {
        return NULL;
    }
    /* Redundant with the header parser, which refuses a table that runs past
     * the file; repeated because everything below sizes buffers from it. */
    if (header.data_offset > (uint64_t)span) return NULL;

    stream = (xx_ulead_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->layout = header.layout;
    stream->block_count = header.block_count;

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup(xx_ulead_name_usable(header.file_name)
                                 ? header.file_name
                                 : XX_ULEAD_MEMBER_NAME);
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = XX_ULEAD_HEADER_SIZE;
    /* The whole file, header included: the block table the codec walks lives
     * inside the header. */
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)header.uncompressed_size;
    member.method = XX_ULEAD_METHOD_LZW;
    /* The header carries no timestamp. */
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_ulead_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_ulead_stream_free(stream);
    return NULL;
}

static bool xx_ulead_decode(Abstractformat *self,
                            const xx_ulead_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < XX_ULEAD_HEADER_SIZE ||
        member->compressed_size > XX_ULEAD_MAX_SIZE) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_ULEAD_MAX_OUTPUT) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_ulead_read_at(self, member->data_offset, input,
                          (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    /* The codec re-reads the header out of this same buffer to find the block
     * table, which is why it is handed the file from offset zero. */
    ok = xx_ulead_decode_memory(input, (size_t)member->compressed_size, output,
                                (size_t)member->uncompressed_size, &written);
    xx_mem_free(input);
    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ulead_init(xx_ulead *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ULEAD_FILE_TYPE_ID;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ulead");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_ulead_check_is_valid;
    archive->format.handle_base_info = xx_ulead_handle_base_info;
    archive->format.get_format_size = xx_ulead_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ulead_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ulead_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ulead_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ulead_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ulead_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ulead_free_archive_records_reading;
    archive->format.destroy = xx_ulead_vtable_destroy;
}

xx_ulead *xx_ulead_create(xx_io_device *device, int64_t base_address) {
    xx_ulead *archive = (xx_ulead *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ulead_init(archive, device, base_address);
    return archive;
}

void xx_ulead_destroy(xx_ulead *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->uncompressed_size = 0U;
    archive->layout = 0U;
    archive->block_count = 0U;
}

void xx_ulead_free(xx_ulead *archive) {
    if (!archive) return;
    xx_ulead_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ulead_vtable_destroy(Abstractformat *self) {
    xx_ulead_destroy((xx_ulead *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ulead_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ulead_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ulead_parse(self, pd);
    if (!stream) return false;
    xx_ulead_stream_free(stream);
    return true;
}

bool xx_ulead_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ulead *archive = (xx_ulead *)self;
    xx_ulead_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ulead_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->uncompressed_size =
        stream->count != 0U ? (uint64_t)stream->items[0].uncompressed_size : 0U;
    archive->layout = stream->layout;
    archive->block_count = stream->block_count;
    xx_ulead_stream_free(stream);
    return true;
}

int64_t xx_ulead_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ulead_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ulead *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ulead_set_record(xx_archive_record *record,
                                const xx_ulead_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ulead_copy_options(xx_list_s *target,
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

static const xx_var *xx_ulead_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ulead_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ulead_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ulead_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ulead_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ulead_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ulead_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ulead_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ulead_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ulead_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_ulead_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ulead_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ulead_set_record(&state->current_record,
                                            &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ulead_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_ulead_stream *stream;
    const xx_ulead_member *member;
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
    stream = (xx_ulead_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ulead_path_safe(member->name)) return false;

    path_option =
        xx_ulead_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ulead_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ulead_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ulead_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
