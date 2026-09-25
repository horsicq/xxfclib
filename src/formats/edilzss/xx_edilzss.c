/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * EDI Install packed files (*.LZS and the "last character replaced by $"
 * install-media names: .DL$, .EX$, .TB$, .SB$, .TX$ ...). A single-payload
 * wrapper, not a multi-member archive, so the record list this reader
 * publishes always holds exactly one entry.
 *
 * Header, derived from the 58-sample reference corpus:
 *
 *   0x00   7  char    signature "EDILZSS"
 *   0x07   1  char    version digit, '1' or '2'
 *   0x08  13  char    ORIGINAL FILE NAME, NUL terminated and NUL padded.
 *                     Always present in version 2; optional in version 1
 *                     (see below).
 *   0x15   4  u32 LE  plaintext length -- VERSION 2 ONLY
 *   +      n  payload the LZSS stream, running to end of file
 *
 * So the payload begins at 0x19 for version 2, at 0x15 for a version 1 file
 * that carries a name, and at 0x08 for one that does not.
 *
 * THE OPTIONAL NAME IS THE ONE PIECE OF GUESSWORK HERE. Six version 1
 * samples, all of them *.LZS, put the LZSS stream straight after the
 * signature with no name field at all, and the format gives no flag saying
 * which shape a file has. The test used is the name field's own plausibility:
 * a run of printable ASCII terminated by a NUL inside the 13 bytes at 0x08.
 * It separates the corpus cleanly because an LZSS stream opens with its
 * control byte, and a compressor that starts with a run of literals -- which
 * is what every one of those six does -- writes 0xFF there. Version 2 is not
 * subject to any of this: its name field is mandatory, and a version 2 file
 * whose name field does not parse is rejected.
 *
 * The codec is the classic Okumura LZSS: 4096-byte ring prefilled with
 * spaces, write cursor starting at 4096-18, a flag byte whose bits are
 * consumed LSB first, 1 = one literal byte, 0 = a two-byte match
 * (position = ((b1 >> 4) << 8) | b0, length = (b1 & 0x0F) + 3). It is small
 * enough, and specific enough to this container, to live here rather than in
 * algo/.
 *
 * Version 2 stores the plaintext length and parse requires the walk to
 * reproduce it exactly -- there is no checksum, so that equality is the
 * format's only integrity check. Version 1 stores no length at all, and the
 * gate there is the eight-byte signature plus a stream that walks to the end
 * of the file without a truncated token.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/edilzss/xx_edilzss.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef EDILZSS
#define XX_EDILZSS_FILE_TYPE XX_FILE_TYPE_EDILZSS
#else
#define XX_EDILZSS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_EDILZSS_SIGNATURE_SIZE 8U
#define XX_EDILZSS_NAME_OFFSET 8U
#define XX_EDILZSS_NAME_SIZE 13U
#define XX_EDILZSS_SIZE_OFFSET 21U
#define XX_EDILZSS_V1_NAMED_DATA 21U
#define XX_EDILZSS_V1_BARE_DATA 8U
#define XX_EDILZSS_V2_DATA 25U
/* A stream cannot be shorter than a flag byte plus one literal. */
#define XX_EDILZSS_MIN_PACKED 2
#define XX_EDILZSS_MAX_INPUT ((int64_t)256 * 1024 * 1024)
#define XX_EDILZSS_MAX_OUTPUT ((int64_t)512 * 1024 * 1024)
#define XX_EDILZSS_MAX_MEMBERS 1
#define XX_EDILZSS_METHOD_LZSS 1U
#define XX_EDILZSS_PLACEHOLDER_NAME "edilzss_data"

/* Okumura LZSS parameters. */
#define XX_EDILZSS_RING_SIZE 4096U
#define XX_EDILZSS_LOOKAHEAD 18U
#define XX_EDILZSS_THRESHOLD 2U
#define XX_EDILZSS_RING_FILL 0x20U

typedef struct xx_edilzss_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t version;
    bool is_folder;
} xx_edilzss_member;

typedef struct xx_edilzss_stream_s {
    xx_edilzss_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_edilzss_stream;

static void xx_edilzss_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_edilzss_read_at(Abstractformat *self, int64_t offset,
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

static uint32_t xx_edilzss_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_edilzss_path_safe(const char *name) {
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

static void xx_edilzss_stream_free(void *pointer) {
    xx_edilzss_stream *stream = (xx_edilzss_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_edilzss_add(xx_edilzss_stream *stream,
                           const xx_edilzss_member *member) {
    xx_edilzss_member *grown = (xx_edilzss_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- codec -- */

/**
 * Walk one LZSS stream.
 *
 * @param output      Receives the plaintext, or NULL to only measure it.
 * @param output_size Capacity of @p output when it is given; the ceiling on
 *                    what a measuring pass is allowed to produce otherwise.
 *                    Either way the walk refuses to exceed it, so a hostile
 *                    stream cannot run away.
 * @param produced    Receives the plaintext length.
 * @return true when the stream ended on a token boundary at the end of its
 *         input. A stream cut off mid-token is a reject, not a short read.
 */
static bool xx_edilzss_lzss(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *produced, xx_pd_struct *pd) {
    uint8_t ring[XX_EDILZSS_RING_SIZE];
    size_t at = 0U;
    size_t written = 0U;
    size_t cursor = XX_EDILZSS_RING_SIZE - XX_EDILZSS_LOOKAHEAD;
    uint32_t flags = 0U;

    if (produced) *produced = 0U;
    if (!input) return false;
    xx_rt_memset(ring, XX_EDILZSS_RING_FILL, sizeof(ring));

    while (at < input_size) {
        unsigned bit;
        if ((flags & 0x100U) == 0U) {
            flags = (uint32_t)input[at++] | 0xff00U;
            /* A lone flag byte at the very end has no tokens behind it; the
             * corpus has such tails and they are not corruption. */
            if (at >= input_size) break;
            if (pd && xx_pd_is_stopped(pd)) return false;
        }
        bit = flags & 1U;
        flags >>= 1U;
        if (bit) {
            uint8_t value = input[at++];
            if (written >= output_size) return false;
            if (output) output[written] = value;
            ++written;
            ring[cursor] = value;
            cursor = (cursor + 1U) & (XX_EDILZSS_RING_SIZE - 1U);
        } else {
            unsigned position, length, index;
            uint8_t low, high;
            if (at + 1U >= input_size) return false;
            low = input[at];
            high = input[at + 1U];
            at += 2U;
            position = (unsigned)(((high >> 4) << 8) | low);
            length = (unsigned)(high & 0x0fU) + XX_EDILZSS_THRESHOLD + 1U;
            /* written <= output_size is an invariant here, so the
             * subtraction is the safe direction: output_size - length could
             * wrap when the ceiling is smaller than one match. */
            if (length > output_size - written) return false;
            for (index = 0U; index < length; ++index) {
                uint8_t value =
                    ring[(position + index) & (XX_EDILZSS_RING_SIZE - 1U)];
                if (output) output[written] = value;
                ++written;
                ring[cursor] = value;
                cursor = (cursor + 1U) & (XX_EDILZSS_RING_SIZE - 1U);
            }
        }
    }
    if (produced) *produced = written;
    return true;
}

/* ------------------------------------------------------------- parsing -- */

/* Is the 13-byte field at 0x08 a name rather than the head of a stream? */
static bool xx_edilzss_name_field_plausible(const uint8_t *header,
                                            size_t available) {
    size_t index;

    if (available < XX_EDILZSS_NAME_OFFSET + XX_EDILZSS_NAME_SIZE) return false;
    for (index = 0U; index < XX_EDILZSS_NAME_SIZE; ++index) {
        uint8_t c = header[XX_EDILZSS_NAME_OFFSET + index];
        if (c == 0U) return index != 0U; /* an empty name is not a name */
        if (c < 0x20U || c >= 0x7fU) return false;
    }
    return false; /* no terminator inside the field */
}

/* Copy the stored name into something safe to create on disk. DOS 8.3 ASCII,
 * so a directory prefix or a drive letter is dropped rather than honoured. */
static char *xx_edilzss_make_name(const uint8_t *field) {
    char name[XX_EDILZSS_NAME_SIZE + 1U];
    size_t length = 0U;
    size_t start = 0U;
    size_t index;

    while (length < XX_EDILZSS_NAME_SIZE && field[length] != 0U) ++length;
    if (length == 0U) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        if (c == '/' || c == '\\' || c == ':') start = index + 1U;
    }
    if (start >= length) return NULL;
    for (index = start; index < length; ++index) {
        uint8_t c = field[index];
        if (c < 0x20U || c >= 0x7fU || c == '"' || c == '*' || c == '<' ||
            c == '>' || c == '?' || c == '|') {
            return NULL;
        }
        name[index - start] = (char)c;
    }
    name[length - start] = 0;
    return xx_str_dup(name);
}

static xx_edilzss_stream *xx_edilzss_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_edilzss_stream *stream;
    xx_edilzss_member member;
    uint8_t header[XX_EDILZSS_V2_DATA];
    uint8_t *payload = NULL;
    char *name = NULL;
    int64_t total, span, compressed_size;
    int64_t declared = -1;
    size_t produced = 0U;
    size_t header_size;
    size_t available;
    uint32_t version;
    bool named;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_EDILZSS_SIGNATURE_SIZE + XX_EDILZSS_MIN_PACKED ||
        span > XX_EDILZSS_MAX_INPUT) {
        return NULL;
    }
    available = span < (int64_t)sizeof(header) ? (size_t)span : sizeof(header);
    if (!xx_edilzss_read_at(self, self->base_address, header, available)) {
        return NULL;
    }
    if (xx_rt_memcmp(header, "EDILZSS", 7U) != 0) return NULL;
    if (header[7] == (uint8_t)'1') {
        version = 1U;
    } else if (header[7] == (uint8_t)'2') {
        version = 2U;
    } else {
        return NULL;
    }

    named = xx_edilzss_name_field_plausible(header, available);
    if (version == 2U) {
        /* Version 2's name field and length are both mandatory; a file that
         * fails either is not this format rather than a nameless variant. */
        if (!named || available < XX_EDILZSS_V2_DATA) return NULL;
        header_size = XX_EDILZSS_V2_DATA;
        declared = (int64_t)xx_edilzss_le32(header + XX_EDILZSS_SIZE_OFFSET);
        if (declared < 1 || declared > XX_EDILZSS_MAX_OUTPUT) return NULL;
    } else {
        header_size = named ? XX_EDILZSS_V1_NAMED_DATA : XX_EDILZSS_V1_BARE_DATA;
        if (available < header_size) return NULL;
    }

    compressed_size = span - (int64_t)header_size;
    if (compressed_size < XX_EDILZSS_MIN_PACKED) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    if (!payload) return NULL;
    if (!xx_edilzss_read_at(self, self->base_address + (int64_t)header_size,
                            payload, (size_t)compressed_size)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* The walk is the validation. For version 2 the declared length doubles
     * as the decoder's ceiling, so a stream that wants to produce more than
     * the header promises stops right there. */
    if (!xx_edilzss_lzss(payload, (size_t)compressed_size, NULL,
                         declared >= 0 ? (size_t)declared
                                       : (size_t)XX_EDILZSS_MAX_OUTPUT,
                         &produced, pd)) {
        goto fail;
    }
    xx_mem_free(payload);
    payload = NULL;
    if (produced == 0U || (int64_t)produced > XX_EDILZSS_MAX_OUTPUT) return NULL;
    /* VERIFIED over the reference corpus: version 2's stream produces
     * exactly the length its header declares. With no checksum anywhere,
     * this equality is the entire gate, and the check a later reader will be
     * tempted to loosen into "close enough". */
    if (declared >= 0 && (int64_t)produced != declared) return NULL;

    stream = (xx_edilzss_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (named) name = xx_edilzss_make_name(header + XX_EDILZSS_NAME_OFFSET);
    if (!name || !xx_edilzss_path_safe(name)) {
        xx_str_free(name);
        name = xx_str_dup(XX_EDILZSS_PLACEHOLDER_NAME);
        if (!name) goto fail_stream;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = (int64_t)header_size;
    member.data_offset = self->base_address + (int64_t)header_size;
    member.compressed_size = compressed_size;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_EDILZSS_METHOD_LZSS;
    member.version = version;
    member.is_folder = false;
    if (!xx_edilzss_add(stream, &member)) goto fail_stream;
    name = NULL;
    if (stream->count != (size_t)XX_EDILZSS_MAX_MEMBERS) goto fail_stream;
    stream->archive_size = span;
    return stream;

fail_stream:
    xx_str_free(name);
    xx_edilzss_stream_free(stream);
    return NULL;
fail:
    xx_mem_free(payload);
    return NULL;
}

/* The payload's plaintext length was measured by parse's trial walk, so the
 * allocation below is bounded by what the decoder itself produced. */
static bool xx_edilzss_decode(Abstractformat *self,
                              const xx_edilzss_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_EDILZSS_METHOD_LZSS) return false;
    if (member->compressed_size < XX_EDILZSS_MIN_PACKED ||
        member->compressed_size > XX_EDILZSS_MAX_INPUT) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_EDILZSS_MAX_OUTPUT) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_edilzss_read_at(self, member->data_offset, input,
                            (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_edilzss_lzss(input, (size_t)member->compressed_size, output,
                         (size_t)member->uncompressed_size, &written, pd) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_edilzss_init(xx_edilzss *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_EDILZSS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-edi-lzss");
    xx_format_set_extension(&archive->format, "lzs");
    archive->format.check_is_valid = xx_edilzss_check_is_valid;
    archive->format.handle_base_info = xx_edilzss_handle_base_info;
    archive->format.get_format_size = xx_edilzss_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_edilzss_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_edilzss_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_edilzss_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_edilzss_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_edilzss_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_edilzss_free_archive_records_reading;
    archive->format.destroy = xx_edilzss_vtable_destroy;
}

xx_edilzss *xx_edilzss_create(xx_io_device *device, int64_t base_address) {
    xx_edilzss *archive = (xx_edilzss *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_edilzss_init(archive, device, base_address);
    return archive;
}

void xx_edilzss_destroy(xx_edilzss *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_edilzss_free(xx_edilzss *archive) {
    if (!archive) return;
    xx_edilzss_destroy(archive);
    xx_mem_free(archive);
}

static void xx_edilzss_vtable_destroy(Abstractformat *self) {
    xx_edilzss_destroy((xx_edilzss *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_edilzss_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_edilzss_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_edilzss_parse(self, pd);
    if (!stream) return false;
    xx_edilzss_stream_free(stream);
    return true;
}

bool xx_edilzss_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_edilzss *archive = (xx_edilzss *)self;
    xx_edilzss_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_edilzss_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_edilzss_stream_free(stream);
    return true;
}

int64_t xx_edilzss_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_edilzss_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_edilzss *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_edilzss_set_record(xx_archive_record *record,
                                  const xx_edilzss_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_VERSION_MADE_BY,
                                          member->version) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_edilzss_copy_options(xx_list_s *target,
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

static const xx_var *xx_edilzss_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_edilzss_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_edilzss_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_edilzss_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_edilzss_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_edilzss_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_edilzss_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_edilzss_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_edilzss_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_edilzss_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_edilzss_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_edilzss_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_edilzss_set_record(&state->current_record,
                                              &stream->items[stream->index]);
    return state->has_record;
}

bool xx_edilzss_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_edilzss_stream *stream;
    const xx_edilzss_member *member;
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
    stream = (xx_edilzss_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_edilzss_path_safe(member->name)) return false;

    path_option =
        xx_edilzss_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_edilzss_decode(self, member, &plain, &plain_size, pd);
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
        !xx_edilzss_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_edilzss_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
