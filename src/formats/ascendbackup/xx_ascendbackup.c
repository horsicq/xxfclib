/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ascend backup volumes (*.000).
 *
 *   record, repeated to end of file, no header and no terminator:
 *     0x00  u16 LE name length, 1..12
 *     0x02  name, exactly that many bytes, NOT NUL terminated
 *     ....  u32 LE packed size
 *     ....  packed size bytes: a complete PKWARE DCL stream, whose first two
 *           bytes are the literal mode and the dictionary size in bits
 *
 * This is a different container from the single-member *.IN! file that
 * xx_ascend reads. That one opens with twelve bytes of date and holds exactly
 * one DCL stream running to EOF; this one has no date, no fixed prefix, and
 * carries many named members. The two share only their payload codec, so
 * neither reader can be made to cover the other without giving up the gate
 * each one depends on.
 *
 * There is no magic here at all: the first two bytes of the file are a small
 * integer. The gate is therefore built entirely out of structure. Every
 * record must carry a real DOS 8.3 name, every payload must open with a legal
 * DCL prelude, and the chain must land exactly on end-of-file. Only once all
 * of that holds is a real decode paid for, which is also the only way to learn
 * a member's plaintext length -- the container stores none.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ascendbackup/xx_ascendbackup.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_ASCENDBACKUP_COPY_CHUNK (64 * 1024)

typedef struct xx_ascendbackup_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ascendbackup_member;

typedef struct xx_ascendbackup_stream_s {
    xx_ascendbackup_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ascendbackup_stream;

static void xx_ascendbackup_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ascendbackup_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ascendbackup_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ascendbackup_path_safe(const char *name) {
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

static void xx_ascendbackup_stream_free(void *pointer) {
    xx_ascendbackup_stream *stream = (xx_ascendbackup_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ascendbackup_add(xx_ascendbackup_stream *stream,
                          const xx_ascendbackup_member *member) {
    xx_ascendbackup_member *grown = (xx_ascendbackup_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ASCENDBACKUP_NAME_LENGTH_SIZE 2
#define XX_ASCENDBACKUP_PACKED_SIZE_SIZE 4
#define XX_ASCENDBACKUP_FIXED_HEADER_SIZE 6
#define XX_ASCENDBACKUP_MAX_NAME_SIZE 12
#define XX_ASCENDBACKUP_MAX_STEM_SIZE 8
#define XX_ASCENDBACKUP_MAX_EXT_SIZE 3
#define XX_ASCENDBACKUP_MIN_ARCHIVE_SIZE 10
#define XX_ASCENDBACKUP_MIN_MEMBERS 2
#define XX_ASCENDBACKUP_MAX_MEMBERS 100000
#define XX_ASCENDBACKUP_MAX_ARCHIVE_SIZE ((int64_t)256 * 1024 * 1024)
#define XX_ASCENDBACKUP_DCL_MAX_LITERAL_MODE 1U
#define XX_ASCENDBACKUP_DCL_MIN_DICT_BITS 4U
#define XX_ASCENDBACKUP_DCL_MAX_DICT_BITS 6U
#define XX_ASCENDBACKUP_METHOD_DCL 0U
#define XX_ASCENDBACKUP_MIN_PACKED_SIZE 3
#define XX_ASCENDBACKUP_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ascendbackup_le16(const uint8_t *data);
static uint32_t xx_ascendbackup_le32(const uint8_t *data);
static bool xx_ascendbackup_name_character(uint8_t character);
static bool xx_ascendbackup_name_is_valid(const uint8_t *name, size_t length);
static bool xx_ascendbackup_dcl_prelude(const uint8_t *prelude);
static bool xx_ascendbackup_measure(Abstractformat *self, xx_ascendbackup_member *member, xx_pd_struct *pd);
static xx_ascendbackup_stream *xx_ascendbackup_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ascendbackup_decode(Abstractformat *self, const xx_ascendbackup_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The names are DOS 8.3 and never carry a path, so 12 is a hard ceiling. The
 * length is read as a u16, not as a byte plus a flag: the high byte is a
 * structural zero on every record, and discarding it would throw away the
 * cheapest reject a format with no magic has. */
/* 2 + one name byte + 4 + the shortest possible DCL stream. */
/* A single record is indistinguishable from a length/name/size coincidence,
 * so the chain has to prove itself at least twice. */
/* No count is stored; this is a runaway guard, not a format limit. The
 * reference volume holds 46 members. */
/* Parse decodes the whole volume once (see below), so the span is bounded. */

static uint16_t xx_ascendbackup_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ascendbackup_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_ascendbackup_name_character(uint8_t character) {
    if (character >= 'A' && character <= 'Z') return true;
    if (character >= 'a' && character <= 'z') return true;
    if (character >= '0' && character <= '9') return true;
    /* The rest of the DOS 8.3 character set. Spaces, path separators and
     * every byte outside 0x20..0x7E are excluded deliberately: this format
     * has no directories, so a name holding a separator is a mis-parse and
     * not a subfolder. */
    switch (character) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '-':
        case '@':
        case '^':
        case '_':
        case '`':
        case '{':
        case '}':
        case '~': return true;
        default: return false;
    }
}

/*
 * A strict DOS 8.3 name. With no magic byte anywhere in the container this is
 * half of the whole gate, so it rejects rather than repairs: one dot at most,
 * never leading, a stem of 1..8 and an extension of 0..3, and no trailing dot.
 */
static bool xx_ascendbackup_name_is_valid(const uint8_t *name, size_t length) {
    size_t index;
    size_t stem = 0U;
    size_t extension = 0U;
    bool has_dot = false;

    if (length < 1U || length > (size_t)XX_ASCENDBACKUP_MAX_NAME_SIZE) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (name[index] == (uint8_t)'.') {
            /* A second dot, or a leading one, cannot occur in an 8.3 name. */
            if (has_dot || index == 0U) return false;
            has_dot = true;
            continue;
        }
        if (!xx_ascendbackup_name_character(name[index])) return false;
        if (has_dot) {
            ++extension;
        } else {
            ++stem;
        }
    }
    if (stem < 1U || stem > (size_t)XX_ASCENDBACKUP_MAX_STEM_SIZE) return false;
    if (extension > (size_t)XX_ASCENDBACKUP_MAX_EXT_SIZE) return false;
    /* A dot with nothing after it is not something the Ascend writer emits. */
    if (has_dot && extension < 1U) return false;
    return true;
}

/* The two bytes a DCL stream opens with: literal mode (0 binary, 1 Huffman)
 * and the dictionary size in bits. Ascend always writes 1 and 6, but the gate
 * accepts the full legal range because that is what the stream format
 * permits -- narrowing it to the observed pair would be fitting the reader to
 * one corpus. */
static bool xx_ascendbackup_dcl_prelude(const uint8_t *prelude) {
    return prelude[0] <= XX_ASCENDBACKUP_DCL_MAX_LITERAL_MODE &&
           prelude[1] >= XX_ASCENDBACKUP_DCL_MIN_DICT_BITS &&
           prelude[1] <= XX_ASCENDBACKUP_DCL_MAX_DICT_BITS;
}

/*
 * Measure one member. The container stores no plaintext length, so the only
 * source for it is the bitstream, and the scan doubles as the strongest check
 * the format offers: the stream must reach its end marker having consumed
 * exactly the packed size the record declared. A mismatch means the record
 * chain and the payload disagree, and then no recovered length can be trusted.
 */
static bool xx_ascendbackup_measure(Abstractformat *self,
                                    xx_ascendbackup_member *member,
                                    xx_pd_struct *pd) {
    uint8_t *packed;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool result;

    if (member->compressed_size > XX_ASCENDBACKUP_MAX_DECODED) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_ascendbackup_read_at(self, member->data_offset, packed,
                                 (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }
    result = xx_dcl_scan_memory(packed, (size_t)member->compressed_size,
                                (size_t)XX_ASCENDBACKUP_MAX_DECODED, &consumed,
                                &produced) &&
             consumed == (size_t)member->compressed_size && produced >= 1U;
    xx_mem_free(packed);
    if (!result) return false;
    member->uncompressed_size = (int64_t)produced;
    return true;
}

static xx_ascendbackup_stream *xx_ascendbackup_parse(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    xx_ascendbackup_stream *stream;
    uint8_t header[XX_ASCENDBACKUP_MAX_NAME_SIZE +
                   XX_ASCENDBACKUP_PACKED_SIZE_SIZE];
    uint8_t prelude[2];
    uint8_t length_field[XX_ASCENDBACKUP_NAME_LENGTH_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    size_t index;
    bool closed_on_eof = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ASCENDBACKUP_MIN_ARCHIVE_SIZE ||
        span > XX_ASCENDBACKUP_MAX_ARCHIVE_SIZE) {
        return NULL;
    }

    stream = (xx_ascendbackup_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        xx_ascendbackup_member member;
        char buffer[XX_ASCENDBACKUP_MAX_NAME_SIZE + 1];
        char *name;
        int64_t name_size;
        int64_t header_size;
        int64_t data_offset;
        int64_t compressed_size;
        size_t position;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_ASCENDBACKUP_MAX_MEMBERS) goto fail;

        if (!xx_ascendbackup_range_within(
                span, offset, (int64_t)XX_ASCENDBACKUP_NAME_LENGTH_SIZE)) {
            goto fail;
        }
        if (!xx_ascendbackup_read_at(self, self->base_address + offset,
                                     length_field, sizeof(length_field))) {
            goto fail;
        }
        name_size = (int64_t)xx_ascendbackup_le16(length_field);
        if (name_size < 1 || name_size > XX_ASCENDBACKUP_MAX_NAME_SIZE) {
            goto fail;
        }

        header_size = (int64_t)XX_ASCENDBACKUP_FIXED_HEADER_SIZE + name_size;
        if (!xx_ascendbackup_range_within(span, offset, header_size)) goto fail;
        /* Name and packed size in one read; the length word above is the only
         * authority for where the name ends -- it is not NUL terminated. */
        if (!xx_ascendbackup_read_at(
                self,
                self->base_address + offset + XX_ASCENDBACKUP_NAME_LENGTH_SIZE,
                header,
                (size_t)(name_size + XX_ASCENDBACKUP_PACKED_SIZE_SIZE))) {
            goto fail;
        }
        if (!xx_ascendbackup_name_is_valid(header, (size_t)name_size)) {
            goto fail;
        }

        compressed_size =
            (int64_t)xx_ascendbackup_le32(header + (size_t)name_size);
        if (compressed_size < XX_ASCENDBACKUP_MIN_PACKED_SIZE) goto fail;
        data_offset = offset + header_size;
        /* A payload running past EOF is a rejection, not a short read. */
        if (!xx_ascendbackup_range_within(span, data_offset,
                                          compressed_size)) {
            goto fail;
        }

        if (!xx_ascendbackup_read_at(self, self->base_address + data_offset,
                                     prelude, sizeof(prelude))) {
            goto fail;
        }
        if (!xx_ascendbackup_dcl_prelude(prelude)) goto fail;

        for (position = 0U; position < (size_t)name_size; ++position) {
            buffer[position] = (char)header[position];
        }
        buffer[(size_t)name_size] = '\0';
        name = xx_str_dup(buffer);
        if (!name) goto fail;
        if (!xx_ascendbackup_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        /* Filled in by the measuring pass below; the container stores no
         * plaintext length at all. */
        member.uncompressed_size = 0;
        member.method = XX_ASCENDBACKUP_METHOD_DCL;
        /* No timestamp, no attributes, no checksum: the six header bytes plus
         * the name account for every byte of the record. */
        member.timestamp = 0U;
        member.is_folder = false;

        if (!xx_ascendbackup_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        offset = data_offset + compressed_size;

        /* No terminator and no global size field: the chain ends by landing
         * exactly on EOF. Slack is a reject, not an overlay -- the exact
         * tiling IS the signature of a container with no magic byte, and
         * tolerating a tail would throw away the entire margin that makes a
         * structural gate safe here. */
        if (offset == span) {
            closed_on_eof = true;
            break;
        }
    }

    if (!closed_on_eof) goto fail;
    if (stream->count < (size_t)XX_ASCENDBACKUP_MIN_MEMBERS) goto fail;

    /* Only now, on a file that already tiles exactly with 8.3 names and DCL
     * preludes throughout, is a real decode worth paying for. Running this
     * inside the loop above would put a decompressor over every probed file,
     * and running it on the first member alone would leave the rest of the
     * listing with no plaintext length -- which the extraction path needs,
     * because nothing in the container supplies it. */
    for (index = 0U; index < stream->count; ++index) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_ascendbackup_measure(self, &stream->items[index], pd)) {
            goto fail;
        }
    }

    stream->archive_size = span;
    return stream;

fail:
    xx_ascendbackup_stream_free(stream);
    return NULL;
}


/* The container has no method field: every member is a PKWARE DCL stream.
 * The constant exists so decode still refuses anything it did not itself
 * publish, rather than falling through to a byte copy. */
/* A DCL stream is at least its two prelude bytes plus one coded symbol. */
/* Both lengths are attacker-controlled -- the plaintext one is driven by the
 * bitstream itself -- so the reader caps what it is willing to allocate. */

static bool xx_ascendbackup_decode(Abstractformat *self,
                                   const xx_ascendbackup_member *member,
                                   uint8_t **out, size_t *out_size,
                                   xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_ASCENDBACKUP_METHOD_DCL) return false;
    if (member->compressed_size < XX_ASCENDBACKUP_MIN_PACKED_SIZE ||
        member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_ASCENDBACKUP_MAX_DECODED ||
        member->uncompressed_size > XX_ASCENDBACKUP_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_ascendbackup_read_at(self, member->data_offset, input,
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
    /* uncompressed_size was measured by the parse-time scan of this very
     * stream, so the decode must reproduce it exactly. A decode that stops
     * short means the file changed underneath us; reporting it as success
     * would write a truncated file the caller cannot tell from a whole one. */
    if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
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

void xx_ascendbackup_init(xx_ascendbackup *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ASCENDBACKUP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ascend-backup");
    xx_format_set_extension(&archive->format, "000");
    archive->format.check_is_valid = xx_ascendbackup_check_is_valid;
    archive->format.handle_base_info = xx_ascendbackup_handle_base_info;
    archive->format.get_format_size = xx_ascendbackup_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ascendbackup_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ascendbackup_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ascendbackup_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ascendbackup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ascendbackup_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ascendbackup_free_archive_records_reading;
    archive->format.destroy = xx_ascendbackup_vtable_destroy;
}

xx_ascendbackup *xx_ascendbackup_create(xx_io_device *device, int64_t base_address) {
    xx_ascendbackup *archive = (xx_ascendbackup *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ascendbackup_init(archive, device, base_address);
    return archive;
}

void xx_ascendbackup_destroy(xx_ascendbackup *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ascendbackup_free(xx_ascendbackup *archive) {
    if (!archive) return;
    xx_ascendbackup_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ascendbackup_vtable_destroy(Abstractformat *self) {
    xx_ascendbackup_destroy((xx_ascendbackup *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ascendbackup_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ascendbackup_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ascendbackup_parse(self, pd);
    if (!stream) return false;
    xx_ascendbackup_stream_free(stream);
    return true;
}

bool xx_ascendbackup_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ascendbackup *archive = (xx_ascendbackup *)self;
    xx_ascendbackup_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ascendbackup_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ascendbackup_stream_free(stream);
    return true;
}

int64_t xx_ascendbackup_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ascendbackup_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ascendbackup *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ascendbackup_set_record(xx_archive_record *record,
                                 const xx_ascendbackup_member *member) {
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

static bool xx_ascendbackup_copy_options(xx_list_s *target,
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

static const xx_var *xx_ascendbackup_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ascendbackup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ascendbackup_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ascendbackup_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ascendbackup_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ascendbackup_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ascendbackup_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ascendbackup_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ascendbackup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ascendbackup_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ascendbackup_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ascendbackup_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ascendbackup_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ascendbackup_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ascendbackup_stream *stream;
    const xx_ascendbackup_member *member;
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
    stream = (xx_ascendbackup_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ascendbackup_path_safe(member->name)) return false;

    path_option = xx_ascendbackup_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ascendbackup_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ascendbackup_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ascendbackup_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
