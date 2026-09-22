/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Delrina DT packed files, the single-file compressor used by the Delrina
 * installers (WinFax PRO, Delrina Communications Suite). This is a wrapper
 * around ONE payload, not a multi-member archive, so the record list this
 * reader publishes always holds exactly one entry.
 *
 *   header, 41 bytes (0x29) at offset 0:
 *     0x00   2  char     magic "DT"
 *     0x02   2  u16 LE   version, always 0x0002
 *     0x04   2  u16 LE   flags, always 0x0001
 *     0x06  23  junk     UNINITIALISED WRITER MEMORY - see below
 *     0x1d   4  u32 LE   rawSize, length of the plaintext
 *     0x21   4  junk     more uninitialised writer memory
 *     0x25   2  u16 LE   DOS date
 *     0x27   2  u16 LE   DOS time
 *     0x29   n  payload  PKWARE DCL Implode stream, its prelude included,
 *                        running to end-of-file
 *
 * Bytes 0x06..0x1C and 0x21..0x24 are NOT a name and NOT a checksum: across
 * the reference corpus they hold recognisable fragments of the packer's own
 * address space - DOS environment strings, 8086 code, error message text -
 * and two files with completely different contents can carry byte-identical
 * junk. Nothing in them may be validated or reported.
 *
 * THE ORIGINAL FILE NAME IS STORED NOWHERE. The packer replaces the file in
 * place on the install media, so the container's own name is the only name
 * there ever was; this reader has no access to it and publishes a fixed
 * placeholder instead.
 *
 * rawSize is redundant with the stream's own end-of-stream code, and that
 * redundancy is the format's ONLY integrity check - there is no CRC
 * anywhere - so parse trial-measures the stream with xx_dcl_scan_memory()
 * and requires the two to agree exactly. Six bytes of magic are far too weak
 * a gate on their own.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dtpacked/xx_dtpacked.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_DTPACKED_COPY_CHUNK (64 * 1024)

typedef struct xx_dtpacked_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_dtpacked_member;

typedef struct xx_dtpacked_stream_s {
    xx_dtpacked_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_dtpacked_stream;

static void xx_dtpacked_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_dtpacked_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_dtpacked_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_dtpacked_path_safe(const char *name) {
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

static void xx_dtpacked_stream_free(void *pointer) {
    xx_dtpacked_stream *stream = (xx_dtpacked_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_dtpacked_add(xx_dtpacked_stream *stream,
                          const xx_dtpacked_member *member) {
    xx_dtpacked_member *grown = (xx_dtpacked_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_DTPACKED_MIN_PACKED_SIZE 3
#define XX_DTPACKED_DCL_MAX_LITERAL_MODE 1U
#define XX_DTPACKED_DCL_MIN_DICT_BITS 4U
#define XX_DTPACKED_DCL_MAX_DICT_BITS 6U
#define XX_DTPACKED_MAX_MEMBERS 1
#define XX_DTPACKED_MAX_RATIO 1024
#define XX_DTPACKED_RATIO_SLACK 4096
#define XX_DTPACKED_PLACEHOLDER_NAME "dt_data"
#define XX_DTPACKED_HEADER_SIZE 41
#define XX_DTPACKED_RAWSIZE_OFFSET 29
#define XX_DTPACKED_DOSDATE_OFFSET 37
#define XX_DTPACKED_DOSTIME_OFFSET 39
#define XX_DTPACKED_VERSION 0x0002U
#define XX_DTPACKED_FLAGS 0x0001U
#define XX_DTPACKED_METHOD_DCL 1U
#define XX_DTPACKED_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_dtpacked_le16(const uint8_t *data);
static uint32_t xx_dtpacked_le32(const uint8_t *data);
static xx_dtpacked_stream *xx_dtpacked_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_dtpacked_decode(Abstractformat *self, const xx_dtpacked_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* A DCL stream cannot be shorter than its two prelude bytes plus one byte
 * holding the start of the end-of-stream code. */
/* This container wraps exactly one payload. The cap exists because the
 * contract asks for one, not because a chain could ever grow. */
/* Decompression-bomb guard. DCL tops out near 259 plaintext bytes per
 * encoded byte and the reference corpus peaks at 68, so this leaves an order
 * of magnitude of headroom and still refuses a header claiming a gigabyte
 * behind a few hundred bytes of payload. */
/* The original name is stored nowhere and this reader cannot see the
 * container's own file name, so the single record gets a fixed placeholder.
 * It is deliberately extension-less: inventing one would be a claim about
 * content the container never makes. */

static uint16_t xx_dtpacked_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_dtpacked_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_dtpacked_stream *xx_dtpacked_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_dtpacked_stream *stream;
    xx_dtpacked_member member;
    uint8_t header[XX_DTPACKED_HEADER_SIZE];
    uint8_t *payload;
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool measured;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_DTPACKED_HEADER_SIZE + XX_DTPACKED_MIN_PACKED_SIZE) {
        return NULL;
    }
    if (!xx_dtpacked_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }

    /* "DT" alone is two bytes of nothing, so the version and flag words are
     * pinned too. Even those six bytes are a weak gate; the trial decode
     * below is what actually keeps this reader off unrelated files. */
    if (header[0] != (uint8_t)'D' || header[1] != (uint8_t)'T') return NULL;
    if (xx_dtpacked_le16(header + 2) != (uint16_t)XX_DTPACKED_VERSION) {
        return NULL;
    }
    if (xx_dtpacked_le16(header + 4) != (uint16_t)XX_DTPACKED_FLAGS) {
        return NULL;
    }

    /* The payload is everything behind the header: the container stores no
     * packed size, so end-of-file is the only boundary there is. */
    compressed_size = span - XX_DTPACKED_HEADER_SIZE;
    if (compressed_size < XX_DTPACKED_MIN_PACKED_SIZE) return NULL;
    if (compressed_size > XX_DTPACKED_MAX_DECODED) return NULL;

    uncompressed_size =
        (int64_t)xx_dtpacked_le32(header + XX_DTPACKED_RAWSIZE_OFFSET);
    /* A zero plaintext length would make extraction write an empty file and
     * call it success, so it is a reject rather than an empty member. */
    if (uncompressed_size < 1) return NULL;
    if (uncompressed_size > XX_DTPACKED_MAX_DECODED) return NULL;
    if (uncompressed_size >
        (compressed_size * XX_DTPACKED_MAX_RATIO) + XX_DTPACKED_RATIO_SLACK) {
        return NULL;
    }
    if (!xx_dtpacked_range_within(span, (int64_t)XX_DTPACKED_HEADER_SIZE,
                                  compressed_size)) {
        return NULL;
    }

    payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    if (!payload) return NULL;
    if (!xx_dtpacked_read_at(self,
                             self->base_address + XX_DTPACKED_HEADER_SIZE,
                             payload, (size_t)compressed_size)) {
        xx_mem_free(payload);
        return NULL;
    }
    /* Cheap prelude check first, so a stray "DT" hit costs nothing. */
    if (payload[0] > XX_DTPACKED_DCL_MAX_LITERAL_MODE ||
        payload[1] < XX_DTPACKED_DCL_MIN_DICT_BITS ||
        payload[1] > XX_DTPACKED_DCL_MAX_DICT_BITS) {
        xx_mem_free(payload);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return NULL;
    }
    /* The declared length doubles as the decoder's ceiling, so a stream that
     * wants to produce more than the header promises stops right there. */
    measured = xx_dcl_scan_memory(payload, (size_t)compressed_size,
                                  (size_t)uncompressed_size, &consumed,
                                  &produced);
    xx_mem_free(payload);
    if (!measured) return NULL;
    /* VERIFIED over the reference corpus: the plaintext the stream produces
     * is exactly the length the header declares. With no checksum anywhere,
     * this equality is the entire gate, and the check a later reader will be
     * tempted to loosen into "close enough". */
    if ((int64_t)produced != uncompressed_size) return NULL;
    /* Almost every stream ends exactly at EOF; a few carry diskette padding
     * behind the end-of-stream code, so trailing slack is tolerated but
     * overrun is not. */
    if (consumed < (size_t)XX_DTPACKED_MIN_PACKED_SIZE ||
        (int64_t)consumed > compressed_size) {
        return NULL;
    }

    stream = (xx_dtpacked_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_DTPACKED_PLACEHOLDER_NAME);
    if (!name) goto fail;
    if (!xx_dtpacked_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_DTPACKED_HEADER_SIZE;
    member.data_offset = self->base_address + XX_DTPACKED_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_DTPACKED_METHOD_DCL;
    /* Published as (date << 16) | time; the header stores the date word
     * first and the time word second, the reverse of the packed DOS order,
     * and swapping them yields plausible nonsense rather than an error. */
    member.timestamp =
        ((uint64_t)xx_dtpacked_le16(header + XX_DTPACKED_DOSDATE_OFFSET)
         << 16) |
        (uint64_t)xx_dtpacked_le16(header + XX_DTPACKED_DOSTIME_OFFSET);
    /* The wrapper has no directory entries and never will: it holds one
     * file. */
    member.is_folder = false;

    if (!xx_dtpacked_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_DTPACKED_MAX_MEMBERS) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_dtpacked_stream_free(stream);
    return NULL;
}



/* The container carries no method field - the payload is always a DCL
 * stream - so parse stamps this one synthetic value and decode refuses
 * anything else. Treating an unrecognised value as stored would hand the
 * caller garbage that looks exactly like data. */

/* rawSize is attacker-controlled, so the allocation it drives is capped. */

/* The single payload is a complete PKWARE DCL stream whose plaintext length
 * the header declares and parse has already reproduced with a trial scan. */
static bool xx_dtpacked_decode(Abstractformat *self,
                               const xx_dtpacked_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_DTPACKED_METHOD_DCL) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_DTPACKED_MAX_DECODED ||
        member->uncompressed_size > XX_DTPACKED_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_dtpacked_read_at(self, member->data_offset, input,
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
    /* Exactly the declared plaintext length, or nothing. The format has no
     * checksum, so this equality is the whole of extraction's correctness
     * check, and a short decode reported as success is the one failure the
     * caller cannot detect. */
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_dtpacked_init(xx_dtpacked *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_DTPACKED;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-delrina-dt");
    xx_format_set_extension(&archive->format, "dt");
    archive->format.check_is_valid = xx_dtpacked_check_is_valid;
    archive->format.handle_base_info = xx_dtpacked_handle_base_info;
    archive->format.get_format_size = xx_dtpacked_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dtpacked_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dtpacked_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dtpacked_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dtpacked_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dtpacked_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dtpacked_free_archive_records_reading;
    archive->format.destroy = xx_dtpacked_vtable_destroy;
}

xx_dtpacked *xx_dtpacked_create(xx_io_device *device, int64_t base_address) {
    xx_dtpacked *archive = (xx_dtpacked *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_dtpacked_init(archive, device, base_address);
    return archive;
}

void xx_dtpacked_destroy(xx_dtpacked *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_dtpacked_free(xx_dtpacked *archive) {
    if (!archive) return;
    xx_dtpacked_destroy(archive);
    xx_mem_free(archive);
}

static void xx_dtpacked_vtable_destroy(Abstractformat *self) {
    xx_dtpacked_destroy((xx_dtpacked *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_dtpacked_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dtpacked_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_dtpacked_parse(self, pd);
    if (!stream) return false;
    xx_dtpacked_stream_free(stream);
    return true;
}

bool xx_dtpacked_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dtpacked *archive = (xx_dtpacked *)self;
    xx_dtpacked_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_dtpacked_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_dtpacked_stream_free(stream);
    return true;
}

int64_t xx_dtpacked_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_dtpacked_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_dtpacked *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_dtpacked_set_record(xx_archive_record *record,
                                 const xx_dtpacked_member *member) {
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

static bool xx_dtpacked_copy_options(xx_list_s *target,
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

static const xx_var *xx_dtpacked_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_dtpacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_dtpacked_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_dtpacked_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_dtpacked_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_dtpacked_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_dtpacked_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_dtpacked_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_dtpacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dtpacked_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_dtpacked_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dtpacked_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_dtpacked_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dtpacked_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_dtpacked_stream *stream;
    const xx_dtpacked_member *member;
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
    stream = (xx_dtpacked_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_dtpacked_path_safe(member->name)) return false;

    path_option = xx_dtpacked_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_dtpacked_decode(self, member, &plain, &plain_size, pd);
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
        !xx_dtpacked_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_dtpacked_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
