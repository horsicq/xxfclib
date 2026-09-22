/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GPFPACK, the single-file packer shipped with the GPF ("GUI Programming
 * Facility") development system for OS/2 and Windows.  The packed copy keeps
 * the original name with the last character of the extension replaced by
 * '#' (SAMPLE.ICO -> SAMPLE.IC#, GPFDLL21.DLL -> GPFDLL21.DL#).
 *
 * Undocumented, but U3 recognises it and both halves are recovered in
 * F:\utils\U3\src (FORMAT_INDEX.md "archive / 243 GPFPACK", class nsa, VMT
 * 0x0056d318; slot 0 -> FUN_0056d390, slot 1 -> FUN_0056d440).  The layout
 * is FUN_0056d390 transcribed and holds for all 51 samples in
 * F:\ARC\ARC\GPFPACK:
 *
 *   0x00  u32      0x000000C0 - format constant
 *   0x04  char[8]  "GPFPACK" and its NUL
 *   0x0C  u16      1 - version, which U3 requires to be exactly 1
 *   0x0E  char[14] name field: the original name, NUL terminated and NUL
 *                  padded out to a fixed 14 bytes
 *   0x1C  ...      the payload: a chain of packed blocks
 *
 * Each block is introduced by a little-endian u32 giving the block's length
 * IN BITS; the block's bytes are the next (bits + 7) / 8 of the file, and
 * the next block's count follows immediately.  U3 reads that field as a
 * SIGNED int and refuses a value below 1 (FUN_0056d3d0), and its decoder
 * spends the count as a bit budget - which is what proves it is a bit count
 * and not a byte count.  Walking the chain from 0x1C lands exactly on EOF in
 * all 51 samples.
 *
 * THE CODEC IS LZW, read out of U3's per-block decoder FUN_0056cf30.  It is
 * the same TIFF-shaped dialect the WinLink reader uses, one bit narrower and
 * with a different end-of-stream convention:
 *
 *   - codes are MSB-first out of the top of a 32-bit accumulator refilled a
 *     byte at a time;
 *   - widths run 9 to 13 bits with the EARLY width change: before every
 *     code, if (1 << width) - 1 <= next-assignable-code the width grows by
 *     one.  Widening one code later, as GIF does, desynchronises at once;
 *   - 0x100 is CLEAR and the first assignable code is 0x102; the table stops
 *     growing at 0x2000 entries;
 *   - there is NO end code.  0x101 is never emitted and U3 treats it as an
 *     error; a block ends when its bit budget is spent, tested before each
 *     code is read;
 *   - a CLEAR resets the width and the next-assignable code but not the bit
 *     accumulator, and the code after a CLEAR is a bare literal adding no
 *     table entry;
 *   - each block is an independent LZW run that starts byte aligned, and
 *     every block in the corpus consumes exactly (bits + 7) / 8 bytes.
 *
 * The container records no plaintext length, so the size is obtained by
 * running the chain once with no output buffer and then decoding into a
 * buffer of exactly that size.
 *
 * Verified byte for byte against U3's own output for all 51 samples.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gpfpack/xx_gpfpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* The enumerator is added by the coordinator, not by this file. */
#ifdef GPFPACK
#define XX_GPFPACK_FILE_TYPE XX_FILE_TYPE_GPFPACK
#else
#define XX_GPFPACK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_GPFPACK_HEADER_SIZE 28
#define XX_GPFPACK_CONSTANT UINT32_C(0x000000c0)
#define XX_GPFPACK_TAG_OFFSET 4
#define XX_GPFPACK_TAG_SIZE 8U
#define XX_GPFPACK_VERSION_OFFSET 12
#define XX_GPFPACK_VERSION 1U
#define XX_GPFPACK_NAME_OFFSET 14
#define XX_GPFPACK_NAME_FIELD 14U

/* Only guards against a pathological file made of a huge number of empty
 * blocks; a real container has tens of them. */
#define XX_GPFPACK_MAX_BLOCKS UINT64_C(16777216)
/* U3 reads the per-block bit count as a signed int and rejects anything
 * below 1; this is the other end of the same field. */
#define XX_GPFPACK_MAX_BLOCK_BITS UINT32_C(0x7fffffff)

/* LZW constants, all read out of FUN_0056cf30. */
#define XX_GPFPACK_LZW_CLEAR 0x100U
#define XX_GPFPACK_LZW_END 0x101U
#define XX_GPFPACK_LZW_FIRST 0x102U
#define XX_GPFPACK_LZW_TABLE 0x2000U
#define XX_GPFPACK_LZW_MIN_BITS 9U
#define XX_GPFPACK_LZW_MAX_BITS 13U

/* Bound the payload before anything is read or allocated for it. */
#define XX_GPFPACK_MAX_PACKED ((int64_t)128 * 1024 * 1024)
#define XX_GPFPACK_MAX_OUTPUT ((size_t)256U * 1024U * 1024U)

typedef struct xx_gpfpack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    uint64_t unpacked_size; /* 0 until the measuring pass has run */
} xx_gpfpack_member;

typedef struct xx_gpfpack_stream_s {
    xx_gpfpack_member member;
    size_t count;
    int64_t archive_size;
    uint64_t blocks;
    uint16_t version;
} xx_gpfpack_stream;

/* ------------------------------------------------------------ helpers --- */

static uint16_t xx_gpfpack_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t xx_gpfpack_le32(const uint8_t *bytes) {
    return (uint32_t)xx_gpfpack_le16(bytes) |
           ((uint32_t)xx_gpfpack_le16(bytes + 2U) << 16U);
}

static bool xx_gpfpack_read_at(Abstractformat *self, int64_t offset,
                               void *buffer, size_t size) {
    size_t done = 0U;

    if (!self || !self->device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(self->device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_gpfpack_name_field_sane(const uint8_t *field, size_t *length) {
    size_t index = 0U;

    while (index < XX_GPFPACK_NAME_FIELD && field[index] != 0U) {
        uint8_t c = field[index];
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':')
            return false;
        ++index;
    }
    if (index == 0U || index == XX_GPFPACK_NAME_FIELD) return false;
    *length = index;
    for (; index < XX_GPFPACK_NAME_FIELD; ++index)
        if (field[index] != 0U) return false;
    return true;
}

static char *xx_gpfpack_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input;
    size_t output = 0U;

    if (!bytes || size == 0U || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '"' ||
            c == '*' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output > 0U && (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

/* Walk the block chain from @p start.  It must consume the container's
 * payload exactly; a chain that overruns the file or stops short of it is
 * not a GPFPACK payload.  Every length is bounded against the real remaining
 * extent before it is used to advance. */
static bool xx_gpfpack_walk_blocks(Abstractformat *self, int64_t start,
                                   int64_t span, uint64_t *blocks,
                                   xx_pd_struct *pd) {
    int64_t cursor = start;
    uint64_t count = 0U;

    while (cursor < span) {
        uint8_t field[4];
        uint32_t bits;
        int64_t bytes;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (span - cursor < 4) return false;
        if (!xx_gpfpack_read_at(self, self->base_address + cursor, field,
                                sizeof(field)))
            return false;
        bits = xx_gpfpack_le32(field);
        if (bits == 0U || bits > XX_GPFPACK_MAX_BLOCK_BITS) return false;
        bytes = (int64_t)((bits + 7U) / 8U);
        cursor += 4;
        if (bytes > span - cursor) return false;
        cursor += bytes;
        if (++count > XX_GPFPACK_MAX_BLOCKS) return false;
    }
    if (cursor != span || count == 0U) return false;
    *blocks = count;
    return true;
}

static void xx_gpfpack_stream_free(void *pointer) {
    xx_gpfpack_stream *stream = (xx_gpfpack_stream *)pointer;

    if (!stream) return;
    xx_str_free(stream->member.name);
    xx_mem_free(stream);
}

static xx_gpfpack_stream *xx_gpfpack_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    uint8_t header[XX_GPFPACK_HEADER_SIZE];
    xx_gpfpack_stream *stream;
    int64_t total;
    int64_t span;
    uint64_t blocks = 0U;
    size_t name_length = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus at least one block header and one payload byte. */
    if (span < (int64_t)XX_GPFPACK_HEADER_SIZE + 5) return NULL;
    if (!xx_gpfpack_read_at(self, self->base_address, header, sizeof(header)))
        return NULL;
    if (xx_gpfpack_le32(header) != XX_GPFPACK_CONSTANT) return NULL;
    if (xx_rt_memcmp(header + XX_GPFPACK_TAG_OFFSET, "GPFPACK",
                     XX_GPFPACK_TAG_SIZE) != 0)
        return NULL;
    /* U3 requires the version word to be exactly 1, and every sample is. */
    if (xx_gpfpack_le16(header + XX_GPFPACK_VERSION_OFFSET) !=
        XX_GPFPACK_VERSION)
        return NULL;
    if (!xx_gpfpack_name_field_sane(header + XX_GPFPACK_NAME_OFFSET,
                                    &name_length))
        return NULL;
    if (!xx_gpfpack_walk_blocks(self, (int64_t)XX_GPFPACK_HEADER_SIZE, span,
                                &blocks, pd))
        return NULL;

    stream = (xx_gpfpack_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->member.name =
        xx_gpfpack_normalize_name(header + XX_GPFPACK_NAME_OFFSET,
                                  name_length);
    if (!stream->member.name) {
        xx_mem_free(stream);
        return NULL;
    }
    stream->version = xx_gpfpack_le16(header + XX_GPFPACK_VERSION_OFFSET);
    stream->blocks = blocks;
    stream->member.header_offset = self->base_address;
    stream->member.header_size = XX_GPFPACK_HEADER_SIZE;
    stream->member.data_offset = self->base_address + XX_GPFPACK_HEADER_SIZE;
    stream->member.compressed_size = span - (int64_t)XX_GPFPACK_HEADER_SIZE;
    stream->member.unpacked_size = 0U;
    stream->count = 1U;
    stream->archive_size = span;
    return stream;
}

/* ---------------------------------------------------------- LZW codec --- */

/* MSB-first bit source: bytes drop into the top of a 32-bit accumulator and
 * codes come off the top.  It is reset for every block, since every block
 * starts byte aligned. */
typedef struct xx_gpfpack_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    unsigned available;
} xx_gpfpack_bits;

static bool xx_gpfpack_bits_get(xx_gpfpack_bits *bits, unsigned width,
                                unsigned *code) {
    while (bits->available < width) {
        if (bits->position >= bits->size) return false;
        bits->accumulator +=
            (uint32_t)bits->data[bits->position++] << (24U - bits->available);
        bits->available += 8U;
    }
    *code = (unsigned)(bits->accumulator >> (32U - width));
    bits->accumulator <<= width;
    bits->available -= width;
    return true;
}

/* The early width change, tested before every code and against the CURRENT
 * mask, so the widest code of a width is never used. */
static bool xx_gpfpack_next_code(xx_gpfpack_bits *bits, unsigned *width,
                                 unsigned next, unsigned *code) {
    if (((1U << *width) - 1U) <= next && *width < XX_GPFPACK_LZW_MAX_BITS)
        ++(*width);
    return xx_gpfpack_bits_get(bits, *width, code);
}

/* Output sink.  With a NULL buffer it only counts, which is how the member
 * is measured before anything is allocated for it. */
typedef struct xx_gpfpack_sink_s {
    uint8_t *data;
    size_t limit;
    size_t count;
} xx_gpfpack_sink;

static bool xx_gpfpack_sink_put(xx_gpfpack_sink *sink, uint8_t value) {
    if (sink->count >= sink->limit) return false;
    if (sink->data) sink->data[sink->count] = value;
    ++sink->count;
    return true;
}

typedef struct xx_gpfpack_lzw_s {
    uint16_t *prefix;
    uint8_t *suffix;
    uint8_t *stack;
} xx_gpfpack_lzw;

static void xx_gpfpack_lzw_cleanup(xx_gpfpack_lzw *lzw) {
    if (!lzw) return;
    xx_mem_free(lzw->prefix);
    xx_mem_free(lzw->suffix);
    xx_mem_free(lzw->stack);
    lzw->prefix = NULL;
    lzw->suffix = NULL;
    lzw->stack = NULL;
}

/* 32 KiB of tables is too much for the stack, so they are heap allocated
 * once for the whole chain and re-seeded per block. */
static bool xx_gpfpack_lzw_setup(xx_gpfpack_lzw *lzw) {
    lzw->prefix = (uint16_t *)xx_mem_alloc(XX_GPFPACK_LZW_TABLE *
                                           sizeof(*lzw->prefix));
    lzw->suffix = (uint8_t *)xx_mem_alloc(XX_GPFPACK_LZW_TABLE);
    lzw->stack = (uint8_t *)xx_mem_alloc(XX_GPFPACK_LZW_TABLE);
    if (!lzw->prefix || !lzw->suffix || !lzw->stack) {
        xx_gpfpack_lzw_cleanup(lzw);
        return false;
    }
    return true;
}

static void xx_gpfpack_lzw_seed(xx_gpfpack_lzw *lzw) {
    unsigned index;

    for (index = 0U; index < 0x100U; ++index) {
        lzw->prefix[index] = 0U;
        lzw->suffix[index] = (uint8_t)index;
    }
}

/* One block: an independent LZW run whose only terminator is its bit budget.
 * @p bit_count is the block's declared bit length and @p input holds exactly
 * the (bit_count + 7) / 8 bytes the chain walk allotted it, so the decoder
 * can never read into the next block's header. */
static bool xx_gpfpack_lzw_block(xx_gpfpack_lzw *lzw, const uint8_t *input,
                                 size_t input_size, uint32_t bit_count,
                                 xx_gpfpack_sink *sink) {
    xx_gpfpack_bits bits;
    unsigned width = XX_GPFPACK_LZW_MIN_BITS;
    unsigned next = XX_GPFPACK_LZW_FIRST;
    unsigned previous = 0U;
    unsigned first = 0U;
    int64_t budget = (int64_t)bit_count;
    bool segment_start = true;

    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.accumulator = 0U;
    bits.available = 0U;
    xx_gpfpack_lzw_seed(lzw);

    for (;;) {
        unsigned code;
        unsigned current;
        size_t depth = 0U;

        /* The budget is the block's only end marker, and it is spent before
         * a code is read, never after. */
        if (budget < 1) return true;
        if (!xx_gpfpack_next_code(&bits, &width, next, &code)) return false;
        budget -= (int64_t)width;
        if (code == XX_GPFPACK_LZW_CLEAR) {
            width = XX_GPFPACK_LZW_MIN_BITS;
            next = XX_GPFPACK_LZW_FIRST;
            segment_start = true;
            continue;
        }
        /* This dialect has no end code; 0x101 is never assigned and never
         * emitted, so seeing one means the stream is not ours. */
        if (code == XX_GPFPACK_LZW_END) return false;
        if (segment_start) {
            if (code > 0xffU) return false;
            if (!xx_gpfpack_sink_put(sink, (uint8_t)code)) return false;
            first = code;
            previous = code;
            segment_start = false;
            continue;
        }
        /* A code past the next assignable one cannot be resolved; only
         * exactly-next is the legal self-referential case. */
        if (code > next || code >= XX_GPFPACK_LZW_TABLE) return false;
        current = code;
        if (current == next) {
            lzw->stack[depth++] = (uint8_t)first;
            current = previous;
        }
        while (current > 0xffU) {
            if (current >= XX_GPFPACK_LZW_TABLE ||
                depth >= XX_GPFPACK_LZW_TABLE)
                return false;
            lzw->stack[depth++] = lzw->suffix[current];
            current = lzw->prefix[current];
        }
        if (depth >= XX_GPFPACK_LZW_TABLE) return false;
        first = current;
        lzw->stack[depth++] = (uint8_t)current;
        while (depth != 0U)
            if (!xx_gpfpack_sink_put(sink, lzw->stack[--depth])) return false;
        if (next < XX_GPFPACK_LZW_TABLE) {
            lzw->prefix[next] = (uint16_t)previous;
            lzw->suffix[next] = (uint8_t)current;
            ++next;
        }
        previous = code;
    }
}

/* Walk the block chain in memory and decode every block into @p sink.  The
 * chain must land exactly on the end of the payload. */
static bool xx_gpfpack_lzw_run(const uint8_t *payload, size_t payload_size,
                               uint8_t *output, size_t limit,
                               size_t *produced) {
    xx_gpfpack_lzw lzw;
    xx_gpfpack_sink sink;
    size_t cursor = 0U;
    uint64_t blocks = 0U;
    bool result = true;

    if (produced) *produced = 0U;
    if (!payload || payload_size < 5U) return false;
    xx_mem_zero(&lzw, sizeof(lzw));
    if (!xx_gpfpack_lzw_setup(&lzw)) return false;
    sink.data = output;
    sink.limit = limit;
    sink.count = 0U;

    while (cursor < payload_size) {
        uint32_t bit_count;
        size_t block_bytes;

        if (payload_size - cursor < 4U) {
            result = false;
            break;
        }
        bit_count = xx_gpfpack_le32(payload + cursor);
        if (bit_count == 0U || bit_count > XX_GPFPACK_MAX_BLOCK_BITS) {
            result = false;
            break;
        }
        cursor += 4U;
        block_bytes = (size_t)((bit_count + 7U) / 8U);
        if (block_bytes > payload_size - cursor ||
            ++blocks > XX_GPFPACK_MAX_BLOCKS) {
            result = false;
            break;
        }
        if (!xx_gpfpack_lzw_block(&lzw, payload + cursor, block_bytes,
                                  bit_count, &sink)) {
            result = false;
            break;
        }
        cursor += block_bytes;
    }
    if (cursor != payload_size || blocks == 0U) result = false;
    xx_gpfpack_lzw_cleanup(&lzw);
    if (result && produced) *produced = sink.count;
    return result;
}

static uint8_t *xx_gpfpack_read_packed(Abstractformat *self,
                                       const xx_gpfpack_member *member,
                                       size_t *size) {
    uint8_t *packed;

    if (!self || !member || !size) return NULL;
    if (member->compressed_size < 5 ||
        member->compressed_size > XX_GPFPACK_MAX_PACKED)
        return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return NULL;
    if (!xx_gpfpack_read_at(self, member->data_offset, packed,
                            (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    *size = (size_t)member->compressed_size;
    return packed;
}

/* The container stores no plaintext length, so it is measured by running the
 * chain once with no output buffer. */
static bool xx_gpfpack_measure(Abstractformat *self,
                               xx_gpfpack_member *member) {
    uint8_t *packed = NULL;
    size_t packed_size = 0U;
    size_t produced = 0U;
    bool result;

    if (!self || !member) return false;
    if (member->unpacked_size != 0U) return true;
    packed = xx_gpfpack_read_packed(self, member, &packed_size);
    if (!packed) return false;
    result = xx_gpfpack_lzw_run(packed, packed_size, NULL,
                                XX_GPFPACK_MAX_OUTPUT, &produced) &&
             produced != 0U;
    xx_mem_free(packed);
    if (!result) return false;
    member->unpacked_size = (uint64_t)produced;
    return true;
}

static bool xx_gpfpack_decode(Abstractformat *self,
                              const xx_gpfpack_member *member, uint8_t **plain,
                              size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t packed_size = 0U;
    size_t produced = 0U;
    size_t output_size;

    if (!self || !member || !plain || !plain_size) return false;
    if (member->unpacked_size == 0U || member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = xx_gpfpack_read_packed(self, member, &packed_size);
    if (!packed) return false;
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!output ||
        !xx_gpfpack_lzw_run(packed, packed_size, output, output_size,
                            &produced) ||
        produced != output_size) {
        xx_mem_free(packed);
        if (output) xx_mem_free(output);
        return false;
    }
    xx_mem_free(packed);
    *plain = output;
    *plain_size = produced;
    return true;
}

/* --------------------------------------------------------- lifecycle --- */

static void xx_gpfpack_vtable_destroy(Abstractformat *self);

void xx_gpfpack_init(xx_gpfpack *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GPFPACK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gpfpack");
    xx_format_set_extension(&archive->format, "gpf");
    archive->format.check_is_valid = xx_gpfpack_check_is_valid;
    archive->format.handle_base_info = xx_gpfpack_handle_base_info;
    archive->format.get_format_size = xx_gpfpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gpfpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gpfpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gpfpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gpfpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gpfpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gpfpack_free_archive_records_reading;
    archive->format.destroy = xx_gpfpack_vtable_destroy;
}

xx_gpfpack *xx_gpfpack_create(xx_io_device *device, int64_t base_address) {
    xx_gpfpack *archive = (xx_gpfpack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_gpfpack_init(archive, device, base_address);
    return archive;
}

void xx_gpfpack_destroy(xx_gpfpack *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_gpfpack_free(xx_gpfpack *archive) {
    if (!archive) return;
    xx_gpfpack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_gpfpack_vtable_destroy(Abstractformat *self) {
    xx_gpfpack_destroy((xx_gpfpack *)self);
}

/* ------------------------------------------------------------ format --- */

bool xx_gpfpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gpfpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_gpfpack_parse(self, pd);
    if (!stream) return false;
    xx_gpfpack_stream_free(stream);
    return true;
}

bool xx_gpfpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gpfpack *archive = (xx_gpfpack *)self;
    xx_gpfpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_gpfpack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->number_of_blocks = stream->blocks;
    archive->version = stream->version;
    xx_gpfpack_stream_free(stream);
    return true;
}

int64_t xx_gpfpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_gpfpack_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_gpfpack *)self)->number_of_records : 0U;
}

/* ----------------------------------------------------------- records --- */

static bool xx_gpfpack_set_record(xx_archive_record *record,
                                  const xx_gpfpack_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           /* Measured by running the chain; 0 when it would not decode. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_gpfpack_copy_options(xx_list_s *target,
                                    const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_gpfpack_option(const xx_list_s *options,
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

xx_archive_record_state *xx_gpfpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_gpfpack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_gpfpack_parse(self, pd);
    if (!stream) return NULL;
    /* No plaintext length is stored, so it is measured here.  A stream that
     * will not measure is still listed - with size 0 for "unknown" - and
     * unpacking it later fails closed. */
    (void)xx_gpfpack_measure(self, &stream->member);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_gpfpack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_gpfpack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_gpfpack_copy_options(&state->options, options) ||
        !xx_gpfpack_set_record(&state->current_record, &stream->member)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_gpfpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gpfpack_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    (void)pd;
    if (!self || !state || state->format != self) return false;
    /* A GPFPACK container holds exactly one member. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_gpfpack_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_gpfpack_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_gpfpack_stream *)state->internal_state;
    if (!stream) return false;
    if (!xx_gpfpack_measure(self, &stream->member)) return false;
    if (!xx_gpfpack_decode(self, &stream->member, &plain, &plain_size))
        return false;
    /* With no unpack path the caller only wanted to know the member decodes;
     * it does, so this is a success with nothing written. */
    path_option =
        xx_gpfpack_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->member.name)
               : xx_str_concat(base, stream->member.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount =
                xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    xx_mem_free(plain);
    xx_str_free(path);
    xx_str_free(owned_base);
    return result;
}

void xx_gpfpack_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
