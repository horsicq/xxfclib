/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SPIS archives.
 *
 * The file opens with a 21-byte archive header:
 *
 *   0x00  5 bytes magic "SPIS\x1a"
 *   0x05  3 bytes method tag: "NON", "RLE", "LZH", "CUS" or "LH5"
 *   0x08  u32 LE total uncompressed size of this segment
 *   0x0c  u8  archive type: 0 = single payload, 1 = member directory
 *   0x0d  u32 LE byte-sum checksum (type 0 only; unused in type 1)
 *   0x11  u32 LE flags, 0..2 (2 = payloads are XOR masked)
 *
 * Type 0 is one member: everything after the header is its payload, the
 * method tag in the header is its method, and the header's size field is its
 * uncompressed length. It carries no name, so the reader synthesises
 * "payload.<ext>" from a sniff of the first decoded bytes.
 *
 * Type 1 is a chain of 25-byte record headers, each followed by its name and
 * then its payload:
 *
 *   0x00  u16 LE name length, 1..4096
 *   0x02  u32 LE packed DOS date/time, date in the high word
 *   0x06  u16 LE DOS attributes
 *   0x08  u32 LE uncompressed size
 *   0x0c  u32 LE compressed size
 *   0x10  u8  method: 0 NON, 1 RLE, 2 LZH, 3 CUS, 4 LH5
 *   0x11  u32 LE byte-sum checksum over the DECOMPRESSED member
 *   0x15  u32 LE flags, 0..2
 *   0x19  name, exactly nameLen bytes, "\dir\file" with a LEADING separator
 *
 * A type-1 chain may be interrupted by a REPEATED 21-byte archive header,
 * which starts a new segment: every header states the uncompressed total of
 * its OWN segment, so the running sum is closed and restarted at each one.
 * The magic can never be mistaken for a record header, because it would mean
 * a name length of 0x5053 bytes, far over the cap.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/spis/xx_spis.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "../../../algo/lzh/xx_lzh5_internal.h"
#include "xx_spis_key_internal.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_SPIS_COPY_CHUNK (64 * 1024)

typedef struct xx_spis_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_spis_member;

typedef struct xx_spis_small_context_s {
    uint8_t *snapshot;
    size_t size;
    int64_t base;
    uint32_t key;
} xx_spis_small_context;

typedef struct xx_spis_stream_s {
    xx_spis_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t key;
    bool key_ready;
    bool truncated_tail;
    xx_spis_small_context *small;
    xx_spis_small_context *multi;
} xx_spis_stream;

static void xx_spis_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_spis_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;
    int64_t cursor;
    bool result = false;
    if (!self || !self->device || offset < 0 || (cursor = xx_io_tell(self->device)) < 0) return false;
    if (xx_io_seek64(self->device, offset, SEEK_SET) != 0) goto done;
    while (completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) goto done;
        completed += (size_t)received;
    }
    result = true;
done:
    if (xx_io_seek64(self->device, cursor, SEEK_SET) != 0) result = false;
    return result;
}

static bool xx_spis_range_within(int64_t total, int64_t offset, int64_t size)
{
    return offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_spis_path_safe(const char *name)
{
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

static void xx_spis_stream_free(void *pointer)
{
    xx_spis_stream *stream = (xx_spis_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    if (stream->small) { xx_mem_free(stream->small->snapshot); xx_mem_free(stream->small); }
    if (stream->multi) { xx_mem_free(stream->multi->snapshot); xx_mem_free(stream->multi); }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_spis_add(xx_spis_stream *stream, const xx_spis_member *member)
{
    xx_spis_member *grown = (xx_spis_member *)xx_mem_realloc(stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define XX_SPIS_MAX_MEMBERS 100000
#define XX_SPIS_SNIFF_PACKED 512
#define XX_SPIS_SNIFF_SIZE 64
#define XX_SPIS_HEADER_SIZE 21
#define XX_SPIS_RECORD_HEADER_SIZE 25
#define XX_SPIS_MAX_MEMBER_SIZE ((int64_t)512 * 1024 * 1024)
#define XX_SPIS_MAX_NAME 4096
#define XX_SPIS_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_SPIS_METHOD_NON 0U
#define XX_SPIS_METHOD_RLE 1U
#define XX_SPIS_METHOD_LZH 2U
#define XX_SPIS_METHOD_CUS 3U
#define XX_SPIS_METHOD_LH5 4U
#define XX_SPIS_METHOD_INVALID 5U
#define XX_SPIS_FLAG_MAX 2U
#define XX_SPIS_FLAG_OBFUSCATED 2U
#define XX_SPIS_FLAG_KEY 0x01f4410bU
#define XX_SPIS_KEY_PROBE_MAX_RAW 256U
#define XX_SPIS_KEY_PROBE_MAX_PACKED (64U * 1024U)
#define XX_SPIS_RLE_ESCAPE 0x94U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_spis_tag_method(const uint8_t *tag);
static char *xx_spis_make_name(const uint8_t *raw, size_t size);
static bool xx_spis_parse_single(Abstractformat *self, int64_t span, uint32_t method, uint32_t total_raw, uint32_t checksum, xx_spis_stream *stream);
static xx_spis_stream *xx_spis_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_spis_rle_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written, bool partial);
static bool xx_spis_checksum_matches(uint32_t method, uint32_t flags, uint32_t stored, uint32_t sum, uint32_t key);
static bool xx_spis_member_guard(Abstractformat *self, const xx_spis_member *member, uint32_t *flags, uint32_t *checksum);
static bool xx_spis_prepare_key(Abstractformat *self, xx_spis_stream *stream, xx_pd_struct *pd);
static bool xx_spis_decode(Abstractformat *self, const xx_spis_member *member, uint32_t key, uint8_t **out, size_t *out_size, xx_pd_struct *pd);

/* No member count is stored, so this is a runaway guard rather than a format
 * limit; it matches the reference implementation's bound. */
/* How many payload bytes the type-0 extension sniff may look at, and how many
 * decoded bytes it needs. */

/* Maps the 3-byte header tag to the same method numbers the record headers
 * use, so a listing shows one vocabulary. */
/* Keep all records when DOS/Windows filenames collide, including names
 * that differ only in case. Reserve original names before choosing suffixes;
 * a later real "(0)file" must not be overwritten by an earlier duplicate. */
static unsigned char xx_spis_fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}
static size_t xx_spis_name_slot(char **slots, size_t capacity, const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    uint32_t hash = UINT32_C(2166136261);
    size_t slot;
    while (*p) { hash ^= xx_spis_fold(*p++); hash *= UINT32_C(16777619); }
    slot = (size_t)hash & (capacity - 1U);
    while (slots[slot]) {
        const unsigned char *a = (const unsigned char *)slots[slot], *b = (const unsigned char *)name;
        while (*a && *b && xx_spis_fold(*a) == xx_spis_fold(*b)) { ++a; ++b; }
        if (!*a && !*b) break;
        slot = (slot + 1U) & (capacity - 1U);
    }
    return slot;
}
static bool xx_spis_unique_names(xx_spis_stream *stream, xx_pd_struct *pd)
{
    size_t capacity = 8U, i;
    char **reserved, **used;
    bool ok = false;
    while (capacity < stream->count * 2U) capacity *= 2U;
    reserved = (char **)xx_mem_calloc(capacity * 2U, sizeof(*reserved));
    if (!reserved) return false;
    used = reserved + capacity;
    for (i = 0U; i < stream->count; ++i) {
        size_t slot = xx_spis_name_slot(reserved, capacity, stream->items[i].name);
        if (!reserved[slot]) reserved[slot] = stream->items[i].name;
    }
    for (i = 0U; i < stream->count; ++i) {
        char *name = stream->items[i].name;
        size_t slot = xx_spis_name_slot(used, capacity, name);
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (used[slot]) {
            size_t length = xx_rt_strlen(name), prefix = 0U, j;
            unsigned serial;
            char *candidate = (char *)xx_mem_alloc(length + 32U);
            if (!candidate) goto done;
            for (j = 0U; j < length; ++j) if (name[j] == '/') prefix = j + 1U;
            for (serial = 0U; serial <= stream->count; ++serial) {
                xx_rt_snprintf(candidate, length + 32U, "%.*s(%u)%s", (int)prefix, name, serial, name + prefix);
                slot = xx_spis_name_slot(used, capacity, candidate);
                if (!used[slot] && !reserved[xx_spis_name_slot(reserved, capacity, candidate)]) break;
            }
            if (serial > stream->count) { xx_mem_free(candidate); goto done; }
            stream->items[i].name = candidate;
            xx_str_free(name);
            name = candidate;
        }
        used[slot] = name;
    }
    ok = true;
done:
    xx_mem_free(reserved);
    return ok;
}

static uint32_t xx_spis_tag_method(const uint8_t *tag)
{
    if (xx_rt_memcmp(tag, "NON", 3) == 0) return XX_SPIS_METHOD_NON;
    if (xx_rt_memcmp(tag, "RLE", 3) == 0) return XX_SPIS_METHOD_RLE;
    if (xx_rt_memcmp(tag, "LZH", 3) == 0) return XX_SPIS_METHOD_LZH;
    if (xx_rt_memcmp(tag, "CUS", 3) == 0) return XX_SPIS_METHOD_CUS;
    if (xx_rt_memcmp(tag, "LH5", 3) == 0) return XX_SPIS_METHOD_LH5;
    return XX_SPIS_METHOD_INVALID;
}

/*
 * Member names are stored DOS-style with a LEADING separator ("\dir\file").
 * The reference strips it rather than treating the name as absolute, so this
 * does too; everything else - empty, "." or ".." components, a drive colon -
 * is a refusal.
 *
 * The reference decodes the raw bytes as Latin-1, but a name outside printable
 * ASCII is far more likely to be payload bytes read as a header than a real
 * member, and that is exactly the false positive this format has least other
 * defence against. Printable ASCII only.
 */
static char *xx_spis_make_name(const uint8_t *raw, size_t size)
{
    char *name;
    size_t index;
    size_t start = 0U;
    size_t length;
    size_t part;

    if (size == 0U || size > (size_t)XX_SPIS_MAX_NAME) return NULL;
    for (index = 0U; index < size; ++index) {
        if (raw[index] < 0x20U || raw[index] > 0x7EU) return NULL;
        if (raw[index] == ':') return NULL;
    }
    name = (char *)xx_mem_alloc(size + 1U);
    if (!name) return NULL;
    for (index = 0U; index < size; ++index) {
        name[index] = (raw[index] == '\\') ? '/' : (char)raw[index];
    }
    name[size] = 0;
    while (start < size && name[start] == '/') ++start;
    length = size - start;
    if (length == 0U) {
        xx_str_free(name);
        return NULL;
    }
    for (index = 0U; index < length; ++index) name[index] = name[start + index];
    name[length] = 0;

    /* Every path component must be a real name: an empty one means a doubled
     * separator, and "." or ".." would escape the extraction directory. */
    part = 0U;
    for (index = 0U; index <= length; ++index) {
        if (index == length || name[index] == '/') {
            size_t component = index - part;

            if (component == 0U) {
                xx_str_free(name);
                return NULL;
            }
            if (component == 1U && name[part] == '.') {
                xx_str_free(name);
                return NULL;
            }
            if (component == 2U && name[part] == '.' && name[part + 1] == '.') {
                xx_str_free(name);
                return NULL;
            }
            part = index + 1U;
        }
    }
    if (!xx_spis_path_safe(name)) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

/* A type-0 archive carries no member name at all, so the extension is sniffed
 * from the first decoded bytes the way the reference does. Nothing depends on
 * getting this right: an unrecognised payload is simply ".bin". */
static const char *xx_spis_payload_ext(const uint8_t *prefix, size_t size)
{
    if (size >= 2U && prefix[0] == 'B' && prefix[1] == 'M') return "bmp";
    if (size >= 2U && prefix[0] == 'M' && prefix[1] == 'Z') return "exe";
    if (size >= 4U && xx_rt_memcmp(prefix, "PK\x03\x04", 4) == 0) return "zip";
    if (size >= 6U && (xx_rt_memcmp(prefix, "GIF87a", 6) == 0 || xx_rt_memcmp(prefix, "GIF89a", 6) == 0)) {
        return "gif";
    }
    if (size >= 8U && xx_rt_memcmp(prefix, "\x89PNG\x0d\x0a\x1a\x0a", 8) == 0) {
        return "png";
    }
    if (size >= 12U && xx_rt_memcmp(prefix, "RIFF", 4) == 0 && xx_rt_memcmp(prefix + 8, "WAVE", 4) == 0) {
        return "wav";
    }
    return "bin";
}

/* The single-payload layout: the whole file behind the 21-byte header is one
 * member whose method and size come from that header. */
static bool xx_spis_parse_single(Abstractformat *self, int64_t span, uint32_t method, uint32_t total_raw, uint32_t checksum, xx_spis_stream *stream)
{
    uint8_t packed[XX_SPIS_SNIFF_PACKED];
    uint8_t prefix[XX_SPIS_SNIFF_SIZE];
    uint8_t name_buffer[32];
    const char *extension;
    size_t prefix_size = 0U;
    size_t sniff_packed;
    size_t index;
    int64_t packed_size;
    xx_spis_member member;

    (void)checksum;
    packed_size = span - XX_SPIS_HEADER_SIZE;
    /* A type-0 archive with no payload at all is not a format this reader
     * recognises - it is a bare header, which any file may happen to start
     * with. */
    if (packed_size <= 0) return false;
    if (total_raw == 0U || (int64_t)total_raw > XX_SPIS_MAX_MEMBER_SIZE) {
        return false;
    }
    /* The one place a type-0 header states both sizes for the same bytes. */
    if (method == XX_SPIS_METHOD_NON && packed_size != (int64_t)total_raw) {
        return false;
    }

    sniff_packed = (size_t)((packed_size < (int64_t)XX_SPIS_SNIFF_PACKED) ? packed_size : (int64_t)XX_SPIS_SNIFF_PACKED);
    if (!xx_spis_read_at(self, self->base_address + XX_SPIS_HEADER_SIZE, packed, sniff_packed)) {
        return false;
    }
    if (method == XX_SPIS_METHOD_NON) {
        prefix_size = (sniff_packed < (size_t)XX_SPIS_SNIFF_SIZE) ? sniff_packed : (size_t)XX_SPIS_SNIFF_SIZE;
        for (index = 0U; index < prefix_size; ++index) prefix[index] = packed[index];
    } else if (method == XX_SPIS_METHOD_RLE) {
        /* Partial expansion: the sniff window almost never ends on a token
         * boundary, so a truncated tail here is expected, not an error. */
        if (!xx_spis_rle_decode(packed, sniff_packed, prefix, (size_t)XX_SPIS_SNIFF_SIZE, &prefix_size, true)) {
            prefix_size = 0U;
        }
    }
    extension = xx_spis_payload_ext(prefix, prefix_size);

    name_buffer[0] = 'p';
    name_buffer[1] = 'a';
    name_buffer[2] = 'y';
    name_buffer[3] = 'l';
    name_buffer[4] = 'o';
    name_buffer[5] = 'a';
    name_buffer[6] = 'd';
    name_buffer[7] = '.';
    for (index = 0U; index < 3U && extension[index]; ++index) {
        name_buffer[8U + index] = (uint8_t)extension[index];
    }
    name_buffer[8U + index] = 0U;

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup((const char *)name_buffer);
    if (!member.name) return false;
    member.header_offset = self->base_address;
    member.header_size = XX_SPIS_HEADER_SIZE;
    member.data_offset = self->base_address + XX_SPIS_HEADER_SIZE;
    member.compressed_size = packed_size;
    member.uncompressed_size = (int64_t)total_raw;
    member.method = method;
    member.is_folder = false;
    if (!xx_spis_add(stream, &member)) {
        xx_str_free(member.name);
        return false;
    }
    return true;
}

static xx_spis_stream *xx_spis_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_spis_stream *stream = NULL;
    uint8_t header[XX_SPIS_RECORD_HEADER_SIZE];
    uint8_t *name_bytes = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    uint32_t method;
    uint32_t total_raw;
    uint32_t checksum;
    uint32_t flags;
    uint8_t archive_type;
    uint64_t segment_sum = 0U;
    uint64_t segment_total;
    int64_t inner_headers = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_SPIS_HEADER_SIZE) return NULL;
    if (!xx_spis_read_at(self, self->base_address, header, (size_t)XX_SPIS_HEADER_SIZE)) {
        return NULL;
    }
    /* The five-byte magic plus a known method tag plus a type of 0 or 1 plus
     * a flags word of 0..2 is the whole of this format's identity. TCompress
     * lets an application replace the ID, so only the default form is
     * recognised - a looser magic here would claim files at random. */
    if (xx_rt_memcmp(header, "SPIS\x1a", 5) != 0) return NULL;
    method = xx_spis_tag_method(header + 5);
    total_raw = xx_data_get_u32(header + 8, 4, 0, false);
    archive_type = header[12];
    checksum = xx_data_get_u32(header + 13, 4, 0, false);
    flags = xx_data_get_u32(header + 17, 4, 0, false);
    /* The word at +17 is a FLAGS field, not a reserved zero. */
    if (method == XX_SPIS_METHOD_INVALID || archive_type > 1U || flags > XX_SPIS_FLAG_MAX || total_raw == 0U || (int64_t)total_raw > XX_SPIS_MAX_MEMBER_SIZE) {
        return NULL;
    }

    stream = (xx_spis_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->key = XX_SPIS_FLAG_KEY;

    if (archive_type == 0U) {
        if (!xx_spis_parse_single(self, span, method, total_raw, checksum, stream)) {
            goto fail;
        }
        stream->archive_size = span;
        return stream;
    }

    segment_total = (uint64_t)total_raw;
    offset = XX_SPIS_HEADER_SIZE;
    while (offset < span) {
        uint16_t name_size;
        uint32_t raw_size;
        uint32_t packed_size;
        uint32_t record_flags;
        uint32_t record_method;
        int64_t available;
        xx_spis_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_SPIS_MAX_MEMBERS) goto fail;

        if (span - offset >= XX_SPIS_HEADER_SIZE) {
            if (!xx_spis_read_at(self, self->base_address + offset, header, (size_t)XX_SPIS_HEADER_SIZE)) {
                goto fail;
            }
            if (xx_rt_memcmp(header, "SPIS\x1a", 5) == 0) {
                uint32_t inner_method = xx_spis_tag_method(header + 5);
                uint32_t inner_total = xx_data_get_u32(header + 8, 4, 0, false);
                uint32_t inner_flags = xx_data_get_u32(header + 17, 4, 0, false);

                if (inner_headers >= XX_SPIS_MAX_MEMBERS) goto fail;
                /* A segment holding only empty members declares a total of
                 * zero, so zero is legal here even though the outermost
                 * header's total may not be zero. */
                if (inner_method == XX_SPIS_METHOD_INVALID || header[12] != 1U || inner_flags > XX_SPIS_FLAG_MAX || (int64_t)inner_total > XX_SPIS_MAX_MEMBER_SIZE) {
                    goto fail;
                }
                /* The segment that just ended must account for exactly the
                 * uncompressed bytes its own header declared. This sum is the
                 * format's only end-to-end structural check, and loosening it
                 * would let a chain of plausible-looking records through. */
                if (segment_sum != segment_total) goto fail;
                segment_sum = 0U;
                segment_total = (uint64_t)inner_total;
                ++inner_headers;
                offset += XX_SPIS_HEADER_SIZE;
                continue;
            }
        }

        if (span - offset < XX_SPIS_RECORD_HEADER_SIZE) goto partial;
        if (!xx_spis_read_at(self, self->base_address + offset, header, (size_t)XX_SPIS_RECORD_HEADER_SIZE)) {
            goto fail;
        }
        name_size = xx_data_get_u16(header, 2, 0, false);
        raw_size = xx_data_get_u32(header + 8, 4, 0, false);
        packed_size = xx_data_get_u32(header + 12, 4, 0, false);
        record_method = (uint32_t)header[16];
        record_flags = xx_data_get_u32(header + 21, 4, 0, false);

        /* A zero-length member is real: GP-Install setups ship placeholder
         * data files with no bytes at all. Both sizes are then zero - either
         * one alone is a malformed record and is still refused. */
        if (name_size == 0U || (int64_t)name_size > XX_SPIS_MAX_NAME || record_method >= XX_SPIS_METHOD_INVALID || record_flags > XX_SPIS_FLAG_MAX ||
            (int64_t)raw_size > XX_SPIS_MAX_MEMBER_SIZE) {
            goto fail;
        }
        if (!((raw_size == 0U && packed_size == 0U) || (raw_size != 0U && packed_size != 0U))) {
            goto fail;
        }
        /* The one place a record states both sizes for the same bytes. */
        if (record_method == XX_SPIS_METHOD_NON && packed_size != raw_size) {
            goto fail;
        }
        if (!xx_spis_range_within(span, offset + XX_SPIS_RECORD_HEADER_SIZE, (int64_t)name_size)) {
            goto partial;
        }
        available = span - offset - XX_SPIS_RECORD_HEADER_SIZE - (int64_t)name_size;
        /* The reference salvages a final member cut short by EOF, but every
         * member is authenticated by a sum over its DECOMPRESSED bytes, so
         * there is nothing to check a prefix against: an extent past EOF is a
         * rejection here. */
        if ((int64_t)packed_size > available) goto partial;

        name_bytes = (uint8_t *)xx_mem_alloc((size_t)name_size);
        if (!name_bytes) goto fail;
        if (!xx_spis_read_at(self, self->base_address + offset + XX_SPIS_RECORD_HEADER_SIZE, name_bytes, (size_t)name_size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_spis_make_name(name_bytes, (size_t)name_size);
        xx_mem_free(name_bytes);
        name_bytes = NULL;
        if (!member.name) goto fail;

        member.header_offset = self->base_address + offset;
        member.header_size = XX_SPIS_RECORD_HEADER_SIZE + (int64_t)name_size;
        member.data_offset = member.header_offset + member.header_size;
        member.compressed_size = (int64_t)packed_size;
        member.uncompressed_size = (int64_t)raw_size;
        member.method = record_method;
        /* Already packed date-high / time-low by the container, so it is
         * stored verbatim rather than re-assembled. */
        member.timestamp = (uint64_t)xx_data_get_u32(header + 2, 4, 0, false);
        /* The format is flat: directories are implied by the names alone. */
        member.is_folder = false;
        if (!xx_spis_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        segment_sum += (uint64_t)raw_size;
        if (segment_sum > segment_total) goto fail;
        offset = (member.data_offset - self->base_address) + member.compressed_size;
    }

    /* The chain has to end exactly at EOF, hold at least one member, and
     * close the last segment's declared total. */
    if (offset != span || stream->count == 0U || segment_sum != segment_total) {
        goto fail;
    }
    stream->archive_size = span;
    if (!xx_spis_unique_names(stream, pd)) goto fail;
    return stream;

partial:
    /* No bytes from the short member are published. Earlier records still
     * undergo their complete decode and byte-sum checks at extraction. */
    if (stream->count != 0U && !(pd && xx_pd_is_stopped(pd))) {
        stream->truncated_tail = true;
        stream->archive_size = offset;
        if (!xx_spis_unique_names(stream, pd)) goto fail;
        return stream;
    }
fail:
    xx_mem_free(name_bytes);
    xx_spis_stream_free(stream);
    return NULL;
}

/* The grammar's own ceiling on any single member, taken from the reference. */
/* Separate from the grammar bound above: this one guards the two allocations
 * decode makes from a container-stated, attacker-controlled length. */

/* "CUS" is the format's escape hatch for an application-supplied codec. It is
 * a defined method with no defined bitstream, so it is refused rather than
 * guessed at. */

/* The flags word accepts only 0, 1 and 2. */
/* Literal in the reference expander. Flags 1 and 2 bias the stored checksum
 * by this constant, and under flag 2 its four little-endian bytes are ALSO
 * the repeating XOR mask over each non-stored payload, cycling from that
 * member's data offset. Stored members are never masked. */

/* The escape byte of the format's RLE. */

/*
 * The SPIS RLE. This is not a general-purpose codec with a library entry
 * point of its own: it is a dozen lines of escape-byte run expansion that
 * exist only inside this container, so it lives here rather than in
 * xxfclib/algo.
 *
 * 0x94 escapes the next byte: n >= 2 repeats the last emitted byte n - 1 more
 * times, n == 0 emits a literal 0x94, and n == 1 emits nothing at all. The
 * original expander assigns "last = b" after EVERY token, so a repeat token
 * leaves 0x94 as the next repeatable byte - a run longer than 255 has to
 * restart with a fresh literal. Reproducing that quirk is what keeps this
 * byte-for-byte with the producer.
 *
 * With partial = true the expansion stops once out_size bytes exist and a
 * truncated tail is not an error; that mode only ever feeds the type-0
 * extension sniff, never an extraction.
 */
static bool xx_spis_rle_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written, bool partial)
{
    size_t index;
    size_t produced = 0U;
    bool escape = false;
    uint8_t last = 0U;

    if (written) *written = 0U;
    if (!input || (!output && output_size != 0U)) return false;
    for (index = 0U; index < input_size; ++index) {
        uint8_t value = input[index];

        if (!escape) {
            if (value == (uint8_t)XX_SPIS_RLE_ESCAPE) {
                escape = true;
                continue;
            }
            if (produced >= output_size) {
                if (partial) break;
                /* More output than the container promised: the stream and the
                 * declared size disagree, which is a refusal, never a clamp. */
                return false;
            }
            output[produced++] = value;
            last = value;
            continue;
        }
        escape = false;
        if (value >= 2U) {
            size_t repeat = (size_t)value - 1U;
            while (repeat > 0U) {
                if (produced >= output_size) {
                    if (partial) break;
                    return false;
                }
                output[produced++] = last;
                --repeat;
            }
            if (partial && produced >= output_size) break;
        } else if (value == 0U) {
            if (produced >= output_size) {
                if (partial) break;
                return false;
            }
            output[produced++] = (uint8_t)XX_SPIS_RLE_ESCAPE;
        }
        /* value == 1 emits nothing, and every token - repeat included - leaves
         * the escape byte as the next repeatable value. */
        last = (uint8_t)XX_SPIS_RLE_ESCAPE;
    }
    if (!partial) {
        /* A stream ending mid-escape is truncated, and a short expansion means
         * the member is not the length the container claims. */
        if (escape || produced != output_size) return false;
    }
    if (written) *written = produced;
    return true;
}

/*
 * The reference's four-case checksum rule. Honouring only the plain equality
 * drops members from archives that otherwise extract cleanly: a stored member
 * whose stored value is zero is not verified at all, and the two obfuscation
 * flags bias the sum by the key.
 */
static bool xx_spis_checksum_matches(uint32_t method, uint32_t flags, uint32_t stored, uint32_t sum, uint32_t key)
{
    if (method == XX_SPIS_METHOD_NON && stored == 0U) return true;
    if (sum == stored) return true;
    if (flags == 0U) return false;
    if (method == XX_SPIS_METHOD_NON) sum = 0U;
    return (uint32_t)(sum + key) == stored;
}

/*
 * The member struct carries no room for the flags and the checksum, so decode
 * recovers them by re-reading the header it was told about. Which layout that
 * is follows from the magic: "SPIS\x1a" means the single-payload archive
 * header (checksum at +13, flags at +17), anything else is a record header
 * (checksum at +17, flags at +21).
 */
static bool xx_spis_member_guard(Abstractformat *self, const xx_spis_member *member, uint32_t *flags, uint32_t *checksum)
{
    uint8_t header[XX_SPIS_RECORD_HEADER_SIZE];

    if (!self || !member || !flags || !checksum) return false;
    if (member->header_size < XX_SPIS_HEADER_SIZE) return false;
    if (!xx_spis_read_at(self, member->header_offset, header, (size_t)XX_SPIS_HEADER_SIZE)) {
        return false;
    }
    if (xx_rt_memcmp(header, "SPIS\x1a", 5) == 0) {
        *checksum = xx_data_get_u32(header + 13, 4, 0, false);
        *flags = xx_data_get_u32(header + 17, 4, 0, false);
        return true;
    }
    if (member->header_size < XX_SPIS_RECORD_HEADER_SIZE) return false;
    if (!xx_spis_read_at(self, member->header_offset, header, (size_t)XX_SPIS_RECORD_HEADER_SIZE)) {
        return false;
    }
    *checksum = xx_data_get_u32(header + 17, 4, 0, false);
    *flags = xx_data_get_u32(header + 21, 4, 0, false);
    return true;
}

typedef struct xx_spis_key_probe_s {
    uint8_t *packed;
    uint8_t *unmasked;
    uint8_t *plain;
    size_t packed_size;
    size_t plain_size;
    uint32_t checksum;
} xx_spis_key_probe;

static void xx_spis_key_probe_free(xx_spis_key_probe *probe)
{
    if (!probe) return;
    xx_mem_free(probe->packed);
    xx_mem_free(probe->unmasked);
    xx_mem_free(probe->plain);
    xx_mem_zero(probe, sizeof(*probe));
}

static bool xx_spis_key_probe_load(Abstractformat *self, const xx_spis_member *member, xx_spis_key_probe *probe)
{
    uint32_t flags;
    if (!self || !member || !probe || !xx_spis_member_guard(self, member, &flags, &probe->checksum) || flags != XX_SPIS_FLAG_OBFUSCATED) return false;
    probe->packed_size = (size_t)member->compressed_size;
    probe->plain_size = (size_t)member->uncompressed_size;
    probe->packed = (uint8_t *)xx_mem_alloc(probe->packed_size);
    probe->unmasked = (uint8_t *)xx_mem_alloc(probe->packed_size);
    probe->plain = (uint8_t *)xx_mem_alloc(probe->plain_size);
    return probe->packed && probe->unmasked && probe->plain && xx_spis_read_at(self, member->data_offset, probe->packed, probe->packed_size);
}

static bool xx_spis_key_probe_matches(xx_spis_key_probe *probe, uint32_t key)
{
    size_t index;
    size_t written = 0U;
    uint32_t sum = 0U;
    if (!probe || !probe->packed || !probe->unmasked || !probe->plain) return false;
    for (index = 0U; index < probe->packed_size; ++index) {
        probe->unmasked[index] = (uint8_t)(probe->packed[index] ^ (uint8_t)((key >> (8U * (index & 3U))) & 0xffU));
    }
    if (!xx_lzh1_decode_memory(probe->unmasked, probe->packed_size, probe->plain, probe->plain_size, &written) || written != probe->plain_size) return false;
    for (index = 0U; index < written; ++index) sum += probe->plain[index];
    return (uint32_t)(sum + key) == probe->checksum;
}

/* TCompress uses a caller-selected key for flag-2 archives.  The checksum is
 * the decoded byte sum plus that same key, so a small member bounds the key
 * search to at most 255 times its raw size.  Two independent LZH members must
 * both decode and satisfy their checksums before selecting a recovered key.
 * The conservative size caps bound work on untrusted archives. */
static bool xx_spis_prepare_key(Abstractformat *self, xx_spis_stream *stream, xx_pd_struct *pd)
{
    xx_spis_key_probe probes[2];
    size_t selected[2] = {SIZE_MAX, SIZE_MAX};
    size_t index;
    uint32_t delta;
    uint32_t bound;
    bool result = false;
    if (!self || !stream) return false;
    if (stream->key_ready) return true;
    for (index = 0U; index < stream->count; ++index) {
        const xx_spis_member *member = &stream->items[index];
        uint32_t flags;
        uint32_t checksum;
        if (member->method != XX_SPIS_METHOD_LZH || member->uncompressed_size <= 0 || member->uncompressed_size > XX_SPIS_KEY_PROBE_MAX_RAW ||
            member->compressed_size <= 0 || member->compressed_size > XX_SPIS_KEY_PROBE_MAX_PACKED)
            continue;
        if (!xx_spis_member_guard(self, member, &flags, &checksum)) return false;
        if (flags != XX_SPIS_FLAG_OBFUSCATED) continue;
        if (selected[0] == SIZE_MAX || member->uncompressed_size < stream->items[selected[0]].uncompressed_size) {
            selected[1] = selected[0];
            selected[0] = index;
        } else if (selected[1] == SIZE_MAX || member->uncompressed_size < stream->items[selected[1]].uncompressed_size) {
            selected[1] = index;
        }
    }
    if (selected[1] == SIZE_MAX) {
        stream->key_ready = true;
        return true;
    }
    xx_mem_zero(probes, sizeof(probes));
    if (!xx_spis_key_probe_load(self, &stream->items[selected[0]], &probes[0]) || !xx_spis_key_probe_load(self, &stream->items[selected[1]], &probes[1])) goto done;
    if (xx_spis_key_probe_matches(&probes[0], stream->key) && xx_spis_key_probe_matches(&probes[1], stream->key)) {
        result = true;
        goto done;
    }
    bound = (uint32_t)(probes[0].plain_size * 255U);
    for (delta = 0U; delta <= bound; ++delta) {
        uint32_t candidate = probes[0].checksum - delta;
        if ((delta & 255U) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (candidate == stream->key) continue;
        if (xx_spis_key_probe_matches(&probes[0], candidate) && xx_spis_key_probe_matches(&probes[1], candidate)) {
            stream->key = candidate;
            result = true;
            break;
        }
    }
    /* No recovered key leaves the historical default available for a later
     * member. Its normal checksum gate still prevents bad bytes escaping. */
    if (!result) result = true;
done:
    xx_spis_key_probe_free(&probes[0]);
    xx_spis_key_probe_free(&probes[1]);
    if (result) stream->key_ready = true;
    return result;
}

static bool xx_spis_decode(Abstractformat *self, const xx_spis_member *member, uint32_t key, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    size_t index;
    uint32_t flags = 0U;
    uint32_t stored = 0U;
    uint32_t sum = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_SPIS_MAX_DECODED || member->uncompressed_size > XX_SPIS_MAX_DECODED) {
        return false;
    }
    /* "CUS" is defined by the format but has no bitstream this reader can
     * read; treating it as stored would publish garbage that looks like data. */
    if (member->method != XX_SPIS_METHOD_NON && member->method != XX_SPIS_METHOD_RLE && member->method != XX_SPIS_METHOD_LZH && member->method != XX_SPIS_METHOD_LH5) {
        return false;
    }

    if (member->uncompressed_size == 0) {
        /* A zero-length member is real, not malformed: GP-Install setups ship
         * placeholder data files carrying no bytes at all. There is nothing
         * for the codec to read and nothing for the byte sum to cover, so the
         * empty output is published without consulting the checksum. */
        if (member->compressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;
    if (!xx_spis_member_guard(self, member, &flags, &stored)) return false;
    if (flags > XX_SPIS_FLAG_MAX) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_spis_read_at(self, member->data_offset, input, (size_t)member->compressed_size)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* Under flag 2 the payload is XOR masked and the codec must never see it
     * that way. The mask cycles from the member's data offset, so index 0 here
     * is the first payload byte. Stored members are not masked. */
    if (flags == XX_SPIS_FLAG_OBFUSCATED && member->method != XX_SPIS_METHOD_NON) {
        for (index = 0U; index < (size_t)member->compressed_size; ++index) {
            input[index] = (uint8_t)(input[index] ^ (uint8_t)((key >> (8U * (index & 3U))) & 0xFFU));
        }
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) goto fail;

    if (member->method == XX_SPIS_METHOD_NON) {
        if (member->compressed_size != member->uncompressed_size) goto fail;
        for (index = 0U; index < (size_t)member->uncompressed_size; ++index) {
            output[index] = input[index];
        }
        written = (size_t)member->uncompressed_size;
    } else if (member->method == XX_SPIS_METHOD_RLE) {
        if (!xx_spis_rle_decode(input, (size_t)member->compressed_size, output, (size_t)member->uncompressed_size, &written, false)) {
            goto fail;
        }
    } else if (member->method == XX_SPIS_METHOD_LZH) {
        if (!xx_lzh1_decode_memory(input, (size_t)member->compressed_size, output, (size_t)member->uncompressed_size, &written)) {
            goto fail;
        }
    } else {
        /* The 5 is the LHA method digit "LH5" names: it selects the 13-bit
         * window. 4, 6 or 7 would decode the same bytes into plausible
         * garbage instead of failing. */
        if (!xx_lzh5_decode_memory(input, (size_t)member->compressed_size, output, (size_t)member->uncompressed_size, 5, &written)) {
            goto fail;
        }
    }
    /* Exactly the container's length, or nothing: a short decode reported as
     * success is the one failure the caller cannot detect. */
    if (written != (size_t)member->uncompressed_size) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* The format authenticates every member by a plain 32-bit sum of its
     * DECOMPRESSED bytes. This is the only check that a wrong codec, a wrong
     * LZH window or a wrong XOR mask cannot pass. */
    for (index = 0U; index < (size_t)member->uncompressed_size; ++index) {
        sum += (uint32_t)output[index];
    }
    if (!xx_spis_checksum_matches(member->method, flags, stored, sum, key)) {
        goto fail;
    }

    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;

fail:
    xx_mem_free(output);
    xx_mem_free(input);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_spis_init(xx_spis *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SPIS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-spis");
    xx_format_set_extension(&archive->format, "spis");
    archive->format.check_is_valid = xx_spis_check_is_valid;
    archive->format.handle_base_info = xx_spis_handle_base_info;
    archive->format.get_format_size = xx_spis_get_format_size;
    archive->format.get_number_of_archive_records = xx_spis_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_spis_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_spis_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_spis_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_spis_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_spis_free_archive_records_reading;
    archive->format.destroy = xx_spis_vtable_destroy;
}

xx_spis *xx_spis_create(xx_io_device *device, int64_t base_address)
{
    xx_spis *archive = (xx_spis *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_spis_init(archive, device, base_address);
    return archive;
}

void xx_spis_destroy(xx_spis *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_spis_free(xx_spis *archive)
{
    if (!archive) return;
    xx_spis_destroy(archive);
    xx_mem_free(archive);
}

static void xx_spis_vtable_destroy(Abstractformat *self)
{
    xx_spis_destroy((xx_spis *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_spis_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_spis_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_spis_parse(self, pd);
    if (!stream) return false;
    xx_spis_stream_free(stream);
    return true;
}

bool xx_spis_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_spis *archive = (xx_spis *)self;
    xx_spis_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_spis_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->is_complete = !stream->truncated_tail;
    xx_spis_stream_free(stream);
    return true;
}

int64_t xx_spis_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_spis_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_spis *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_spis_set_record(xx_archive_record *record, const xx_spis_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->timestamp) &&
           /* Also the packed DOS words: the listing reads TIMESTAMP as Unix seconds. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE, (member->timestamp >> 16) & 0xFFFFU) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME, member->timestamp & 0xFFFFU) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->is_folder) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_spis_copy_options(xx_list_s *target, const xx_list_s *options)
{
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) || !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_spis_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

/* A single tiny flag-2 LH5 member can bound the caller's numeric key through
 * checksum = sum(decoded bytes) + key. This is format-key recovery, not a
 * general password search. Ordinary/default and two-LZH paths remain separate.
 * The scan domain is <=65281 keys; charged byte work is <=32 MiB. */
#define XX_SPIS_SMALL_RAW 256U
#define XX_SPIS_SMALL_PACKED 4096U
#define XX_SPIS_SMALL_NAME 256U
#define XX_SPIS_SMALL_WORK (32U * 1024U * 1024U)
/* Includes conservative codec stack/workspace and resident iterator/name/
 * context overhead. Snapshot, unmasked input and output are charged separately. */
#define XX_SPIS_SMALL_FIXED_MEMORY 32768U

static bool xx_spis_small_name_safe(const char *name)
{
    const char *p = name;
    if (!xx_spis_path_safe(name)) return false;
    while (*p) {
        const char *end = p, *trim;
        while (*end && *end != '/') ++end;
        trim = end;
        while (trim > p && (trim[-1] == '.' || trim[-1] == ' ')) --trim;
        /* Win32 trims component endings; all-dot/space aliases must never
         * authorize this new extraction route. Legacy naming is unchanged. */
        if (trim == p) return false;
        p = *end ? end + 1 : end;
    }
    return true;
}

static bool xx_spis_small_limits(Abstractformat *self, xx_archive_record_state *state, const xx_spis_stream *stream)
{
    const xx_spis_member *m = &stream->items[0];
    uint64_t charge = XX_SPIS_SMALL_FIXED_MEMORY + (uint64_t)stream->archive_size +
                      (uint64_t)m->compressed_size + (uint64_t)m->uncompressed_size;
    const xx_var *v;
    /* An operation may tighten, but never raise, a format-level budget. */
    v = xx_format_resolve_extra_parameter(self, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v && charge > xx_var_get_u64(v)) return false;
    v = xx_spis_get_option(&state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v && charge > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(self, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && (uint64_t)m->uncompressed_size > xx_var_get_u64(v)) return false;
    v = xx_spis_get_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    return !v || (uint64_t)m->uncompressed_size <= xx_var_get_u64(v);
}

static bool xx_spis_small_unchanged(Abstractformat *self, const xx_spis_small_context *context, xx_pd_struct *pd)
{
    uint8_t chunk[256];
    size_t at = 0U;
    int64_t total = xx_io_total_size(self->device);
    if (self->base_address != context->base || total < context->base ||
        (uint64_t)(total - context->base) != context->size) return false;
    while (at < context->size) {
        size_t n = context->size - at;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        if ((pd && xx_pd_is_stopped(pd)) || !xx_spis_read_at(self, context->base + (int64_t)at, chunk, n) ||
            xx_rt_memcmp(chunk, context->snapshot + at, n) != 0) return false;
        at += n;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

static bool xx_spis_small_shape(Abstractformat *self, const xx_spis_stream *stream, const uint8_t *header)
{
    const xx_spis_member *m = &stream->items[0];
    const uint8_t *h = header + XX_SPIS_HEADER_SIZE;
    return xx_rt_memcmp(header, "SPIS\x1aLH5", 8U) == 0 && header[12] == 1U &&
        xx_data_get_u32(header + 8, 4, 0, false) == (uint32_t)m->uncompressed_size &&
        xx_data_get_u32(header + 17, 4, 0, false) == 2U &&
        xx_data_get_u16(h, 2, 0, false) == (uint16_t)(m->header_size - XX_SPIS_RECORD_HEADER_SIZE) &&
        xx_data_get_u32(h + 2, 4, 0, false) == m->timestamp &&
        xx_data_get_u32(h + 8, 4, 0, false) == (uint32_t)m->uncompressed_size &&
        xx_data_get_u32(h + 12, 4, 0, false) == (uint32_t)m->compressed_size && h[16] == XX_SPIS_METHOD_LH5 &&
        xx_data_get_u32(h + 21, 4, 0, false) == 2U &&
        m->header_offset == self->base_address + XX_SPIS_HEADER_SIZE &&
        m->data_offset == m->header_offset + m->header_size &&
        m->data_offset - self->base_address + m->compressed_size == stream->archive_size;
}

static bool xx_spis_small_candidate(const uint8_t *packed, size_t packed_size, uint8_t *input, uint8_t *plain,
                                    size_t raw_size, uint32_t stored, uint32_t key)
{
    size_t i, written = 0U;
    uint32_t sum = 0U;
    for (i = 0U; i < packed_size; ++i) input[i] = packed[i] ^ (uint8_t)(key >> (8U * (i & 3U)));
    if (!xx_lzh5_decode_exact_memory(input, packed_size, plain, raw_size, 5, &written) || written != raw_size) return false;
    for (i = 0U; i < raw_size; ++i) sum += plain[i];
    /* The ordinary checksum's alternative bare-sum acceptance is deliberately
     * not used to authorize a recovered key. */
    return (uint32_t)(sum + key) == stored;
}

static bool xx_spis_small_prepare(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_spis_stream *stream = (xx_spis_stream *)state->internal_state;
    const xx_spis_member *m;
    xx_spis_small_context *context = NULL;
    uint8_t header[XX_SPIS_HEADER_SIZE + XX_SPIS_RECORD_HEADER_SIZE];
    uint8_t *input = NULL, *plain = NULL, *legacy = NULL;
    size_t legacy_size = 0U;
    uint32_t stored, bound, delta;
    xx_spis_key_selection selection = {0U, 0U};
    bool result = true;
    char *name = NULL;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (stream->count != 1U || stream->truncated_tail) return true;
    m = &stream->items[0];
    if (m->is_folder || m->method != XX_SPIS_METHOD_LH5 || m->uncompressed_size <= 0 ||
        m->uncompressed_size > XX_SPIS_SMALL_RAW || m->compressed_size <= 0 || m->compressed_size > XX_SPIS_SMALL_PACKED ||
        m->header_size <= XX_SPIS_RECORD_HEADER_SIZE || m->header_size > XX_SPIS_RECORD_HEADER_SIZE + XX_SPIS_SMALL_NAME ||
        self->base_address < 0 || self->base_address > INT64_MAX - XX_SPIS_HEADER_SIZE ||
        stream->archive_size != XX_SPIS_HEADER_SIZE + m->header_size + m->compressed_size ||
        xx_io_total_size(self->device) - self->base_address != stream->archive_size) return true;
    bound = (uint32_t)m->uncompressed_size * 255U;
    if ((uint64_t)(bound + 1U) * (uint64_t)(m->compressed_size + m->uncompressed_size) > XX_SPIS_SMALL_WORK ||
        !xx_spis_small_limits(self, state, stream)) return true;
    if (!xx_spis_read_at(self, self->base_address, header, sizeof(header))) return false;
    if (!xx_spis_small_shape(self, stream, header)) return true;
    if (!xx_spis_small_name_safe(m->name)) return true;
    context = (xx_spis_small_context *)xx_mem_calloc(1U, sizeof(*context));
    if (!context) return false;
    context->base = self->base_address;
    context->size = (size_t)stream->archive_size;
    context->snapshot = (uint8_t *)xx_mem_alloc(context->size);
    if (!context->snapshot || !xx_spis_read_at(self, context->base, context->snapshot, context->size) ||
        xx_rt_memcmp(header, context->snapshot, sizeof(header)) != 0) { result = false; goto done; }
    name = xx_spis_make_name(context->snapshot + sizeof(header), (size_t)m->header_size - XX_SPIS_RECORD_HEADER_SIZE);
    if (!name || xx_rt_strcmp(name, m->name) != 0) { result = false; goto done; }
    xx_str_free(name); name = NULL;
    /* Preserve historical default-key acceptance, including its tolerant LH5
     * boundaries. Failed recovery never blocks an otherwise valid default. */
    if (xx_spis_decode(self, m, XX_SPIS_FLAG_KEY, &legacy, &legacy_size, pd)) goto done;
    if (pd && xx_pd_is_stopped(pd)) { result = false; goto done; }
    input = (uint8_t *)xx_mem_alloc((size_t)m->compressed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)m->uncompressed_size);
    if (!input || !plain) { result = false; goto done; }
    stored = xx_data_get_u32(context->snapshot + XX_SPIS_HEADER_SIZE + 17U, 4, 0, false);
    for (delta = 0U; delta <= bound; ++delta) {
        uint32_t candidate = stored - delta;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_spis_small_limits(self, state, stream)) { result = false; goto done; }
        if (xx_spis_small_candidate(context->snapshot + (size_t)(m->data_offset - self->base_address),
                (size_t)m->compressed_size, input, plain, (size_t)m->uncompressed_size, stored, candidate))
            xx_spis_key_selection_add(&selection, candidate);
    }
    /* Complete the entire domain even after a match, and reject ambiguity. */
    if (!xx_spis_small_unchanged(self, context, pd)) { result = false; goto done; }
    if (xx_spis_key_selection_unique(&selection)) {
        context->key = selection.key;
        stream->small = context;
        context = NULL;
    }
done:
    xx_str_free(name);
    xx_mem_free(legacy);
    xx_mem_free(input);
    xx_mem_free(plain);
    if (context) { xx_mem_free(context->snapshot); xx_mem_free(context); }
    return result;
}

static bool xx_spis_small_decode(Abstractformat *self, xx_archive_record_state *state, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    xx_spis_stream *s = (xx_spis_stream *)state->internal_state;
    const xx_spis_member *m = &s->items[0];
    xx_spis_small_context *c = s->small;
    uint8_t *input = NULL, *plain = NULL;
    uint32_t stored;
    bool result = false;
    *out = NULL; *out_size = 0U;
    if (!c || !xx_spis_small_name_safe(m->name) || !xx_spis_small_limits(self, state, s) || !xx_spis_small_unchanged(self, c, pd)) return false;
    input = (uint8_t *)xx_mem_alloc((size_t)m->compressed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)m->uncompressed_size);
    if (!input || !plain) goto done;
    stored = xx_data_get_u32(c->snapshot + XX_SPIS_HEADER_SIZE + 17U, 4, 0, false);
    if (!xx_spis_small_candidate(c->snapshot + (size_t)(m->data_offset - c->base), (size_t)m->compressed_size,
            input, plain, (size_t)m->uncompressed_size, stored, c->key) || !xx_spis_small_unchanged(self, c, pd)) goto done;
    *out = plain; *out_size = (size_t)m->uncompressed_size; plain = NULL; result = true;
done:
    xx_mem_free(input); xx_mem_free(plain);
    return result;
}

/* A separate bounded, single-segment multi-LH5 policy. It never changes the
 * existing single-member or two-LZH/default key policies. */
#define XX_SPIS_MULTI_MEMBERS 16U
#define XX_SPIS_MULTI_MEMBER 65536U
#define XX_SPIS_MULTI_PACKED (256U * 1024U)
#define XX_SPIS_MULTI_RAW (1024U * 1024U)
#define XX_SPIS_MULTI_SPAN (1024U * 1024U)
#define XX_SPIS_MULTI_CANDIDATES 16U
typedef struct xx_spis_multi_bounds_s {
    size_t seed, max_packed, max_raw;
    uint64_t packed, raw;
} xx_spis_multi_bounds;

static bool xx_spis_multi_bounds_get(Abstractformat *self, const xx_spis_stream *s, xx_spis_multi_bounds *b)
{
    size_t i;
    uint64_t at = XX_SPIS_HEADER_SIZE, seed_work = UINT64_MAX;
    xx_mem_zero(b, sizeof(*b)); b->seed = SIZE_MAX;
    if (s->count < 2U || s->count > XX_SPIS_MULTI_MEMBERS || s->truncated_tail || s->archive_size <= 0 ||
        s->archive_size > XX_SPIS_MULTI_SPAN || self->base_address < 0 || self->base_address > INT64_MAX - s->archive_size) return false;
    for (i = 0U; i < s->count; ++i) {
        const xx_spis_member *m = &s->items[i];
        uint64_t work;
        if (m->is_folder || m->method != XX_SPIS_METHOD_LH5 || m->uncompressed_size <= 0 ||
            m->uncompressed_size > XX_SPIS_MULTI_MEMBER || m->compressed_size <= 0 || m->compressed_size > XX_SPIS_MULTI_MEMBER ||
            m->header_size <= XX_SPIS_RECORD_HEADER_SIZE || m->header_size > XX_SPIS_RECORD_HEADER_SIZE + XX_SPIS_SMALL_NAME ||
            m->header_offset != self->base_address + (int64_t)at || m->data_offset != m->header_offset + m->header_size) return false;
        at += (uint64_t)m->header_size + (uint64_t)m->compressed_size;
        if (at > (uint64_t)s->archive_size) return false;
        b->packed += (uint64_t)m->compressed_size; b->raw += (uint64_t)m->uncompressed_size;
        if ((size_t)m->compressed_size > b->max_packed) b->max_packed = (size_t)m->compressed_size;
        if ((size_t)m->uncompressed_size > b->max_raw) b->max_raw = (size_t)m->uncompressed_size;
        if (m->uncompressed_size <= XX_SPIS_SMALL_RAW && m->compressed_size <= XX_SPIS_SMALL_PACKED) {
            work = ((uint64_t)m->uncompressed_size * 255U + 1U) * (uint64_t)(m->compressed_size + m->uncompressed_size);
            if (work < seed_work) { seed_work = work; b->seed = i; }
        }
    }
    /* Reserve one complete historical-default pass plus at most sixteen
     * authenticated seed candidates checked against every member. */
    return at == (uint64_t)s->archive_size && b->seed != SIZE_MAX && b->packed <= XX_SPIS_MULTI_PACKED && b->raw <= XX_SPIS_MULTI_RAW &&
        seed_work + (XX_SPIS_MULTI_CANDIDATES + 1U) * (b->packed + b->raw) <= XX_SPIS_SMALL_WORK;
}

static bool xx_spis_multi_limits(Abstractformat *self, xx_archive_record_state *state, const xx_spis_stream *s)
{
    xx_spis_multi_bounds b;
    const xx_var *v;
    uint64_t charge;
    if (!xx_spis_multi_bounds_get(self, s, &b)) return false;
    charge = XX_SPIS_SMALL_FIXED_MEMORY + (uint64_t)s->archive_size + b.max_packed + b.max_raw;
    v = xx_format_resolve_extra_parameter(self, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v && charge > xx_var_get_u64(v)) return false;
    v = xx_spis_get_option(&state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (v && charge > xx_var_get_u64(v)) return false;
    v = xx_format_resolve_extra_parameter(self, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (v && b.max_raw > xx_var_get_u64(v)) return false;
    v = xx_spis_get_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    return !v || b.max_raw <= xx_var_get_u64(v);
}

static bool xx_spis_multi_name_safe(const char *name)
{
    const char *p;
    if (!xx_spis_small_name_safe(name)) return false;
    /* Multiple published records need unambiguous Win32 components. Preserve
     * the older single-member route's separately tested alias behavior. */
    for (p = name; *p; ++p) if ((!p[1] || p[1] == '/') && (*p == '.' || *p == ' ')) return false;
    return true;
}

static bool xx_spis_multi_conflict(const char *a, const char *b)
{
    while (*a && *b && xx_spis_fold((unsigned char)*a) == xx_spis_fold((unsigned char)*b)) { ++a; ++b; }
    return (!*a && (!*b || *b == '/')) || (!*b && *a == '/');
}

static bool xx_spis_multi_shape(Abstractformat *self, const xx_spis_stream *s, const xx_spis_small_context *c,
                                 const xx_spis_multi_bounds *bounds, xx_pd_struct *pd)
{
    size_t i, j;
    const uint8_t *h = c->snapshot;
    if (xx_rt_memcmp(h, "SPIS\x1aLH5", 8U) != 0 || h[12] != 1U ||
        xx_data_get_u32(h + 8, 4, 0, false) != bounds->raw || xx_data_get_u32(h + 17, 4, 0, false) != 2U) return false;
    for (i = 0U; i < s->count; ++i) {
        const xx_spis_member *m = &s->items[i];
        char *name;
        bool same;
        if (pd && xx_pd_is_stopped(pd)) return false;
        h = c->snapshot + (size_t)(m->header_offset - self->base_address);
        if (xx_data_get_u16(h, 2, 0, false) != m->header_size - XX_SPIS_RECORD_HEADER_SIZE ||
            xx_data_get_u32(h + 2, 4, 0, false) != m->timestamp ||
            xx_data_get_u32(h + 8, 4, 0, false) != (uint32_t)m->uncompressed_size ||
            xx_data_get_u32(h + 12, 4, 0, false) != (uint32_t)m->compressed_size || h[16] != XX_SPIS_METHOD_LH5 ||
            xx_data_get_u32(h + 21, 4, 0, false) != 2U) return false;
        name = xx_spis_make_name(h + XX_SPIS_RECORD_HEADER_SIZE, (size_t)m->header_size - XX_SPIS_RECORD_HEADER_SIZE);
        same = name && xx_rt_strcmp(name, m->name) == 0 && xx_spis_multi_name_safe(name);
        xx_str_free(name);
        if (!same) return false; /* Includes raw duplicates renamed by legacy parsing. */
        for (j = 0U; j < i; ++j) if (xx_spis_multi_conflict(m->name, s->items[j].name)) return false;
    }
    return true;
}

static bool xx_spis_multi_candidate(const xx_spis_stream *s, const xx_spis_small_context *c, uint32_t key,
                                     uint8_t *input, uint8_t *plain, xx_archive_record_state *state, Abstractformat *self, xx_pd_struct *pd, bool *interrupted)
{
    size_t i;
    *interrupted = false;
    for (i = 0U; i < s->count; ++i) {
        const xx_spis_member *m = &s->items[i];
        uint32_t stored;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_spis_multi_limits(self, state, s)) { *interrupted = true; return false; }
        stored = xx_data_get_u32(c->snapshot + (size_t)(m->header_offset - c->base) + 17U, 4, 0, false);
        if (!xx_spis_small_candidate(c->snapshot + (size_t)(m->data_offset - c->base), (size_t)m->compressed_size,
                input, plain, (size_t)m->uncompressed_size, stored, key)) return false;
    }
    return true;
}

static bool xx_spis_multi_prepare(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_spis_stream *s = (xx_spis_stream *)state->internal_state;
    xx_spis_multi_bounds bounds;
    xx_spis_small_context *c = NULL;
    const xx_spis_member *seed;
    uint8_t *input = NULL, *plain = NULL, *legacy = NULL;
    uint8_t archive_header[XX_SPIS_HEADER_SIZE];
    size_t legacy_size = 0U, i;
    uint32_t stored, bound, delta;
    unsigned seed_matches = 0U;
    bool result = true, default_ok = true, too_many = false;
    xx_spis_key_selection selected = {0U, 0U};
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!xx_spis_multi_bounds_get(self, s, &bounds) || !xx_spis_multi_limits(self, state, s) ||
        xx_io_total_size(self->device) - self->base_address != s->archive_size) return true;
    if (!xx_spis_read_at(self, self->base_address, archive_header, sizeof(archive_header))) return false;
    if (xx_rt_memcmp(archive_header, "SPIS\x1aLH5", 8U) != 0 || archive_header[12] != 1U ||
        xx_data_get_u32(archive_header + 8, 4, 0, false) != bounds.raw ||
        xx_data_get_u32(archive_header + 17, 4, 0, false) != 2U) return true;
    /* Check the masked-only scope using small header reads before allocating
     * a potentially larger snapshot. Captured shape is verified again below. */
    for (i = 0U; i < s->count; ++i) {
        uint32_t flags, checksum;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_spis_member_guard(self, &s->items[i], &flags, &checksum)) return false;
        if (flags != 2U) return true;
    }
    c = (xx_spis_small_context *)xx_mem_calloc(1U, sizeof(*c));
    if (!c) return false;
    c->base = self->base_address; c->size = (size_t)s->archive_size;
    c->snapshot = (uint8_t *)xx_mem_alloc(c->size);
    if (!c->snapshot || !xx_spis_read_at(self, c->base, c->snapshot, c->size)) { result = false; goto done; }
    if (!xx_spis_multi_shape(self, s, c, &bounds, pd)) { if (pd && xx_pd_is_stopped(pd)) result = false; goto done; }
    /* A complete successful default-key pass keeps all legacy behavior. */
    for (i = 0U; i < s->count; ++i) {
        if (!xx_spis_multi_limits(self, state, s) || (pd && xx_pd_is_stopped(pd))) { result = false; goto done; }
        default_ok = xx_spis_decode(self, &s->items[i], XX_SPIS_FLAG_KEY, &legacy, &legacy_size, pd);
        xx_mem_free(legacy); legacy = NULL;
        if (!default_ok) break;
    }
    if (pd && xx_pd_is_stopped(pd)) { result = false; goto done; }
    if (default_ok) goto done;
    input = (uint8_t *)xx_mem_alloc(bounds.max_packed);
    plain = (uint8_t *)xx_mem_alloc(bounds.max_raw);
    if (!input || !plain) { result = false; goto done; }
    seed = &s->items[bounds.seed]; bound = (uint32_t)seed->uncompressed_size * 255U;
    stored = xx_data_get_u32(c->snapshot + (size_t)(seed->header_offset - c->base) + 17U, 4, 0, false);
    for (delta = 0U; delta <= bound; ++delta) {
        uint32_t key = stored - delta;
        bool interrupted;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_spis_multi_limits(self, state, s)) { result = false; goto done; }
        if (!xx_spis_small_candidate(c->snapshot + (size_t)(seed->data_offset - c->base), (size_t)seed->compressed_size,
                input, plain, (size_t)seed->uncompressed_size, stored, key)) continue;
        if (++seed_matches > XX_SPIS_MULTI_CANDIDATES) { too_many = true; continue; }
        if (xx_spis_multi_candidate(s, c, key, input, plain, state, self, pd, &interrupted)) xx_spis_key_selection_add(&selected, key);
        if (interrupted) { result = false; goto done; }
        if (pd && xx_pd_is_stopped(pd)) { result = false; goto done; }
    }
    if (!xx_spis_multi_limits(self, state, s) || !xx_spis_small_unchanged(self, c, pd)) { result = false; goto done; }
    if (!too_many && xx_spis_key_selection_unique(&selected)) { c->key = selected.key; s->multi = c; c = NULL; }
done:
    xx_mem_free(legacy); xx_mem_free(input); xx_mem_free(plain);
    if (c) { xx_mem_free(c->snapshot); xx_mem_free(c); }
    return result;
}

static bool xx_spis_multi_decode(Abstractformat *self, xx_archive_record_state *state, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    xx_spis_stream *s = (xx_spis_stream *)state->internal_state;
    const xx_spis_member *m = &s->items[s->index];
    xx_spis_small_context *c = s->multi;
    uint8_t *input = NULL, *plain = NULL;
    uint32_t stored;
    bool result = false;
    *out = NULL; *out_size = 0U;
    if (!c || !xx_spis_multi_limits(self, state, s) || !xx_spis_small_unchanged(self, c, pd)) return false;
    input = (uint8_t *)xx_mem_alloc((size_t)m->compressed_size); plain = (uint8_t *)xx_mem_alloc((size_t)m->uncompressed_size);
    if (!input || !plain) goto done;
    stored = xx_data_get_u32(c->snapshot + (size_t)(m->header_offset - c->base) + 17U, 4, 0, false);
    if (!xx_spis_small_candidate(c->snapshot + (size_t)(m->data_offset - c->base), (size_t)m->compressed_size,
            input, plain, (size_t)m->uncompressed_size, stored, c->key) || !xx_spis_small_unchanged(self, c, pd)) goto done;
    *out = plain; *out_size = (size_t)m->uncompressed_size; plain = NULL; result = true;
done:
    xx_mem_free(input); xx_mem_free(plain); return result;
}

static bool xx_spis_verified_limits(Abstractformat *self, xx_archive_record_state *state, const xx_spis_stream *s)
{
    return s->multi ? xx_spis_multi_limits(self, state, s) : xx_spis_small_limits(self, state, s);
}
static xx_spis_small_context *xx_spis_verified_context(const xx_spis_stream *s) { return s->multi ? s->multi : s->small; }

static bool xx_spis_small_stage_alias(const char *target, const char *suffix)
{
    const char *base = target, *p;
    size_t n, i;
    for (p = target; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    n = (size_t)(p - base);
    /* Short Win32 paths fold case and trim final dots/spaces. Avoid publishing
     * a temporary directly through a member alias of our staging basename. */
    while (n && (base[n - 1U] == '.' || base[n - 1U] == ' ')) --n;
    if (n != xx_rt_strlen(suffix)) return false;
    for (i = 0U; i < n; ++i) if (xx_spis_fold((unsigned char)base[i]) != xx_spis_fold((unsigned char)suffix[i])) return false;
    return true;
}

static bool xx_spis_small_publish(Abstractformat *self, xx_archive_record_state *state, const char *target,
                                  const uint8_t *plain, size_t size, xx_pd_struct *pd)
{
    xx_spis_stream *s = (xx_spis_stream *)state->internal_state;
    const xx_var *v = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    bool overwrite = v && xx_var_get_bool(v), result = false;
    char *parent = NULL, *stage = NULL;
    size_t i, cut = 0U, completed = 0U;
    unsigned attempt;
    xx_io_device *output = NULL;
    if ((!overwrite && xx_io_file_exists_a(target)) || !xx_spis_verified_limits(self, state, s) ||
        !xx_spis_small_unchanged(self, xx_spis_verified_context(s), pd)) return false;
    parent = xx_str_dup(target);
    if (!parent || !xx_store_create_dirs_a(target, false)) goto done;
    for (i = 0U; parent[i]; ++i) if (parent[i] == '/' || parent[i] == '\\') cut = i + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40];
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_spis.tmp.%u", attempt);
        stage = xx_str_concat(parent, suffix);
        if (!stage) goto done;
        /* A member whose name is itself the staging basename must not alias. */
        if (!xx_spis_small_stage_alias(target, suffix)) output = xx_io_file_open(stage, "wbx");
        if (output) break;
        xx_str_free(stage); stage = NULL;
    }
    if (!output) goto done;
    while (completed < size) {
        ssize_t n;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        n = xx_io_write(output, plain + completed, size - completed);
        if (n <= 0 || (size_t)n > size - completed) goto done;
        completed += (size_t)n;
    }
    { int closed = xx_io_close(output); output = NULL; if (closed != 0) goto done; }
    if (!xx_spis_verified_limits(self, state, s) || !xx_spis_small_unchanged(self, xx_spis_verified_context(s), pd)) goto done;
    result = xx_io_file_replace_a(stage, target, overwrite);
done:
    if (output) xx_io_close(output);
    if (stage) { if (!result) xx_rt_remove(stage); xx_str_free(stage); }
    xx_str_free(parent);
    return result;
}

xx_archive_record_state *xx_spis_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_spis_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_spis_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_spis_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_spis_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_spis_copy_options(&state->options, options) || !xx_spis_small_prepare(self, state, pd) || !xx_spis_multi_prepare(self, state, pd) || (stream->count != 0U && !xx_spis_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_spis_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    xx_spis_stream *stream;
    if (!self || !state || state->format != self || !state->has_record) return NULL;
    stream = (xx_spis_stream *)state->internal_state;
    if (stream && stream->truncated_tail && !xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_COMMENT,
            "Truncated SPIS tail; only complete members with verified decoded checksums can be extracted")) return NULL;
    return &state->current_record;
}

bool xx_spis_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_spis_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_spis_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_spis_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_spis_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_spis_stream *stream;
    const xx_spis_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_spis_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_spis_path_safe(member->name)) return false;
    if (!stream->small && !stream->multi && !xx_spis_prepare_key(self, stream, pd)) return false;

    path_option = (stream->small || stream->multi) ? xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH) :
                                 xx_spis_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = stream->multi ? xx_spis_multi_decode(self, state, &plain, &plain_size, pd) :
                 stream->small ? xx_spis_small_decode(self, state, &plain, &plain_size, pd) :
                                 xx_spis_decode(self, member, stream->key, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
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
    if (stream->small || stream->multi) {
        if (stream->multi ? xx_spis_multi_decode(self, state, &plain, &plain_size, pd) :
                            xx_spis_small_decode(self, state, &plain, &plain_size, pd))
            result = xx_spis_small_publish(self, state, target_path, plain, plain_size, pd);
        xx_mem_free(plain); xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) || !xx_spis_decode(self, member, stream->key, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_spis_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
