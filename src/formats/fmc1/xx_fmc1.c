/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Form Master FMC1 archives.
 *
 *   header, 4 bytes at offset 0:
 *     0x00  4 bytes  magic, "FMC1"
 *
 *   record, 24 bytes, one per member, immediately followed by its payload:
 *     0x00  12 bytes  name, NUL terminated inside the field; the bytes
 *                     BEHIND the terminator are stale, not padding
 *     0x0c  u16 LE    DOS time
 *     0x0e  u16 LE    DOS date
 *     0x10  u16 LE    unknown, varies across the corpus
 *     0x12  i16 LE    reserved, zero in every known member
 *     0x14  i32 LE    compressed size
 *
 *   payload: compressed_size bytes of an Okumura LZSS stream (4 KiB ring,
 *     F = 18), starting immediately after the record.
 *
 * There is no member count, no central directory and no terminator: the
 * record chain starts at offset 4 and the next record sits at
 * data_offset + compressed_size. The chain landing exactly on end-of-file is
 * this format's principal self-check.
 *
 * The container never stores an uncompressed size, so parse walks each
 * member's token stream to count the bytes it would emit. That walk is also
 * the second structural check: a chain of records that decodes to nothing is
 * not this format.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fmc1/xx_fmc1.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ampk/xx_ampk.h"

#include <stdio.h>

#define XX_FMC1_COPY_CHUNK (64 * 1024)

typedef struct xx_fmc1_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_fmc1_member;

typedef struct xx_fmc1_stream_s {
    xx_fmc1_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_fmc1_stream;

static void xx_fmc1_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_fmc1_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_fmc1_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_fmc1_path_safe(const char *name) {
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

static void xx_fmc1_stream_free(void *pointer) {
    xx_fmc1_stream *stream = (xx_fmc1_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_fmc1_add(xx_fmc1_stream *stream,
                          const xx_fmc1_member *member) {
    xx_fmc1_member *grown = (xx_fmc1_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_FMC1_MAX_MEMBERS 100000
#define XX_FMC1_SCAN_CHUNK 0x10000
#define XX_FMC1_NAME_BUFFER (XX_FMC1_NAME_SIZE * 3 + 1)
#define XX_FMC1_MAGIC_SIZE 4
#define XX_FMC1_RECORD_SIZE 24
#define XX_FMC1_NAME_SIZE 12
#define XX_FMC1_METHOD_LZSS 0U
#define XX_FMC1_MAX_COMPRESSED ((int64_t)0x10000000)
#define XX_FMC1_MAX_DECODED ((int64_t)0x10000000)

typedef struct xx_fmc1_scan_s {
    Abstractformat *self;
    int64_t base;      /* absolute offset of payload byte 0 */
    int64_t size;      /* payload length */
    uint8_t *buffer;   /* XX_FMC1_SCAN_CHUNK bytes, owned by parse */
    int64_t chunk_offset;
    int64_t chunk_size;
} xx_fmc1_scan;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_fmc1_le16(const uint8_t *data);
static uint32_t xx_fmc1_le32(const uint8_t *data);
static bool xx_fmc1_scan_byte(xx_fmc1_scan *scan, int64_t position, uint8_t *out);
static bool xx_fmc1_name_length(const uint8_t *field, size_t *out_length);
static bool xx_fmc1_name_string(const uint8_t *field, size_t length, size_t member_index, char **out_name);
static bool xx_fmc1_measure(Abstractformat *self, int64_t base, int64_t size, uint8_t *buffer, int64_t *out_size, xx_pd_struct *pd);
static xx_fmc1_stream *xx_fmc1_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_fmc1_decode(Abstractformat *self, const xx_fmc1_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No member count is stored, so this is a runaway guard, not a format
 * limit. */

/* Member payloads are walked, not buffered: the walk needs at most two bytes
 * at a time, and a member may be hundreds of kilobytes. */

/* Worst case every name byte escapes to "%XX". */



static uint16_t xx_fmc1_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_fmc1_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Fetch one payload byte, refilling the window when the cursor leaves it. */
static bool xx_fmc1_scan_byte(xx_fmc1_scan *scan, int64_t position,
                              uint8_t *out) {
    int64_t wanted;

    if (position < 0 || position >= scan->size) return false;
    if (position < scan->chunk_offset ||
        position >= scan->chunk_offset + scan->chunk_size) {
        wanted = scan->size - position;
        if (wanted > XX_FMC1_SCAN_CHUNK) wanted = XX_FMC1_SCAN_CHUNK;
        if (!xx_fmc1_read_at(scan->self, scan->base + position, scan->buffer,
                             (size_t)wanted)) {
            return false;
        }
        scan->chunk_offset = position;
        scan->chunk_size = wanted;
    }
    *out = scan->buffer[position - scan->chunk_offset];
    return true;
}

/* The 12-byte field is NOT cleanly padded: "fm.doc" is followed by a NUL and
 * then stale bytes (0x14 0x20 ...). Only the run up to the first NUL is part
 * of the name, and only that run may be validated - checking the tail would
 * reject archives the format genuinely produces. */
static bool xx_fmc1_name_length(const uint8_t *field, size_t *out_length) {
    size_t length = 0U;
    size_t index;

    *out_length = 0U;
    while (length < (size_t)XX_FMC1_NAME_SIZE && field[length] != 0U) {
        ++length;
    }
    /* An empty name means the record chain has wandered off the rails; this
     * and the reserved word below are what stop a stray "FMC1" from walking
     * a bogus chain. */
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        /* Names are plain ASCII here; the format grants no exemption. */
        if (field[index] < 0x20U || field[index] > 0x7EU) return false;
    }
    *out_length = length;
    return true;
}

/* Path separators and Windows reserved punctuation are escaped as %XX rather
 * than folded to '_': escaping is reversible and cannot collapse two distinct
 * members onto one output file. */
static bool xx_fmc1_name_string(const uint8_t *field, size_t length,
                                size_t member_index, char **out_name) {
    char buffer[XX_FMC1_NAME_BUFFER];
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0U;
    size_t index;
    uint8_t character;
    char *name;

    *out_name = NULL;
    /* Trailing pad spaces are common and are not part of the name. */
    while (length > 0U && field[length - 1U] == 0x20U) --length;

    for (index = 0U; index < length; ++index) {
        character = field[index];
        if (character > 0x20U && character < 0x7FU && character != '%' &&
            character != '/' && character != '\\' && character != ':' &&
            character != '*' && character != '?' && character != '"' &&
            character != '<' && character != '>' && character != '|') {
            buffer[used++] = (char)character;
        } else {
            buffer[used++] = '%';
            buffer[used++] = digits[(character >> 4) & 0x0F];
            buffer[used++] = digits[character & 0x0F];
        }
    }
    buffer[used] = '\0';

    /* A name that was nothing but spaces still names a real member, so give
     * it a positional stand-in rather than rejecting the archive. */
    if (used == 0U) {
        if (xx_rt_snprintf(buffer, sizeof(buffer), "record%u",
                           (unsigned)member_index) <= 0) {
            return false;
        }
    }

    name = xx_str_dup(buffer);
    if (!name) return false;
    *out_name = name;
    return true;
}

/* Walk the LZSS token stream and count the bytes it would emit. The container
 * never stores the decoded length, so this is the only way to learn it, and
 * it mirrors the reference decompressor's termination rules exactly: the
 * stream ends when the payload is spent, a truncated token simply stops the
 * walk, and nothing is counted past that point. */
static bool xx_fmc1_measure(Abstractformat *self, int64_t base, int64_t size,
                            uint8_t *buffer, int64_t *out_size,
                            xx_pd_struct *pd) {
    xx_fmc1_scan scan;
    int64_t position = 0;
    int64_t remaining = size;
    int64_t produced = 0;
    uint32_t flags = 0U;
    uint8_t byte = 0U;

    *out_size = 0;
    if (size <= 0) return false;
    scan.self = self;
    scan.base = base;
    scan.size = size;
    scan.buffer = buffer;
    scan.chunk_offset = 0;
    scan.chunk_size = 0;

    for (;;) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (remaining < 1) break;
        if (!xx_fmc1_scan_byte(&scan, position, &byte)) return false;
        ++position;
        --remaining;

        flags >>= 1;
        if ((flags & 0x100U) == 0U) {
            /* The flag byte and the first token byte are read back to back; a
             * flag byte with nothing behind it ends the stream. */
            if (remaining == 0) break;
            flags = (uint32_t)byte | 0xff00U;
            /* Skip the first token byte: its value never affects the count. */
            ++position;
            --remaining;
        }

        if (flags & 1U) {
            ++produced;
        } else {
            if (remaining < 1) break;
            if (!xx_fmc1_scan_byte(&scan, position, &byte)) return false;
            ++position;
            --remaining;
            /* Length is stored three less than the true run length. */
            produced += (int64_t)(byte & 0x0fU) + 3;
        }
        if (produced > XX_FMC1_MAX_DECODED) return false;
    }

    /* A member that decodes to nothing is not a member. */
    if (produced <= 0) return false;
    *out_size = produced;
    return true;
}

static xx_fmc1_stream *xx_fmc1_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[XX_FMC1_MAGIC_SIZE] = {
        (uint8_t)'F', (uint8_t)'M', (uint8_t)'C', (uint8_t)'1'};
    xx_fmc1_stream *stream = NULL;
    uint8_t header[XX_FMC1_MAGIC_SIZE];
    uint8_t record[XX_FMC1_RECORD_SIZE];
    uint8_t *scratch = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Magic plus at least one whole record; anything shorter describes
     * nothing. */
    if (span < XX_FMC1_MAGIC_SIZE + XX_FMC1_RECORD_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_fmc1_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* Four bytes of magic are weak on their own; what actually keeps a stray
     * "FMC1" out is the record chain and the token walk below. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    scratch = (uint8_t *)xx_mem_alloc((size_t)XX_FMC1_SCAN_CHUNK);
    if (!scratch) return NULL;
    stream = (xx_fmc1_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(scratch);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_FMC1_MAGIC_SIZE;
    while (offset < span) {
        xx_fmc1_member member;
        char *name;
        size_t name_length;
        int64_t compressed_size;
        int64_t data_offset;
        int64_t uncompressed_size = 0;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_FMC1_MAX_MEMBERS) goto fail;
        if (XX_FMC1_RECORD_SIZE > span - offset) goto fail;
        if (!xx_fmc1_read_at(self, self->base_address + offset, record,
                             sizeof(record))) {
            goto fail;
        }

        if (!xx_fmc1_name_length(record, &name_length)) goto fail;
        /* Reserved word, zero in every member of the reference corpus.
         * Together with the name check it is the cheapest way to stop a
         * random four-byte magic hit from walking into a bogus chain. */
        if ((int16_t)xx_fmc1_le16(record + 0x12) != 0) goto fail;

        /* Signed on purpose: a size with the top bit set is a corrupt field,
         * not a four-gigabyte member. */
        compressed_size = (int64_t)(int32_t)xx_fmc1_le32(record + 0x14);
        if (compressed_size <= 0 ||
            compressed_size > XX_FMC1_MAX_COMPRESSED) {
            goto fail;
        }

        data_offset = offset + XX_FMC1_RECORD_SIZE;
        /* A member extending past EOF is a rejection, not a short read. */
        if (!xx_fmc1_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        /* The uncompressed size exists nowhere in the container; walking the
         * tokens is the only way to obtain it, and a payload that does not
         * walk is not a member. */
        if (!xx_fmc1_measure(self, self->base_address + data_offset,
                             compressed_size, scratch, &uncompressed_size,
                             pd)) {
            goto fail;
        }

        if (!xx_fmc1_name_string(record, name_length, (size_t)count, &name)) {
            goto fail;
        }
        if (!xx_fmc1_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_FMC1_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = XX_FMC1_METHOD_LZSS;
        /* DOS date/time, packed date-high / time-low. The record stores the
         * time word first and the date word second, the reverse of the
         * obvious order; swapping them yields plausible nonsense rather than
         * an error. */
        member.timestamp = ((uint64_t)xx_fmc1_le16(record + 0x0e) << 16) |
                           (uint64_t)xx_fmc1_le16(record + 0x0c);
        /* The format has no directory entries. */
        member.is_folder = false;

        if (!xx_fmc1_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;
        offset = data_offset + compressed_size;
    }

    /* No count, no directory, no terminator: the chain has to tile the
     * container exactly, and the reference reader fails the archive on any
     * leftover byte. This is the check a later reader will be tempted to
     * loosen, and loosening it makes the format match almost anything that
     * starts with "FMC1". */
    if (offset != span) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    xx_mem_free(scratch);
    return stream;

fail:
    xx_mem_free(scratch);
    xx_fmc1_stream_free(stream);
    return NULL;
}



/* The container carries no method field: every member is Okumura LZSS. Zero
 * is the only value parse ever publishes, and decode refuses anything else so
 * that a future method cannot be silently mistaken for this one. */

/* The corpus tops out at 109 KB unpacked from a 45 KB member; these caps only
 * bound a corrupt size field. The decoded ceiling is also the largest output
 * the shared AMPK LZSS decoder accepts. */

/* One Okumura LZSS stream per member, whose decoded length parse measured by
 * walking the tokens. The decoder is output-driven: it stops exactly at
 * out_size and fails if the input runs out first, so a decode that would be
 * short is reported as failure rather than as a partial buffer. */
static bool xx_fmc1_decode(Abstractformat *self, const xx_fmc1_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A method this reader does not implement must fail here: treating it as
     * stored would emit compressed bytes that look like data. */
    if (member->method != XX_FMC1_METHOD_LZSS) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_FMC1_MAX_COMPRESSED ||
        member->uncompressed_size > XX_FMC1_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_fmc1_read_at(self, member->data_offset, input,
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
    if (!xx_ampk_lzss_decode_memory(input, (size_t)member->compressed_size,
                                    output,
                                    (size_t)member->uncompressed_size,
                                    &written) ||
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

void xx_fmc1_init(xx_fmc1 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FMC1;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-fmc1");
    xx_format_set_extension(&archive->format, "cmp");
    archive->format.check_is_valid = xx_fmc1_check_is_valid;
    archive->format.handle_base_info = xx_fmc1_handle_base_info;
    archive->format.get_format_size = xx_fmc1_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_fmc1_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_fmc1_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_fmc1_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_fmc1_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_fmc1_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_fmc1_free_archive_records_reading;
    archive->format.destroy = xx_fmc1_vtable_destroy;
}

xx_fmc1 *xx_fmc1_create(xx_io_device *device, int64_t base_address) {
    xx_fmc1 *archive = (xx_fmc1 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_fmc1_init(archive, device, base_address);
    return archive;
}

void xx_fmc1_destroy(xx_fmc1 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_fmc1_free(xx_fmc1 *archive) {
    if (!archive) return;
    xx_fmc1_destroy(archive);
    xx_mem_free(archive);
}

static void xx_fmc1_vtable_destroy(Abstractformat *self) {
    xx_fmc1_destroy((xx_fmc1 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_fmc1_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_fmc1_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_fmc1_parse(self, pd);
    if (!stream) return false;
    xx_fmc1_stream_free(stream);
    return true;
}

bool xx_fmc1_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_fmc1 *archive = (xx_fmc1 *)self;
    xx_fmc1_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_fmc1_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_fmc1_stream_free(stream);
    return true;
}

int64_t xx_fmc1_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fmc1_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_fmc1 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_fmc1_set_record(xx_archive_record *record,
                                 const xx_fmc1_member *member) {
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

static bool xx_fmc1_copy_options(xx_list_s *target,
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

static const xx_var *xx_fmc1_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_fmc1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_fmc1_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_fmc1_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_fmc1_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_fmc1_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_fmc1_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_fmc1_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fmc1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fmc1_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_fmc1_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fmc1_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_fmc1_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_fmc1_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_fmc1_stream *stream;
    const xx_fmc1_member *member;
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
    stream = (xx_fmc1_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_fmc1_path_safe(member->name)) return false;

    path_option = xx_fmc1_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_fmc1_decode(self, member, &plain, &plain_size, pd);
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
        !xx_fmc1_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_fmc1_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
