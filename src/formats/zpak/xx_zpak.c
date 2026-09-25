/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZSoft ZPAK archives, in both of their revisions.
 *
 *   header, 6 bytes at offset 0:
 *     0x00   4  char     magic, "zpak" (revision 1) or "zpk2" (revision 2)
 *     0x04   2  u16 LE   member count, 1..65535
 *
 *   directory, count * 33 bytes, immediately at offset 6:
 *     0x00  12  char     name, NUL-padded; only twelve characters are
 *                        significant and byte 12 of the record is padding
 *     0x0d   4  u32 LE   absolute file offset of the payload
 *     0x11   4  u32 LE   compressed size
 *     0x15   2  u16 LE   DOS date
 *     0x17   2  u16 LE   DOS time
 *     0x19   2  u16 LE   CRC, low word
 *     0x1b   2  u16 LE   CRC, high word
 *     0x1d   4  u32 LE   uncompressed size                 (record = 33)
 *
 * THE MAGIC PICKS THE CODEC FOR EVERY MEMBER; it is not a per-entry field.
 * "zpk2" members are plain PKWARE DCL implode streams. "zpak" members are
 * twelve-bit LZW with a chunk layer wrapped round them, and both halves of
 * that are traps:
 *
 *   - THE CHUNK LAYER IS FRAMING, NOT BLOCKING. A payload is a 0xCA marker
 *     byte, then (compressed size - 2) bytes of [u16 LE length][that many
 *     bytes] records, then a 0x00 trailer. The chunk payloads CONCATENATE
 *     into one continuous LZW stream: the codec does not reset, flush or pad
 *     at a chunk boundary, so decoding chunk by chunk yields a correct first
 *     4096-byte chunk and then nothing usable.
 *   - THE LZW SETTINGS ARE GIF-STYLE: LSB-first, widths 9..12, CLEAR 0x100,
 *     END 0x101, first free code 0x102, no block padding, no width-step
 *     bias. A textually identical call site elsewhere in the reference tree
 *     wants MSB-first with bias 1; copying the options across from it still
 *     decodes short members, which is why the two are separate entry points
 *     here.
 *
 * A record's CRC high word being zero means the reference reader does not
 * check the CRC at all, so neither does this one: there is no checksum to
 * rely on for most archives and the decoders' exact-length tests carry the
 * whole of extraction's correctness.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zpak/xx_zpak.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/softronics/xx_softronics.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_ZPAK_COPY_CHUNK (64 * 1024)

typedef struct xx_zpak_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zpak_member;

typedef struct xx_zpak_stream_s {
    xx_zpak_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zpak_stream;

static void xx_zpak_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zpak_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zpak_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zpak_path_safe(const char *name) {
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

static void xx_zpak_stream_free(void *pointer) {
    xx_zpak_stream *stream = (xx_zpak_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zpak_add(xx_zpak_stream *stream,
                          const xx_zpak_member *member) {
    xx_zpak_member *grown = (xx_zpak_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZPAK_MAX_MEMBERS 65535
#define XX_ZPAK_REC_DATAOFFSET 0x0d
#define XX_ZPAK_REC_COMPRESSED 0x11
#define XX_ZPAK_REC_DOSDATE 0x15
#define XX_ZPAK_REC_DOSTIME 0x17
#define XX_ZPAK_REC_UNCOMPRESSED 0x1d
#define XX_ZPAK_MIN_DATA_OFFSET 0x26
#define XX_ZPAK_HEADER_SIZE 6
#define XX_ZPAK_RECORD_SIZE 33
#define XX_ZPAK_NAME_SIZE 12
#define XX_ZPAK_MIN_SIZE 0x27
#define XX_ZPAK_METHOD_LZW 1U
#define XX_ZPAK_METHOD_DCL 2U
#define XX_ZPAK_CHUNK_MARKER 0xCAU
#define XX_ZPAK_MIN_FRAMED_SIZE 2
#define XX_ZPAK_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_zpak_le16(const uint8_t *data);
static uint32_t xx_zpak_le32(const uint8_t *data);
static bool xx_zpak_copy_name(const uint8_t *record, char *buffer);
static xx_zpak_stream *xx_zpak_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_zpak_unchunk(const uint8_t *payload, size_t size, uint8_t **out, size_t *out_size);
static bool xx_zpak_decode(Abstractformat *self, const xx_zpak_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The payload of the first member can never start inside the header plus its
 * own directory record, so the reference rejects an offset at or below this.
 * It is one of only three value checks standing behind a four-byte magic. */

static uint16_t xx_zpak_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_zpak_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name field is twelve bytes, NUL-padded. The reference keeps the raw
 * bytes as Latin-1; this reader refuses anything outside printable ASCII,
 * because a DOS 8.3 name never needed more and a random byte string passing
 * the four-byte magic is far more likely to contain control bytes than a
 * real archive is. */
static bool xx_zpak_copy_name(const uint8_t *record, char *buffer) {
    size_t index;
    size_t length = 0U;

    for (index = 0U; index < (size_t)XX_ZPAK_NAME_SIZE; ++index) {
        uint8_t value = record[index];
        /* NUL ends the name; the remaining bytes of the twelve are padding
         * and are not examined - writers leave stale bytes there. */
        if (value == 0U) break;
        if (value < 0x20U || value > 0x7EU) return false;
        buffer[length++] = (char)value;
    }
    buffer[length] = '\0';
    /* An unnamed member cannot be written out, so it is a rejection rather
     * than a record with an empty path. */
    return length != 0U;
}

static xx_zpak_stream *xx_zpak_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zpak_stream *stream = NULL;
    xx_zpak_member member;
    uint8_t probe[XX_ZPAK_MIN_SIZE];
    uint8_t *directory = NULL;
    char name_buffer[XX_ZPAK_NAME_SIZE + 1];
    char *name;
    int64_t total;
    int64_t span;
    int64_t directory_size;
    int64_t archive_end;
    uint32_t count;
    uint32_t method;
    uint32_t index;
    bool is_v2;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ZPAK_MIN_SIZE) return NULL;
    if (!xx_zpak_read_at(self, self->base_address, probe, sizeof(probe))) {
        return NULL;
    }

    /* Four magic bytes, and they are ordinary lowercase ASCII - weak on
     * their own. What keeps this reader off unrelated files is the
     * combination of the magic with the three first-record checks below and
     * the containment test on every record; each is cheap and each rules out
     * a large slice of random data. */
    is_v2 = (probe[0] == (uint8_t)'z' && probe[1] == (uint8_t)'p' &&
             probe[2] == (uint8_t)'k' && probe[3] == (uint8_t)'2');
    if (!is_v2) {
        if (probe[0] != (uint8_t)'z' || probe[1] != (uint8_t)'p' ||
            probe[2] != (uint8_t)'a' || probe[3] != (uint8_t)'k') {
            return NULL;
        }
    }
    method = is_v2 ? XX_ZPAK_METHOD_DCL : XX_ZPAK_METHOD_LZW;

    count = (uint32_t)xx_zpak_le16(probe + 4);
    /* An empty archive is not a thing this format writes, and a count of
     * zero would make every later check vacuous. */
    if (count == 0U || count > (uint32_t)XX_ZPAK_MAX_MEMBERS) return NULL;

    /* The reference detector's own three checks, all on the FIRST directory
     * record. They are the format's real gate and the lines a later reader
     * will be tempted to drop as redundant with the per-record validation
     * further down - they are not: they run before a single directory byte
     * is read, so they are what stops a 64 KB directory being allocated for
     * a file that merely begins "zpak". */
    if ((int32_t)xx_zpak_le32(probe + XX_ZPAK_HEADER_SIZE +
                              XX_ZPAK_REC_DATAOFFSET) <=
        (int32_t)XX_ZPAK_MIN_DATA_OFFSET) {
        return NULL;
    }
    if ((int32_t)xx_zpak_le32(probe + XX_ZPAK_HEADER_SIZE +
                              XX_ZPAK_REC_COMPRESSED) < 0) {
        return NULL;
    }
    if ((int32_t)xx_zpak_le32(probe + XX_ZPAK_HEADER_SIZE +
                              XX_ZPAK_REC_UNCOMPRESSED) < 0) {
        return NULL;
    }

    directory_size = (int64_t)count * XX_ZPAK_RECORD_SIZE;
    if (!xx_zpak_range_within(span, (int64_t)XX_ZPAK_HEADER_SIZE,
                              directory_size)) {
        return NULL;
    }
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_zpak_read_at(self, self->base_address + XX_ZPAK_HEADER_SIZE,
                         directory, (size_t)directory_size)) {
        xx_mem_free(directory);
        return NULL;
    }

    stream = (xx_zpak_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));
    archive_end = (int64_t)XX_ZPAK_HEADER_SIZE + directory_size;

    for (index = 0U; index < count; ++index) {
        const uint8_t *record = directory + ((size_t)index * XX_ZPAK_RECORD_SIZE);
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;

        if (!xx_zpak_copy_name(record, name_buffer)) goto fail;

        /* All three sizes are read as SIGNED 32-bit values, matching the
         * reference: a negative one is a malformed archive, not a member
         * four gigabytes long. */
        if ((int32_t)xx_zpak_le32(record + XX_ZPAK_REC_DATAOFFSET) < 0 ||
            (int32_t)xx_zpak_le32(record + XX_ZPAK_REC_COMPRESSED) < 0 ||
            (int32_t)xx_zpak_le32(record + XX_ZPAK_REC_UNCOMPRESSED) < 0) {
            goto fail;
        }
        data_offset =
            (int64_t)(int32_t)xx_zpak_le32(record + XX_ZPAK_REC_DATAOFFSET);
        compressed_size =
            (int64_t)(int32_t)xx_zpak_le32(record + XX_ZPAK_REC_COMPRESSED);
        uncompressed_size =
            (int64_t)(int32_t)xx_zpak_le32(record + XX_ZPAK_REC_UNCOMPRESSED);

        /* The payload offset is absolute, so a record can point anywhere;
         * this containment test is the only thing that keeps a member's
         * extent inside the file. */
        if (!xx_zpak_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        if (compressed_size > XX_ZPAK_MAX_DECODED ||
            uncompressed_size > XX_ZPAK_MAX_DECODED) {
            goto fail;
        }

        name = xx_str_dup(name_buffer);
        if (!name) goto fail;
        if (!xx_zpak_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + XX_ZPAK_HEADER_SIZE +
                               ((int64_t)index * XX_ZPAK_RECORD_SIZE);
        member.header_size = XX_ZPAK_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The magic, not the record, names the codec - every member of an
         * archive shares it. */
        member.method = method;
        /* Published as (date << 16) | time; the record stores the date word
         * first, the reverse of the packed DOS order. */
        member.timestamp =
            ((uint64_t)xx_zpak_le16(record + XX_ZPAK_REC_DOSDATE) << 16) |
            (uint64_t)xx_zpak_le16(record + XX_ZPAK_REC_DOSTIME);
        /* The directory has no attribute byte: ZPAK stores flat names. */
        member.is_folder = false;

        if (!xx_zpak_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        if (data_offset + compressed_size > archive_end) {
            archive_end = data_offset + compressed_size;
        }
    }

    if (stream->count == 0U) goto fail;
    xx_mem_free(directory);
    /* Payloads live at absolute offsets that need not be in directory order
     * and need not reach EOF, so the archive ends at the furthest member,
     * clamped to the file: anything past that is overlay, not ZPAK. */
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_mem_free(directory);
    xx_zpak_stream_free(stream);
    return NULL;
}


/* The header plus one whole directory record: the reference probes exactly
 * this much before it trusts anything, because three of its detection checks
 * read fields of the FIRST record. */

/* The magic picks the codec, so these two synthetic values stand in for the
 * per-member method field the container does not have. Keeping them distinct
 * is what lets decode refuse rather than guess. */

/* First byte of a framed "zpak" payload. */
/* Marker plus trailer: the smallest framing that can exist. */

/* Stored sizes are attacker-controlled, so the allocations they drive are
 * capped. */

/* Strip the 0xCA / [u16 LE length][data] / 0x00 framing, concatenating the
 * chunk payloads into the one continuous LZW stream the codec expects.
 *
 * THIS IS STRICTER THAN THE REFERENCE ON PURPOSE. The reference's unchunk()
 * clamps a chunk length that runs past the framed area and keeps whatever
 * bytes are really there, so a truncated archive yields a short stream that
 * its decoder then half-decodes. That cannot be done here: the codec entry
 * point below demands a leading CLEAR, a trailing END and zero padding after
 * it, and a clamped chunk simply loses the END - so the only two outcomes
 * available are "refuse" and "silently short", and the decode contract
 * forbids the second. A truncated member the reference would partially
 * recover is therefore a hard failure here. */
static bool xx_zpak_unchunk(const uint8_t *payload, size_t size,
                            uint8_t **out, size_t *out_size) {
    uint8_t *result;
    size_t framed_size;
    size_t position = 0U;
    size_t produced = 0U;
    size_t index;
    bool zero_terminated = false;

    *out = NULL;
    *out_size = 0U;
    if (!payload || size < (size_t)XX_ZPAK_MIN_FRAMED_SIZE) return false;
    if (payload[0] != (uint8_t)XX_ZPAK_CHUNK_MARKER) return false;
    /* The trailer byte is part of the frame, not of a chunk: the framed area
     * is payload[1 .. size-2]. */
    if (payload[size - 1U] != 0U) return false;
    framed_size = size - 2U;

    result = (uint8_t *)xx_mem_alloc(framed_size ? framed_size : 1U);
    if (!result) return false;

    while (position + 2U <= framed_size) {
        size_t chunk_size = (size_t)payload[1U + position] |
                            ((size_t)payload[2U + position] << 8);
        position += 2U;
        /* A zero length terminates the stream early; whatever follows is
         * writer padding and is not part of the LZW stream. */
        if (chunk_size == 0U) {
            zero_terminated = true;
            break;
        }
        if (chunk_size > framed_size - position) {
            /* Truncated archive - see the note above. */
            xx_mem_free(result);
            return false;
        }
        for (index = 0U; index < chunk_size; ++index) {
            result[produced + index] = payload[1U + position + index];
        }
        produced += chunk_size;
        position += chunk_size;
    }

    /* Anything left over that is neither a whole record nor an explicit
     * terminator is a malformed frame, not slack. */
    if (!zero_terminated && position != framed_size) {
        xx_mem_free(result);
        return false;
    }
    if (produced == 0U) {
        xx_mem_free(result);
        return false;
    }
    *out = result;
    *out_size = produced;
    return true;
}

/* Two codecs, chosen by the container magic and recorded by parse. An
 * unrecognised value is refused rather than copied through: a stored-bytes
 * fallback here would write LZW codes to disk and call it the file. */
static bool xx_zpak_decode(Abstractformat *self, const xx_zpak_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    uint8_t *unframed = NULL;
    size_t unframed_size = 0U;
    size_t written = 0U;
    bool ok = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_ZPAK_METHOD_LZW &&
        member->method != XX_ZPAK_METHOD_DCL) {
        return false;
    }
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_ZPAK_MAX_DECODED ||
        member->uncompressed_size > XX_ZPAK_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_zpak_read_at(self, member->data_offset, input,
                         (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_ZPAK_METHOD_LZW) {
        if (!xx_zpak_unchunk(input, (size_t)member->compressed_size, &unframed,
                             &unframed_size)) {
            xx_mem_free(input);
            return false;
        }
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(unframed);
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_ZPAK_METHOD_LZW) {
        /* The GIF-style entry point, not the MSB-first dialect: see the
         * codec note in the file comment. It requires the stream to open
         * with CLEAR, close with END and carry only zero bits after it -
         * requirements the chunk framing above is responsible for
         * delivering intact. */
        ok = xx_softronics_lzw_decompress_memory(
            unframed, unframed_size, output,
            (size_t)member->uncompressed_size, &written);
        /* This entry point reports the INPUT bytes it consumed, not the
         * output it produced, so the exact-length test is the decoder's own
         * internal one and there is nothing further to compare here. */
        written = ok ? (size_t)member->uncompressed_size : 0U;
    } else {
        ok = xx_dcl_decode_memory(input, (size_t)member->compressed_size,
                                  output, (size_t)member->uncompressed_size,
                                  &written);
    }
    /* Exactly the declared plaintext length, or nothing. Most archives carry
     * no usable CRC (a zero high word means the reference does not check
     * one), so this equality is the whole of extraction's correctness check,
     * and a short decode reported as success is the one failure the caller
     * cannot detect. */
    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(unframed);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(unframed);
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zpak_init(xx_zpak *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZPAK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zpak");
    xx_format_set_extension(&archive->format, "zpk");
    archive->format.check_is_valid = xx_zpak_check_is_valid;
    archive->format.handle_base_info = xx_zpak_handle_base_info;
    archive->format.get_format_size = xx_zpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zpak_free_archive_records_reading;
    archive->format.destroy = xx_zpak_vtable_destroy;
}

xx_zpak *xx_zpak_create(xx_io_device *device, int64_t base_address) {
    xx_zpak *archive = (xx_zpak *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zpak_init(archive, device, base_address);
    return archive;
}

void xx_zpak_destroy(xx_zpak *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zpak_free(xx_zpak *archive) {
    if (!archive) return;
    xx_zpak_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zpak_vtable_destroy(Abstractformat *self) {
    xx_zpak_destroy((xx_zpak *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zpak_parse(self, pd);
    if (!stream) return false;
    xx_zpak_stream_free(stream);
    return true;
}

bool xx_zpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zpak *archive = (xx_zpak *)self;
    xx_zpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zpak_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zpak_stream_free(stream);
    return true;
}

int64_t xx_zpak_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zpak_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zpak *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zpak_set_record(xx_archive_record *record,
                                 const xx_zpak_member *member) {
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

static bool xx_zpak_copy_options(xx_list_s *target,
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

static const xx_var *xx_zpak_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zpak_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zpak_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zpak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zpak_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zpak_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zpak_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zpak_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zpak_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zpak_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zpak_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zpak_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zpak_stream *stream;
    const xx_zpak_member *member;
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
    stream = (xx_zpak_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zpak_path_safe(member->name)) return false;

    path_option = xx_zpak_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zpak_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zpak_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_zpak_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
