/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NetWare installation-disk files (the loose "C.004", "EBT.005", "NWADMIN.IN_"
 * members found on Novell NetWare / Client-32 diskettes).
 *
 * The file is a flat chain of length-prefixed named records.  Every record is
 *
 *   +0x00  u32 LE  record size, INCLUDING these four bytes
 *   +0x04  u8      name count
 *   +0x05  char    name, count - 1 bytes
 *   +0x04+count    payload, record size - 4 - count bytes
 *
 * The count is one larger than the stored name: the producer counts the NUL
 * it does not write, so the byte the count reaches is already the first
 * payload byte.  Reading `count` name bytes swallows that byte and shifts
 * every following field, which is why the name length here is count - 1.
 * The DOS name inside the "NetWareFile" payload is counted the other way
 * round - exact length, explicit NUL behind it - so the two must not be
 * parsed with the same rule.
 *
 * The chain covers the whole file, record by record, with no padding and no
 * terminator: the last record is the one that ends exactly at end of file.
 * That exact-fit requirement is most of this reader's false-positive defence.
 *
 * Observed records, in order:
 *
 *   "NetWareFileInfo"  the signature.  Payload is the fixed 15-byte blob
 *                      "\n\n\x1a__oU\n\n\x1a\0VUuU" - the "\n\n\x1a" pair is
 *                      the usual DOS type/EOF guard.  Always first.
 *   "NetWareFile"      the member metadata, see below.
 *   "VeRsIoN="         optional NLM version string, e.g. "2.00".
 *   "CoPyRiGhT="       optional NLM copyright string.
 *   "PackedData"       the member data: u8 version 0x01, u8 method 0x0A,
 *                      u32 LE uncompressed size, then the token stream.
 *                      That is byte for byte the tail of a Personal NetWare
 *                      "Packed File " header, so the codec is shared with
 *                      the netwarepacked reader.
 *
 * "NetWareFile" payload:
 *
 *   +0x00  u8   0x04, a sub-tag
 *   +0x01  u8   flags, 0x03 or 0x0B
 *   +0x02  50   printable ASCII: "MM-DD-YY", "HH:MM:SS", then an attribute
 *               field and a hex id.  Kept as-is, only the date and time are
 *               interpreted.
 *   +0x34  u32 LE uncompressed size, repeated by "PackedData"
 *   +0x38  u32 LE unidentified (packer stamp)
 *   +0x3c  u8   name count, then the DOS name, count bytes, then a NUL
 *   then   12   unidentified trailer bytes
 *
 * The two size fields are cross-checked against each other; a file whose
 * metadata and stream disagree is rejected rather than trusted.
 *
 * Both size fields are declared sizes and neither is a bound: everything the
 * chain declares is measured against the real file size before it is used.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/netware2/xx_netware2.h"

#include "xxfclib/algo/netwarepack/xx_netwarepack.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The enum arrives with the registration; the shim keeps this file building
 * until then and never invents a value. */
#ifdef NETWARE2
#define XX_NETWARE2_FILE_TYPE XX_FILE_TYPE_NETWARE2
#else
#define XX_NETWARE2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_NETWARE2_SIG_SIZE 20
#define XX_NETWARE2_MIN_RECORD 5
#define XX_NETWARE2_MAX_RECORDS 256
#define XX_NETWARE2_MAX_NAME 64
#define XX_NETWARE2_MAX_HEADER 512
#define XX_NETWARE2_INFO_PAYLOAD 15
#define XX_NETWARE2_META_FIXED 61
#define XX_NETWARE2_META_TRAILER 13
#define XX_NETWARE2_ASCII_SIZE 50
#define XX_NETWARE2_PACKED_FIXED 6
#define XX_NETWARE2_VERSION 0x01U
#define XX_NETWARE2_METHOD_LZH 0x0AU
#define XX_NETWARE2_MAX_INPUT ((int64_t)1024 * 1024 * 1024)
#define XX_NETWARE2_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_NETWARE2_FALLBACK_NAME "netware2.bin"

typedef struct xx_netware2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_netware2_member;

typedef struct xx_netware2_stream_s {
    xx_netware2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_netware2_stream;

static void xx_netware2_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_netware2_read_at(Abstractformat *self, int64_t offset,
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

static uint32_t xx_netware2_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_netware2_range_within(int64_t total, int64_t offset,
                                     int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_netware2_path_safe(const char *name) {
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

/* The name becomes an output file name, so every separator, traversal and
 * control character is rejected here rather than downstream. */
static bool xx_netware2_name_ok(const uint8_t *bytes, size_t length) {
    size_t index;

    if (length < 1U || length > (size_t)XX_NETWARE2_MAX_NAME) return false;
    if (length == 1U && bytes[0] == '.') return false;
    if (length == 2U && bytes[0] == '.' && bytes[1] == '.') return false;
    for (index = 0U; index < length; ++index) {
        uint8_t character = bytes[index];
        if (character < 0x20U || character >= 0x7FU) return false;
        if (character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|') {
            return false;
        }
    }
    return true;
}

static void xx_netware2_stream_free(void *pointer) {
    xx_netware2_stream *stream = (xx_netware2_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member; the caller keeps ownership of @p member->name only on
 * failure. */
static bool xx_netware2_add(xx_netware2_stream *stream,
                            const xx_netware2_member *member) {
    xx_netware2_member *grown = (xx_netware2_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool xx_netware2_digits(const uint8_t *bytes, size_t count) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (bytes[index] < '0' || bytes[index] > '9') return false;
    }
    return true;
}

/* "MM-DD-YYHH:MM:SS" as stored, folded into a DOS-style packed stamp.  Two
 * digit years are read the way the producer meant them: 80..99 are 19xx and
 * everything below is 20xx.  A field that does not parse yields 0 rather
 * than a guess. */
static uint64_t xx_netware2_timestamp(const uint8_t *ascii) {
    uint32_t month, day, year, hour, minute, second;

    /* "MM-DD-YY" immediately followed by "HH:MM:SS": the separators sit at
     * 2, 5, 10 and 13. */
    if (ascii[2] != '-' || ascii[5] != '-' || ascii[10] != ':' ||
        ascii[13] != ':') {
        return 0U;
    }
    if (!xx_netware2_digits(ascii, 2) || !xx_netware2_digits(ascii + 3, 2) ||
        !xx_netware2_digits(ascii + 6, 2) || !xx_netware2_digits(ascii + 8, 2) ||
        !xx_netware2_digits(ascii + 11, 2) ||
        !xx_netware2_digits(ascii + 14, 2)) {
        return 0U;
    }
    month = (uint32_t)((ascii[0] - '0') * 10 + (ascii[1] - '0'));
    day = (uint32_t)((ascii[3] - '0') * 10 + (ascii[4] - '0'));
    year = (uint32_t)((ascii[6] - '0') * 10 + (ascii[7] - '0'));
    hour = (uint32_t)((ascii[8] - '0') * 10 + (ascii[9] - '0'));
    minute = (uint32_t)((ascii[11] - '0') * 10 + (ascii[12] - '0'));
    second = (uint32_t)((ascii[14] - '0') * 10 + (ascii[15] - '0'));
    year += (year >= 80U) ? 1900U : 2000U;
    if (month < 1U || month > 12U || day < 1U || day > 31U || hour > 23U ||
        minute > 59U || second > 59U || year < 1980U) {
        return 0U;
    }
    return ((uint64_t)(year - 1980U) << 25) | ((uint64_t)month << 21) |
           ((uint64_t)day << 16) | ((uint64_t)hour << 11) |
           ((uint64_t)minute << 5) | (uint64_t)(second / 2U);
}

/* ---------------------------------------------------------------- parse -- */

/* One walked record.  `payload_offset` and `payload_size` are already
 * bounded against the file. */
typedef struct xx_netware2_record_s {
    int64_t offset;
    int64_t size;
    int64_t payload_offset;
    int64_t payload_size;
    uint8_t header[XX_NETWARE2_MAX_HEADER];
    size_t header_size;
    size_t name_length;
} xx_netware2_record;

static bool xx_netware2_name_is(const xx_netware2_record *record,
                                const char *text, size_t length) {
    return record->name_length == length &&
           xx_rt_memcmp(record->header + 5, text, length) == 0;
}

/* Reads the record at @p offset (relative to base_address) and bounds every
 * declared length against @p span before returning. */
static bool xx_netware2_read_record(Abstractformat *self, int64_t span,
                                    int64_t offset,
                                    xx_netware2_record *record) {
    uint8_t head[XX_NETWARE2_MIN_RECORD];
    uint32_t declared;
    uint32_t count;
    int64_t want;

    if (offset < 0 || span - offset < XX_NETWARE2_MIN_RECORD) return false;
    if (!xx_netware2_read_at(self, self->base_address + offset, head,
                             sizeof(head))) {
        return false;
    }
    declared = xx_netware2_le32(head);
    count = head[4];
    /* The size counts its own four bytes and the count byte, so anything
     * below five cannot describe a record, and anything past the file end
     * is a truncated or forged chain. */
    if (declared < (uint32_t)XX_NETWARE2_MIN_RECORD) return false;
    if ((int64_t)declared > span - offset) return false;
    /* count - 1 name bytes plus the four size bytes must fit inside the
     * record, and a zero count leaves no name at all. */
    if (count < 2U || (int64_t)count + 4 > (int64_t)declared) return false;

    record->offset = offset;
    record->size = (int64_t)declared;
    record->name_length = (size_t)count - 1U;
    record->payload_offset = offset + 4 + (int64_t)count;
    record->payload_size = (int64_t)declared - 4 - (int64_t)count;
    if (!xx_netware2_range_within(span, record->payload_offset,
                                  record->payload_size)) {
        return false;
    }
    if (record->name_length > (size_t)XX_NETWARE2_MAX_NAME) return false;

    /* Header window: the name plus as much payload as the widest record we
     * interpret needs, never more than the record actually holds. */
    want = (int64_t)XX_NETWARE2_MAX_HEADER;
    if (want > record->size) want = record->size;
    record->header_size = (size_t)want;
    return xx_netware2_read_at(self, self->base_address + offset,
                               record->header, record->header_size);
}

static char *xx_netware2_member_name(const uint8_t *bytes, size_t length) {
    char buffer[XX_NETWARE2_MAX_NAME + 1];
    size_t index;

    if (xx_netware2_name_ok(bytes, length)) {
        for (index = 0U; index < length; ++index) buffer[index] = (char)bytes[index];
        buffer[length] = '\0';
    } else {
        /* Not a reason to reject the container, but a reason not to publish
         * the bytes as a file name. */
        const char *fallback = XX_NETWARE2_FALLBACK_NAME;
        index = 0U;
        while (fallback[index] != '\0') {
            buffer[index] = fallback[index];
            ++index;
        }
        buffer[index] = '\0';
    }
    return xx_str_dup(buffer);
}

static xx_netware2_stream *xx_netware2_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    static const char signature[XX_NETWARE2_SIG_SIZE] = {
        '#', 0x00, 0x00, 0x00, 0x10, 'N', 'e', 't', 'W', 'a',
        'r', 'e',  'F',  'i',  'l',  'e', 'I', 'n', 'f', 'o'};
    xx_netware2_stream *stream;
    xx_netware2_record record;
    xx_netware2_member pending;
    uint8_t probe[XX_NETWARE2_SIG_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    size_t records = 0U;
    bool have_pending = false;
    size_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_NETWARE2_SIG_SIZE || span > XX_NETWARE2_MAX_INPUT) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_netware2_read_at(self, self->base_address, probe, sizeof(probe))) {
        return NULL;
    }
    /* Size, count and tag of the first record in one comparison. */
    for (index = 0U; index < (size_t)XX_NETWARE2_SIG_SIZE; ++index) {
        if (probe[index] != (uint8_t)signature[index]) return NULL;
    }

    stream = (xx_netware2_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    xx_mem_zero(&pending, sizeof(pending));

    offset = 0;
    while (offset < span) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (++records > (size_t)XX_NETWARE2_MAX_RECORDS) goto fail;
        if (!xx_netware2_read_record(self, span, offset, &record)) goto fail;

        if (xx_netware2_name_is(&record, "NetWareFileInfo", 15U)) {
            /* Only ever the first record, and its payload is fixed width. */
            if (offset != 0 || record.payload_size != XX_NETWARE2_INFO_PAYLOAD) {
                goto fail;
            }
        } else if (offset == 0) {
            goto fail;
        } else if (xx_netware2_name_is(&record, "NetWareFile", 11U)) {
            const uint8_t *payload;
            size_t name_count;
            int64_t declared;

            /* The metadata has to be present in the header window in full,
             * name included, or the record is not the one we think it is. */
            if (record.payload_size < XX_NETWARE2_META_FIXED) goto fail;
            if ((size_t)(record.payload_offset - record.offset) >=
                record.header_size) {
                goto fail;
            }
            payload = record.header + (record.payload_offset - record.offset);
            if ((size_t)(record.payload_offset - record.offset) +
                    (size_t)XX_NETWARE2_META_FIXED >
                record.header_size) {
                goto fail;
            }
            if (payload[0] != 0x04U) goto fail;
            for (index = 0U; index < (size_t)XX_NETWARE2_ASCII_SIZE; ++index) {
                uint8_t character = payload[2U + index];
                if (character < 0x20U || character >= 0x7FU) goto fail;
            }
            /* The DOS name is counted the other way round from the record
             * tags: here the count is the exact length and a NUL follows it,
             * so reading count - 1 bytes clips the last character off every
             * name. */
            name_count = payload[60];
            if (name_count < 1U || name_count > (size_t)XX_NETWARE2_MAX_NAME) {
                goto fail;
            }
            if ((int64_t)XX_NETWARE2_META_FIXED + (int64_t)name_count +
                    XX_NETWARE2_META_TRAILER >
                record.payload_size) {
                goto fail;
            }
            if ((size_t)(record.payload_offset - record.offset) + 61U +
                    name_count + 1U >
                record.header_size) {
                goto fail;
            }
            /* The terminator the count does not cover. */
            if (payload[61 + name_count] != 0x00U) goto fail;
            declared = (int64_t)xx_netware2_le32(payload + 52);
            if (declared < 1 || declared > XX_NETWARE2_MAX_DECODED) goto fail;

            if (have_pending) {
                /* Metadata with no stream behind it. */
                xx_str_free(pending.name);
                pending.name = NULL;
                have_pending = false;
            }
            xx_mem_zero(&pending, sizeof(pending));
            pending.name = xx_netware2_member_name(payload + 61, name_count);
            if (!pending.name) goto fail;
            pending.header_offset = self->base_address + record.offset;
            pending.header_size = record.size;
            pending.uncompressed_size = declared;
            pending.timestamp = xx_netware2_timestamp(payload + 2);
            pending.is_folder = false;
            have_pending = true;
        } else if (xx_netware2_name_is(&record, "PackedData", 10U)) {
            const uint8_t *payload;
            int64_t declared;

            if (!have_pending) goto fail;
            if (record.payload_size < XX_NETWARE2_PACKED_FIXED) goto fail;
            if ((size_t)(record.payload_offset - record.offset) +
                    (size_t)XX_NETWARE2_PACKED_FIXED >
                record.header_size) {
                goto fail;
            }
            payload = record.header + (record.payload_offset - record.offset);
            /* 01/0A is the only pair that exists.  Accepting another would
             * hand an arbitrary byte range to the codec. */
            if (payload[0] != XX_NETWARE2_VERSION ||
                payload[1] != XX_NETWARE2_METHOD_LZH) {
                goto fail;
            }
            declared = (int64_t)xx_netware2_le32(payload + 2);
            /* Two independent statements of the same size: disagreement
             * means this is not the pairing the container describes. */
            if (declared != pending.uncompressed_size) goto fail;

            pending.data_offset = self->base_address + record.payload_offset +
                                  XX_NETWARE2_PACKED_FIXED;
            pending.compressed_size =
                record.payload_size - XX_NETWARE2_PACKED_FIXED;
            if (pending.compressed_size < 1) goto fail;
            if (!xx_netware2_range_within(
                    span, record.payload_offset + XX_NETWARE2_PACKED_FIXED,
                    pending.compressed_size)) {
                goto fail;
            }
            pending.method = XX_NETWARE2_METHOD_LZH;
            if (!xx_netware2_path_safe(pending.name)) goto fail;
            if (!xx_netware2_add(stream, &pending)) goto fail;
            pending.name = NULL;
            have_pending = false;
        }
        /* "VeRsIoN=" and "CoPyRiGhT=" carry NLM strings, not members; any
         * other tag is walked over the same way.  The chain arithmetic is
         * what validates them. */
        offset += record.size;
    }
    /* The chain has to land exactly on end of file: a record that overshoots
     * was already refused, and one that stops short means the file is not
     * this container. */
    if (offset != span) goto fail;
    if (have_pending || stream->count == 0U) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_str_free(pending.name);
    xx_netware2_stream_free(stream);
    return NULL;
}

/* --------------------------------------------------------------- decode -- */

static bool xx_netware2_decode(Abstractformat *self,
                               const xx_netware2_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != (uint32_t)XX_NETWARE2_METHOD_LZH) return false;
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_NETWARE2_MAX_INPUT) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_NETWARE2_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_netware2_read_at(self, member->data_offset, input,
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
    /* The declared size is the stream's only end marker, so a short decode
     * is a failure and never a short read. */
    if (!xx_netwarepack_decode_memory(input, (size_t)member->compressed_size,
                                      output, (size_t)member->uncompressed_size,
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

void xx_netware2_init(xx_netware2 *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_NETWARE2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-netware-file");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_netware2_check_is_valid;
    archive->format.handle_base_info = xx_netware2_handle_base_info;
    archive->format.get_format_size = xx_netware2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_netware2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_netware2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_netware2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_netware2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_netware2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_netware2_free_archive_records_reading;
    archive->format.destroy = xx_netware2_vtable_destroy;
}

xx_netware2 *xx_netware2_create(xx_io_device *device, int64_t base_address) {
    xx_netware2 *archive = (xx_netware2 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_netware2_init(archive, device, base_address);
    return archive;
}

void xx_netware2_destroy(xx_netware2 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_netware2_free(xx_netware2 *archive) {
    if (!archive) return;
    xx_netware2_destroy(archive);
    xx_mem_free(archive);
}

static void xx_netware2_vtable_destroy(Abstractformat *self) {
    xx_netware2_destroy((xx_netware2 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_netware2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_netware2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_netware2_parse(self, pd);
    if (!stream) return false;
    xx_netware2_stream_free(stream);
    return true;
}

bool xx_netware2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_netware2 *archive = (xx_netware2 *)self;
    xx_netware2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_netware2_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_netware2_stream_free(stream);
    return true;
}

int64_t xx_netware2_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_netware2_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_netware2 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_netware2_set_record(xx_archive_record *record,
                                   const xx_netware2_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
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

static bool xx_netware2_copy_options(xx_list_s *target,
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

static const xx_var *xx_netware2_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_netware2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_netware2_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_netware2_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_netware2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_netware2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_netware2_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_netware2_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_netware2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_netware2_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_netware2_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_netware2_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_netware2_set_record(&state->current_record,
                                               &stream->items[stream->index]);
    return state->has_record;
}

bool xx_netware2_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_netware2_stream *stream;
    const xx_netware2_member *member;
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
    stream = (xx_netware2_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_netware2_path_safe(member->name)) return false;

    path_option =
        xx_netware2_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_netware2_decode(self, member, &plain, &plain_size, pd);
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

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_netware2_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_netware2_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
