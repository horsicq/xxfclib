/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the ".CPX" distribution container.  XArchive has no module
 * for it, so the layout below was derived from the corpora under
 * F:\ARC\ARC\CPX and F:\ARC\ARC\CPX4 with U3 as the plaintext oracle.
 *
 * Four bytes of stamp open every file: one version byte then "\x16\x27\x93".
 * The version byte selects one of two container shapes.
 *
 * v1 (version byte 0x28 or 0x2a) - a flat directory:
 *
 *   +0   u8   version stamp
 *   +1   3    16 27 93
 *   +4   u16  member count
 *   +6        count * 26-byte records
 *
 *   record:
 *     +0   13   name, NUL terminated and NUL padded (8.3, no path)
 *     +13  u8   group byte - a path/volume id, not a method
 *     +14  u32  MS-DOS date/time (date in the high half)
 *     +18  u32  plaintext size; bit 31 set means the member is STORED, and
 *               then the low bits are unreliable - the extent is the size
 *     +22  u32  absolute file offset of the payload
 *
 *   Records are not necessarily in offset order (CBUS2.CPX is name-ordered),
 *   so a member's packed length is the distance to the next offset in SORTED
 *   order, or to the end of file for the last one.  A small hole between the
 *   directory and the first payload is tolerated; that file has one.
 *
 * v4 (version byte 0x2c) - sectioned, with the tables at the end:
 *
 *   +0   u8   0x2c
 *   +1   3    16 27 93
 *   +4   u16  header size (36 across the corpus; also the first data offset)
 *   +6   u16  version (2)
 *   +8   u32  record table size, u32 record table offset
 *   +16  u32  name table size, u32 name table offset
 *   +24  u32  extra section size, u32 extra section offset
 *   +32  u32  stamp (0x00010102)
 *
 *   record (28 bytes):
 *     +0   u32  offset of the name inside the name table (NUL terminated)
 *     +4   u32  attribute word (11 or 12 across the corpus - not a method)
 *     +8   u32  plaintext size
 *     +12  u32  timestamp
 *     +16  u32  flags
 *     +20  u32  packed size
 *     +24  u32  absolute file offset of the payload
 *
 *   A member is stored when its packed size equals its plaintext size.
 *
 * Payload codec, shared by both shapes: MSB-first LZW, code width 9 growing to
 * 14, CLEAR = 0x100, END = 0x101, first assignable code 0x102, and the "early
 * change" width rule (widen when next_free + 1 reaches 1 << width).  The table
 * never fills in practice because the encoder emits CLEAR first; a full table
 * simply stops growing here rather than guessing at a reset rule.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cpx/xx_cpx.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef CPX
#define XX_CPX_FILE_TYPE XX_FILE_TYPE_CPX
#else
#define XX_CPX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#ifdef CPX4
#define XX_CPX4_FILE_TYPE XX_FILE_TYPE_CPX4
#else
#define XX_CPX4_FILE_TYPE XX_CPX_FILE_TYPE
#endif

#define CPX_V1_HEADER_SIZE 6
#define CPX_V1_RECORD_SIZE 26
#define CPX_V1_NAME_SIZE 13U
#define CPX_V4_HEADER_SIZE 36
#define CPX_V4_RECORD_SIZE 28
#define CPX_MAX_MEMBERS 65535U
#define CPX_MAX_NAME 4096U
/* A declared plaintext size is bounded twice: absolutely, and against the
 * packed extent it claims to come from.  LZW cannot beat 1:1024 on any real
 * input, so a small member can never ask for a large allocation. */
#define CPX_MAX_UNPACKED UINT64_C(0x10000000)
#define CPX_MAX_RATIO 1024U

#define CPX_LZW_MAX_BITS 14
#define CPX_LZW_TABLE (1U << CPX_LZW_MAX_BITS)
#define CPX_LZW_CLEAR 256U
#define CPX_LZW_END 257U
#define CPX_LZW_FIRST 258U

typedef struct cpx_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t attributes;
    uint32_t timestamp;
    uint32_t flags;
    bool stored;
} cpx_member;

typedef struct cpx_stream_s {
    cpx_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint8_t version;
} cpx_stream;

static uint16_t cpx_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t cpx_le32(const uint8_t *bytes) {
    return (uint32_t)cpx_le16(bytes) | ((uint32_t)cpx_le16(bytes + 2U) << 16U);
}

static bool cpx_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* MSB-first LZW, 9..14 bits                                                  */
/* ------------------------------------------------------------------------- */

typedef struct cpx_lzw_s {
    uint16_t prefix[CPX_LZW_TABLE];
    uint8_t suffix[CPX_LZW_TABLE];
    uint8_t stack[CPX_LZW_TABLE];
} cpx_lzw;

static bool cpx_lzw_decode(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written) {
    cpx_lzw *tables;
    size_t produced = 0U;
    size_t bit = 0U;
    size_t total_bits;
    unsigned width = 9U;
    unsigned next = CPX_LZW_FIRST;
    int32_t previous = -1;
    uint8_t previous_first = 0U;
    bool ok = false;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    if (input_size > SIZE_MAX / 8U) return false;
    total_bits = input_size * 8U;

    tables = (cpx_lzw *)xx_mem_alloc(sizeof(*tables));
    if (!tables) return false;

    while (produced < output_size) {
        uint32_t code = 0U;
        uint32_t walk;
        size_t depth = 0U;
        unsigned index;
        uint8_t first;

        if (total_bits - bit < width) goto done;
        for (index = 0U; index < width; ++index) {
            size_t at = bit + index;
            code = (code << 1U) |
                   (uint32_t)((input[at >> 3U] >> (7U - (at & 7U))) & 1U);
        }
        bit += width;

        if (code == CPX_LZW_CLEAR) {
            width = 9U;
            next = CPX_LZW_FIRST;
            previous = -1;
            continue;
        }
        if (code == CPX_LZW_END) goto done;

        walk = code;
        if (code >= (uint32_t)next) {
            /* The KwKwK case: only ever the very next code, and only when a
             * previous phrase exists. */
            if ((previous < 0) || (code != (uint32_t)next)) goto done;
            if (depth >= sizeof(tables->stack)) goto done;
            tables->stack[depth++] = previous_first;
            walk = (uint32_t)previous;
        }
        while (walk >= CPX_LZW_CLEAR) {
            if ((walk >= CPX_LZW_TABLE) || (walk < CPX_LZW_FIRST) ||
                (walk >= (uint32_t)next) || (depth >= sizeof(tables->stack)))
                goto done;
            tables->stack[depth++] = tables->suffix[walk];
            walk = tables->prefix[walk];
        }
        if (depth >= sizeof(tables->stack)) goto done;
        first = (uint8_t)walk;
        tables->stack[depth++] = first;

        if (depth > output_size - produced) goto done;
        while (depth > 0U) output[produced++] = tables->stack[--depth];

        if (previous >= 0) {
            if (next < CPX_LZW_TABLE) {
                tables->prefix[next] = (uint16_t)previous;
                tables->suffix[next] = first;
                ++next;
            }
        }
        previous = (int32_t)code;
        previous_first = first;
        if ((next + 1U >= (1U << width)) && (width < CPX_LZW_MAX_BITS))
            ++width;
    }
    ok = (produced == output_size);

done:
    xx_mem_free(tables);
    if (ok && written) *written = produced;
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Names                                                                      */
/* ------------------------------------------------------------------------- */

/* DOS 8.3 names with no path component.  Characters a host filesystem would
 * object to are neutralized; an empty result is rejected by the caller. */
static char *cpx_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input, output = 0U;
    if (size > CPX_MAX_NAME) return NULL;
    name = (char *)xx_mem_alloc(size + 1U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c == 0U) break;
        if (c < 0x20U || c == '/' || c == '\\' || c == '"' || c == '*' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output != 0U &&
           (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    name[output] = 0;
    return name;
}

static bool cpx_safe_output_name(const char *name) {
    size_t length;
    if (!name || !name[0]) return false;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    for (length = 0U; name[length]; ++length) {
        unsigned char c = (unsigned char)name[length];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    return true;
}

static void cpx_stream_free(void *opaque) {
    cpx_stream *stream = (cpx_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static cpx_stream *cpx_stream_new(size_t count) {
    cpx_stream *stream;
    if (count == 0U || count > CPX_MAX_MEMBERS) return NULL;
    stream = (cpx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->items = (cpx_member *)xx_mem_calloc(count, sizeof(cpx_member));
    if (!stream->items) {
        xx_mem_free(stream);
        return NULL;
    }
    return stream;
}

static bool cpx_check_size(uint64_t unpacked, int64_t packed) {
    if (unpacked > CPX_MAX_UNPACKED) return false;
    if (packed < 0) return false;
    /* A tiny extent cannot legitimately declare a huge plaintext. */
    if (unpacked > (uint64_t)packed * CPX_MAX_RATIO + 0x10000U) return false;
    return true;
}

/* Heapsort: a member's extent is the distance to the next payload offset in
 * sorted order, and the record order is not that order. */
static void cpx_sift(int64_t *values, size_t start, size_t count) {
    size_t root = start;
    for (;;) {
        size_t child = root * 2U + 1U;
        size_t swap = root;
        if (child >= count) break;
        if (values[swap] < values[child]) swap = child;
        if (child + 1U < count && values[swap] < values[child + 1U])
            swap = child + 1U;
        if (swap == root) break;
        {
            int64_t temporary = values[root];
            values[root] = values[swap];
            values[swap] = temporary;
        }
        root = swap;
    }
}

static void cpx_sort_offsets(int64_t *values, size_t count) {
    size_t index;
    if (count < 2U) return;
    for (index = count / 2U; index-- > 0U;) cpx_sift(values, index, count);
    for (index = count; index-- > 1U;) {
        int64_t temporary = values[0];
        values[0] = values[index];
        values[index] = temporary;
        cpx_sift(values, 0U, index);
    }
}

/* ------------------------------------------------------------------------- */
/* v1: flat directory                                                         */
/* ------------------------------------------------------------------------- */

static bool cpx_parse_v1(Abstractformat *format, int64_t size,
                         const uint8_t *header, cpx_stream **result) {
    cpx_stream *stream;
    uint8_t *directory = NULL;
    size_t count = (size_t)cpx_le16(header + 4U);
    int64_t directory_size, directory_end;
    size_t index;

    if (count == 0U) return false;
    directory_size = (int64_t)count * CPX_V1_RECORD_SIZE;
    if (size - CPX_V1_HEADER_SIZE < directory_size) return false;
    directory_end = CPX_V1_HEADER_SIZE + directory_size;

    stream = cpx_stream_new(count);
    if (!stream) return false;
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory ||
        !cpx_read_at(format->device, format->base_address + CPX_V1_HEADER_SIZE,
                     directory, (size_t)directory_size))
        goto fail;

    for (index = 0U; index < count; ++index) {
        const uint8_t *record = directory + index * CPX_V1_RECORD_SIZE;
        cpx_member *member = &stream->items[index];
        uint32_t declared;
        int64_t offset;
        size_t scan;
        bool terminated = false;

        for (scan = 0U; scan < CPX_V1_NAME_SIZE; ++scan) {
            if (record[scan] == 0U) {
                terminated = true;
                break;
            }
        }
        if (!terminated || scan == 0U) goto fail;
        for (; scan < CPX_V1_NAME_SIZE; ++scan)
            if (record[scan] != 0U) goto fail;

        offset = (int64_t)cpx_le32(record + 22U);
        if (offset < directory_end || offset >= size) goto fail;
        declared = cpx_le32(record + 18U);

        member->name = cpx_normalize_name(record, CPX_V1_NAME_SIZE);
        if (!member->name || !member->name[0]) goto fail;
        member->header_offset = format->base_address + CPX_V1_HEADER_SIZE +
                                (int64_t)index * CPX_V1_RECORD_SIZE;
        member->header_size = CPX_V1_RECORD_SIZE;
        member->data_offset = format->base_address + offset;
        member->attributes = record[13];
        member->timestamp = cpx_le32(record + 14U);
        member->flags = declared;
        member->stored = (declared & UINT32_C(0x80000000)) != 0U;
        member->unpacked_size = declared & UINT32_C(0x7fffffff);
        member->packed_size = 0;
        ++stream->count;
    }

    /* Records need not be in offset order, so a member's extent runs to the
     * next offset in sorted order - or to the end of file for the last. */
    {
        int64_t *sorted = (int64_t *)xx_mem_alloc(stream->count *
                                                  sizeof(int64_t));
        if (!sorted) goto fail;
        for (index = 0U; index < stream->count; ++index)
            sorted[index] = stream->items[index].data_offset;
        cpx_sort_offsets(sorted, stream->count);
        for (index = 0U; index < stream->count; ++index) {
            int64_t here = stream->items[index].data_offset;
            int64_t end = format->base_address + size;
            size_t lo = 0U, hi = stream->count;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2U;
                if (sorted[mid] > here) hi = mid;
                else lo = mid + 1U;
            }
            if (lo < stream->count) end = sorted[lo];
            stream->items[index].packed_size = end - here;
        }
        xx_mem_free(sorted);
    }
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].packed_size <= 0) goto fail;
        if (stream->items[index].stored) {
            /* The low bits of a stored member's size field do not agree with
             * its extent across the corpus, so the extent is authoritative. */
            stream->items[index].unpacked_size =
                (uint64_t)stream->items[index].packed_size;
        } else if (!cpx_check_size(stream->items[index].unpacked_size,
                                   stream->items[index].packed_size)) {
            goto fail;
        }
    }

    xx_mem_free(directory);
    stream->archive_size = size;
    stream->version = header[0];
    *result = stream;
    return true;
fail:
    if (directory) xx_mem_free(directory);
    cpx_stream_free(stream);
    return false;
}

/* ------------------------------------------------------------------------- */
/* v4: sectioned                                                              */
/* ------------------------------------------------------------------------- */

static bool cpx_parse_v4(Abstractformat *format, int64_t size,
                         const uint8_t *header, cpx_stream **result) {
    cpx_stream *stream = NULL;
    uint8_t *records = NULL;
    uint8_t *names = NULL;
    int64_t header_size = (int64_t)cpx_le16(header + 4U);
    int64_t record_size = (int64_t)cpx_le32(header + 8U);
    int64_t record_offset = (int64_t)cpx_le32(header + 12U);
    int64_t name_size = (int64_t)cpx_le32(header + 16U);
    int64_t name_offset = (int64_t)cpx_le32(header + 20U);
    int64_t extra_size = (int64_t)cpx_le32(header + 24U);
    int64_t extra_offset = (int64_t)cpx_le32(header + 28U);
    int64_t end;
    size_t count, index;

    if (header_size < CPX_V4_HEADER_SIZE || header_size > size) return false;
    if (record_size <= 0 || (record_size % CPX_V4_RECORD_SIZE) != 0)
        return false;
    if (record_offset < header_size || record_offset > size ||
        size - record_offset < record_size)
        return false;
    if (name_size <= 0 || name_offset < header_size || name_offset > size ||
        size - name_offset < name_size)
        return false;
    if (extra_size < 0 || extra_offset < 0 || extra_offset > size ||
        size - extra_offset < extra_size)
        return false;

    count = (size_t)(record_size / CPX_V4_RECORD_SIZE);
    stream = cpx_stream_new(count);
    if (!stream) return false;
    records = (uint8_t *)xx_mem_alloc((size_t)record_size);
    names = (uint8_t *)xx_mem_alloc((size_t)name_size);
    if (!records || !names ||
        !cpx_read_at(format->device, format->base_address + record_offset,
                     records, (size_t)record_size) ||
        !cpx_read_at(format->device, format->base_address + name_offset, names,
                     (size_t)name_size))
        goto fail;

    for (index = 0U; index < count; ++index) {
        const uint8_t *record = records + index * CPX_V4_RECORD_SIZE;
        cpx_member *member = &stream->items[index];
        int64_t name_at = (int64_t)cpx_le32(record);
        int64_t packed = (int64_t)cpx_le32(record + 20U);
        int64_t offset = (int64_t)cpx_le32(record + 24U);
        uint64_t unpacked = cpx_le32(record + 8U);
        int64_t limit;

        if (name_at < 0 || name_at >= name_size) goto fail;
        limit = name_size - name_at;
        if (limit > (int64_t)CPX_MAX_NAME) limit = (int64_t)CPX_MAX_NAME;
        member->name = cpx_normalize_name(names + name_at, (size_t)limit);
        if (!member->name || !member->name[0]) goto fail;

        if (packed < 0 || offset < header_size || offset > size ||
            size - offset < packed)
            goto fail;
        member->header_offset =
            format->base_address + record_offset +
            (int64_t)index * CPX_V4_RECORD_SIZE;
        member->header_size = CPX_V4_RECORD_SIZE;
        member->data_offset = format->base_address + offset;
        member->packed_size = packed;
        member->attributes = cpx_le32(record + 4U);
        member->timestamp = cpx_le32(record + 12U);
        member->flags = cpx_le32(record + 16U);
        member->stored = (unpacked == (uint64_t)packed);
        member->unpacked_size = unpacked;
        if (!member->stored && !cpx_check_size(unpacked, packed)) goto fail;
        ++stream->count;
    }

    end = record_offset + record_size;
    if (name_offset + name_size > end) end = name_offset + name_size;
    if (extra_offset + extra_size > end) end = extra_offset + extra_size;

    xx_mem_free(records);
    xx_mem_free(names);
    stream->archive_size = end;
    stream->version = header[0];
    *result = stream;
    return true;
fail:
    if (records) xx_mem_free(records);
    if (names) xx_mem_free(names);
    cpx_stream_free(stream);
    return false;
}

static bool cpx_parse(Abstractformat *format, cpx_stream **result) {
    uint8_t header[CPX_V4_HEADER_SIZE];
    int64_t total, size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < CPX_V1_HEADER_SIZE + CPX_V1_RECORD_SIZE) return false;
    if (!cpx_read_at(format->device, format->base_address, header,
                     (size_t)(size < (int64_t)sizeof(header)
                                  ? size
                                  : (int64_t)sizeof(header))))
        return false;
    if (header[1] != 0x16U || header[2] != 0x27U || header[3] != 0x93U)
        return false;
    if (header[0] == 0x28U || header[0] == 0x2aU)
        return cpx_parse_v1(format, size, header, result);
    if (header[0] == 0x2cU) {
        if (size < CPX_V4_HEADER_SIZE) return false;
        return cpx_parse_v4(format, size, header, result);
    }
    return false;
}

/* ------------------------------------------------------------------------- */

static bool cpx_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *cpx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cpx_set_record(xx_archive_record *record,
                           const cpx_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->stored ? 0U : 1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool cpx_decode_member(Abstractformat *format, const cpx_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size ||
        member->packed_size <= 0 || member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output ||
        !cpx_read_at(format->device, member->data_offset, packed,
                     (size_t)member->packed_size))
        goto fail;
    if (member->stored) {
        if (output_size != (size_t)member->packed_size) goto fail;
        xx_rt_memcpy(output, packed, output_size);
        written = output_size;
        decoded = true;
    } else {
        decoded = cpx_lzw_decode(packed, (size_t)member->packed_size, output,
                                 output_size, &written);
    }
    if (!decoded || written != output_size) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_cpx_init(xx_cpx *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CPX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cpx");
    xx_format_set_extension(&archive->format, "cpx");
    archive->format.check_is_valid = xx_cpx_check_is_valid;
    archive->format.handle_base_info = xx_cpx_handle_base_info;
    archive->format.get_format_size = xx_cpx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cpx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cpx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cpx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cpx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cpx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cpx_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_cpx *xx_cpx_create(xx_io_device *device, int64_t base_address) {
    xx_cpx *archive = (xx_cpx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_cpx_init(archive, device, base_address);
    return archive;
}

void xx_cpx_destroy(xx_cpx *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_cpx_free(xx_cpx *archive) {
    if (!archive) return;
    xx_cpx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_cpx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    cpx_stream *stream;
    (void)pd;
    if (!cpx_parse(format, &stream)) return false;
    cpx_stream_free(stream);
    return true;
}

bool xx_cpx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    cpx_stream *stream;
    xx_cpx *archive;
    (void)pd;
    if (!format || !cpx_parse(format, &stream)) return false;
    archive = (xx_cpx *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->container_version = stream->version;
    format->file_type = (stream->version == 0x2cU) ? XX_CPX4_FILE_TYPE
                                                   : XX_CPX_FILE_TYPE;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    cpx_stream_free(stream);
    return true;
}

int64_t xx_cpx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cpx_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_cpx_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cpx_handle_base_info(format, pd))
               ? ((xx_cpx *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_cpx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    cpx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!cpx_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cpx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = cpx_stream_free;
    state->total_records = stream->count;
    if (!cpx_copy_options(&state->options, options) ||
        !cpx_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_cpx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_cpx_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    cpx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (cpx_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = cpx_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cpx_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    cpx_stream *stream;
    cpx_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (cpx_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!cpx_safe_output_name(member->name) ||
        !cpx_decode_member(format, member, &plain, &plain_size))
        goto done;
    path_option = cpx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_cpx_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
