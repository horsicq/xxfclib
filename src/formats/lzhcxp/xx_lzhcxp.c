/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "LZ" single-file containers (XLZHCXP).
 *
 *   header, 2 bytes:
 *     0x00  "LZ"
 *
 *   payload, from 0x02 to EOF: a chain of BLOCKS.
 *     u8    block length, 1..0xFF; zero ends the file
 *     ....  that many bytes of LZW bit stream
 *
 * That is the whole container. There is no length field, no checksum, no
 * name and no timestamp - which is the problem this reader has to solve
 * twice over. It cannot publish an uncompressed size without decoding, and
 * it cannot tell an "LZ" file from any other two bytes spelling 'L','Z'
 * without decoding either. So parse measures the stream with
 * xx_lzhuf_lzhcxp_scan_memory() and a successful measurement IS the
 * detection.
 *
 * Before paying for that, three bit-level facts about the very first code
 * are checked, because the stream's opening is fully determined:
 *
 *   - codes are LSB-first and 10 bits wide at the start;
 *   - the first code must be CLEAR (0x200), so the first payload byte after
 *     the block length carries its low eight bits and must be 0x00, and the
 *     two bits completing it must be 0b10;
 *   - the code after CLEAR is emitted directly, so it is a literal, so the
 *     two high bits of that 10-bit code are clear.
 *
 * Together those are fourteen bits that must hold a fixed pattern, which is
 * what makes the trial decode affordable: almost every accidental "LZ" is
 * rejected before a single dictionary entry is built.
 *
 * The block framing is invisible to the codec, so a reader that hands it a
 * flat slice starting at a fixed offset decodes garbage. The codec's input
 * begins at offset 2, at the FIRST BLOCK LENGTH BYTE, and it walks the
 * chain itself.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzhcxp/xx_lzhcxp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzhuf/xx_lzhuf.h"

#include <stdio.h>

#define XX_LZHCXP_COPY_CHUNK (64 * 1024)

typedef struct xx_lzhcxp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_lzhcxp_member;

typedef struct xx_lzhcxp_stream_s {
    xx_lzhcxp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lzhcxp_stream;

static void xx_lzhcxp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lzhcxp_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_lzhcxp_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lzhcxp_path_safe(const char *name) {
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

static void xx_lzhcxp_stream_free(void *pointer) {
    xx_lzhcxp_stream *stream = (xx_lzhcxp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lzhcxp_add(xx_lzhcxp_stream *stream,
                          const xx_lzhcxp_member *member) {
    xx_lzhcxp_member *grown = (xx_lzhcxp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_LZHCXP_HEADER_SIZE 2
#define XX_LZHCXP_GATE_SIZE 6
#define XX_LZHCXP_MAX_INPUT_SIZE 0x4000000  /* 64 MiB */
#define XX_LZHCXP_MAX_OUTPUT_SIZE 0x8000000 /* 128 MiB */
#define XX_LZHCXP_FULL_BLOCK 0xFF
#define XX_LZHCXP_METHOD_LZW 0U
#define XX_LZHCXP_MAX_MEMBERS 1
#define XX_LZHCXP_MAX_DECODED (128 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_lzhcxp_le16(const uint8_t *data);
static xx_lzhcxp_stream *xx_lzhcxp_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_lzhcxp_decode(Abstractformat *self, const xx_lzhcxp_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Detection runs a full decode, so both ends of it have to stay bounded. */
/* A full first block means more blocks may follow; any shorter value means
 * this is the only one. */
/* The container holds exactly one member and states no method; this number
 * is synthesised so the decode switch has something to key on. */

static uint16_t xx_lzhcxp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static xx_lzhcxp_stream *xx_lzhcxp_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_lzhcxp_stream *stream = NULL;
    xx_lzhcxp_member member;
    uint8_t header[XX_LZHCXP_GATE_SIZE];
    uint8_t terminator;
    uint8_t *payload = NULL;
    size_t produced = 0U;
    int64_t total;
    int64_t span;
    int64_t first_block_size;
    int64_t payload_size;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Two magic bytes, a block length, and at least the three bytes the gate
     * below inspects. */
    if (span < XX_LZHCXP_GATE_SIZE) return NULL;
    /* Refused rather than truncated: the trial decode reads the whole
     * payload, so an unbounded file would be an unbounded allocation. */
    if (span > (int64_t)XX_LZHCXP_MAX_INPUT_SIZE) return NULL;
    if (!xx_lzhcxp_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != (uint8_t)'L' || header[1] != (uint8_t)'Z') return NULL;

    first_block_size = (int64_t)header[2];
    /* A zero length is the end-of-chain marker, so a file whose first block
     * is empty carries no stream at all. */
    if (first_block_size == 0) return NULL;

    /* The bit-level gate, and the reason a two-byte magic is survivable. The
     * first code is always CLEAR (0x200) and codes are LSB-first 10 bits, so
     * the first payload byte holds CLEAR's low eight bits - which are zero -
     * and the next two bits are 0b10. The code after CLEAR is emitted
     * literally, so its two high bits are clear as well. Fourteen bits of
     * fixed pattern: loosening any part of this turns the reader into a
     * two-byte match backed by an expensive trial decode. */
    if (header[3] != 0U) return NULL;
    if ((uint32_t)(xx_lzhcxp_le16(header + 4) & 0xC03U) != 2U) return NULL;

    /* The first block plus the two magic bytes, its length byte and the
     * chain terminator. */
    if ((first_block_size + 4) > span) return NULL;
    if (first_block_size != (int64_t)XX_LZHCXP_FULL_BLOCK) {
        /* Only a full-size block can be followed by another, so a short
         * first block means the file ends right after it plus the zero
         * terminator - an exact length, not an upper bound. */
        if ((first_block_size + 4) != span) return NULL;
        if (!xx_lzhcxp_read_at(self, self->base_address + 3 + first_block_size,
                               &terminator, 1U)) {
            return NULL;
        }
        if (terminator != 0U) return NULL;
    }

    if (pd && xx_pd_is_stopped(pd)) return NULL;

    payload_size = span - XX_LZHCXP_HEADER_SIZE;
    if (payload_size <= 0) return NULL;
    if ((uint64_t)payload_size > (uint64_t)SIZE_MAX) return NULL;
    payload = (uint8_t *)xx_mem_alloc((size_t)payload_size);
    if (!payload) return NULL;
    if (!xx_lzhcxp_read_at(self, self->base_address + XX_LZHCXP_HEADER_SIZE,
                           payload, (size_t)payload_size)) {
        xx_mem_free(payload);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return NULL;
    }

    /* Nothing in the container states the decompressed size, so measuring it
     * is the only way to publish one - and running the real codec over the
     * real block chain is simultaneously the only proof that this is an LZ
     * stream rather than two bytes that happen to spell "LZ". Running out of
     * input is the ORDINARY way these streams stop, so a truncated file
     * measures successfully; that is what the reference accepts and what its
     * detection relies on, so `consumed` is deliberately not demanded to
     * cover the whole payload. */
    if (!xx_lzhuf_lzhcxp_scan_memory(payload, (size_t)payload_size,
                                     (size_t)XX_LZHCXP_MAX_OUTPUT_SIZE, NULL,
                                     &produced)) {
        xx_mem_free(payload);
        return NULL;
    }
    xx_mem_free(payload);
    /* A stream that decodes to nothing is not a member. */
    if (produced == 0U) return NULL;
    if ((uint64_t)produced > (uint64_t)XX_LZHCXP_MAX_OUTPUT_SIZE) return NULL;

    stream = (xx_lzhcxp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    /* Nothing in the file names the member; the reference derives a name
     * from the host file and falls back to this literal, which is all that
     * is available here. */
    member.name = xx_str_dup("lzhcxp_data");
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = XX_LZHCXP_HEADER_SIZE;
    /* Offset 2, i.e. the first block LENGTH byte - the codec walks the block
     * chain and must see its framing. */
    member.data_offset = self->base_address + XX_LZHCXP_HEADER_SIZE;
    member.compressed_size = payload_size;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_LZHCXP_METHOD_LZW;
    member.timestamp = 0U; /* No timestamp exists in this container. */
    member.is_folder = false;

    if (!xx_lzhcxp_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }

    stream->archive_size = span;
    return stream;

fail:
    xx_lzhcxp_stream_free(stream);
    return NULL;
}


/* The size published by parse came from a trial decode, not from the
 * container, so it cannot be inflated by an attacker directly - but the
 * scan's own ceiling is what bounds it, and the allocation is still capped
 * here so the two limits cannot drift apart. */

static bool xx_lzhcxp_decode(Abstractformat *self,
                             const xx_lzhcxp_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_LZHCXP_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_LZHCXP_MAX_INPUT_SIZE) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    /* One member, one method, and it is synthesised rather than stored. A
     * value that is not the one parse writes means the member did not come
     * from this reader. */
    if (member->method != XX_LZHCXP_METHOD_LZW) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_lzhcxp_read_at(self, member->data_offset, packed,
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
    /* The input starts at the first BLOCK LENGTH byte: the decoder walks the
     * block chain itself, and handing it a slice that starts inside a block
     * misaligns every code that follows. */
    if (!xx_lzhuf_lzhcxp_decode_memory(packed, (size_t)member->compressed_size,
                                       plain,
                                       (size_t)member->uncompressed_size,
                                       &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* Parse measured this length from the same bytes with the same core, so
     * a disagreement means the device changed underneath us. Exactly the
     * promised length or nothing. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_lzhcxp_init(xx_lzhcxp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LZHCXP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzhcxp");
    xx_format_set_extension(&archive->format, "lz");
    archive->format.check_is_valid = xx_lzhcxp_check_is_valid;
    archive->format.handle_base_info = xx_lzhcxp_handle_base_info;
    archive->format.get_format_size = xx_lzhcxp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lzhcxp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lzhcxp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lzhcxp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lzhcxp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lzhcxp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lzhcxp_free_archive_records_reading;
    archive->format.destroy = xx_lzhcxp_vtable_destroy;
}

xx_lzhcxp *xx_lzhcxp_create(xx_io_device *device, int64_t base_address) {
    xx_lzhcxp *archive = (xx_lzhcxp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lzhcxp_init(archive, device, base_address);
    return archive;
}

void xx_lzhcxp_destroy(xx_lzhcxp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lzhcxp_free(xx_lzhcxp *archive) {
    if (!archive) return;
    xx_lzhcxp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lzhcxp_vtable_destroy(Abstractformat *self) {
    xx_lzhcxp_destroy((xx_lzhcxp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lzhcxp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzhcxp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lzhcxp_parse(self, pd);
    if (!stream) return false;
    xx_lzhcxp_stream_free(stream);
    return true;
}

bool xx_lzhcxp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzhcxp *archive = (xx_lzhcxp *)self;
    xx_lzhcxp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lzhcxp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lzhcxp_stream_free(stream);
    return true;
}

int64_t xx_lzhcxp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lzhcxp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lzhcxp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lzhcxp_set_record(xx_archive_record *record,
                                 const xx_lzhcxp_member *member) {
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

static bool xx_lzhcxp_copy_options(xx_list_s *target,
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

static const xx_var *xx_lzhcxp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lzhcxp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lzhcxp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lzhcxp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lzhcxp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lzhcxp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lzhcxp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lzhcxp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lzhcxp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lzhcxp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lzhcxp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lzhcxp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lzhcxp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lzhcxp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lzhcxp_stream *stream;
    const xx_lzhcxp_member *member;
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
    stream = (xx_lzhcxp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lzhcxp_path_safe(member->name)) return false;

    path_option = xx_lzhcxp_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lzhcxp_decode(self, member, &plain, &plain_size, pd);
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
        !xx_lzhcxp_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_lzhcxp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
