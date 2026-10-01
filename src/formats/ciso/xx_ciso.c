/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CISO v1/v2 and ZISO, block-compressed ISO containers used by PSP and PS2
 * loaders. v2 can mix raw Deflate and LZ4 blocks; ZISO uses LZ4 blocks.
 *
 * Header (24 bytes, little endian)
 *   +0x00  "CISO"
 *   +0x04  u32  header size (0 in practice)
 *   +0x08  u64  uncompressed total size
 *   +0x10  u32  block size
 *   +0x14  u8   version      +0x15  u8  index align shift
 *   +0x16  u16  reserved
 *
 * Then blocks + 1 little-endian index words.  Word i gives block i's start
 * as (word & 0x7fffffff) << align and word i+1 gives its end, which is why
 * the index always carries one extra entry.  The top bit means the block is
 * stored; otherwise it is RAW Deflate with no zlib wrapper.
 *
 * Ported from XArchive diskimages/xcisoimage.cpp and
 * Algos/xdiskimagedecoder.cpp::decodeCiso; U3 implements the same format as
 * archive/657 (class qga, VMT 0x0049e528).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ciso/xx_ciso.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/formats/iso9660/xx_iso9660.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef CISO
#define XX_CISO_FILE_TYPE XX_FILE_TYPE_CISO
#else
#define XX_CISO_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CISO_MAX_MEMBERS 16U
#define CISO_MAX_NESTED_ISO (256U * 1024U * 1024U)

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct ciso_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} ciso_member;

typedef struct ciso_stream_s {
    ciso_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} ciso_stream;

typedef struct ciso_nested_iso_s {
    uint8_t *image;
    xx_io_device *device;
    xx_iso9660 iso;
} ciso_nested_iso;

static void ciso_nested_iso_free(ciso_nested_iso *nested) {
    if (!nested) return;
    if (nested->device) xx_iso9660_destroy(&nested->iso);
    if (nested->device) xx_io_close(nested->device);
    xx_mem_free(nested->image);
    xx_mem_free(nested);
}

static bool ciso_prefix_iso_record(xx_archive_record_state *state) {
    const char *name;
    char *prefixed;
    bool result;
    if (!state || !state->has_record) return true;
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!name || !name[0]) return false;
    prefixed = xx_str_concat("ISO/", name);
    if (!prefixed) return false;
    result = xx_archive_record_set_original_name(&state->current_record,
                                                  prefixed);
    xx_str_free(prefixed);
    return result;
}

static uint16_t ciso_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t ciso_le32(const uint8_t *b) {
    return (uint32_t)ciso_le16(b) | ((uint32_t)ciso_le16(b + 2U) << 16U);
}

static uint64_t ciso_le64(const uint8_t *b) {
    return (uint64_t)ciso_le32(b) | ((uint64_t)ciso_le32(b + 4U) << 32U);
}

static uint32_t ciso_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t ciso_be64(const uint8_t *b) {
    return ((uint64_t)ciso_be32(b) << 32U) | (uint64_t)ciso_be32(b + 4U);
}

static bool ciso_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool ciso_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool ciso_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buffer = NULL;
    bool buffer_result = false;
    uint64_t left = size;
    if (!source || offset < 0) { buffer_result = (false); goto buffer_done; }
    if (!destination) { buffer_result = (true); goto buffer_done; }
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) { buffer_result = (false); goto buffer_done; }
    if (capacity > (SIZE_MAX >> 1U)) capacity = SIZE_MAX >> 1U;
    if (left) { if(capacity>left) capacity=(size_t)left; buffer = (uint8_t *)xx_mem_alloc(capacity); if (!buffer) { buffer_result = false; goto buffer_done; } }
    while (left != 0U) {
        size_t want = left < capacity ? (size_t)left : capacity;
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) { buffer_result = (false); goto buffer_done; }
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) { buffer_result = (false); goto buffer_done; }
            done += (size_t)amount;
        }
        if (!ciso_write_all(destination, buffer, want, pd)) { buffer_result = (false); goto buffer_done; }
        left -= want;
    }
    { buffer_result = (true); goto buffer_done; }

buffer_done:
    xx_mem_free(buffer);
    return buffer_result;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool ciso_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *buffer = NULL;
    bool buffer_result = false;
    uint64_t left = size;
    if (!destination) { buffer_result = (true); goto buffer_done; }
    if (capacity > (SIZE_MAX >> 1U)) capacity = SIZE_MAX >> 1U;
    if (left) { if(capacity>left) capacity=(size_t)left; buffer = (uint8_t *)xx_mem_alloc(capacity); if (!buffer) { buffer_result = false; goto buffer_done; } }
    if (!left) { buffer_result = true; goto buffer_done; }
    xx_mem_zero(buffer, capacity);
    while (left != 0U) {
        size_t want = left < capacity ? (size_t)left : capacity;
        if (!ciso_write_all(destination, buffer, want, pd)) { buffer_result = (false); goto buffer_done; }
        left -= want;
    }
    { buffer_result = (true); goto buffer_done; }

buffer_done:
    xx_mem_free(buffer);
    return buffer_result;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *ciso_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *ciso_clean_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool ciso_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void ciso_stream_free(void *opaque) {
    ciso_stream *stream = (ciso_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool ciso_add_member(ciso_stream *stream, const ciso_member *member) {
    ciso_member *grown;
    if (!stream || !member || stream->count >= CISO_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (ciso_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define CISO_HEADER_SIZE 24
#define CISO_MAX_BLOCKS 4000000U
#define CISO_MAX_BLOCK_SIZE (4U * 1024U * 1024U)

/* DAX stores 8 KiB zlib frames, with separate 32-bit offsets and 16-bit
 * lengths. Version 1 may mark ranges of frames as uncompressed. */
static bool ciso_parse_dax(Abstractformat *format, ciso_stream **result) {
    uint8_t header[32];
    uint32_t plain_size, version, areas;
    uint64_t frames, table_bytes;
    int64_t total, size;
    ciso_stream *stream;
    ciso_member member;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (total = xx_io_total_size(format->device)) < format->base_address)
        return false;
    size = total - format->base_address;
    if (size < (int64_t)sizeof(header) ||
        !ciso_read_at(format->device, format->base_address, header, sizeof(header)) ||
        xx_rt_memcmp(header, "DAX\0", 4U) != 0) return false;
    plain_size = ciso_le32(header + 4U);
    version = ciso_le32(header + 8U);
    areas = ciso_le32(header + 12U);
    if (!plain_size || (plain_size & 2047U) != 0U || version > 1U ||
        (version == 0U && areas) ||
        (frames = ((uint64_t)plain_size + 8191U) / 8192U) > CISO_MAX_BLOCKS ||
        areas > frames) return false;
    table_bytes = frames * 6U + (uint64_t)areas * 8U;
    if (table_bytes > (uint64_t)size - sizeof(header)) return false;
    stream = (ciso_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = areas;
    stream->aux1 = 8192U;
    stream->aux2 = frames;
    xx_mem_zero(&member, sizeof(member));
    member.name = ciso_make_name("image", -1, ".iso");
    if (!member.name) { ciso_stream_free(stream); return false; }
    member.header_offset = format->base_address;
    member.header_size = sizeof(header);
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = plain_size;
    member.method = 4U;
    member.attributes = 8192U;
    if (!ciso_add_member(stream, &member)) {
        xx_mem_free(member.name);
        ciso_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
}

/* CISO/ZISO: a 24-byte header followed by blocks + 1 little-endian index
 * words. The meaning of the high bit depends on the container version. */
static bool ciso_parse(Abstractformat *format, ciso_stream **result) {
    uint8_t header[CISO_HEADER_SIZE];
    ciso_stream *stream = NULL;
    ciso_member member;
    int64_t total, size;
    uint64_t uncompressed, blocks;
    uint32_t block_size;
    uint8_t align, variant;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size >= 32 &&
        ciso_read_at(format->device, format->base_address, header, 4U) &&
        xx_rt_memcmp(header, "DAX\0", 4U) == 0)
        return ciso_parse_dax(format, result);
    if (size <= CISO_HEADER_SIZE ||
        !ciso_read_at(format->device, format->base_address, header,
                      sizeof(header)) ||
        (xx_rt_memcmp(header, "CISO", 4U) != 0 &&
         xx_rt_memcmp(header, "ZISO", 4U) != 0))
        return false;

    if (xx_rt_memcmp(header, "ZISO", 4U) == 0) {
        if (header[20] != 1U || ciso_le32(header + 4U) != CISO_HEADER_SIZE ||
            header[22] != 0U || header[23] != 0U) return false;
        variant = 3U;
    } else if (header[20] == 2U) {
        if (ciso_le32(header + 4U) != CISO_HEADER_SIZE ||
            header[22] != 0U || header[23] != 0U) return false;
        variant = 2U;
    } else if (header[20] <= 1U) {
        variant = 1U;
    } else return false;

    uncompressed = ciso_le64(header + 8U);
    block_size = ciso_le32(header + 16U);
    align = header[21];
    if (uncompressed == 0U || block_size == 0U ||
        block_size > CISO_MAX_BLOCK_SIZE || align > 31U ||
        (uncompressed % block_size) != 0U)
        return false;
    blocks = uncompressed / block_size;
    if (blocks == 0U || blocks > CISO_MAX_BLOCKS) return false;
    /* The index must be inside the file before anything indexes it. */
    if ((blocks + 1U) > (uint64_t)(size - CISO_HEADER_SIZE) / 4U) return false;

    stream = (ciso_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = align;
    stream->aux1 = block_size;
    stream->aux2 = blocks;

    xx_mem_zero(&member, sizeof(member));
    member.name = ciso_make_name("image", -1, ".iso");
    if (!member.name) goto fail;
    member.header_offset = format->base_address;
    member.header_size = CISO_HEADER_SIZE;
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = uncompressed;
    member.method = variant;
    member.attributes = block_size;
    if (!ciso_add_member(stream, &member)) {
        xx_mem_free(member.name);
        goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    ciso_stream_free(stream);
    return false;
}

static bool ciso_write_dax_member(Abstractformat *format, ciso_stream *stream,
                                  const ciso_member *member,
                                  xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *table = NULL, *packed = NULL, *plain = NULL, *raw = NULL;
    uint64_t frames = stream->aux2, areas = stream->aux0;
    uint64_t table_bytes = frames * 6U + areas * 8U;
    uint64_t first_data = 32U + table_bytes;
    uint64_t prior_end = first_data, frame;
    bool result = false;
    if (table_bytes > SIZE_MAX || frames > SIZE_MAX ||
        member->packed_size < 0 || first_data > (uint64_t)member->packed_size)
        return false;
    table = (uint8_t *)xx_mem_alloc((size_t)table_bytes);
    packed = (uint8_t *)xx_mem_alloc(65536U);
    plain = (uint8_t *)xx_mem_alloc(8192U);
    raw = (uint8_t *)xx_mem_calloc((size_t)frames, 1U);
    if (!table || !packed || !plain || !raw ||
        !ciso_read_at(format->device, member->header_offset + 32,
                      table, (size_t)table_bytes)) goto done;
    for (frame = 0U; frame < areas; ++frame) {
        const uint8_t *area = table + frames * 6U + frame * 8U;
        uint32_t start = ciso_le32(area), count = ciso_le32(area + 4U);
        uint64_t j;
        if (!count || start >= frames || count > frames - start) goto done;
        for (j = start; j < (uint64_t)start + count; ++j) {
            if (raw[j]) goto done;
            raw[j] = 1U;
        }
    }
    for (frame = 0U; frame < frames; ++frame) {
        uint32_t start = ciso_le32(table + frame * 4U);
        uint32_t length = ciso_le16(table + frames * 4U + frame * 2U);
        uint64_t remaining = member->unpacked_size - frame * 8192U;
        size_t wanted = (size_t)(remaining < 8192U ? remaining : 8192U);
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !length || start < prior_end ||
            (uint64_t)start + length > (uint64_t)member->packed_size)
            goto done;
        prior_end = (uint64_t)start + length;
        if (raw[frame]) {
            if (length != wanted ||
                !ciso_copy_range(format->device,
                                 member->header_offset + start,
                                 wanted, destination, pd)) goto done;
        } else {
            if (!ciso_read_at(format->device, member->header_offset + start,
                              packed, length) ||
                !xx_zlib_stream_decode_memory(packed, length, plain,
                                              wanted, &written) ||
                written != wanted ||
                !xx_zlib_stream_trailer_matches(packed, length, plain, wanted) ||
                !ciso_write_all(destination, plain, wanted, pd)) goto done;
        }
    }
    result = true;
done:
    if (table) xx_mem_free(table);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    if (raw) xx_mem_free(raw);
    return result;
}

static bool ciso_write_member(Abstractformat *format, ciso_stream *stream,
                              const ciso_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *index = NULL;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    uint64_t blocks, block_size, index_bytes, block;
    int64_t size;
    unsigned align;
    bool result = false;

    if (!format || !stream || !member) return false;
    if (member->method == 4U)
        return ciso_write_dax_member(format, stream, member, destination, pd);
    align = (unsigned)stream->aux0;
    block_size = stream->aux1;
    blocks = stream->aux2;
    if (block_size == 0U || blocks == 0U) return false;
    size = member->packed_size;
    index_bytes = (blocks + 1U) * 4U;
    if (index_bytes > (uint64_t)SIZE_MAX) return false;

    index = (uint8_t *)xx_mem_alloc((size_t)index_bytes);
    packed = (uint8_t *)xx_mem_alloc((size_t)block_size * 2U + 64U);
    plain = (uint8_t *)xx_mem_alloc((size_t)block_size);
    if (!index || !packed || !plain ||
        !ciso_read_at(format->device, member->header_offset + CISO_HEADER_SIZE,
                      index, (size_t)index_bytes))
        goto done;

    for (block = 0U; block < blocks; ++block) {
        uint32_t this_word = ciso_le32(index + block * 4U);
        uint32_t next_word = ciso_le32(index + (block + 1U) * 4U);
        uint64_t start = (uint64_t)(this_word & 0x7fffffffU) << align;
        uint64_t end = (uint64_t)(next_word & 0x7fffffffU) << align;
        uint64_t extent;
        size_t written = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (end < start || end > (uint64_t)size) goto done;
        extent = end - start;
        if ((member->method == 2U && extent >= block_size) ||
            (member->method != 2U && (this_word & 0x80000000U) != 0U)) {
            uint64_t run = extent < block_size ? extent : block_size;
            if (run != block_size) goto done;
            if (!ciso_copy_range(format->device,
                                 member->header_offset + (int64_t)start, run,
                                 destination, pd))
                goto done;
            continue;
        }
        if (extent == 0U || extent > (uint64_t)block_size * 2U + 64U) goto done;
        if (!ciso_read_at(format->device,
                          member->header_offset + (int64_t)start, packed,
                          (size_t)extent))
            goto done;
        if (member->method == 3U ||
            (member->method == 2U && (this_word & 0x80000000U) != 0U)) {
            if (!xx_lz4_decompress_block(packed, (size_t)extent, plain,
                                         (size_t)block_size, &written) ||
                written != (size_t)block_size) goto done;
        } else if (!xx_deflate_decompress_memory(packed, (size_t)extent, plain,
                                                (size_t)block_size, &written,
                                                false) ||
                   written != (size_t)block_size) goto done;
        if (!ciso_write_all(destination, plain, written, pd)) goto done;
    }
    result = true;
done:
    if (index) xx_mem_free(index);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

static bool ciso_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ciso_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ciso_set_record(xx_archive_record *record,
                           const ciso_member *member) {
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
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_ciso_init(xx_ciso *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CISO_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ciso");
    xx_format_set_extension(&archive->format, "cso");
    archive->format.check_is_valid = xx_ciso_check_is_valid;
    archive->format.handle_base_info = xx_ciso_handle_base_info;
    archive->format.get_format_size = xx_ciso_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ciso_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ciso_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ciso_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ciso_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ciso_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ciso_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_ciso *xx_ciso_create(xx_io_device *device, int64_t base_address) {
    xx_ciso *archive = (xx_ciso *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ciso_init(archive, device, base_address);
    return archive;
}

void xx_ciso_destroy(xx_ciso *archive) {
    if (!archive) return;
    ciso_nested_iso_free((ciso_nested_iso *)archive->nested_iso);
    archive->nested_iso = NULL;
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ciso_free(xx_ciso *archive) {
    if (!archive) return;
    xx_ciso_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ciso_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ciso_stream *stream;
    (void)pd;
    if (!ciso_parse(format, &stream)) return false;
    ciso_stream_free(stream);
    return true;
}

bool xx_ciso_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    ciso_stream *stream;
    xx_ciso *archive;
    (void)pd;
    if (format) {
        archive = (xx_ciso *)format;
        ciso_nested_iso_free((ciso_nested_iso *)archive->nested_iso);
        archive->nested_iso = NULL;
        archive->number_of_records = 0U;
    }
    if (!format || !ciso_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_ciso *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = stream->items[0].method == 4U ? XX_FILE_TYPE_DAX :
                        stream->items[0].method == 3U ? XX_FILE_TYPE_ZISO :
                        stream->items[0].method == 2U ? XX_FILE_TYPE_CISO2 :
                        XX_CISO_FILE_TYPE;
    if (stream->items[0].method == 3U)
        xx_format_set_extension(format, "zso");
    else if (stream->items[0].method == 4U)
        xx_format_set_extension(format, "dax");
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    if (stream->count == 1U && stream->items[0].unpacked_size != 0U &&
        stream->items[0].unpacked_size <= CISO_MAX_NESTED_ISO) {
        ciso_nested_iso *nested =
            (ciso_nested_iso *)xx_mem_calloc(1U, sizeof(*nested));
        if (nested) {
            size_t image_size = (size_t)stream->items[0].unpacked_size;
            nested->image = (uint8_t *)xx_mem_alloc(image_size);
            if (nested->image) {
                xx_io_device *writer = xx_io_mem_open(nested->image,
                                                      image_size);
                if (writer) {
                    bool decoded = ciso_write_member(format, stream,
                                                     &stream->items[0], writer,
                                                     pd) &&
                                   xx_io_tell(writer) == (int64_t)image_size;
                    xx_io_close(writer);
                    if (decoded) {
                        nested->device = xx_io_mem_open_ro(nested->image,
                                                            image_size);
                        if (nested->device) {
                            xx_iso9660_init(&nested->iso, nested->device, 0);
                            if (xx_iso9660_handle_base_info(
                                    &nested->iso.format, pd)) {
                                archive->nested_iso = nested;
                                archive->number_of_records =
                                    nested->iso.number_of_records;
                                format->number_of_archive_records =
                                    nested->iso.number_of_records;
                                nested = NULL;
                            }
                        }
                    }
                }
            }
            ciso_nested_iso_free(nested);
        }
    }
    ciso_stream_free(stream);
    return true;
}

int64_t xx_ciso_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ciso_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_ciso_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_ciso_handle_base_info(format, pd))
               ? ((xx_ciso *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_ciso_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ciso_nested_iso *nested =
        format ? (ciso_nested_iso *)((xx_ciso *)format)->nested_iso : NULL;
    ciso_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (nested) {
        state = xx_iso9660_create_archive_records_reading(
            &nested->iso.format, options, pd);
        if (state && !ciso_prefix_iso_record(state)) {
            xx_iso9660_free_archive_records_reading(&nested->iso.format, state);
            return NULL;
        }
        return state;
    }
    if (!ciso_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ciso_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ciso_stream_free;
    state->total_records = stream->count;
    if (!ciso_copy_options(&state->options, options) ||
        !ciso_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_ciso_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    ciso_nested_iso *nested =
        format ? (ciso_nested_iso *)((xx_ciso *)format)->nested_iso : NULL;
    if (nested) return xx_iso9660_get_current_archive_record(
        &nested->iso.format, state);
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ciso_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    ciso_nested_iso *nested =
        format ? (ciso_nested_iso *)((xx_ciso *)format)->nested_iso : NULL;
    ciso_stream *stream;
    if (nested) {
        return xx_iso9660_archive_record_move_to_next(
                   &nested->iso.format, state, pd) &&
               ciso_prefix_iso_record(state);
    }
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ciso_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        ciso_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ciso_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    ciso_nested_iso *nested =
        format ? (ciso_nested_iso *)((xx_ciso *)format)->nested_iso : NULL;
    ciso_stream *stream;
    ciso_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (nested) return xx_iso9660_unpack_current_archive_record(
        &nested->iso.format, state, pd);
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ciso_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!ciso_safe_output_name(member->name)) return false;
    path_option = ciso_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return ciso_write_member(format, stream, member, NULL, pd);
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
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = ciso_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ciso_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    ciso_nested_iso *nested =
        format ? (ciso_nested_iso *)((xx_ciso *)format)->nested_iso : NULL;
    if (nested) {
        xx_iso9660_free_archive_records_reading(&nested->iso.format, state);
        return;
    }
    (void)format;
    xx_archive_record_state_free(state);
}
