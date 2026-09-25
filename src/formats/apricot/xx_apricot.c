/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ACT Apricot disk images (".dsk").  Ported from XArchive's
 * diskimages/xapricotimage.cpp together with the Apricot half of its
 * Algos/xdiskimagedecoder.cpp, and cross-checked against U3's Apricot handler
 * (class fcb, VMT 006137a8).
 *
 *   preamble, 0x80 bytes at offset 0, opening with the 22-byte ASCII
 *   signature "ACT Apricot disk image".
 *
 *   then a chain of chunks, each a 16-byte header plus its data:
 *     +0x00  2  u16 LE   kind; 0..3, only kind 1 carries image bytes
 *     +0x02  2  u16 LE   tag, always 0xe31d
 *     +0x04  2  u16 LE   subtype: 0x9e90 literal, 0x3e5a run
 *     +0x06  2  u16 LE   header length, >= 16; the excess is skipped
 *     +0x08  4  u32 LE   data length
 *     +0x0c  4           unused
 *
 *   a literal chunk's data is up to 512 image bytes verbatim.  A run chunk's
 *   data is exactly 3 bytes: a u16 LE repeat count of at most 512 and the
 *   byte to repeat.  Chunks of any other kind are skipped over by their data
 *   length.  The image is the concatenation of what kind-1 chunks produce.
 *
 * The container therefore holds exactly one member: the decoded image, named
 * "image.img".  Recognition is the 22-byte signature plus a complete walk of
 * the chunk chain -- every chunk must be well formed, stay inside the file,
 * and the chain must yield a non-empty image.  That walk is also how the
 * reader learns the uncompressed size, since the format never records one.
 *
 * The chunk walk runs over an in-memory copy of the file, so the input is
 * capped at 64 MB; real images are well under 1 MB.
 *
 * All 3 corpus samples in F:\ARC\ARC\APRICOT parse and decode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apricot/xx_apricot.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef APRICOT
#define XX_APRICOT_FILE_TYPE XX_FILE_TYPE_APRICOT
#else
#define XX_APRICOT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_APRICOT_METHOD_RLE 20U
#define XX_APRICOT_SIGNATURE_SIZE 22
#define XX_APRICOT_PREAMBLE 0x80
#define XX_APRICOT_CHUNK_HEADER 16
#define XX_APRICOT_TAG 0xe31dU
#define XX_APRICOT_LITERAL 0x9e90U
#define XX_APRICOT_RUN 0x3e5aU
#define XX_APRICOT_MAX_CHUNK 512
/* The walk needs the whole packed image in memory; real ones are well under
 * 1 MB, so this ceiling only exists to keep a bogus file from asking for an
 * unbounded allocation. */
#define XX_APRICOT_MAX_INPUT (64 * 1024 * 1024)
#define XX_APRICOT_MAX_OUTPUT 0x7fffffff

typedef struct xx_apricot_member_s {
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
} xx_apricot_member;

typedef struct xx_apricot_stream_s {
    xx_apricot_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_apricot_stream;

static void xx_apricot_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_apricot_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_apricot_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_apricot_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_apricot_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_apricot_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_apricot_path_safe(const char *path) {
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
static char *xx_apricot_make_name(const uint8_t *raw, size_t size,
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

static void xx_apricot_stream_free(void *pointer) {
    xx_apricot_stream *stream = (xx_apricot_stream *)pointer;
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
static bool xx_apricot_add(xx_apricot_stream *stream,
                          const xx_apricot_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_apricot_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_apricot_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* One pass over the chunk chain.  With output == NULL nothing is produced and
 * only the length is accumulated -- that is how the reader learns the size it
 * has to declare, since the format never records one.  Every chunk field is
 * bounded against the buffer before it is used. */
static bool xx_apricot_walk(const uint8_t *data, size_t size, uint8_t *output,
                            size_t output_size, int64_t *produced,
                            xx_pd_struct *pd) {
    int64_t cursor = XX_APRICOT_PREAMBLE;
    int64_t total = 0;

    if (!data || (int64_t)size < XX_APRICOT_PREAMBLE) return false;
    while ((int64_t)size - cursor >= XX_APRICOT_CHUNK_HEADER) {
        const uint8_t *head = data + (size_t)cursor;
        uint32_t kind = xx_apricot_le16(head);
        uint32_t tag = xx_apricot_le16(head + 2);
        uint32_t subtype = xx_apricot_le16(head + 4);
        uint32_t header_length = xx_apricot_le16(head + 6);
        int64_t data_length = (int64_t)(int32_t)xx_apricot_le32(head + 8);

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (tag != XX_APRICOT_TAG || kind > 3U ||
            header_length < XX_APRICOT_CHUNK_HEADER || data_length < 0) {
            return false;
        }
        cursor += header_length;
        if (cursor > (int64_t)size) return false;

        if (kind != 1U) {
            /* Not image data; skip the payload, bounded like any other. */
            if (data_length > (int64_t)size - cursor) return false;
            cursor += data_length;
            continue;
        }
        if (subtype == XX_APRICOT_LITERAL) {
            if (data_length > XX_APRICOT_MAX_CHUNK ||
                data_length > (int64_t)size - cursor) {
                return false;
            }
            if (output) {
                if ((uint64_t)total + (uint64_t)data_length > output_size) {
                    return false;
                }
                xx_mem_copy(output + total, data + cursor,
                            (size_t)data_length);
            }
            total += data_length;
            cursor += data_length;
        } else if (subtype == XX_APRICOT_RUN) {
            int64_t count;
            if (data_length != 3 || (int64_t)size - cursor < 3) return false;
            count = (int64_t)xx_apricot_le16(data + cursor);
            if (count > XX_APRICOT_MAX_CHUNK) return false;
            if (output) {
                if ((uint64_t)total + (uint64_t)count > output_size) {
                    return false;
                }
                xx_rt_memset(output + total, data[cursor + 2],
                             (size_t)count);
            }
            total += count;
            cursor += 3;
        } else {
            return false;
        }
        if (total > XX_APRICOT_MAX_OUTPUT) return false;
    }
    if (total <= 0) return false;
    if (produced) *produced = total;
    return true;
}

/* Read the packed image whole so the chunk chain can be walked. */
static uint8_t *xx_apricot_load(Abstractformat *self, int64_t span) {
    uint8_t *data;

    if (span <= 0 || span > XX_APRICOT_MAX_INPUT) return NULL;
    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_apricot_read_at(self, self->base_address, data, (size_t)span)) {
        xx_mem_free(data);
        return NULL;
    }
    return data;
}


/* --------------------------------------------------------------- parse -- */

static xx_apricot_stream *xx_apricot_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_apricot_stream *stream = NULL;
    uint8_t *data = NULL;
    xx_apricot_member member;
    int64_t total;
    int64_t span;
    int64_t produced = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span <= XX_APRICOT_PREAMBLE || span > XX_APRICOT_MAX_INPUT) {
        return NULL;
    }

    data = xx_apricot_load(self, span);
    if (!data) return NULL;
    if (xx_rt_memcmp(data, "ACT Apricot disk image",
                     XX_APRICOT_SIGNATURE_SIZE) != 0) {
        goto fail;
    }
    /* The signature alone would accept a truncated or spliced image; the
     * complete chunk walk is what actually proves the file, and it is also
     * the only way to learn the decoded size. */
    if (!xx_apricot_walk(data, (size_t)span, NULL, 0U, &produced, pd)) {
        goto fail;
    }

    stream = (xx_apricot_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup("image.img");
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = XX_APRICOT_PREAMBLE;
    member.data_offset = self->base_address;
    member.packed_size = span;
    member.unpacked_size = (uint64_t)produced;
    member.method = XX_APRICOT_METHOD_RLE;
    if (!xx_apricot_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }

    xx_mem_free(data);
    stream->archive_size = span;
    return stream;

fail:
    if (data) xx_mem_free(data);
    xx_apricot_stream_free(stream);
    return NULL;
}

/* Replay the chunk chain into a buffer of the size the measuring pass found.
 * A second walk that produces a different length means the two passes
 * disagreed, which is an error rather than something to publish. */
static bool xx_apricot_decode(Abstractformat *self,
                              const xx_apricot_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *data;
    uint8_t *plain;
    int64_t produced = 0;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size <= 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->unpacked_size == 0U ||
        member->unpacked_size > (uint64_t)XX_APRICOT_MAX_OUTPUT) {
        return false;
    }
    data = xx_apricot_load(self, member->packed_size);
    if (!data) return false;
    plain = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!plain) {
        xx_mem_free(data);
        return false;
    }
    if (!xx_apricot_walk(data, (size_t)member->packed_size, plain,
                         (size_t)member->unpacked_size, &produced, pd) ||
        (uint64_t)produced != member->unpacked_size) {
        xx_mem_free(data);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(data);
    *out = plain;
    *out_size = (size_t)produced;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_apricot_init(xx_apricot *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_APRICOT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-apricot-image");
    xx_format_set_extension(&archive->format, "dsk");
    archive->format.check_is_valid = xx_apricot_check_is_valid;
    archive->format.handle_base_info = xx_apricot_handle_base_info;
    archive->format.get_format_size = xx_apricot_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_apricot_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_apricot_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_apricot_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_apricot_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_apricot_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_apricot_free_archive_records_reading;
    archive->format.destroy = xx_apricot_vtable_destroy;
}

xx_apricot *xx_apricot_create(xx_io_device *device, int64_t base_address) {
    xx_apricot *archive = (xx_apricot *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_apricot_init(archive, device, base_address);
    return archive;
}

void xx_apricot_destroy(xx_apricot *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_apricot_free(xx_apricot *archive) {
    if (!archive) return;
    xx_apricot_destroy(archive);
    xx_mem_free(archive);
}

static void xx_apricot_vtable_destroy(Abstractformat *self) {
    xx_apricot_destroy((xx_apricot *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_apricot_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_apricot_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_apricot_parse(self, pd);
    if (!stream) return false;
    xx_apricot_stream_free(stream);
    return true;
}

bool xx_apricot_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_apricot *archive = (xx_apricot *)self;
    xx_apricot_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_apricot_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_apricot_stream_free(stream);
    return true;
}

int64_t xx_apricot_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_apricot_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_apricot *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_apricot_set_record(xx_archive_record *record,
                                 const xx_apricot_member *member) {
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

static bool xx_apricot_copy_options(xx_list_s *target,
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

static const xx_var *xx_apricot_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_apricot_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_apricot_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_apricot_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_apricot_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_apricot_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_apricot_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_apricot_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_apricot_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_apricot_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_apricot_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_apricot_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_apricot_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_apricot_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_apricot_stream *stream;
    const xx_apricot_member *member;
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
    stream = (xx_apricot_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_apricot_path_safe(member->name)) return false;

    path_option =
        xx_apricot_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_apricot_decode(self, member, &plain, &plain_size, pd);
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
        !xx_apricot_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_apricot_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
