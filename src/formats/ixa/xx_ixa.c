/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IXALANCE archives (".ixa").  XArchive has no module for this one.  The
 * layout was recovered from U3's IXA handler -- class uva, VMT 005b7e68,
 * recognition predicate decompiled/functions/005b/005b84b0.c, which
 * tail-calls decompiled/functions/005b/005b7ee0.c -- and then confirmed
 * against the corpus and against U3's own listing output.
 *
 *   header:
 *     0x00   9  char[9]  "IXALANCE " (the predicate tests only "IXALANCE")
 *     0x09  31  char     archive title, NUL padded, through 0x27
 *     0x28   4  u32 LE   must be 0x30
 *     0x2c   4  u32 LE   must be 1 (format version)
 *     0x30   4  u32 LE   member slot count PLUS ONE
 *     0x34   4  u32 LE   offset just past the member table
 *     0x38   4  u32 LE   size of an auxiliary block that follows the table
 *     0x3c   4  u32 LE   zero in every sample
 *
 *   member table, 12 bytes per slot, starting at 0x40:
 *     +0x00  4  u32 LE   absolute payload offset
 *     +0x04  4  u32 LE   packed size
 *     +0x08  4  u32 LE   plain size
 *
 *   a slot whose packed and plain sizes are both zero is an unused hole; U3
 *   skips those when it lists, and so does this reader.  Payloads run back to
 *   back from (table end + auxiliary size) and finish exactly on end-of-file.
 *
 * U3's predicate is only the magic plus the two constants at 0x28 and 0x2c.
 * This reader adds the structural requirements that make a false positive
 * essentially impossible: the table end must be exactly 0x40 + slots*12, the
 * first payload must start at table end plus the auxiliary size, every
 * payload must follow the previous one exactly, and the last must land on
 * end-of-file.
 *
 * Slots carry no names.  U3 synthesises "<index>.<ext>" with the extension
 * sniffed from the DECOMPRESSED bytes; this reader cannot sniff what it
 * cannot decode, so it files each slot under a zero-padded index and ".bin".
 *
 * COMPRESSION IS NOT IMPLEMENTED.  Every payload in the corpus is compressed
 * (no slot has packed == plain), and the scheme is IXALANCE's own -- it is
 * not any of the ~75 decoders already in this library, and U3's decoder was
 * not recovered far enough to port here.  Listing, offsets and both sizes are
 * exact; unpack fails closed rather than emitting a plausible-looking wrong
 * result.
 *
 * All 4 corpus samples in F:\ARC\ARC\IXA parse, for 18 live members, which
 * matches U3's own listing member for member.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ixa/xx_ixa.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef IXA
#define XX_IXA_FILE_TYPE XX_FILE_TYPE_IXA
#else
#define XX_IXA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_IXA_METHOD_IXALANCE 1U
#define XX_IXA_TABLE_OFFSET 0x40
#define XX_IXA_ENTRY_SIZE 12
#define XX_IXA_MAGIC_CHECK_A 0x30U
#define XX_IXA_MAGIC_CHECK_B 1U
/* The table-fits-in-the-file check is what actually bounds the allocation. */
#define XX_IXA_MAX_SLOTS 1000000U

typedef struct xx_ixa_member_s {
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
} xx_ixa_member;

typedef struct xx_ixa_stream_s {
    xx_ixa_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_ixa_stream;

static void xx_ixa_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_ixa_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ixa_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_ixa_be16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[0] << 8));
}

static uint32_t xx_ixa_be32(const uint8_t *data) {
    return (uint32_t)data[3] | ((uint32_t)data[2] << 8) |
           ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

static bool xx_ixa_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_ixa_path_safe(const char *path) {
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
static char *xx_ixa_make_name(const uint8_t *raw, size_t size,
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

static void xx_ixa_stream_free(void *pointer) {
    xx_ixa_stream *stream = (xx_ixa_stream *)pointer;
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
static bool xx_ixa_add(xx_ixa_stream *stream,
                          const xx_ixa_member *member) {
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_ixa_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_ixa_member *)xx_mem_realloc(stream->items,
                                                   wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Slots carry no names; they are filed under a zero-padded index, the width
 * taken from the slot count the way U3's listing does it. */
static char *xx_ixa_slot_name(uint32_t index, uint32_t width) {
    char text[32];
    size_t length = 0U;
    uint32_t scale = 1U;
    uint32_t digits = 1U;

    while (digits < width && scale <= 100000000U) {
        scale *= 10U;
        ++digits;
    }
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

static uint32_t xx_ixa_digits(uint32_t value) {
    uint32_t digits = 1U;

    while (value >= 10U) {
        value /= 10U;
        ++digits;
    }
    return digits;
}


/* --------------------------------------------------------------- parse -- */

static xx_ixa_stream *xx_ixa_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_ixa_stream *stream = NULL;
    uint8_t head[XX_IXA_TABLE_OFFSET];
    uint8_t *table = NULL;
    int64_t total;
    int64_t span;
    int64_t table_end;
    int64_t cursor;
    uint32_t slots;
    uint32_t auxiliary;
    uint32_t width;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_IXA_TABLE_OFFSET + XX_IXA_ENTRY_SIZE) return NULL;
    if (!xx_ixa_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    /* U3's predicate: the magic plus the two header constants. */
    if (xx_rt_memcmp(head, "IXALANCE", 8U) != 0 ||
        xx_ixa_le32(head + 0x28) != XX_IXA_MAGIC_CHECK_A ||
        xx_ixa_le32(head + 0x2c) != XX_IXA_MAGIC_CHECK_B) {
        return NULL;
    }

    /* The stored value is the slot count plus one. */
    if (xx_ixa_le32(head + 0x30) == 0U) return NULL;
    slots = xx_ixa_le32(head + 0x30) - 1U;
    auxiliary = xx_ixa_le32(head + 0x38);
    if (slots == 0U || slots > XX_IXA_MAX_SLOTS) return NULL;
    /* Bound the table against the real file before allocating it, and make
     * the header's own republished table end agree exactly -- that is the
     * cheapest check there is on the slot count. */
    if ((int64_t)slots * XX_IXA_ENTRY_SIZE > span - XX_IXA_TABLE_OFFSET) {
        return NULL;
    }
    table_end = XX_IXA_TABLE_OFFSET + (int64_t)slots * XX_IXA_ENTRY_SIZE;
    if ((int64_t)xx_ixa_le32(head + 0x34) != table_end) return NULL;
    if ((int64_t)auxiliary > span - table_end) return NULL;

    table = (uint8_t *)xx_mem_alloc((size_t)slots * XX_IXA_ENTRY_SIZE);
    if (!table) return NULL;
    if (!xx_ixa_read_at(self, self->base_address + XX_IXA_TABLE_OFFSET, table,
                        (size_t)slots * XX_IXA_ENTRY_SIZE)) {
        goto fail;
    }

    stream = (xx_ixa_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    width = xx_ixa_digits(slots);
    cursor = table_end + (int64_t)auxiliary;
    for (index = 0U; index < slots; ++index) {
        const uint8_t *entry = table + (size_t)index * XX_IXA_ENTRY_SIZE;
        int64_t offset = (int64_t)xx_ixa_le32(entry);
        int64_t packed = (int64_t)xx_ixa_le32(entry + 4);
        int64_t plain = (int64_t)xx_ixa_le32(entry + 8);
        xx_ixa_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* Every payload starts exactly where the previous one ended, and its
         * size is bounded against what is left before it moves the cursor. */
        if (offset != cursor || packed < 0 || plain < 0 ||
            packed > span - cursor) {
            goto fail;
        }
        cursor = offset + packed;
        /* An empty slot is a hole in the numbering, not a member. */
        if (packed == 0 && plain == 0) continue;
        if (packed == 0 || plain == 0) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_ixa_slot_name(index + 1U, width);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + XX_IXA_TABLE_OFFSET +
                               (int64_t)index * XX_IXA_ENTRY_SIZE;
        member.header_size = XX_IXA_ENTRY_SIZE;
        member.data_offset = self->base_address + offset;
        member.packed_size = packed;
        member.unpacked_size = (uint64_t)plain;
        member.method = XX_IXA_METHOD_IXALANCE;
        if (!xx_ixa_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    /* The payload chain fills the file; a leftover tail means the table did
     * not describe this file. */
    if (cursor != span || stream->count == 0U) goto fail;

    xx_mem_free(table);
    stream->archive_size = span;
    return stream;

fail:
    if (table) xx_mem_free(table);
    xx_ixa_stream_free(stream);
    return NULL;
}

/* IXALANCE's own compressor is not implemented, so unpack fails closed.  No
 * corpus member is stored (none has packed == plain), so in practice this
 * always refuses; that is deliberate -- a reader that refuses is worth more
 * than one that emits garbage.  The stored case is still handled so that a
 * future archive that uses it works without a code change. */
static bool xx_ixa_decode(Abstractformat *self, const xx_ixa_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->packed_size <= 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if ((uint64_t)member->packed_size != member->unpacked_size) return false;
    if ((uint64_t)member->packed_size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    if (!output) return false;
    if (!xx_ixa_read_at(self, member->data_offset, output,
                        (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->packed_size;
    return true;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_ixa_init(xx_ixa *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_IXA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ixalance");
    xx_format_set_extension(&archive->format, "ixa");
    archive->format.check_is_valid = xx_ixa_check_is_valid;
    archive->format.handle_base_info = xx_ixa_handle_base_info;
    archive->format.get_format_size = xx_ixa_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ixa_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ixa_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ixa_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ixa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ixa_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ixa_free_archive_records_reading;
    archive->format.destroy = xx_ixa_vtable_destroy;
}

xx_ixa *xx_ixa_create(xx_io_device *device, int64_t base_address) {
    xx_ixa *archive = (xx_ixa *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ixa_init(archive, device, base_address);
    return archive;
}

void xx_ixa_destroy(xx_ixa *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ixa_free(xx_ixa *archive) {
    if (!archive) return;
    xx_ixa_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ixa_vtable_destroy(Abstractformat *self) {
    xx_ixa_destroy((xx_ixa *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ixa_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ixa_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ixa_parse(self, pd);
    if (!stream) return false;
    xx_ixa_stream_free(stream);
    return true;
}

bool xx_ixa_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ixa *archive = (xx_ixa *)self;
    xx_ixa_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ixa_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ixa_stream_free(stream);
    return true;
}

int64_t xx_ixa_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ixa_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ixa *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ixa_set_record(xx_archive_record *record,
                                 const xx_ixa_member *member) {
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

static bool xx_ixa_copy_options(xx_list_s *target,
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

static const xx_var *xx_ixa_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ixa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ixa_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ixa_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ixa_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ixa_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ixa_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ixa_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ixa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ixa_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ixa_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ixa_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_ixa_set_record(&state->current_record,
                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ixa_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ixa_stream *stream;
    const xx_ixa_member *member;
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
    stream = (xx_ixa_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ixa_path_safe(member->name)) return false;

    path_option =
        xx_ixa_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ixa_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ixa_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_ixa_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
