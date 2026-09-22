/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Quantum archive (.PAK / .001) - David Stafford's Q.EXE, the compressor
 * Microsoft later licensed as CAB compression type 2.  Borland shipped it as
 * the volume format of several installer sets (OWL, PVCS, WATCOM .001).
 *
 * Header (8 bytes, little endian):
 *   char    magic[2]        "DS"
 *   uint8   zero            always 0
 *   uint8   version         never 0; < 0x17 selects the OLD stream shape
 *   uint16  number_of_files never 0
 *   uint8   window_bits     10..21, the LZ window order
 *   uint8   level           packer effort, does not affect decoding
 *
 * Directory, immediately after the header, one record per file:
 *   varlen  name_size       1 byte, or 2 when bit 7 is set:
 *                           ((b0 & 0x7f) << 8) | b1
 *   char    name[name_size]
 *   varlen  extra_size      a second string (packer version tag); skipped
 *   char    extra[extra_size]
 *   uint32  uncompressed_size
 *   uint16  dos_time
 *   uint16  dos_date
 *   uint16  crc             OLD variant only
 *
 * Everything after the directory is ONE solid arithmetic-coded stream: models,
 * LZ window and coder registers run continuously across the members, so a
 * member can only be produced by replaying the members in front of it.  That
 * is why every record publishes the whole body as its stream.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS READER CAN EXTRACT, AND WHY IT IS NOT EVERYTHING
 *
 * The only Quantum decoder in this library is xx_quantum_cab_decode(), which
 * is the CAB dialect: it takes a list of blocks, RE-PRIMES the arithmetic
 * coder at the start of each one and never reads anything between blocks.
 *
 * The standalone archive is not laid out that way.  Its new shape (version >=
 * 0x17) primes the coder ONCE and then, after each member, consumes a 16-bit
 * raw checksum word off the bit buffer.  Handing the whole body to the CAB
 * entry point as a single block with the summed plaintext length would
 * therefore desynchronise at the first member boundary - the trailer would be
 * decoded as coded data.  The old shape (version < 0x17) is a different codec
 * again: five reversed selector symbols, a 29-slot length table, weighted
 * model seeding, narrowed position models and "extra" bits that ride the
 * arithmetic coder instead of coming off the bit buffer raw.  None of that
 * exists in the CAB decoder.
 *
 * So extraction is supported for exactly one case - a NEW-variant archive with
 * a SINGLE member - where the solid stream is one block, there is no
 * inter-member trailer to skip and the CAB entry point is a byte-for-byte
 * match for the stream shape.  Everything else is enumerated in full (names,
 * sizes, timestamps, CRCs, stream geometry) and refuses to extract rather than
 * writing bytes this library cannot vouch for.  Writing the missing decoder
 * belongs in algo/quantum, not here.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/quantum/xx_quantum.h"

#include "xxfclib/algo/quantum/xx_quantum.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_QUANTUM_HEADER_SIZE 8
#define XX_QUANTUM_MIN_WINDOW_BITS 10U
#define XX_QUANTUM_MAX_WINDOW_BITS 21U
#define XX_QUANTUM_OLD_VERSION_LIMIT 0x17U
/* The count field is 16 bit; nothing larger can be a real archive. */
#define XX_QUANTUM_MAX_ENTRIES 65535U
/* The name and the packer tag share the same 15-bit length prefix, so 0x7fff
 * is the producer ceiling for either. */
#define XX_QUANTUM_MAX_STRING 0x7fffU
/* One member of a solid stream is decoded whole, in memory. */
#define XX_QUANTUM_MAX_MEMBER ((int64_t)256 * 1024 * 1024)
/* The solid stream is handed to the codec in one piece. */
#define XX_QUANTUM_MAX_STREAM ((int64_t)256 * 1024 * 1024)

typedef struct xx_quantum_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t uncompressed_size;
    uint16_t dos_time;
    uint16_t dos_date;
    uint16_t crc;
    bool has_crc;
} xx_quantum_member;

typedef struct xx_quantum_stream_s {
    xx_quantum_member *items;
    size_t count;
    size_t index;
    int64_t stream_offset;
    int64_t stream_size;
    int64_t archive_size;
    uint32_t window_bits;
    uint32_t version;
    uint32_t level;
    bool old_variant;
} xx_quantum_stream;

static void xx_quantum_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_quantum_read16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_quantum_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_quantum_read_at(Abstractformat *self, int64_t offset,
                               uint8_t *buffer, size_t size) {
    size_t completed = 0U;
    if (!self || !self->device || offset < 0 || (!buffer && size != 0U) ||
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
static bool xx_quantum_path_safe(const char *name) {
    const char *cursor = name;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/' && *end != '\\') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/*
 * Read the 1- or 2-byte length prefix the directory uses for both strings.
 * @p cursor is advanced past whatever was consumed; @p limit is the first
 * offset past the end of the readable span.
 */
static bool xx_quantum_read_varlen(Abstractformat *self, int64_t *cursor,
                                   int64_t limit, uint32_t *value) {
    uint8_t bytes[2];
    if (!cursor || !value || *cursor < 0 || *cursor >= limit) return false;
    if (!xx_quantum_read_at(self, *cursor, bytes, 1U)) return false;
    ++(*cursor);
    if ((bytes[0] & 0x80U) != 0U) {
        if (*cursor >= limit) return false;
        if (!xx_quantum_read_at(self, *cursor, bytes + 1, 1U)) return false;
        ++(*cursor);
        *value = (uint32_t)(((uint32_t)(bytes[0] & 0x7fU) << 8U) | bytes[1]);
    } else {
        *value = bytes[0];
    }
    return *value <= XX_QUANTUM_MAX_STRING;
}

/*
 * The names are DOS paths (8.3, occasionally with a directory part).  Path
 * separators and the Windows reserved punctuation are escaped as %XX rather
 * than folded to '_': escaping is reversible and cannot collapse two distinct
 * members onto one output file.
 */
static char *xx_quantum_name_to_string(const uint8_t *raw, size_t size) {
    static const char digits[] = "0123456789ABCDEF";
    char *result;
    size_t position = 0U;
    size_t cursor;
    if (size == 0U || size > XX_QUANTUM_MAX_STRING) return NULL;
    /* Worst case every byte escapes to three characters. */
    result = (char *)xx_mem_alloc(size * 3U + 1U);
    if (!result) return NULL;
    for (cursor = 0U; cursor < size; ++cursor) {
        unsigned char ch = raw[cursor];
        bool safe = ch > 0x20U && ch < 0x7fU && ch != '%' && ch != '/' &&
                    ch != '\\' && ch != ':' && ch != '*' && ch != '?' &&
                    ch != '"' && ch != '<' && ch != '>' && ch != '|';
        if (safe) {
            result[position++] = (char)ch;
        } else {
            result[position++] = '%';
            result[position++] = digits[(ch >> 4U) & 0x0fU];
            result[position++] = digits[ch & 0x0fU];
        }
    }
    /* size != 0 was checked above and every byte emits at least one
     * character, so the name is never empty here. */
    result[position] = '\0';
    return result;
}

static void xx_quantum_stream_free(void *pointer) {
    xx_quantum_stream *stream = (xx_quantum_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name on success. */
static bool xx_quantum_add(xx_quantum_stream *stream,
                           const xx_quantum_member *member) {
    xx_quantum_member *grown;
    if (!stream || !member || stream->count >= XX_QUANTUM_MAX_ENTRIES ||
        stream->count + 1U > SIZE_MAX / sizeof(*grown)) {
        return false;
    }
    grown = (xx_quantum_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_quantum_stream *xx_quantum_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_quantum_stream *stream;
    uint8_t header[XX_QUANTUM_HEADER_SIZE];
    uint8_t *name_buffer = NULL;
    int64_t total;
    int64_t span;
    int64_t limit;
    int64_t cursor;
    uint32_t entries;
    uint32_t index;
    uint32_t fixed_size;

    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return NULL;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus the smallest possible record plus at least one coded byte. */
    if (span < (int64_t)XX_QUANTUM_HEADER_SIZE + 12) return NULL;
    if (!xx_quantum_read_at(self, self->base_address, header,
                            sizeof(header))) {
        return NULL;
    }
    if (header[0] != 'D' || header[1] != 'S' || header[2] != 0U) return NULL;
    if (header[3] == 0U) return NULL;
    entries = xx_quantum_read16(header + 4);
    if (entries == 0U) return NULL;
    if (header[6] < XX_QUANTUM_MIN_WINDOW_BITS ||
        header[6] > XX_QUANTUM_MAX_WINDOW_BITS) {
        return NULL;
    }

    stream = (xx_quantum_stream *)xx_mem_calloc(1U, sizeof(*stream));
    name_buffer = (uint8_t *)xx_mem_alloc(XX_QUANTUM_MAX_STRING);
    if (!stream || !name_buffer) goto fail;

    stream->version = header[3];
    stream->window_bits = header[6];
    stream->level = header[7];
    stream->old_variant = stream->version < XX_QUANTUM_OLD_VERSION_LIMIT;
    fixed_size = stream->old_variant ? 10U : 8U;

    /* The directory is variable length, so it is walked rather than indexed;
     * every field is bounded against the real end of the device. */
    limit = self->base_address + span;
    cursor = self->base_address + XX_QUANTUM_HEADER_SIZE;
    for (index = 0U; index < entries; ++index) {
        uint8_t fixed[10];
        xx_quantum_member member;
        uint32_t name_size = 0U;
        uint32_t extra_size = 0U;
        int64_t record_start = cursor;
        uint32_t position;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_quantum_read_varlen(self, &cursor, limit, &name_size) ||
            name_size == 0U || (int64_t)name_size > limit - cursor ||
            !xx_quantum_read_at(self, cursor, name_buffer, name_size)) {
            goto fail;
        }
        for (position = 0U; position < name_size; ++position) {
            /* Real names are printable DOS text; refusing control bytes is
             * what keeps a stray "DS\0" from parsing as a directory. */
            if (name_buffer[position] < 0x20U ||
                name_buffer[position] == 0x7fU) {
                goto fail;
            }
        }
        cursor += (int64_t)name_size;

        if (!xx_quantum_read_varlen(self, &cursor, limit, &extra_size) ||
            (int64_t)extra_size > limit - cursor) {
            goto fail;
        }
        cursor += (int64_t)extra_size;

        if ((int64_t)fixed_size > limit - cursor ||
            !xx_quantum_read_at(self, cursor, fixed, fixed_size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = record_start;
        member.header_size = cursor + (int64_t)fixed_size - record_start;
        member.uncompressed_size = (int64_t)xx_quantum_read32(fixed);
        member.dos_time = xx_quantum_read16(fixed + 4);
        member.dos_date = xx_quantum_read16(fixed + 6);
        member.has_crc = stream->old_variant;
        member.crc = stream->old_variant ? xx_quantum_read16(fixed + 8) : 0U;
        cursor += (int64_t)fixed_size;

        member.name = xx_quantum_name_to_string(name_buffer, name_size);
        if (!member.name) goto fail;
        if (!xx_quantum_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    stream->stream_offset = cursor;
    stream->stream_size = limit - cursor;
    /* A solid stream always carries at least the 16 priming bits. */
    if (stream->stream_size < 2) goto fail;
    stream->archive_size = span;
    xx_mem_free(name_buffer);
    return stream;
fail:
    xx_mem_free(name_buffer);
    xx_quantum_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/*
 * Only the one case the CAB entry point genuinely covers; see the header
 * comment.  Refusing is the honest answer for the rest - a wrong stream shape
 * does not error out, it produces plausible-looking garbage.
 */
static bool xx_quantum_can_decode(const xx_quantum_stream *stream) {
    return stream && stream->count == 1U && !stream->old_variant;
}

static bool xx_quantum_decode(Abstractformat *self,
                              const xx_quantum_stream *stream, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    const uint8_t *blocks[1];
    size_t block_sizes[1];
    size_t plain_sizes[1];
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size;
    size_t output_size;
    size_t written = 0U;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !out || !out_size || !xx_quantum_can_decode(stream)) {
        return false;
    }
    if (stream->stream_size <= 0 || stream->stream_size > XX_QUANTUM_MAX_STREAM ||
        stream->items[0].uncompressed_size <= 0 ||
        stream->items[0].uncompressed_size > XX_QUANTUM_MAX_MEMBER ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    input_size = (size_t)stream->stream_size;
    output_size = (size_t)stream->items[0].uncompressed_size;
    input = (uint8_t *)xx_mem_alloc(input_size);
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!input || !output ||
        !xx_quantum_read_at(self, stream->stream_offset, input, input_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    blocks[0] = input;
    block_sizes[0] = input_size;
    plain_sizes[0] = output_size;
    if (!xx_quantum_cab_decode(blocks, block_sizes, plain_sizes, 1U,
                               (unsigned)stream->window_bits, output,
                               output_size, &written) ||
        written != output_size) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = output_size;
    return true;
}

/* ------------------------------------------------------------ lifetime -- */

void xx_quantum_init(xx_quantum *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    /* Registration pending: xxfc_defs.h is shared and out of scope here, so
     * the file type stays generic until XX_FILE_TYPE_QUANTUM lands. */
    archive->format.file_type = XX_FILE_TYPE_QUANTUM;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-quantum-archive");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_quantum_check_is_valid;
    archive->format.handle_base_info = xx_quantum_handle_base_info;
    archive->format.get_format_size = xx_quantum_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_quantum_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_quantum_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_quantum_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_quantum_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_quantum_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_quantum_free_archive_records_reading;
    archive->format.destroy = xx_quantum_vtable_destroy;
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

xx_quantum *xx_quantum_create(xx_io_device *device, int64_t base_address) {
    xx_quantum *archive = (xx_quantum *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_quantum_init(archive, device, base_address);
    return archive;
}

void xx_quantum_destroy(xx_quantum *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

static void xx_quantum_vtable_destroy(Abstractformat *self) {
    xx_quantum_destroy((xx_quantum *)self);
}

void xx_quantum_free(xx_quantum *archive) {
    if (!archive) return;
    xx_quantum_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_quantum_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_quantum_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_quantum_parse(self, pd);
    if (!stream) return false;
    xx_quantum_stream_free(stream);
    return true;
}

bool xx_quantum_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_quantum *archive = (xx_quantum *)self;
    xx_quantum_stream *stream;

    if (!self) return false;
    self->base_info_handled = true;
    stream = xx_quantum_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        archive->stream_offset = -1;
        archive->stream_size = -1;
        return false;
    }
    self->is_valid = true;
    /* The solid stream runs to end of file: there is no overlay to claim. */
    self->format_size = stream->archive_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->stream_offset = stream->stream_offset;
    archive->stream_size = stream->stream_size;
    archive->window_bits = stream->window_bits;
    archive->version = stream->version;
    archive->level = stream->level;
    archive->old_variant = stream->old_variant;
    {
        char text[8];
        uint32_t value = stream->version;
        size_t length = 0U;
        char reversed[8];
        do {
            reversed[length++] = (char)('0' + (char)(value % 10U));
            value /= 10U;
        } while (value != 0U && length < sizeof(reversed));
        for (value = 0U; value < (uint32_t)length; ++value) {
            text[value] = reversed[length - 1U - value];
        }
        text[length] = '\0';
        xx_format_set_version(self, text);
    }
    xx_quantum_stream_free(stream);
    return true;
}

int64_t xx_quantum_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_quantum_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_quantum *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_quantum_set_record(xx_archive_record *record,
                                  const xx_quantum_stream *stream,
                                  size_t index) {
    const xx_quantum_member *member;
    if (!record || !stream || index >= stream->count) return false;
    member = &stream->items[index];
    if (!member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    /* Solid: every member's coded bytes are the whole body. */
    record->data_offset = stream->stream_offset;
    record->compressed_size = stream->stream_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->stream_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          stream->old_variant ? 1U : 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           (!member->has_crc ||
            xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                           member->crc)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_quantum_copy_options(xx_list_s *target,
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

static const xx_var *xx_quantum_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_quantum_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_quantum_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_quantum_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_quantum_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_quantum_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_quantum_copy_options(&state->options, options) ||
        !xx_quantum_set_record(&state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_quantum_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_quantum_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_quantum_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_quantum_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_quantum_set_record(&state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_quantum_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_quantum_stream *stream;
    const xx_quantum_member *member;
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
    stream = (xx_quantum_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    /* The stream shapes this library has no decoder for are refused here, not
     * decoded into something that merely looks like data. */
    if (!xx_quantum_can_decode(stream)) return false;
    member = &stream->items[stream->index];
    if (!xx_quantum_path_safe(member->name)) return false;

    path_option =
        xx_quantum_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_quantum_decode(self, stream, &plain, &plain_size, pd);
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
        !xx_quantum_decode(self, stream, &plain, &plain_size, pd)) {
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

void xx_quantum_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_quantum_get_number_of_records(const xx_quantum *archive) {
    return archive ? archive->number_of_records : 0U;
}

uint32_t xx_quantum_get_window_bits(const xx_quantum *archive) {
    return archive ? archive->window_bits : 0U;
}

bool xx_quantum_get_old_variant(const xx_quantum *archive) {
    return archive ? archive->old_variant : false;
}
