/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Turbo Packer ("TPWM") packed files, the small single-file cruncher found on
 * Amiga and Atari collections. This is a wrapper around ONE payload, not a
 * multi-member archive, so the record list this reader publishes always holds
 * exactly one entry.
 *
 *   header, 8 bytes at offset 0:
 *     0x00   4  char     magic "TPWM"
 *     0x04   4  u32 BE   rawSize, the length of the plaintext
 *     0x08   n  payload  the LZ77 stream, running to end-of-file
 *
 * There is no name, no timestamp, no checksum and no packed-size field: the
 * eight bytes above are the whole container. The payload is a bit tagged
 * LZ77 stream, MSB first:
 *
 *     bit 0  ->  one literal byte follows
 *     bit 1  ->  two bytes follow, b1 and b2:
 *                  distance = ((b1 & 0xf0) << 4) | b2     (12 bits, backwards
 *                                                          from the current
 *                                                          output position)
 *                  count    = (b1 & 0x0f) + 3             (3 .. 18 bytes)
 *
 * The tag byte is re-read from the same stream as soon as its eight bits are
 * spent, so tags and payload bytes interleave. A match may overlap the bytes
 * it is producing (distance below count is normal run-length coding) and the
 * final match is clamped to what rawSize still has room for, which is the one
 * place the declared length changes the decode rather than merely bounding it.
 *
 * The window is a 4096 byte RING that starts out zeroed, addressed as
 * (outputPosition - distance) & 0xfff. Two consequences matter: distance zero
 * is a legal back reference naming the byte one whole window earlier, and a
 * distance larger than the number of bytes produced so far lands on a ring
 * slot this stream has not written, which reads as zero. Files in the corpus
 * use both.
 *
 * The bit/byte layout above is DOCUMENTED: it is the same decode as Teemu
 * Suutari's ancient (src/TPWMDecompressor.cpp), and the corpus agrees with it
 * byte for byte. Everything else here - the ratio ceiling, the trial scan,
 * the treatment of trailing bytes - is this reader's own policy.
 *
 * rawSize is the format's ONLY integrity check: there is no CRC anywhere, so
 * parse trial-scans the stream with tpwm_scan(), which walks the tokens
 * without materialising any output, and requires the produced length to be
 * exactly rawSize. Four magic bytes are far too weak a gate on their own.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tpwm/xx_tpwm.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as TPWM is registered there.
 * Until then the reader identifies itself as unknown rather than borrowing
 * another format's id. See the port report for the registration this needs. */
#ifdef TPWM
#define XX_TPWM_FILE_TYPE XX_FILE_TYPE_TPWM
#else
#define XX_TPWM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_TPWM_HEADER_SIZE 8
/* A stream cannot say anything at all in fewer than one tag byte plus one
 * payload byte, and ancient refuses anything under twelve bytes outright. */
#define XX_TPWM_MIN_FILE_SIZE 12
#define XX_TPWM_MIN_PACKED_SIZE 2
#define XX_TPWM_MAX_MEMBERS 1
#define XX_TPWM_METHOD_LZ 1U
#define XX_TPWM_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* The best this codec can do is eighteen plaintext bytes for one tag bit plus
 * two payload bytes, i.e. 18 bytes per 17 bits, a hair under 8.5x. Nine plus a
 * token's worth of slack is therefore a true ceiling for the format and not a
 * guess: a header claiming a gigabyte behind a few hundred bytes is refused
 * before anything is allocated. */
#define XX_TPWM_MAX_RATIO 9
#define XX_TPWM_RATIO_SLACK 18
#define XX_TPWM_MATCH_MIN_COUNT 3
/* The packer keeps a 4096 byte ring window, which is what a twelve bit
 * distance addresses and what makes distance zero mean "one window back". */
#define XX_TPWM_WINDOW_SIZE 4096U
/* The original file name is stored nowhere - the packer replaces the file in
 * place - and this reader cannot see the container's own name, so the single
 * record gets a fixed, deliberately extension-less placeholder. */
#define XX_TPWM_PLACEHOLDER_NAME "tpwm_data"

typedef struct xx_tpwm_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    bool is_folder;
} xx_tpwm_member;

typedef struct xx_tpwm_stream_s {
    xx_tpwm_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_tpwm_stream;

static void xx_tpwm_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_tpwm_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static bool xx_tpwm_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_tpwm_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_tpwm_path_safe(const char *name) {
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

static void xx_tpwm_stream_free(void *pointer) {
    xx_tpwm_stream *stream = (xx_tpwm_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_tpwm_add(xx_tpwm_stream *stream, const xx_tpwm_member *member) {
    xx_tpwm_member *grown = (xx_tpwm_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- codec -- */

/* One pass over the token stream.
 *
 * @p output is optional: when it is NULL the walk only measures, which is what
 * parse wants - a header claiming a large plaintext is verified without
 * allocating a byte for it. When it is given it must have room for @p limit
 * bytes, and @p limit is always the declared rawSize, so the decoder can never
 * be talked into writing past the buffer the caller sized from that field.
 *
 * Returns false when the input is exhausted before @p limit bytes have been
 * produced: that means the stream is not what the header says it is. A back
 * reference that reaches past the start of the plaintext is NOT a fault - the
 * window is a zeroed ring, so it reads as zero - and refusing it was what made
 * this reader turn away streams the reference decoder accepts. */
static bool xx_tpwm_scan(const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t limit, size_t *consumed,
                         size_t *produced) {
    size_t position = 0U;
    size_t written = 0U;
    uint8_t tag = 0U;
    unsigned bits_left = 0U;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input) return false;

    while (written < limit) {
        unsigned bit;

        if (bits_left == 0U) {
            if (position >= input_size) return false;
            tag = input[position++];
            bits_left = 8U;
        }
        bit = (unsigned)((tag >> 7) & 1U);
        tag = (uint8_t)(tag << 1);
        --bits_left;

        if (bit == 0U) {
            if (position >= input_size) return false;
            if (output) output[written] = input[position];
            ++position;
            ++written;
            continue;
        }
        {
            uint8_t first, second;
            size_t distance, count, index;

            if (input_size - position < 2U) return false;
            first = input[position];
            second = input[position + 1U];
            position += 2U;
            distance = (size_t)(((uint32_t)(first & 0xf0U) << 4) |
                                (uint32_t)second);
            count = (size_t)(first & 0x0fU) + XX_TPWM_MATCH_MIN_COUNT;
            /* The packer's window is a 4096 byte ring that starts zeroed and
             * is addressed as (position - distance) & 0xfff, so a distance of
             * zero names the byte one whole window back, and a distance that
             * reaches before the first output byte lands on a ring slot this
             * stream has not written yet - a zero.  Neither is a fault; both
             * happen in real files, and refusing them is what made this
             * reader reject streams the reference decoder accepts. */
            if (distance == 0U) distance = XX_TPWM_WINDOW_SIZE;
            /* The last match of a stream routinely overshoots the declared
             * length; the surplus is dropped rather than treated as a fault. */
            if (count > limit - written) count = limit - written;
            for (index = 0U; index < count; ++index) {
                if (output) {
                    output[written] = distance > written
                                          ? (uint8_t)0
                                          : output[written - distance];
                }
                ++written;
            }
        }
    }
    if (consumed) *consumed = position;
    if (produced) *produced = written;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_tpwm_stream *xx_tpwm_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_tpwm_stream *stream;
    xx_tpwm_member member;
    uint8_t header[XX_TPWM_HEADER_SIZE];
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
    if (span < XX_TPWM_MIN_FILE_SIZE) return NULL;
    if (!xx_tpwm_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != (uint8_t)'T' || header[1] != (uint8_t)'P' ||
        header[2] != (uint8_t)'W' || header[3] != (uint8_t)'M') {
        return NULL;
    }

    /* The container stores no packed size, so end-of-file is the only
     * boundary there is. */
    compressed_size = span - XX_TPWM_HEADER_SIZE;
    if (compressed_size < XX_TPWM_MIN_PACKED_SIZE) return NULL;
    if (compressed_size > XX_TPWM_MAX_DECODED) return NULL;

    uncompressed_size = (int64_t)xx_tpwm_be32(header + 4);
    /* A zero plaintext length would make extraction write an empty file and
     * call it success, so it is a reject rather than an empty member. */
    if (uncompressed_size < 1) return NULL;
    if (uncompressed_size > XX_TPWM_MAX_DECODED) return NULL;
    if (uncompressed_size >
        (compressed_size * XX_TPWM_MAX_RATIO) + XX_TPWM_RATIO_SLACK) {
        return NULL;
    }
    if (!xx_tpwm_range_within(span, (int64_t)XX_TPWM_HEADER_SIZE,
                              compressed_size)) {
        return NULL;
    }

    payload = (uint8_t *)xx_mem_alloc((size_t)compressed_size);
    if (!payload) return NULL;
    if (!xx_tpwm_read_at(self, self->base_address + XX_TPWM_HEADER_SIZE,
                         payload, (size_t)compressed_size)) {
        xx_mem_free(payload);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return NULL;
    }
    /* Measure only: the trial walk allocates nothing for the plaintext, so a
     * header claiming the ceiling costs a walk and not 256 MiB. */
    measured = xx_tpwm_scan(payload, (size_t)compressed_size, NULL,
                            (size_t)uncompressed_size, &consumed, &produced);
    xx_mem_free(payload);
    if (!measured) return NULL;
    /* VERIFIED over the reference corpus: the stream produces exactly the
     * length the header declares. With no checksum anywhere this equality is
     * the entire gate, and it is the check a later reader will be tempted to
     * loosen into "close enough". */
    if ((int64_t)produced != uncompressed_size) return NULL;
    /* Most streams end exactly at end-of-file; some carry padding or unrelated
     * bytes behind the last token, so trailing slack is tolerated and reported
     * as overlay, while an overrun is impossible by construction. */
    if (consumed < (size_t)XX_TPWM_MIN_PACKED_SIZE ||
        (int64_t)consumed > compressed_size) {
        return NULL;
    }

    stream = (xx_tpwm_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_TPWM_PLACEHOLDER_NAME);
    if (!name) goto fail;
    if (!xx_tpwm_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_TPWM_HEADER_SIZE;
    member.data_offset = self->base_address + XX_TPWM_HEADER_SIZE;
    /* The member's stream is what the scan actually read, not the rest of the
     * file: that is what makes the bytes behind it overlay. */
    member.compressed_size = (int64_t)consumed;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_TPWM_METHOD_LZ;
    /* The wrapper has no directory entries and never will: it holds one
     * file. */
    member.is_folder = false;

    if (!xx_tpwm_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_TPWM_MAX_MEMBERS) goto fail;
    stream->archive_size = (int64_t)XX_TPWM_HEADER_SIZE + (int64_t)consumed;
    return stream;

fail:
    xx_tpwm_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* The payload is one complete Turbo Packer stream whose plaintext length the
 * header declares and parse has already reproduced with a trial scan. */
static bool xx_tpwm_decode(Abstractformat *self, const xx_tpwm_member *member,
                           uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_TPWM_METHOD_LZ) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_TPWM_MAX_DECODED ||
        member->uncompressed_size > XX_TPWM_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_tpwm_read_at(self, member->data_offset, input,
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
    if (!xx_tpwm_scan(input, (size_t)member->compressed_size, output,
                      (size_t)member->uncompressed_size, NULL, &written) ||
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

void xx_tpwm_init(xx_tpwm *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* The one multi-byte field in the container is big endian. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_TPWM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-tpwm");
    xx_format_set_extension(&archive->format, "tpwm");
    archive->format.check_is_valid = xx_tpwm_check_is_valid;
    archive->format.handle_base_info = xx_tpwm_handle_base_info;
    archive->format.get_format_size = xx_tpwm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tpwm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tpwm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tpwm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tpwm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tpwm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tpwm_free_archive_records_reading;
    archive->format.destroy = xx_tpwm_vtable_destroy;
}

xx_tpwm *xx_tpwm_create(xx_io_device *device, int64_t base_address) {
    xx_tpwm *archive = (xx_tpwm *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_tpwm_init(archive, device, base_address);
    return archive;
}

void xx_tpwm_destroy(xx_tpwm *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_tpwm_free(xx_tpwm *archive) {
    if (!archive) return;
    xx_tpwm_destroy(archive);
    xx_mem_free(archive);
}

static void xx_tpwm_vtable_destroy(Abstractformat *self) {
    xx_tpwm_destroy((xx_tpwm *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_tpwm_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tpwm_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_tpwm_parse(self, pd);
    if (!stream) return false;
    xx_tpwm_stream_free(stream);
    return true;
}

bool xx_tpwm_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tpwm *archive = (xx_tpwm *)self;
    xx_tpwm_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_tpwm_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;

    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    xx_tpwm_stream_free(stream);
    return true;
}

int64_t xx_tpwm_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_tpwm_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_tpwm *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_tpwm_set_record(xx_archive_record *record,
                               const xx_tpwm_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_tpwm_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_tpwm_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_tpwm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tpwm_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_tpwm_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_tpwm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_tpwm_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_tpwm_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_tpwm_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_tpwm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_tpwm_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_tpwm_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tpwm_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_tpwm_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_tpwm_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_tpwm_stream *stream;
    const xx_tpwm_member *member;
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
    stream = (xx_tpwm_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_tpwm_path_safe(member->name)) return false;

    path_option = xx_tpwm_get_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_tpwm_decode(self, member, &plain, &plain_size, pd);
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
        !xx_tpwm_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_tpwm_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
