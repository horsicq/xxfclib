/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Solaris compressed lofi disk images (*.lofi).
 *
 * The container has no per-member framing at all -- the whole image is one
 * logical output. Header at the base address, every integer BIG endian:
 *
 *   0x00  char[36]  algorithm name, "lzma" followed by 32 zero bytes
 *   0x24  u32       segment size
 *   0x28  u32       number of index entries
 *   0x2C  u32       size of the final segment, 1..segment size
 *   0x30  u64[n]    index
 *
 * An index entry is the offset of a segment measured from the END of the
 * index. Entry 0 is 0, and the LAST entry is the end of the segment data
 * rather than a segment start, so n entries describe n-1 segments and the
 * image is (n - 2) full segments plus the final one.
 *
 * Each segment is [u8 0x01][13-byte LZMA "alone" header][LZMA data]. The
 * alone header carries the props, the dictionary size and the segment's
 * uncompressed length, which must equal the segment size for every segment
 * but the last. Each segment is an independent stream decoded to its exact
 * declared length with no end marker: state and dictionary reset at every
 * segment boundary. Nothing is checksummed anywhere in the container.
 *
 * Only the "lzma" flavour is implemented. `lofiadm -C gzip` writes the same
 * container with a "gzip" name field and deflate segments, and the reference
 * refuses it outright rather than guessing at its framing, so this reader
 * does too.
 *
 * Because the decoder re-reads the header and the index for itself, the
 * member's published extent starts at the base address rather than at the
 * first segment, and runs to the end of the segment data.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lofi/xx_lofi.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lofi/xx_lofi.h"
#include <stdio.h>

#define XX_LOFI_COPY_CHUNK (64 * 1024)

typedef struct xx_lofi_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_lofi_member;

typedef struct xx_lofi_stream_s {
    xx_lofi_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lofi_stream;

static void xx_lofi_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lofi_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_lofi_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lofi_path_safe(const char *name) {
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

static void xx_lofi_stream_free(void *pointer) {
    xx_lofi_stream *stream = (xx_lofi_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lofi_add(xx_lofi_stream *stream,
                          const xx_lofi_member *member) {
    xx_lofi_member *grown = (xx_lofi_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_LOFI_NAME_SIZE 36
#define XX_LOFI_INDEX_OFFSET 0x30
#define XX_LOFI_INDEX_ENTRY_SIZE 8
#define XX_LOFI_SEGMENT_PREFIX 1
#define XX_LOFI_ALONE_HEADER 13
#define XX_LOFI_MAX_SEGMENT_SIZE (64 * 1024 * 1024)
#define XX_LOFI_MAX_INDEX_ENTRIES (1 << 20)
#define XX_LOFI_MAX_MEMBERS 1
#define XX_LOFI_METHOD_LZMA 1U
#define XX_LOFI_MEMBER_NAME "lofi_image.img"
#define XX_LOFI_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_lofi_be32(const uint8_t *data);
static uint64_t xx_lofi_be64(const uint8_t *data);
static xx_lofi_stream *xx_lofi_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_lofi_decode(Abstractformat *self, const xx_lofi_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Each segment carries a framing byte and an LZMA "alone" header ahead of its
 * data, so consecutive index entries must differ by at least this much. */
/* `lofiadm` caps the segment size far below this; the bound only keeps a
 * corrupt header from describing an absurd image. */
/* An 8 MiB index, i.e. a million segments. The library tolerates far more,
 * but this reader reads the index into memory to validate it, so the bound is
 * also a bound on that allocation. */
/* One logical image, so one member. */
/* The container names the algorithm rather than numbering it; this is the
 * only flavour the library decodes. */

static uint32_t xx_lofi_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t xx_lofi_be64(const uint8_t *data) {
    return ((uint64_t)xx_lofi_be32(data) << 32) |
           (uint64_t)xx_lofi_be32(data + 4);
}

static xx_lofi_stream *xx_lofi_parse(Abstractformat *self,
                                     xx_pd_struct *pd) {
    xx_lofi_stream *stream = NULL;
    xx_lofi_member member;
    uint8_t *index = NULL;
    char *name = NULL;
    uint8_t header[XX_LOFI_INDEX_OFFSET];
    int64_t total;
    int64_t span;
    int64_t segment_size;
    int64_t last_segment_size;
    int64_t index_entries;
    int64_t index_bytes;
    int64_t data_offset;
    int64_t data_size;
    int64_t image_size;
    int64_t segments;
    int64_t entry;
    int64_t previous = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_LOFI_INDEX_OFFSET + XX_LOFI_INDEX_ENTRY_SIZE) return NULL;
    if (!xx_lofi_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* The algorithm name and its 32-byte zero pad. Four magic characters
     * alone would be thin, but a name field that is "lzma" followed by
     * thirty-two zeros is 36 bytes of fixed content, and it is also what
     * separates an lzma image from the "gzip" flavour this reader refuses
     * rather than guessing at. */
    if (header[0] != 'l' || header[1] != 'z' || header[2] != 'm' ||
        header[3] != 'a') {
        return NULL;
    }
    for (entry = 4; entry < XX_LOFI_NAME_SIZE; ++entry) {
        if (header[entry] != 0U) return NULL;
    }

    segment_size = (int64_t)xx_lofi_be32(header + 0x24);
    index_entries = (int64_t)xx_lofi_be32(header + 0x28);
    last_segment_size = (int64_t)xx_lofi_be32(header + 0x2C);
    if (segment_size <= 0 || segment_size > XX_LOFI_MAX_SEGMENT_SIZE) {
        return NULL;
    }
    /* Entry 0 and the trailing end-of-data entry are both present in every
     * image, so a single entry describes no segment at all. */
    if (index_entries <= 1 || index_entries > XX_LOFI_MAX_INDEX_ENTRIES) {
        return NULL;
    }
    if (last_segment_size <= 0 || last_segment_size > segment_size) {
        return NULL;
    }

    index_bytes = index_entries * XX_LOFI_INDEX_ENTRY_SIZE;
    data_offset = XX_LOFI_INDEX_OFFSET + index_bytes;
    if (!xx_lofi_range_within(span, XX_LOFI_INDEX_OFFSET, index_bytes)) {
        return NULL;
    }

    index = (uint8_t *)xx_mem_alloc((size_t)index_bytes);
    if (!index) return NULL;
    if (!xx_lofi_read_at(self, self->base_address + XX_LOFI_INDEX_OFFSET,
                         index, (size_t)index_bytes) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(index);
        return NULL;
    }

    /* The index is the container's real signature. Entry 0 must be zero and
     * every later entry must exceed its predecessor by at least a segment's
     * own framing bytes -- a rule an arbitrary run of big-endian u64s does
     * not satisfy, and the reason this format is detectable without a magic
     * number longer than its name field. The library re-runs exactly these
     * rules at decode time, so relaxing them here can only produce members
     * that then refuse to extract. */
    for (entry = 0; entry < index_entries; ++entry) {
        uint64_t value = xx_lofi_be64(index + entry *
                                              XX_LOFI_INDEX_ENTRY_SIZE);
        if (value > (uint64_t)INT64_MAX) {
            xx_mem_free(index);
            return NULL;
        }
        if (entry == 0) {
            if (value != 0U) {
                xx_mem_free(index);
                return NULL;
            }
        } else if ((int64_t)value < previous + XX_LOFI_SEGMENT_PREFIX +
                                        XX_LOFI_ALONE_HEADER) {
            /* Written as an addition rather than as a difference: the
             * reference does the subtraction signed, where a descending index
             * goes negative and is refused; unsigned it would wrap and be
             * accepted. */
            xx_mem_free(index);
            return NULL;
        }
        previous = (int64_t)value;
    }
    xx_mem_free(index);
    index = NULL;

    data_size = previous;
    if (!xx_lofi_range_within(span, data_offset, data_size)) return NULL;

    segments = index_entries - 1;
    image_size = (segments - 1) * segment_size + last_segment_size;

    stream = (xx_lofi_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (stream->count >= (size_t)XX_LOFI_MAX_MEMBERS) goto fail;
    name = xx_str_dup(XX_LOFI_MEMBER_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = data_offset;
    /* The decoder re-reads the header and the index for itself, so the
     * published extent starts at the container's first byte, not at the first
     * segment. */
    member.data_offset = self->base_address;
    member.compressed_size = data_offset + data_size;
    member.uncompressed_size = image_size;
    member.method = XX_LOFI_METHOD_LZMA;
    /* A disk image carries no timestamp in its lofi wrapper. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_lofi_add(stream, &member)) goto fail;
    name = NULL;

    stream->archive_size = data_offset + data_size;
    return stream;

fail:
    xx_str_free(name);
    xx_lofi_stream_free(stream);
    return NULL;
}


/* The image size is derived from header fields an attacker controls, so it is
 * capped before it becomes an allocation; so is the container read, which for
 * this format is the whole compressed image. */

static bool xx_lofi_decode(Abstractformat *self, const xx_lofi_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    /* The container names its algorithm instead of numbering it, and the
     * parse admits only "lzma"; the "gzip" flavour would need framing this
     * reader does not implement, and copying its segments through as stored
     * would hand back compressed bytes. */
    if (member->method != XX_LOFI_METHOD_LZMA) return false;
    if (member->compressed_size <= 0 ||
        member->compressed_size > (int64_t)XX_LOFI_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size <= 0 ||
        member->uncompressed_size > (int64_t)XX_LOFI_MAX_DECODED) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    /* The whole container from its first byte: the entry point parses the
     * header and the index again itself. */
    if (!xx_lofi_read_at(self, member->data_offset, packed,
                         (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* Every segment is decoded to its exact declared length with no end
     * marker, and the entry point refuses a run that does not sum to the
     * image size the geometry describes. A short image reported as complete
     * would be a silently truncated disk. */
    if (!xx_lofi_decode_memory(packed, (size_t)member->compressed_size, plain,
                               (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_lofi_init(xx_lofi *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_LOFI;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-solaris-lofi");
    xx_format_set_extension(&archive->format, "lofi");
    archive->format.check_is_valid = xx_lofi_check_is_valid;
    archive->format.handle_base_info = xx_lofi_handle_base_info;
    archive->format.get_format_size = xx_lofi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lofi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lofi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lofi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lofi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lofi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lofi_free_archive_records_reading;
    archive->format.destroy = xx_lofi_vtable_destroy;
}

xx_lofi *xx_lofi_create(xx_io_device *device, int64_t base_address) {
    xx_lofi *archive = (xx_lofi *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lofi_init(archive, device, base_address);
    return archive;
}

void xx_lofi_destroy(xx_lofi *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lofi_free(xx_lofi *archive) {
    if (!archive) return;
    xx_lofi_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lofi_vtable_destroy(Abstractformat *self) {
    xx_lofi_destroy((xx_lofi *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lofi_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lofi_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lofi_parse(self, pd);
    if (!stream) return false;
    xx_lofi_stream_free(stream);
    return true;
}

bool xx_lofi_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lofi *archive = (xx_lofi *)self;
    xx_lofi_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lofi_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lofi_stream_free(stream);
    return true;
}

int64_t xx_lofi_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lofi_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lofi *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lofi_set_record(xx_archive_record *record,
                                 const xx_lofi_member *member) {
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

static bool xx_lofi_copy_options(xx_list_s *target,
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

static const xx_var *xx_lofi_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lofi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lofi_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lofi_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lofi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lofi_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lofi_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lofi_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lofi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lofi_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lofi_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lofi_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lofi_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lofi_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lofi_stream *stream;
    const xx_lofi_member *member;
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
    stream = (xx_lofi_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lofi_path_safe(member->name)) return false;

    path_option = xx_lofi_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lofi_decode(self, member, &plain, &plain_size, pd);
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

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_lofi_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_lofi_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
