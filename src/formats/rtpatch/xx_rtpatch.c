/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Pocket Soft RTPatch (.rtp) patch packages.
 *
 *   header, 0x1a bytes at offset 0:
 *     0x00  "K*"
 *     0x02  u16 LE generation: 110, 200, 211, 320, 400, 410, 500 or 650
 *     0x04  builder bookkeeping, generation dependent
 *
 * A package is a PATCH, not an archive, and most of its records are delta
 * programs that copy from a checksum-matched source file. Those are not
 * members in any sense this reader can honour, so it publishes only the
 * things a package carries that stand on their own:
 *
 *   - whole-file records, whose stream is the complete destination file;
 *   - the banner block, a counted-string table rendered as Comments.txt;
 *   - delta programs proved to fill the whole destination from their own
 *     literal payload, without reading a source file.
 *
 * All other delta records are deliberately not published: their output
 * depends on a separate checksum-matched source file.
 *
 * The record layout is generation dependent and the records are not chained,
 * so they are found by scanning for the seven bytes every RTPatch stream
 * opens with - b5 9c 00 ff 04 00 10 - and working backwards:
 *
 *   1.10 - 3.20  the 34-byte descriptor sits immediately in front of the
 *                stream, at streamOffset - 34.
 *   4.00 and up  the descriptor is followed by eight opaque bytes and then a
 *                counted, NUL-terminated long name that ends exactly at the
 *                stream, so the descriptor is at nameOffset - 42 and the
 *                name is found by a bounded backward search.
 *
 *   descriptor, 34 bytes:
 *     0x00  name, 14 bytes, NUL terminated and NUL padded (8.3)
 *     0x0e  u16 LE DOS attributes; only read-only, hidden, system and
 *           archive (mask 0x27) may be set
 *     0x10  u32 LE uncompressed size, non-zero
 *     0x14  u16 LE DOS date
 *     0x16  u16 LE DOS time
 *     0x18  10 bytes of builder bookkeeping
 *
 * A whole-file record states its shape in the nine bytes in front of the
 * descriptor: a destination count of exactly 1, then the uncompressed size
 * again, then the stream's exact compressed extent. A delta record puts a
 * source descriptor there instead, which is what tells the two apart without
 * interpreting the opcode program.
 *
 * Names come from the descriptor. The reference additionally resolves each
 * record's installed path out of the bytes ahead of the record header, which
 * is what keeps five copies of MAIN3.BMP under five directories apart; this
 * reader does not, and disambiguates a repeated name with a "~N" suffix
 * instead.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rtpatch/xx_rtpatch.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/rtpatch/xx_rtpatch.h"

#include <stdio.h>

#define XX_RTPATCH_COPY_CHUNK (64 * 1024)

typedef struct xx_rtpatch_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_rtpatch_member;

typedef struct xx_rtpatch_stream_s {
    xx_rtpatch_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_rtpatch_stream;

static void xx_rtpatch_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_rtpatch_read_at(Abstractformat *self, int64_t offset,
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

static XXFC_MAYBE_UNUSED bool xx_rtpatch_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_rtpatch_path_safe(const char *name) {
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

static void xx_rtpatch_stream_free(void *pointer) {
    xx_rtpatch_stream *stream = (xx_rtpatch_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_rtpatch_add(xx_rtpatch_stream *stream,
                          const xx_rtpatch_member *member) {
    xx_rtpatch_member *grown = (xx_rtpatch_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_RTPATCH_NAME_WINDOW 256
#define XX_RTPATCH_MAX_NAME 300
#define XX_RTPATCH_HEADER_SIZE 0x1A
#define XX_RTPATCH_DESCRIPTOR_SIZE 34
#define XX_RTPATCH_AUGMENTED_PREFIX_SIZE 42
#define XX_RTPATCH_RECORD_AREA_OFFSET 8
#define XX_RTPATCH_MAX_LIST_ITEMS 4096
#define XX_RTPATCH_MAX_MEMBERS 4096
#define XX_RTPATCH_MAX_SCAN ((int64_t)64 * 1024 * 1024)
#define XX_RTPATCH_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_RTPATCH_STREAM_PREFIX_SIZE 7
#define XX_RTPATCH_STREAM_MIN_SIZE 8
#define XX_RTPATCH_METHOD_STREAM 1U
#define XX_RTPATCH_METHOD_TEXT 2U
#define XX_RTPATCH_METHOD_DELTA_FILL 3U
#define XX_RTPATCH_DELTA_OVERHEAD 4U
#define XX_RTPATCH_DELTA_PAYLOAD_OFFSET 3U
#define XX_RTPATCH_MAX_SOURCES 8U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_rtpatch_le16(const uint8_t *data);
static uint32_t xx_rtpatch_le32(const uint8_t *data);
static bool xx_rtpatch_version_supported(uint16_t version);
static bool xx_rtpatch_fixed_descriptor(uint16_t version);
static bool xx_rtpatch_dos_valid(uint16_t date, uint16_t time);
static bool xx_rtpatch_safe_name(const uint8_t *data, size_t size, bool fixed_field, char *out_name);
static bool xx_rtpatch_descriptor(const uint8_t *data, int64_t size, int64_t offset, char *out_name, int64_t *out_size, uint64_t *out_timestamp);
static bool xx_rtpatch_stream_at(const uint8_t *data, int64_t size, int64_t offset);
static bool xx_rtpatch_whole_file(const uint8_t *data, int64_t size, int64_t descriptor_offset, int64_t stream_offset, int64_t uncompressed_size, int64_t *out_compressed);
static bool xx_rtpatch_delta_fill(const uint8_t *data, int64_t size, int64_t descriptor_offset, int64_t stream_offset, int64_t uncompressed_size, int64_t *out_header, int64_t *out_compressed, xx_pd_struct *pd);
static bool xx_rtpatch_inline_whole(const uint8_t *data, int64_t size, int64_t stream_offset, char *out_name, int64_t *out_header, int64_t *out_uncompressed, int64_t *out_compressed, xx_pd_struct *pd);
static bool xx_rtpatch_v400_delta_fill(const uint8_t *data, int64_t size, int64_t descriptor_offset, int64_t stream_offset, const char *destination_name, int64_t destination_size, int64_t *out_header, int64_t *out_compressed, xx_pd_struct *pd);
static bool xx_rtpatch_string_list(const uint8_t *data, int64_t size, int64_t offset, int64_t *out_end, int64_t *out_decoded, bool *out_directory);
static bool xx_rtpatch_unique(xx_rtpatch_stream *stream, const char *name, char *out_name, size_t out_size);
static xx_rtpatch_stream *xx_rtpatch_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_rtpatch_decode(Abstractformat *self, const xx_rtpatch_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* How far back of a stream the 4.x/6.x long name may start. The name is a
 * counted string of at most 255 bytes, so 256 covers every spelling. */

static uint16_t xx_rtpatch_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_rtpatch_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_rtpatch_version_supported(uint16_t version) {
    return version == 110U || version == 200U || version == 211U ||
           version == 320U || version == 400U || version == 410U ||
           version == 500U || version == 650U;
}

/* True for the generations that place the fixed 8.3 descriptor at a known
 * distance in front of the stream. 3.20 still uses that layout; 4.00 already
 * carries the augmented block with a counted long name, exactly as 4.10
 * does. */
static bool xx_rtpatch_fixed_descriptor(uint16_t version) {
    return version <= 211U || version == 320U;
}

/* A packed DOS date/time that could not have come from a clock is the
 * cheapest way to reject compressed bytes that happen to look like a
 * descriptor. */
static bool xx_rtpatch_dos_valid(uint16_t date, uint16_t time) {
    uint32_t day = (uint32_t)(date & 0x1FU);
    uint32_t month = (uint32_t)((date >> 5) & 0x0FU);
    uint32_t hour = (uint32_t)((time >> 11) & 0x1FU);
    uint32_t minute = (uint32_t)((time >> 5) & 0x3FU);
    uint32_t second = (uint32_t)(time & 0x1FU);

    return day >= 1U && day <= 31U && month >= 1U && month <= 12U &&
           hour <= 23U && minute <= 59U && second <= 29U;
}

/* Take a name verbatim, normalising only the DOS separator. Nothing is
 * stripped and nothing is folded onto '_', so two distinct records can never
 * collapse onto one output path by rewriting. */
static bool xx_rtpatch_safe_name(const uint8_t *data, size_t size,
                                 bool fixed_field, char *out_name) {
    size_t length = 0U;
    size_t index;

    if (size == 0U || size >= (size_t)XX_RTPATCH_MAX_NAME) return false;
    while (length < size && data[length] != 0U) ++length;
    if (length == 0U) return false;
    /* A fixed field must be NUL padded to its end: a stale byte behind the
     * terminator means this is not the buffer the parser assumes. */
    if (fixed_field) {
        for (index = length; index < size; ++index) {
            if (data[index] != 0U) return false;
        }
    }
    for (index = 0U; index < length; ++index) {
        if (data[index] < 0x20U || data[index] > 0x7EU) return false;
        out_name[index] = (data[index] == '\\') ? '/' : (char)data[index];
    }
    out_name[length] = '\0';

    if (out_name[0] == '/') return false;
    index = 0U;
    while (index < length) {
        size_t start = index;
        size_t part;
        while (index < length && out_name[index] != '/') ++index;
        part = index - start;
        if (part == 0U || (part == 1U && out_name[start] == '.') ||
            (part == 2U && out_name[start] == '.' &&
             out_name[start + 1U] == '.')) {
            return false;
        }
        if (index < length) ++index;
    }
    return true;
}

/* The 34-byte 8.3 descriptor. Attributes, a non-zero size and a real
 * date/time are what keep compressed bytes from passing as one: a random
 * 34-byte window clears the 0x27 attribute mask about one time in 2000 and
 * then still has to produce a valid calendar date. */
static bool xx_rtpatch_descriptor(const uint8_t *data, int64_t size,
                                  int64_t offset, char *out_name,
                                  int64_t *out_size, uint64_t *out_timestamp) {
    const uint8_t *field;
    uint16_t attributes;
    uint32_t stored_size;
    uint16_t date;
    uint16_t time;

    if (offset < 0 || offset > size - XX_RTPATCH_DESCRIPTOR_SIZE) return false;
    field = data + offset;
    if (!xx_rtpatch_safe_name(field, 14U, true, out_name)) return false;

    attributes = xx_rtpatch_le16(field + 14);
    stored_size = xx_rtpatch_le32(field + 16);
    date = xx_rtpatch_le16(field + 20);
    time = xx_rtpatch_le16(field + 22);
    /* Read-only, hidden, system and archive in any combination. What a
     * member descriptor must never carry is the volume-label or directory
     * bit, and compressed data mimicking a descriptor almost always sets
     * something above them. An exact 0/0x20/0x21 test would silently drop
     * every hidden or system member. */
    if ((attributes | 0x27U) != 0x27U) return false;
    if (stored_size == 0U || stored_size > 0x7FFFFFFFU) return false;
    if (!xx_rtpatch_dos_valid(date, time)) return false;

    *out_size = (int64_t)stored_size;
    *out_timestamp = ((uint64_t)date << 16) | (uint64_t)time;
    return true;
}

static bool xx_rtpatch_stream_at(const uint8_t *data, int64_t size,
                                 int64_t offset) {
    static const uint8_t prefix[XX_RTPATCH_STREAM_PREFIX_SIZE] = {
        0xB5U, 0x9CU, 0x00U, 0xFFU, 0x04U, 0x00U, 0x10U};
    size_t index;

    if (offset < 0 || offset > size - XX_RTPATCH_STREAM_MIN_SIZE) return false;
    for (index = 0U; index < sizeof(prefix); ++index) {
        if (data[offset + (int64_t)index] != prefix[index]) return false;
    }
    /* The eighth byte is the model selector; its high nibble is always 8. */
    return (data[offset + 7] & 0xF0U) == 0x80U;
}

/* Whole-file records have no source descriptor. Immediately before their
 * single destination descriptor they carry the count byte, the uncompressed
 * size, and the exact compressed extent. Delta records instead place a source
 * descriptor in this position, so this invariant distinguishes the two
 * without attempting to interpret the delta opcode program - and refusing
 * here is what keeps a patch record from being published as a file. */
static bool xx_rtpatch_whole_file(const uint8_t *data, int64_t size,
                                  int64_t descriptor_offset,
                                  int64_t stream_offset,
                                  int64_t uncompressed_size,
                                  int64_t *out_compressed) {
    uint32_t stated_size;
    uint32_t compressed;

    *out_compressed = 0;
    if (descriptor_offset < 9 ||
        stream_offset < descriptor_offset + XX_RTPATCH_DESCRIPTOR_SIZE ||
        stream_offset > size - XX_RTPATCH_STREAM_MIN_SIZE) {
        return false;
    }
    if (data[descriptor_offset - 9] != 1U) return false;
    stated_size = xx_rtpatch_le32(data + descriptor_offset - 8);
    compressed = xx_rtpatch_le32(data + descriptor_offset - 4);
    if ((int64_t)stated_size != uncompressed_size) return false;
    if (compressed < XX_RTPATCH_STREAM_MIN_SIZE ||
        (int64_t)compressed > size - stream_offset) {
        return false;
    }
    *out_compressed = (int64_t)compressed;
    return true;
}

static bool xx_rtpatch_delta_program_is_fill(const uint8_t *program,
                                             size_t destination_size) {
    uint8_t fill;
    uint8_t end_a;
    uint8_t end_b;
    uint8_t end;
    if (!program || destination_size == 0U) return false;
    fill = program[0] == 0x15U ? 0x12U : 5U;
    end_a = program[0] == 0x15U ? 0x16U : 1U;
    end_b = program[0] == 0x15U ? 0x15U : 2U;
    end = program[destination_size + XX_RTPATCH_DELTA_PAYLOAD_OFFSET];
    return (program[0] == 2U || program[0] == 0x15U) &&
           program[1] == 0U && program[2] == fill &&
           (end == end_a || end == end_b);
}

static bool xx_rtpatch_delta_payload_proved(const uint8_t *data,
                                             int64_t size,
                                             int64_t stream_offset,
                                             int64_t destination_size,
                                             int64_t compressed_size,
                                             xx_pd_struct *pd) {
    uint8_t *program;
    size_t written = 0U;
    size_t program_size;
    bool valid;
    if (destination_size <= 0 ||
        destination_size > XX_RTPATCH_MAX_SCAN - XX_RTPATCH_DELTA_OVERHEAD ||
        compressed_size < XX_RTPATCH_STREAM_MIN_SIZE ||
        stream_offset < 0 || stream_offset > size ||
        compressed_size > size - stream_offset ||
        (pd && xx_pd_is_stopped(pd))) return false;
    program_size = (size_t)destination_size + XX_RTPATCH_DELTA_OVERHEAD;
    program = (uint8_t *)xx_mem_alloc(program_size);
    if (!program) return false;
    valid = xx_rtpatch_decode_memory(data + stream_offset,
                                      (size_t)compressed_size,
                                      program, program_size, &written) &&
            written == program_size &&
            xx_rtpatch_delta_program_is_fill(program,
                                               (size_t)destination_size);
    xx_mem_free(program);
    return valid;
}

/* Reference type 0x4000 normally needs a checksum-matched source file.  The
 * special four-byte opcode envelope [mode, destination 0, fill, end] carries
 * the whole destination inside the compressed stream.  Prove both the record
 * header and the decoded opcode shape before publishing it. */
static bool xx_rtpatch_delta_fill(const uint8_t *data, int64_t size,
                                   int64_t descriptor_offset,
                                   int64_t stream_offset,
                                   int64_t uncompressed_size,
                                   int64_t *out_header,
                                   int64_t *out_compressed,
                                   xx_pd_struct *pd) {
    int64_t header = -1;
    int64_t program_size;
    uint32_t sources;

    *out_header = -1;
    *out_compressed = 0;
    if (descriptor_offset < 9 ||
        stream_offset != descriptor_offset + XX_RTPATCH_DESCRIPTOR_SIZE ||
        uncompressed_size <= 0 ||
        uncompressed_size > XX_RTPATCH_MAX_SCAN - XX_RTPATCH_DELTA_OVERHEAD ||
        !xx_rtpatch_stream_at(data, size, stream_offset)) return false;
    program_size = uncompressed_size + XX_RTPATCH_DELTA_OVERHEAD;

    /* The two-count form names one or more source descriptors; the shorter
     * one-count form occurs on later alternatives of the same record. */
    for (sources = 1U; sources <= XX_RTPATCH_MAX_SOURCES; ++sources) {
        int64_t block = descriptor_offset -
                        (int64_t)XX_RTPATCH_DESCRIPTOR_SIZE * sources;
        uint32_t i;
        bool source_block = true;
        if (block < 10) continue;
        for (i = 0U; i < sources; ++i) {
            char source_name[XX_RTPATCH_MAX_NAME];
            int64_t source_size;
            uint64_t source_time;
            if (!xx_rtpatch_descriptor(data, size,
                    block + (int64_t)XX_RTPATCH_DESCRIPTOR_SIZE * i,
                    source_name, &source_size, &source_time)) {
                source_block = false;
                break;
            }
        }
        if (source_block && data[block - 10] == sources &&
            data[block - 9] == 1U) {
            header = block - 8;
            break;
        }
    }
    if (header < 0 && data[descriptor_offset - 9] == 1U)
        header = descriptor_offset - 8;
    if (header < 0 || header > size - 8 ||
        (int64_t)xx_rtpatch_le32(data + header) != program_size) return false;

    *out_compressed = (int64_t)xx_rtpatch_le32(data + header + 4);
    if (!xx_rtpatch_delta_payload_proved(data, size, stream_offset,
                                          uncompressed_size,
                                          *out_compressed, pd)) return false;
    *out_header = header;
    return true;
}

/* Some 4.00 packages put a flat whole-file record before the descriptor
 * chain.  Its short and long counted names flank a duplicate size field;
 * there is no 34-byte descriptor.  The 0x6440 record tag, ten reserved zero
 * bytes, one destination, two matching names, exact size fields and a full
 * codec decode together prove the member without scanning arbitrary bytes. */
static bool xx_rtpatch_inline_whole(const uint8_t *data, int64_t size,
                                     int64_t stream_offset, char *out_name,
                                     int64_t *out_header,
                                     int64_t *out_uncompressed,
                                     int64_t *out_compressed,
                                     xx_pd_struct *pd) {
    int64_t name_offset, lowest;
    if (!xx_rtpatch_stream_at(data, size, stream_offset)) return false;
    lowest = stream_offset - XX_RTPATCH_NAME_WINDOW;
    if (lowest < 34) lowest = 34;
    for (name_offset = stream_offset - 2; name_offset >= lowest;
         --name_offset) {
        int64_t length = data[name_offset];
        char long_name[XX_RTPATCH_MAX_NAME];
        int64_t short_offset;
        if (length < 2 || name_offset + 1 + length != stream_offset ||
            data[stream_offset - 1] != 0U ||
            !xx_rtpatch_safe_name(data + name_offset + 1,
                                  (size_t)length - 1U, false, long_name))
            continue;
        for (short_offset = name_offset - 11; short_offset >= lowest;
             --short_offset) {
            int64_t short_length = data[short_offset];
            int64_t end, header, unpacked, packed, gap;
            char short_name[XX_RTPATCH_MAX_NAME];
            uint8_t *plain;
            size_t written = 0U;
            bool valid;
            if (short_length < 2 || short_length > 14 ||
                short_offset < 21 ||
                short_offset + 1 + short_length > name_offset ||
                !xx_rtpatch_safe_name(data + short_offset + 1,
                                      (size_t)short_length, false,
                                      short_name) ||
                data[short_offset + short_length] != 0U ||
                xx_str_icmp(short_name, long_name)) continue;
            end = short_offset + 1 + short_length;
            gap = name_offset - end;
            if (gap != 9 && gap != 11) continue;
            header = short_offset - 8;
            if (data[short_offset - 21] != 0x40U ||
                data[short_offset - 20] != 0x64U ||
                data[short_offset - 9] != 1U) continue;
            {
                int64_t i;
                bool zeros = true;
                for (i = short_offset - 19; i < short_offset - 9; ++i)
                    if (data[i] != 0U) zeros = false;
                if (!zeros) continue;
            }
            unpacked = (int64_t)xx_rtpatch_le32(data + header);
            packed = (int64_t)xx_rtpatch_le32(data + header + 4);
            if (unpacked <= 0 || unpacked > XX_RTPATCH_MAX_SCAN ||
                packed < XX_RTPATCH_STREAM_MIN_SIZE ||
                packed > size - stream_offset) continue;
            if (gap == 9) {
                int64_t i;
                if (xx_rtpatch_le32(data + end) != (uint32_t)unpacked)
                    continue;
                for (i = end + 4; i < name_offset; ++i)
                    if (data[i] != 0U) break;
                if (i != name_offset) continue;
            } else {
                int64_t i;
                if (data[end] != 0x60U ||
                    xx_rtpatch_le32(data + end + 1) != (uint32_t)unpacked)
                    continue;
                for (i = end + 5; i < name_offset; ++i)
                    if (data[i] != 0U) break;
                if (i != name_offset) continue;
            }
            if (pd && xx_pd_is_stopped(pd)) return false;
            plain = (uint8_t *)xx_mem_alloc((size_t)unpacked);
            if (!plain) return false;
            valid = xx_rtpatch_decode_memory(data + stream_offset,
                                              (size_t)packed, plain,
                                              (size_t)unpacked, &written) &&
                    written == (size_t)unpacked;
            xx_mem_free(plain);
            if (!valid) continue;
            if (xx_rt_snprintf(out_name, XX_RTPATCH_MAX_NAME,
                               "%s", long_name) < 0) return false;
            *out_header = header;
            *out_uncompressed = unpacked;
            *out_compressed = packed;
            return true;
        }
    }
    return false;
}

/* A 4.00 source-free delta has a source descriptor with its counted long
 * name immediately before the destination descriptor.  The preceding two
 * count bytes are both one; the stream header states destination size + 4,
 * and decoding must prove the same literal-fill opcode envelope as 2.x. */
static bool xx_rtpatch_v400_delta_fill(const uint8_t *data, int64_t size,
                                       int64_t descriptor_offset,
                                       int64_t stream_offset,
                                       const char *destination_name,
                                       int64_t destination_size,
                                       int64_t *out_header,
                                       int64_t *out_compressed,
                                       xx_pd_struct *pd) {
    int64_t name_offset, lowest;
    if (descriptor_offset < XX_RTPATCH_AUGMENTED_PREFIX_SIZE ||
        stream_offset < descriptor_offset + XX_RTPATCH_DESCRIPTOR_SIZE ||
        destination_size <= 0 ||
        destination_size > XX_RTPATCH_MAX_SCAN - XX_RTPATCH_DELTA_OVERHEAD)
        return false;
    lowest = descriptor_offset - XX_RTPATCH_NAME_WINDOW;
    if (lowest < XX_RTPATCH_AUGMENTED_PREFIX_SIZE)
        lowest = XX_RTPATCH_AUGMENTED_PREFIX_SIZE;
    for (name_offset = descriptor_offset - 2; name_offset >= lowest;
         --name_offset) {
        int64_t length = data[name_offset];
        int64_t source_descriptor, header, source_size;
        uint64_t source_time;
        int64_t packed;
        char source_short[XX_RTPATCH_MAX_NAME];
        char source_long[XX_RTPATCH_MAX_NAME];
        if (length < 2 || name_offset + 1 + length != descriptor_offset ||
            data[descriptor_offset - 1] != 0U) continue;
        source_descriptor = name_offset - XX_RTPATCH_AUGMENTED_PREFIX_SIZE;
        header = source_descriptor - 8;
        if (header < 2 ||
            !xx_rtpatch_descriptor(data, size, source_descriptor,
                                   source_short, &source_size,
                                   &source_time) ||
            !xx_rtpatch_safe_name(data + name_offset + 1,
                                  (size_t)length - 1U, false,
                                  source_long) ||
            source_size != destination_size ||
            xx_str_icmp(source_short, source_long) ||
            xx_str_icmp(source_long, destination_name) ||
            data[header - 2] != 1U || data[header - 1] != 1U ||
            xx_rtpatch_le32(data + header) !=
                (uint32_t)(destination_size + XX_RTPATCH_DELTA_OVERHEAD))
            continue;
        packed = (int64_t)xx_rtpatch_le32(data + header + 4);
        if (!xx_rtpatch_delta_payload_proved(data, size, stream_offset,
                                              destination_size, packed, pd))
            continue;
        *out_header = header;
        *out_compressed = packed;
        return true;
    }
    return false;
}

/* One counted-string list: a u16 count and that many length-prefixed,
 * NUL-terminated strings. Both the directory table and the banner use it. */
static bool xx_rtpatch_string_list(const uint8_t *data, int64_t size,
                                   int64_t offset, int64_t *out_end,
                                   int64_t *out_decoded, bool *out_directory) {
    int64_t position;
    int64_t decoded = 0;
    uint32_t count;
    uint32_t index;
    bool directory = true;

    *out_end = 0;
    *out_decoded = 0;
    *out_directory = false;
    if (offset < 0 || offset > size - 2) return false;
    count = (uint32_t)xx_rtpatch_le16(data + offset);
    if (count == 0U || count > (uint32_t)XX_RTPATCH_MAX_LIST_ITEMS) {
        return false;
    }

    position = offset + 2;
    for (index = 0U; index < count; ++index) {
        uint32_t length;
        uint32_t byte;

        if (position >= size) return false;
        length = (uint32_t)data[position++];
        /* Every string is counted AND NUL terminated, with the terminator
         * counted; an interior NUL means this is not a string table. */
        if (length == 0U || position > size - (int64_t)length) return false;
        if (data[position + (int64_t)length - 1] != 0U) return false;
        for (byte = 0U; byte + 1U < length; ++byte) {
            if (data[position + (int64_t)byte] == 0U) return false;
        }
        for (byte = 0U; byte + 1U < length; ++byte) {
            uint8_t character = data[position + (int64_t)byte];
            /* A directory entry is a printable path with no spaces and no
             * DOS wildcard characters; a banner line is prose and will fail
             * this almost immediately. */
            if (character < 0x21U || character > 0x7EU || character == '"' ||
                character == '<' || character == '>' || character == '|' ||
                character == '?' || character == '*') {
                directory = false;
            }
        }
        if (length == 1U) directory = false;
        /* Each line is emitted without its NUL and followed by CR LF. */
        decoded += (int64_t)length - 1 + 2;
        position += (int64_t)length;
    }

    *out_end = position;
    *out_decoded = decoded;
    *out_directory = directory;
    return true;
}

/* Disambiguate a repeated name. Without the reference's record-path
 * resolution two destinations that differ only by directory arrive here with
 * the same 8.3 name, and letting both through would silently overwrite one
 * with the other at extraction time. */
static bool xx_rtpatch_unique(xx_rtpatch_stream *stream, const char *name,
                              char *out_name, size_t out_size) {
    uint32_t suffix;
    size_t index;
    bool taken = false;

    for (index = 0U; index < stream->count; ++index) {
        if (xx_rt_strcmp(stream->items[index].name, name) == 0) taken = true;
    }
    if (!taken) {
        if (xx_rt_snprintf(out_name, out_size, "%s", name) < 0) return false;
        return true;
    }
    for (suffix = 1U; suffix <= (uint32_t)XX_RTPATCH_MAX_MEMBERS; ++suffix) {
        if (xx_rt_snprintf(out_name, out_size, "%s~%u", name,
                           (unsigned)suffix) < 0) {
            return false;
        }
        taken = false;
        for (index = 0U; index < stream->count; ++index) {
            if (xx_rt_strcmp(stream->items[index].name, out_name) == 0) {
                taken = true;
            }
        }
        if (!taken) return true;
    }
    return false;
}

static xx_rtpatch_stream *xx_rtpatch_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    xx_rtpatch_stream *stream = NULL;
    uint8_t *data = NULL;
    char name[XX_RTPATCH_MAX_NAME];
    char unique[XX_RTPATCH_MAX_NAME + 16];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t first_record = -1;
    int64_t list_offset;
    uint16_t version;
    int32_t attempt;
    bool fixed;
    bool record_seen = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_RTPATCH_HEADER_SIZE || span > XX_RTPATCH_MAX_SCAN) {
        return NULL;
    }

    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_rtpatch_read_at(self, self->base_address, data, (size_t)span)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    version = xx_rtpatch_le16(data + 2);
    if (data[0] != 'K' || data[1] != '*' ||
        !xx_rtpatch_version_supported(version)) {
        goto fail;
    }
    /* Two bytes of magic and a version word from a set of eight are thin on
     * their own - over a large corpus the version word alone selects dozens
     * of positions inside compressed data - so each generation's header tail
     * is required to hold its invariants too. This is the check a later
     * reader will be tempted to drop, and dropping it is what makes the
     * format open on a stray "K*". */
    if (version <= 211U) {
        for (offset = 0x10; offset < XX_RTPATCH_HEADER_SIZE; ++offset) {
            if (data[offset] != 0U) goto fail;
        }
    } else if (version == 320U || version == 400U) {
        /* 3.20 and 4.00 carry package totals at 0x0c and 0x10 and the
         * builder's fixed 4 at 0x18, while the reserved u32 at 0x14 still
         * reads zero. */
        if (xx_rtpatch_le32(data + 0x14) != 0U ||
            xx_rtpatch_le16(data + 0x18) != 4U) {
            goto fail;
        }
    }

    stream = (xx_rtpatch_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    fixed = xx_rtpatch_fixed_descriptor(version);

    /* The records are not chained and their layout is generation dependent,
     * so they are found by scanning for the stream prefix and working
     * backwards to the descriptor. */
    for (offset = XX_RTPATCH_HEADER_SIZE;
         offset <= span - XX_RTPATCH_STREAM_MIN_SIZE; ++offset) {
        xx_rtpatch_member member;
        int64_t descriptor_offset = -1;
        int64_t uncompressed_size = 0;
        int64_t compressed_size = 0;
        int64_t delta_header = -1;
        int64_t inline_header = -1;
        int64_t long_name_offset = -1;
        uint64_t timestamp = 0U;
        bool found = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_rtpatch_stream_at(data, span, offset)) continue;

        if (version == 400U && xx_rtpatch_inline_whole(
                data, span, offset, name, &inline_header,
                &uncompressed_size, &compressed_size, pd)) {
            descriptor_offset = inline_header + 8;
            found = true;
        } else if (fixed) {
            descriptor_offset = offset - XX_RTPATCH_DESCRIPTOR_SIZE;
            found = xx_rtpatch_descriptor(data, span, descriptor_offset, name,
                                          &uncompressed_size, &timestamp);
        } else {
            /* 4.x/6.x keeps the 8.3 descriptor, adds eight opaque bytes, then
             * places a counted long name immediately before the stream.
             * Search backwards and take the nearest fully validating
             * candidate: compressed bytes can accidentally mimic an earlier
             * length byte. */
            int64_t name_offset;
            int64_t lowest = offset - XX_RTPATCH_NAME_WINDOW;

            if (lowest < XX_RTPATCH_AUGMENTED_PREFIX_SIZE) {
                lowest = XX_RTPATCH_AUGMENTED_PREFIX_SIZE;
            }
            for (name_offset = offset - 2; name_offset >= lowest;
                 --name_offset) {
                int64_t length = (int64_t)data[name_offset];
                int64_t scan;
                bool embedded = false;
                char long_name[XX_RTPATCH_MAX_NAME];

                if (length < 2 || name_offset + 1 + length != offset) continue;
                if (data[offset - 1] != 0U) continue;
                for (scan = name_offset + 1; scan < offset - 1; ++scan) {
                    if (data[scan] == 0U) embedded = true;
                }
                if (embedded) continue;

                descriptor_offset =
                    name_offset - XX_RTPATCH_AUGMENTED_PREFIX_SIZE;
                if (!xx_rtpatch_descriptor(data, span, descriptor_offset, name,
                                           &uncompressed_size, &timestamp)) {
                    continue;
                }
                if (!xx_rtpatch_safe_name(data + name_offset + 1,
                                          (size_t)(length - 1), false,
                                          long_name)) {
                    continue;
                }
                /* The long name replaces the 8.3 one; the descriptor is still
                 * what proved the record. */
                if (xx_rt_snprintf(name, sizeof(name), "%s", long_name) < 0) {
                    continue;
                }
                found = true;
                long_name_offset = name_offset;
                break;
            }
        }
        if (!found) continue;

        /* A validated descriptor sitting immediately in front of a stream
         * prefix proves the container even when the record turns out to be a
         * delta we will not publish. Identification and publication are
         * separate questions: a patch made only of deltas is still an
         * RTPatch package. */
        record_seen = true;
        if (first_record < 0 || descriptor_offset < first_record) {
            first_record = descriptor_offset;
        }

        if (inline_header < 0 &&
            !xx_rtpatch_whole_file(data, span, descriptor_offset, offset,
                                   uncompressed_size, &compressed_size)) {
            /* The two bounded delta layouts must both prove the decoded
             * literal-fill program before exposing a source-free member. */
            if (fixed) {
                if (!xx_rtpatch_delta_fill(data, span, descriptor_offset,
                                           offset, uncompressed_size,
                                           &delta_header, &compressed_size,
                                           pd)) continue;
            } else if (version == 400U && long_name_offset >= 0) {
                if (!xx_rtpatch_v400_delta_fill(data, span,
                        descriptor_offset, offset, name,
                        uncompressed_size, &delta_header,
                        &compressed_size, pd)) continue;
            } else continue;
        }
        if (stream->count >= (size_t)XX_RTPATCH_MAX_MEMBERS) goto fail;
        if (!xx_rtpatch_unique(stream, name, unique, sizeof(unique))) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = self->base_address +
                               (delta_header >= 0 ? delta_header :
                                inline_header >= 0 ? inline_header :
                                                     descriptor_offset - 8);
        member.header_size = self->base_address + offset -
                             member.header_offset;
        member.data_offset = self->base_address + offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = delta_header >= 0 ? XX_RTPATCH_METHOD_DELTA_FILL
                                          : XX_RTPATCH_METHOD_STREAM;
        member.timestamp = timestamp;
        member.is_folder = false;
        member.name = xx_str_dup(unique);
        if (!member.name) goto fail;
        if (!xx_rtpatch_path_safe(member.name) ||
            !xx_rtpatch_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    /* The banner is the counted-string list in the record area that is not
     * the directory table. Only the generations with a fixed descriptor
     * place it where it can be found without walking the records, and it has
     * to end before the first record for the area to be what it claims. */
    list_offset = (version == 320U)
                      ? (XX_RTPATCH_HEADER_SIZE + XX_RTPATCH_RECORD_AREA_OFFSET)
                      : (int64_t)XX_RTPATCH_HEADER_SIZE;
    for (attempt = 0; fixed && first_record > 0 && attempt < 2; ++attempt) {
        xx_rtpatch_member member;
        int64_t end = 0;
        int64_t decoded = 0;
        bool directory = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_rtpatch_string_list(data, span, list_offset, &end, &decoded,
                                    &directory) ||
            end > first_record) {
            break;
        }
        /* A package may open with a directory table, a banner, or both, in
         * the same encoding; the table is skipped and the next list tried. */
        if (directory) {
            list_offset = end;
            continue;
        }
        if (decoded <= 0) break;

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = self->base_address + list_offset;
        member.header_size = 2;
        member.data_offset = self->base_address + list_offset;
        member.compressed_size = end - list_offset;
        member.uncompressed_size = decoded;
        member.method = XX_RTPATCH_METHOD_TEXT;
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_rtpatch_unique(stream, "Comments.txt", unique,
                               sizeof(unique))) {
            goto fail;
        }
        member.name = xx_str_dup(unique);
        if (!member.name) goto fail;
        if (!xx_rtpatch_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        break;
    }

    if (version == 400U && first_record > XX_RTPATCH_HEADER_SIZE) {
        int64_t candidate;
        int64_t banner_at = -1, banner_end = -1, banner_size = 0;
        int64_t scan_end = first_record < 4096 ? first_record : 4096;
        /* In a 4.00 package the final counted prose list may sit behind
         * other metadata.  It must end exactly at a record flag with the
         * destination-path bit set; directory lists are never banners. */
        for (candidate = XX_RTPATCH_HEADER_SIZE;
             candidate < scan_end; ++candidate) {
            int64_t end = 0, decoded = 0;
            bool directory = false;
            uint16_t flag;
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            if (!xx_rtpatch_string_list(data, span, candidate, &end,
                                        &decoded, &directory) ||
                directory || decoded <= 0 || decoded > 65536 ||
                end > first_record || end > span - 2) continue;
            flag = xx_rtpatch_le16(data + end);
            if ((flag & 0x0004U) == 0U ||
                (flag & 0xf000U) == 0x1000U) continue;
            banner_at = candidate;
            banner_end = end;
            banner_size = decoded;
        }
        if (banner_at >= 0) {
            xx_rtpatch_member member;
            xx_mem_zero(&member, sizeof(member));
            if (!xx_rtpatch_unique(stream, "Comments.txt", unique,
                                   sizeof(unique))) goto fail;
            member.name = xx_str_dup(unique);
            if (!member.name) goto fail;
            member.header_offset = self->base_address + banner_at;
            member.header_size = 2;
            member.data_offset = self->base_address + banner_at;
            member.compressed_size = banner_end - banner_at;
            member.uncompressed_size = banner_size;
            member.method = XX_RTPATCH_METHOD_TEXT;
            if (!xx_rtpatch_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }
    }

    /* A package whose records are all deltas publishes nothing, and nothing
     * is the honest answer about its MEMBERS: there is no file in it this
     * reader can produce. It is still an RTPatch package, though, so it stays
     * valid as long as the scan proved at least one real record; refusing it
     * outright left two thirds of the corpus reported as plain BINARY. */
    if (stream->count == 0U && !record_seen) goto fail;
    xx_mem_free(data);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(data);
    xx_rtpatch_stream_free(stream);
    return NULL;
}


/* Descriptor, eight opaque bytes, then the counted long name. */
/* 3.20 and later open the record area eight bytes behind the header: a u16
 * record count and six reserved bytes come first. */
/* The corpus tops out at 56 records; this is a runaway guard, and it is kept
 * small enough that the O(n^2) duplicate-name check stays cheap. */
/* The scan needs the whole package in memory. The reference caps at 2 GiB;
 * this reader will not allocate more than 64 MiB for a probe. */
/* Every stream opens with these seven bytes and a model byte whose high
 * nibble is 8. */


static bool xx_rtpatch_decode(Abstractformat *self,
                              const xx_rtpatch_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t decoded_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->compressed_size > XX_RTPATCH_MAX_DECODED ||
        member->uncompressed_size > XX_RTPATCH_MAX_DECODED) {
        return false;
    }
    if (member->method != XX_RTPATCH_METHOD_STREAM &&
        member->method != XX_RTPATCH_METHOD_TEXT &&
        member->method != XX_RTPATCH_METHOD_DELTA_FILL) {
        return false;
    }
    decoded_size = (size_t)member->uncompressed_size;
    if (member->method == XX_RTPATCH_METHOD_DELTA_FILL) {
        if (decoded_size > (size_t)XX_RTPATCH_MAX_DECODED -
                           XX_RTPATCH_DELTA_OVERHEAD) return false;
        decoded_size += XX_RTPATCH_DELTA_OVERHEAD;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_rtpatch_read_at(self, member->data_offset, input,
                            (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(decoded_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_RTPATCH_METHOD_TEXT) {
        /* Not a compressor: a u16 line count and that many counted,
         * NUL-terminated strings, each emitted without its NUL and followed
         * by CR LF. The block must be consumed whole. */
        if (!xx_rtpatch_text_decode_memory(input,
                                           (size_t)member->compressed_size,
                                           output,
                                           (size_t)member->uncompressed_size,
                                           &written)) {
            xx_mem_free(output);
            xx_mem_free(input);
            return false;
        }
    } else if (!xx_rtpatch_decode_memory(input,
                                         (size_t)member->compressed_size,
                                         output, decoded_size, &written)) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }

    xx_mem_free(input);
    /* The codec has no measuring entry point because the container always
     * stores the decoded length. A short decode reported as success is the
     * one failure the caller cannot detect. */
    if (written != decoded_size ||
        (member->method == XX_RTPATCH_METHOD_DELTA_FILL &&
         !xx_rtpatch_delta_program_is_fill(
             output, (size_t)member->uncompressed_size))) {
        xx_mem_free(output);
        return false;
    }
    if (member->method == XX_RTPATCH_METHOD_DELTA_FILL) {
        xx_rt_memmove(output,
                      output + XX_RTPATCH_DELTA_PAYLOAD_OFFSET,
                      (size_t)member->uncompressed_size);
        written = (size_t)member->uncompressed_size;
    }
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_rtpatch_init(xx_rtpatch *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RTPATCH;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rtpatch");
    xx_format_set_extension(&archive->format, "rtp");
    archive->format.check_is_valid = xx_rtpatch_check_is_valid;
    archive->format.handle_base_info = xx_rtpatch_handle_base_info;
    archive->format.get_format_size = xx_rtpatch_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rtpatch_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rtpatch_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rtpatch_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rtpatch_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rtpatch_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rtpatch_free_archive_records_reading;
    archive->format.destroy = xx_rtpatch_vtable_destroy;
}

xx_rtpatch *xx_rtpatch_create(xx_io_device *device, int64_t base_address) {
    xx_rtpatch *archive = (xx_rtpatch *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_rtpatch_init(archive, device, base_address);
    return archive;
}

void xx_rtpatch_destroy(xx_rtpatch *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rtpatch_free(xx_rtpatch *archive) {
    if (!archive) return;
    xx_rtpatch_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rtpatch_vtable_destroy(Abstractformat *self) {
    xx_rtpatch_destroy((xx_rtpatch *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rtpatch_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rtpatch_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_rtpatch_parse(self, pd);
    if (!stream) return false;
    xx_rtpatch_stream_free(stream);
    return true;
}

bool xx_rtpatch_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rtpatch *archive = (xx_rtpatch *)self;
    xx_rtpatch_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_rtpatch_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_rtpatch_stream_free(stream);
    return true;
}

int64_t xx_rtpatch_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rtpatch_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_rtpatch *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_rtpatch_set_record(xx_archive_record *record,
                                 const xx_rtpatch_member *member) {
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

static bool xx_rtpatch_copy_options(xx_list_s *target,
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

static const xx_var *xx_rtpatch_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_rtpatch_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_rtpatch_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_rtpatch_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_rtpatch_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_rtpatch_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_rtpatch_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_rtpatch_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rtpatch_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rtpatch_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_rtpatch_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rtpatch_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_rtpatch_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rtpatch_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_rtpatch_stream *stream;
    const xx_rtpatch_member *member;
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
    stream = (xx_rtpatch_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_rtpatch_path_safe(member->name)) return false;

    path_option = xx_rtpatch_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_rtpatch_decode(self, member, &plain, &plain_size, pd);
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
        !xx_rtpatch_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_rtpatch_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
