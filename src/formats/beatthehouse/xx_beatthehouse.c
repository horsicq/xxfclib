/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Beat The House packed files ("PAK?" streams).  The layout below was derived
 * from the 456 samples in "F:\ARC\ARC\Beat The House"; no published
 * description of the format was available.
 *
 *   header, 8 bytes at offset 0:
 *     0x00   3  char[3] magic "PAK"
 *     0x03   1  char    payload-kind tag.  Five letters occur in the corpus:
 *                       'P' (338x, .BM_ bitmaps), 'V' (109x, .WA_ WAVE audio),
 *                       'D' (5x), 'L' (2x) and 'E' (2x).  It tracks the KIND
 *                       of payload, not the codec - both 'P' and 'V' streams
 *                       use the same escape-byte scheme - and it is published
 *                       as the record's method without being interpreted.
 *     0x04   4  u32 LE  plaintext length
 *     0x08   n  payload packed stream, running to end-of-file
 *
 * THIS IS NOT A CONTAINER.  Every sample holds exactly one payload: there is
 * no member count, no name, no directory and no terminator anywhere in the
 * file, and the byte at 0x03 is a letter rather than a count.  So a valid
 * file yields exactly one record, the way the K-BOOM wrapper does.
 *
 * The plaintext length at 0x04 is corroborated by the payloads themselves:
 * the decompressed head of a .BM_ stream is a BMP file header whose own size
 * field equals it (e.g. BALL4.BM_ declares 0xde and the stream's first
 * literals are "BM", 0xde), and .WA_ streams decompress to RIFF/WAVE.  It
 * exceeds the stored size in 455 of 456 samples, up to 86:1.
 *
 * THE CODEC is Philip Gage's byte pair encoding (BPE, Dr. Dobb's Journal,
 * February 1994) used verbatim, block table and all.  The packed stream is a
 * chain of self-contained blocks; each block is
 *
 *     pair table     a run-length coded 256-entry table.  A control byte
 *                    above 127 declares (c - 127) consecutive literal codes;
 *                    a control byte c <= 127 introduces (c + 1) table
 *                    entries, each a left byte and - unless the left byte
 *                    equals the code it is being stored under, which marks
 *                    the code literal - a right byte.  The table ends when
 *                    256 codes have been accounted for.
 *     u16 BE         the number of packed bytes in this block
 *     data           that many bytes
 *
 * and expansion pushes right[c] then left[c] for every non-literal code,
 * emitting literals as they surface.  The blocks run to end-of-file; their
 * outputs concatenate to exactly the length stored at 0x04, which is how the
 * decode is checked, the format carrying no checksum of its own.
 *
 * That identification was reached from the samples rather than from a
 * description of this game's tooling, and the decoded payloads confirm it:
 * over all 456 samples the output length equals the stored length, every 'P'
 * stream decodes to a BMP whose own size field matches (bar one, a Windows
 * 3.x .HLP with its 3f 5f 03 00 magic), every 'V' stream to a RIFF/WAVE whose
 * chunk size matches, 'D' to Standard MIDI ("MThd") and 'L'/'E' to MZ images.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/beatthehouse/xx_beatthehouse.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"

#include <stdio.h>

#ifdef BEATTHEHOUSE
#define XX_BEATTHEHOUSE_FILE_TYPE XX_FILE_TYPE_BEATTHEHOUSE
#else
#define XX_BEATTHEHOUSE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_BEATTHEHOUSE_HEADER_SIZE 8
#define XX_BEATTHEHOUSE_MIN_PACKED_SIZE 2
#define XX_BEATTHEHOUSE_MAX_MEMBERS 1
#define XX_BEATTHEHOUSE_PLACEHOLDER_NAME "beatthehouse_data"
/* The plaintext length is attacker-controlled; this caps what a future decode
 * could ever be asked to allocate. */
#define XX_BEATTHEHOUSE_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* The corpus tops out near 87:1.  4096:1 leaves generous headroom while still
 * refusing an eight-byte header that claims a gigabyte. */
#define XX_BEATTHEHOUSE_MAX_RATIO 4096
#define XX_BEATTHEHOUSE_RATIO_SLACK 8192
/* Expansion stack.  A pair chain cannot legitimately be deeper than the 256
 * codes it is built from; the corpus never exceeds 18.  The bound is what
 * stops a table whose codes reference each other in a cycle from running
 * away, so it is enforced rather than assumed. */
#define XX_BEATTHEHOUSE_STACK_SIZE 512

typedef struct xx_beatthehouse_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
} xx_beatthehouse_member;

typedef struct xx_beatthehouse_stream_s {
    xx_beatthehouse_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_beatthehouse_stream;

static void xx_beatthehouse_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_beatthehouse_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_beatthehouse_read_at(Abstractformat *self, int64_t offset,
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

static void xx_beatthehouse_stream_free(void *pointer) {
    xx_beatthehouse_stream *stream = (xx_beatthehouse_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_beatthehouse_add(xx_beatthehouse_stream *stream,
                                const xx_beatthehouse_member *member) {
    xx_beatthehouse_member *grown = (xx_beatthehouse_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_beatthehouse_stream *xx_beatthehouse_parse(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    xx_beatthehouse_stream *stream;
    xx_beatthehouse_member member;
    uint8_t header[XX_BEATTHEHOUSE_HEADER_SIZE];
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t raw_size;
    uint8_t kind;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no payload behind it is not a packed stream. */
    if (span <= XX_BEATTHEHOUSE_HEADER_SIZE) return NULL;
    if (!xx_beatthehouse_read_at(self, self->base_address, header,
                                 sizeof(header))) {
        return NULL;
    }
    /* Three magic bytes plus one letter is a thin gate on its own, which is
     * why the length checks below are not optional: together they are what
     * keeps this reader off unrelated files.  The tag is restricted to an
     * uppercase letter rather than to the five seen in the corpus, so a kind
     * this corpus happens not to contain still parses. */
    if (xx_rt_memcmp(header, "PAK", 3U) != 0) return NULL;
    kind = header[3];
    if (kind < 'A' || kind > 'Z') return NULL;

    raw_size = xx_beatthehouse_le32(header + 4);

    /* Bounded before it is used for anything: the length drives the eventual
     * output allocation, so it is capped against the ratio the payload can
     * physically justify as well as against an absolute ceiling. */
    if ((raw_size & 0x80000000U) != 0U) return NULL;
    uncompressed_size = (int64_t)raw_size;
    if (uncompressed_size < 1 ||
        uncompressed_size > XX_BEATTHEHOUSE_MAX_DECODED) {
        return NULL;
    }

    /* The header stores no packed size: end-of-file is the only boundary. */
    compressed_size = span - XX_BEATTHEHOUSE_HEADER_SIZE;
    if (compressed_size < XX_BEATTHEHOUSE_MIN_PACKED_SIZE) return NULL;
    if (compressed_size > XX_BEATTHEHOUSE_MAX_DECODED) return NULL;
    if (uncompressed_size >
        (compressed_size * XX_BEATTHEHOUSE_MAX_RATIO) +
            XX_BEATTHEHOUSE_RATIO_SLACK) {
        return NULL;
    }

    stream = (xx_beatthehouse_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The original name is stored nowhere and this reader cannot see the
     * container's own file name, so the single record gets a fixed, extension
     * -less placeholder: inventing an extension would be a claim about
     * content the format never makes. */
    name = xx_str_dup(XX_BEATTHEHOUSE_PLACEHOLDER_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_BEATTHEHOUSE_HEADER_SIZE;
    member.data_offset = self->base_address + XX_BEATTHEHOUSE_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    /* Published as the method because it is the only classification byte the
     * format has; it names the payload kind, not a codec. */
    member.method = kind;
    if (!xx_beatthehouse_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_BEATTHEHOUSE_MAX_MEMBERS) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_beatthehouse_stream_free(stream);
    return NULL;
}

/* --------------------------------------------------------------- codec -- */

/* Expand one BPE block chain.  Every bound here is checked against the real
 * buffer lengths before it is used: the table, the block size word and the
 * expansion stack are all attacker-controlled. */
static bool xx_beatthehouse_bpe_decode(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written) {
    uint8_t left[256];
    uint8_t right[256];
    uint8_t stack[XX_BEATTHEHOUSE_STACK_SIZE];
    size_t position = 0U;
    size_t produced = 0U;

    if (!input || !output || !written) return false;
    *written = 0U;

    while (position < input_size) {
        size_t depth = 0U;
        size_t block_size;
        unsigned int count = 0U;
        unsigned int control;

        /* Every code is a literal until the table says otherwise. */
        for (control = 0U; control < 256U; ++control) {
            left[control] = (uint8_t)control;
            right[control] = 0U;
        }

        control = input[position++];
        for (;;) {
            unsigned int index;

            if (control > 127U) {
                /* A skip can never take the count past the table. */
                if (count + (control - 127U) > 256U) return false;
                count += control - 127U;
                control = 0U;
            }
            if (count == 256U) break;
            /* (control + 1) entries follow, and they must all fit. */
            if (count + control + 1U > 256U) return false;
            for (index = 0U; index <= control; ++index, ++count) {
                if (position >= input_size) return false;
                left[count] = input[position++];
                if ((unsigned int)left[count] != count) {
                    if (position >= input_size) return false;
                    right[count] = input[position++];
                }
            }
            if (count == 256U) break;
            if (position >= input_size) return false;
            control = input[position++];
        }

        /* Block length, big endian, then that many packed bytes. */
        if (position + 2U > input_size) return false;
        block_size = ((size_t)input[position] << 8) | (size_t)input[position + 1U];
        position += 2U;
        if (block_size > input_size - position) return false;

        while (block_size > 0U || depth > 0U) {
            unsigned int code;

            if (depth > 0U) {
                code = stack[--depth];
            } else {
                code = input[position++];
                --block_size;
            }
            if ((unsigned int)left[code] == code) {
                /* Silently overrunning the caller's buffer is the one error
                 * a caller cannot notice, so it is a hard failure. */
                if (produced >= output_size) return false;
                output[produced++] = (uint8_t)code;
            } else {
                if (depth + 2U > (size_t)XX_BEATTHEHOUSE_STACK_SIZE) {
                    return false;
                }
                stack[depth++] = right[code];
                stack[depth++] = left[code];
            }
        }
    }
    *written = produced;
    return true;
}

/* Decode the whole member into a fresh buffer.  The declared plaintext length
 * is the only correctness check the format offers - there is no checksum - so
 * a decode that does not land on it exactly is reported as a failure. */
static bool xx_beatthehouse_decode(Abstractformat *self,
                                   const xx_beatthehouse_member *member,
                                   uint8_t **out, size_t *out_size,
                                   xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < XX_BEATTHEHOUSE_MIN_PACKED_SIZE ||
        member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_BEATTHEHOUSE_MAX_DECODED ||
        member->uncompressed_size > XX_BEATTHEHOUSE_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_beatthehouse_read_at(self, member->data_offset, input,
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
    if (!xx_beatthehouse_bpe_decode(input, (size_t)member->compressed_size,
                                    output, (size_t)member->uncompressed_size,
                                    &written) ||
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

void xx_beatthehouse_init(xx_beatthehouse *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BEATTHEHOUSE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-beatthehouse-pak");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_beatthehouse_check_is_valid;
    archive->format.handle_base_info = xx_beatthehouse_handle_base_info;
    archive->format.get_format_size = xx_beatthehouse_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_beatthehouse_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_beatthehouse_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_beatthehouse_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_beatthehouse_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_beatthehouse_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_beatthehouse_free_archive_records_reading;
    archive->format.destroy = xx_beatthehouse_vtable_destroy;
}

xx_beatthehouse *xx_beatthehouse_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_beatthehouse *archive = (xx_beatthehouse *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_beatthehouse_init(archive, device, base_address);
    return archive;
}

void xx_beatthehouse_destroy(xx_beatthehouse *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_beatthehouse_free(xx_beatthehouse *archive) {
    if (!archive) return;
    xx_beatthehouse_destroy(archive);
    xx_mem_free(archive);
}

static void xx_beatthehouse_vtable_destroy(Abstractformat *self) {
    xx_beatthehouse_destroy((xx_beatthehouse *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_beatthehouse_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_beatthehouse_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_beatthehouse_parse(self, pd);
    if (!stream) return false;
    xx_beatthehouse_stream_free(stream);
    return true;
}

bool xx_beatthehouse_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_beatthehouse *archive = (xx_beatthehouse *)self;
    xx_beatthehouse_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_beatthehouse_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_beatthehouse_stream_free(stream);
    return true;
}

int64_t xx_beatthehouse_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_beatthehouse_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_beatthehouse *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_beatthehouse_set_record(xx_archive_record *record,
                                       const xx_beatthehouse_member *member) {
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
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_beatthehouse_copy_options(xx_list_s *target,
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

xx_archive_record_state *xx_beatthehouse_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_beatthehouse_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_beatthehouse_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_beatthehouse_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_beatthehouse_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_beatthehouse_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_beatthehouse_set_record(&state->current_record,
                                     &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_beatthehouse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_beatthehouse_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_beatthehouse_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_beatthehouse_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_beatthehouse_set_record(&state->current_record,
                                   &stream->items[stream->index]);
    return state->has_record;
}

static const xx_var *xx_beatthehouse_get_option(const xx_list_s *options,
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

static bool xx_beatthehouse_path_safe(const char *name) {
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

bool xx_beatthehouse_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_beatthehouse_stream *stream;
    const xx_beatthehouse_member *member;
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
    stream = (xx_beatthehouse_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_beatthehouse_path_safe(member->name)) return false;

    path_option = xx_beatthehouse_get_option(&state->options,
                                             XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * against its declared plaintext length without writing anything. */
        result = xx_beatthehouse_decode(self, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_beatthehouse_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_beatthehouse_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
