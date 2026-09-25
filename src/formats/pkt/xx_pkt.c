/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FidoNet mail packets (FTS-0001 type 2).
 *
 *   packet header, 0x3a bytes at offset 0, all words little endian:
 *     0x00  u16  originating node
 *     0x02  u16  destination node
 *     0x04  u16  year   (full, e.g. 1995)
 *     0x06  u16  month  (0 based: 0 = January)
 *     0x08  u16  day    (1..31)
 *     0x0a  u16  hour   (0..23)
 *     0x0c  u16  minute (0..59)
 *     0x0e  u16  second (0..59)
 *     0x10  u16  baud rate
 *     0x12  u16  packet type; MUST be 2
 *     0x14  u16  originating net
 *     0x16  u16  destination net
 *     0x18  ...  product code, revision, password, zone words, filler
 *
 *   message record, from 0x3a onward:
 *     0x00  u16  message type; MUST be 2, and a 0 here is the packet
 *                terminator rather than a record
 *     0x02  u16  originating node
 *     0x04  u16  destination node
 *     0x06  u16  originating net
 *     0x08  u16  destination net
 *     0x0a  u16  attribute word
 *     0x0c  u16  cost
 *     0x0e  ...  five NUL-terminated strings: DateTime, To, From, Subject,
 *                Body. The record ends on the fifth NUL; nothing stores its
 *                length.
 *
 *   packet terminator: a u16 0 where the next record would start. A tail
 *   shorter than three bytes is treated as a terminator too, which is how
 *   the reference stops on packets cut mid-terminator.
 *
 * There is no magic anywhere in this container. What stands in for one is a
 * set of cross-checks between the packet header and the FIRST message
 * record: the type word is 2 in both, and the four address words at 0x00,
 * 0x02, 0x14 and 0x16 are repeated verbatim at 0x3c, 0x3e, 0x40 and 0x42.
 * Six independent equalities plus a plausible timestamp is the whole
 * detector, and loosening any one of them makes arbitrary binary data match.
 *
 * Members are not compressed: each is a packed message record rendered to
 * text as
 *
 *     "Date    = " <date> CR "To      = " <to> CR "From    = " <from> CR
 *     "Subject = " <subject> CR CR <body> CR
 *
 * The container stores no rendered length, so parse measures each record
 * with the codec's own scan entry point -- the same core routine the decode
 * uses, so the two can never disagree.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pkt/xx_pkt.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/pkt/xx_pkt.h"

#include <stdio.h>

#define XX_PKT_COPY_CHUNK (64 * 1024)

typedef struct xx_pkt_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_pkt_member;

typedef struct xx_pkt_stream_s {
    xx_pkt_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_pkt_stream;

static void xx_pkt_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_pkt_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_pkt_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_pkt_path_safe(const char *name) {
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

static void xx_pkt_stream_free(void *pointer) {
    xx_pkt_stream *stream = (xx_pkt_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_pkt_add(xx_pkt_stream *stream,
                          const xx_pkt_member *member) {
    xx_pkt_member *grown = (xx_pkt_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_PKT_HEADER_SIZE 0x3a
#define XX_PKT_MESSAGE_HEADER_SIZE 14
#define XX_PKT_TYPE_2 2U
#define XX_PKT_MAX_MEMBERS 200000
#define XX_PKT_MAX_MESSAGE_SIZE ((int64_t)0x1000000)
#define XX_PKT_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_pkt_le16(const uint8_t *data);
static xx_pkt_stream *xx_pkt_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_pkt_decode(Abstractformat *self, const xx_pkt_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Both the packet and every message carry this in their type word. */
/* A packed message is bounded by the FidoNet transport in practice; this cap
 * only keeps a corrupt packet from being walked to EOF looking for the fifth
 * NUL, and bounds the window parse reads per record. */
/* The rendering is a little longer than the record, never wildly so; the
 * ceiling is what the scan is told to refuse. */

static uint16_t xx_pkt_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static xx_pkt_stream *xx_pkt_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_pkt_stream *stream = NULL;
    uint8_t *window = NULL;
    /* The packet header and the fixed part of the first message record are
     * read together: every cross-check below lives inside that window. */
    uint8_t header[XX_PKT_HEADER_SIZE + XX_PKT_MESSAGE_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t window_size;
    int64_t count = 0;
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)sizeof(header)) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_pkt_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* Headerless format: these eight lines ARE the detector. The two type
     * words say "type 2 packet, type 2 message", and the four equalities
     * say that the first record readdresses itself to exactly the same node
     * and net pair the packet header names -- which is true of every packet
     * a FidoNet mailer ever wrote and essentially never true by accident.
     * Dropping any single one of them makes random binary data match. */
    if (xx_pkt_le16(header + 0x12) != XX_PKT_TYPE_2) return NULL;
    if (xx_pkt_le16(header + 0x3a) != XX_PKT_TYPE_2) return NULL;
    if (xx_pkt_le16(header + 0x00) != xx_pkt_le16(header + 0x3c)) return NULL;
    if (xx_pkt_le16(header + 0x02) != xx_pkt_le16(header + 0x3e)) return NULL;
    if (xx_pkt_le16(header + 0x14) != xx_pkt_le16(header + 0x40)) return NULL;
    if (xx_pkt_le16(header + 0x16) != xx_pkt_le16(header + 0x42)) return NULL;

    year = xx_pkt_le16(header + 0x04);
    month = xx_pkt_le16(header + 0x06);
    day = xx_pkt_le16(header + 0x08);
    hour = xx_pkt_le16(header + 0x0a);
    minute = xx_pkt_le16(header + 0x0c);
    second = xx_pkt_le16(header + 0x0e);
    /* Second half of the detector: the packet date has to be a date. The
     * month is 0 based in FTS-0001, so 11 is December and 12 is wrong. */
    if (year <= 1899U || year >= 3000U) return NULL;
    if (month > 11U) return NULL;
    if (day < 1U || day > 31U) return NULL;
    if (hour > 23U || minute > 59U || second > 59U) return NULL;

    /* A packet always has a destination and both nets; zero in any of them
     * is a field that was never filled in, not an address. */
    if (xx_pkt_le16(header + 0x02) == 0U) return NULL;
    if (xx_pkt_le16(header + 0x14) == 0U) return NULL;
    if (xx_pkt_le16(header + 0x16) == 0U) return NULL;

    /* One scratch window, reused for every record: the scan needs the
     * record's bytes, and nothing before the fifth NUL says how many that
     * is, so the read has to be speculative and bounded. */
    window_size = span - (int64_t)XX_PKT_HEADER_SIZE;
    if (window_size > XX_PKT_MAX_MESSAGE_SIZE) {
        window_size = XX_PKT_MAX_MESSAGE_SIZE;
    }
    if (window_size < XX_PKT_MESSAGE_HEADER_SIZE) return NULL;

    stream = (xx_pkt_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    window = (uint8_t *)xx_mem_alloc((size_t)window_size);
    if (!window) goto fail;

    offset = XX_PKT_HEADER_SIZE;
    for (;;) {
        xx_pkt_member member;
        char name_buffer[32];
        uint8_t type_word[2];
        size_t chunk;
        size_t consumed = 0U;
        size_t produced = 0U;
        int64_t remaining;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        remaining = span - offset;
        /* Fewer than three bytes left cannot hold a record, so it is the
         * terminator whether or not the two zero bytes are actually
         * there. */
        if (remaining < 3) break;
        if (count >= XX_PKT_MAX_MEMBERS) goto fail;

        if (!xx_pkt_read_at(self, self->base_address + offset, type_word,
                            sizeof(type_word))) {
            goto fail;
        }
        /* The explicit end of the packet. */
        if (xx_pkt_le16(type_word) == 0U) break;
        /* Past the terminator every record must be a type 2 message: a word
         * that is neither 0 nor 2 means the chain has desynchronised, and
         * continuing would publish members carved out of noise. */
        if (xx_pkt_le16(type_word) != XX_PKT_TYPE_2) goto fail;
        if (!xx_pkt_range_within(span, offset, XX_PKT_MESSAGE_HEADER_SIZE)) {
            goto fail;
        }

        chunk = (size_t)(remaining < window_size ? remaining : window_size);
        if (!xx_pkt_read_at(self, self->base_address + offset, window,
                            chunk)) {
            goto fail;
        }
        /* The record's extent and its rendered length in one call. The scan
         * fails unless all five strings terminate inside the window, which
         * is also what proves the record is complete. */
        if (!xx_pkt_scan_memory(window, chunk, (size_t)XX_PKT_MAX_DECODED,
                                &consumed, &produced)) {
            goto fail;
        }
        if (consumed < (size_t)XX_PKT_MESSAGE_HEADER_SIZE) goto fail;
        if ((int64_t)consumed > XX_PKT_MAX_MESSAGE_SIZE) goto fail;
        if (produced == 0U) goto fail;
        if (!xx_pkt_range_within(span, offset, (int64_t)consumed)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        /* Nothing in a packed message names a file; the reference numbers
         * the records in packet order, and so does this. */
        if (xx_rt_snprintf(name_buffer, sizeof(name_buffer), "%03lld.txt",
                           (long long)(count + 1)) <= 0) {
            goto fail;
        }
        member.name = xx_str_dup(name_buffer);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_PKT_MESSAGE_HEADER_SIZE;
        /* data_offset is the record's start, header included: the renderer
         * consumes the whole record, not just the string area. */
        member.data_offset = self->base_address + offset;
        member.compressed_size = (int64_t)consumed;
        member.uncompressed_size = (int64_t)produced;
        member.method = XX_PKT_TYPE_2;
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_pkt_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        ++count;
        offset += (int64_t)consumed;
    }

    /* A header alone is not a packet: the cross-checks above already read
     * into the first record, so a packet that publishes nothing means those
     * checks matched something that is not a packet at all. */
    if (count < 1) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    xx_mem_free(window);
    /* The walk stopped at the terminator; anything past it is overlay. */
    stream->archive_size = offset;
    return stream;

fail:
    if (window) xx_mem_free(window);
    xx_pkt_stream_free(stream);
    return NULL;
}


/* Render one packed message record. The record's own bytes, binary header
 * included, are the codec's input: the renderer needs the five strings that
 * follow that header, so data_offset is the record's start, not its body. */
static bool xx_pkt_decode(Abstractformat *self, const xx_pkt_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The only "method" this container has is the message type word, and 2
     * is the only type FTS-0001 defines. Anything else must fail rather
     * than fall back to a stored copy: a stored copy would hand the caller
     * the binary record dressed as the rendered text. */
    if (member->method != XX_PKT_TYPE_2) return false;
    if (member->compressed_size < XX_PKT_MESSAGE_HEADER_SIZE) return false;
    if (member->compressed_size > XX_PKT_MAX_MESSAGE_SIZE) return false;
    if (member->uncompressed_size < 1) return false;
    if (member->uncompressed_size > XX_PKT_MAX_DECODED) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_pkt_read_at(self, member->data_offset, input,
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
    /* parse measured this length with xx_pkt_scan_memory, which shares its
     * core with the renderer, so a mismatch here means the record changed
     * underfoot or the member was hand-edited. Either way it is a failure,
     * never a short buffer. */
    if (!xx_pkt_decode_memory(input, (size_t)member->compressed_size, output,
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

void xx_pkt_init(xx_pkt *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PKT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-fidonet-packet");
    xx_format_set_extension(&archive->format, "pkt");
    archive->format.check_is_valid = xx_pkt_check_is_valid;
    archive->format.handle_base_info = xx_pkt_handle_base_info;
    archive->format.get_format_size = xx_pkt_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pkt_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pkt_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pkt_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pkt_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pkt_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pkt_free_archive_records_reading;
    archive->format.destroy = xx_pkt_vtable_destroy;
}

xx_pkt *xx_pkt_create(xx_io_device *device, int64_t base_address) {
    xx_pkt *archive = (xx_pkt *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pkt_init(archive, device, base_address);
    return archive;
}

void xx_pkt_destroy(xx_pkt *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pkt_free(xx_pkt *archive) {
    if (!archive) return;
    xx_pkt_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pkt_vtable_destroy(Abstractformat *self) {
    xx_pkt_destroy((xx_pkt *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pkt_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pkt_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_pkt_parse(self, pd);
    if (!stream) return false;
    xx_pkt_stream_free(stream);
    return true;
}

bool xx_pkt_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pkt *archive = (xx_pkt *)self;
    xx_pkt_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pkt_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_pkt_stream_free(stream);
    return true;
}

int64_t xx_pkt_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pkt_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pkt *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_pkt_set_record(xx_archive_record *record,
                                 const xx_pkt_member *member) {
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

static bool xx_pkt_copy_options(xx_list_s *target,
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

static const xx_var *xx_pkt_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pkt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pkt_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pkt_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pkt_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pkt_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_pkt_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_pkt_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pkt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pkt_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_pkt_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pkt_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_pkt_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pkt_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_pkt_stream *stream;
    const xx_pkt_member *member;
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
    stream = (xx_pkt_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_pkt_path_safe(member->name)) return false;

    path_option = xx_pkt_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_pkt_decode(self, member, &plain, &plain_size, pd);
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
        !xx_pkt_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_pkt_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
