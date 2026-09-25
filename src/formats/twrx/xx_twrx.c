/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TWRX installer container (*.TZF): a chain of self-describing "TWRX" blocks
 * that tiles the file.  xx_twrx.h carries the field table and the evidence.
 *
 * Three methods occur and all three are decoded:
 *   0  stored;
 *   6  a bare PKWARE DCL (explode) stream;
 *   8  a six-byte prefix (u16 8, u32 stream length) and then raw Deflate.
 * The high dword of the block's 8-byte stamp is, when non-zero, a reflected
 * CRC-32 (poly 0xEDB88320, seed 0, no final complement) of the member's
 * packed bytes; it is verified before anything is decoded.
 *
 * Both coded streams must occupy their payload exactly: no trailing bytes,
 * no read past its end.  Only the packed bytes are ever held in memory for a
 * stored or Deflate member (Deflate is decoded straight into the output);
 * a DCL member is measured first and only a stream that really decodes to
 * the claimed size gets an output buffer.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/twrx/xx_twrx.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as TWRX is registered there. */
#ifdef TWRX
#define XX_TWRX_FILE_TYPE XX_FILE_TYPE_TWRX
#else
#define XX_TWRX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define TWRX_BLOCK_HEADER_SIZE 0x1e
#define TWRX_VERSION 0x0100U
#define TWRX_METHOD_STORED 0U
#define TWRX_METHOD_DCL 6U
#define TWRX_METHOD_DEFLATE 8U
#define TWRX_DEFLATE_PREFIX 6U
#define TWRX_MAX_NAME 255U
#define TWRX_MAX_ENTRIES 65536U
#define TWRX_MAX_MEMBER ((int64_t)512 * 1024 * 1024)
/* Upper bounds on what a stream can expand to, used only to refuse an
 * allocation a hostile header asks for.  Deflate peaks at 1032:1 (258 bytes
 * per two-bit match); a DCL match costs at least 22 bits for 518 bytes. */
#define TWRX_DEFLATE_MAX_RATIO 1032U
#define TWRX_DCL_MAX_RATIO 256U
/* Zero bytes kept after the packed buffer.  The library's Deflate bit reader
 * answers a read past its input with zero bits and no error, so the stream
 * is decoded over the payload plus this guard: a stream that is complete
 * never touches it, and one that reads into it (a truncated stored block,
 * say) reports a consumed length past the payload and is refused.  The guard
 * is wider than the reader's 64-bit cache, so even running past the guard
 * cannot bring the consumed count back to the payload's end. */
#define TWRX_DEFLATE_GUARD 16U

typedef struct twrx_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint32_t tag;
    uint32_t check;
    uint16_t method;
    bool unsafe_name;
} twrx_member;

typedef struct twrx_stream_s {
    twrx_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} twrx_stream;

/* Where a Deflate member decodes to: the output file, or nowhere when the
 * member is only being verified.  Either way no more than `limit` bytes are
 * accepted, so a stream that overshoots its claimed size stops at once. */
typedef struct twrx_sink_s {
    xx_io_device *target;
    size_t limit;
    size_t size;
} twrx_sink;

/* CP437, 0x80..0xFF.  The names are DOS names; the only high byte in the
 * reference corpus is 0xF6, the producer's part marker, which is U+00F7. */
static const uint16_t twrx_cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

static uint16_t twrx_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t twrx_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool twrx_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Reflected CRC-32 with a zero seed and no final complement.  xx_crc32_calc
 * complements on entry and on exit, so seeding it with ~0 and complementing
 * its result cancels both. */
static uint32_t twrx_crc(const uint8_t *data, size_t size) {
    return ~xx_crc32_calc(0xFFFFFFFFU, data, size);
}

static size_t twrx_put_utf8(char *out, uint16_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

static bool twrx_same_upper(const char *text, const char *upper,
                            size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        char c = text[index];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != upper[index]) return false;
    }
    return true;
}

/* Whether one UTF-8 path component names a Windows device: CON, PRN, AUX,
 * NUL, COM0-9, LPT0-9 (also with a superscript 1, 2 or 3), CONIN$, CONOUT$
 * or CLOCK$.  Windows resolves such a name to the device whatever folder it
 * sits in and whatever extension follows it, and ignores spaces before the
 * extension, so only the part before the first '.' is compared. */
static bool twrx_is_device_name(const char *component, size_t length) {
    static const char *const plain[] = {"CON", "PRN", "AUX", "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t base = 0U, index;
    const unsigned char *bytes = (const unsigned char *)component;
    if (!component) return false;
    while (base < length && component[base] != '.') ++base;
    while (base != 0U && component[base - 1U] == ' ') --base;
    for (index = 0U; index < sizeof(plain) / sizeof(plain[0]); ++index)
        if (base == xx_str_len(plain[index]) &&
            twrx_same_upper(component, plain[index], base))
            return true;
    if (base < 4U || (!twrx_same_upper(component, "COM", 3U) &&
                      !twrx_same_upper(component, "LPT", 3U)))
        return false;
    if (base == 4U) return bytes[3] >= '0' && bytes[3] <= '9';
    /* U+00B9, U+00B2, U+00B3 in UTF-8; CP437 0xFD becomes U+00B2. */
    return base == 5U && bytes[3] == 0xC2U &&
           (bytes[4] == 0xB9U || bytes[4] == 0xB2U || bytes[4] == 0xB3U);
}

/* Builds the UTF-8 display name and flags a name that must not be extracted:
 * absolute, drive-qualified, containing a ".." component, a control
 * character or a Windows device name.  Separators become '/', empty and "."
 * components are dropped, characters a path may not hold become '_'.  An
 * unsafe name is still listed, with its ".." components removed, but unpack
 * refuses it. */
static char *twrx_make_name(const uint8_t *bytes, size_t size, bool *unsafe) {
    char *name;
    size_t input = 0U, output = 0U;
    if (!unsafe || (!bytes && size != 0U) || size > (SIZE_MAX - 2U) / 3U)
        return NULL;
    *unsafe = false;
    if (size != 0U && (bytes[0] == '/' || bytes[0] == '\\')) *unsafe = true;
    if (size >= 2U && bytes[1] == ':') *unsafe = true;
    name = (char *)xx_mem_alloc(size * 3U + 2U);
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
            *unsafe = true;
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == 0x7FU) {
                *unsafe = true;
                name[output++] = '_';
            } else if (c == ':') {
                *unsafe = true;
                name[output++] = '_';
            } else if (c == '"' || c == '*' || c == '<' || c == '>' ||
                       c == '?' || c == '|') {
                name[output++] = '_';
            } else if (c >= 0x80U) {
                output += twrx_put_utf8(name + output,
                                        twrx_cp437_high[c - 0x80U]);
            } else {
                name[output++] = (char)c;
            }
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
        if (twrx_is_device_name(name + component_start,
                                output - component_start))
            *unsafe = true;
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

/* Last line of defence on the path actually joined to the output folder. */
static bool twrx_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.') ||
                twrx_is_device_name(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void twrx_stream_free(void *opaque) {
    twrx_stream *stream = (twrx_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool twrx_add_member(twrx_stream *stream, const twrx_member *member) {
    if (!stream || !member || stream->count >= (size_t)TWRX_MAX_ENTRIES)
        return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity == 0U ? 16U : stream->capacity * 2U;
        twrx_member *grown;
        if (capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (twrx_member *)(stream->items
                                    ? xx_mem_realloc(stream->items,
                                                     capacity * sizeof(*grown))
                                    : xx_mem_alloc(capacity * sizeof(*grown)));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static bool twrx_parse(Abstractformat *format, twrx_stream **result) {
    uint8_t header[TWRX_BLOCK_HEADER_SIZE];
    uint8_t raw_name[TWRX_MAX_NAME];
    twrx_stream *stream = NULL;
    int64_t total, size, cursor = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)TWRX_BLOCK_HEADER_SIZE + 1) return false;
    stream = (twrx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    while (cursor < size) {
        twrx_member member;
        int64_t packed, unpacked, name_length, data_offset;
        if (size - cursor < (int64_t)TWRX_BLOCK_HEADER_SIZE ||
            !twrx_read_at(format->device, format->base_address + cursor,
                          header, sizeof(header))) goto fail;
        if (xx_rt_memcmp(header, "TWRX", 4U) != 0) goto fail;
        /* Only one version word exists; anything else is a layout this
         * reader has never been validated against. */
        if (twrx_le16(header + 4U) != TWRX_VERSION) goto fail;
        if (twrx_le16(header + 6U) != 0U) goto fail;
        packed = (int64_t)twrx_le32(header + 0x12U);
        unpacked = (int64_t)twrx_le32(header + 0x16U);
        name_length = (int64_t)twrx_le32(header + 0x1aU);
        if (name_length <= 0 || name_length > (int64_t)TWRX_MAX_NAME)
            goto fail;
        if (packed > TWRX_MAX_MEMBER || unpacked > TWRX_MAX_MEMBER) goto fail;
        if (name_length > size - cursor - (int64_t)TWRX_BLOCK_HEADER_SIZE)
            goto fail;
        data_offset = cursor + (int64_t)TWRX_BLOCK_HEADER_SIZE + name_length;
        if (packed > size - data_offset) goto fail;
        if (!twrx_read_at(format->device,
                          format->base_address + cursor +
                              TWRX_BLOCK_HEADER_SIZE,
                          raw_name, (size_t)name_length)) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.method = twrx_le16(header + 8U);
        /* A stored member that disagrees with itself about its own length is
         * not a stored member.  This holds for every stored member of the
         * corpus. */
        if (member.method == TWRX_METHOD_STORED && packed != unpacked)
            goto fail;
        member.name = twrx_make_name(raw_name, (size_t)name_length,
                                     &member.unsafe_name);
        if (!member.name) goto fail;
        member.tag = twrx_le32(header + 0x0aU);
        member.check = twrx_le32(header + 0x0eU);
        member.header_offset = format->base_address + cursor;
        member.data_offset = format->base_address + data_offset;
        member.packed_size = packed;
        member.unpacked_size = unpacked;
        if (!twrx_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
        /* Every block consumes at least its own header, so the walk always
         * advances and the loop always terminates. */
        cursor = data_offset + packed;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    twrx_stream_free(stream);
    return false;
}

static bool twrx_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *twrx_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool twrx_set_record(xx_archive_record *record,
                            const twrx_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static ssize_t twrx_sink_write(xx_io_device *device, const void *data,
                               size_t size) {
    twrx_sink *sink = device ? (twrx_sink *)device->priv : NULL;
    size_t done = 0U;
    /* limit never exceeds TWRX_MAX_MEMBER, so size fits ssize_t here. */
    if (!sink || (!data && size != 0U) || size > sink->limit - sink->size)
        return -1;
    while (sink->target && done < size) {
        ssize_t amount = xx_io_write(sink->target, (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) return -1;
        done += (size_t)amount;
    }
    sink->size += size;
    return (ssize_t)size;
}

/* Whether a member's claimed size is one its method could reach from its
 * packed size.  Checked before anything is read or allocated. */
static bool twrx_expansion_possible(const twrx_member *member) {
    uint64_t packed = (uint64_t)member->packed_size;
    uint64_t unpacked = (uint64_t)member->unpacked_size;
    switch (member->method) {
    case TWRX_METHOD_STORED:
        return unpacked == packed;
    case TWRX_METHOD_DCL:
        return unpacked <= packed * TWRX_DCL_MAX_RATIO;
    case TWRX_METHOD_DEFLATE:
        return packed > TWRX_DEFLATE_PREFIX &&
               unpacked <= (packed - TWRX_DEFLATE_PREFIX) *
                               TWRX_DEFLATE_MAX_RATIO;
    default:
        return false;
    }
}

/* Method 8: "08 00", a u32 that repeats the rest of the payload's length,
 * then one raw Deflate stream that must end exactly at the payload's end and
 * produce exactly the unpacked size.  `packed` must be followed by
 * TWRX_DEFLATE_GUARD zero bytes; the output goes to `target`, or nowhere
 * when it is NULL. */
static bool twrx_inflate(const uint8_t *packed, size_t packed_size,
                         xx_io_device *target, size_t plain_size,
                         xx_pd_struct *pd) {
    xx_io_device device;
    twrx_sink sink;
    size_t stream_size, consumed = 0U;
    if (packed_size <= TWRX_DEFLATE_PREFIX ||
        twrx_le16(packed) != TWRX_METHOD_DEFLATE ||
        (size_t)twrx_le32(packed + 2U) != packed_size - TWRX_DEFLATE_PREFIX)
        return false;
    stream_size = packed_size - TWRX_DEFLATE_PREFIX;
    xx_mem_zero(&device, sizeof(device));
    sink.target = target;
    sink.limit = plain_size;
    sink.size = 0U;
    device.write = twrx_sink_write;
    device.priv = &sink;
    return xx_deflate_unpack_memory_to_device_ex(
               packed + TWRX_DEFLATE_PREFIX,
               stream_size + TWRX_DEFLATE_GUARD, &device, &consumed, false,
               pd) &&
           consumed == stream_size && sink.size == plain_size;
}

/* Method 6, first step: whether the payload is exactly one DCL stream,
 * header bytes included, that decodes to exactly `plain_size` bytes and ends
 * on the payload's last byte.  The scan keeps only its sliding window, so a
 * hostile size claim costs no allocation. */
static bool twrx_dcl_measure(const uint8_t *packed, size_t packed_size,
                             size_t plain_size) {
    size_t consumed = 0U, produced = 0U;
    /* The scan refuses a zero limit; an empty member's stream must then hold
     * nothing but the end code. */
    return packed_size >= 3U &&
           xx_dcl_scan_memory(packed, packed_size,
                              plain_size != 0U ? plain_size : 1U, &consumed,
                              &produced) &&
           produced == plain_size && consumed == packed_size;
}

/* Method 6, second step, after twrx_dcl_measure: the decode proper. */
static bool twrx_explode(const uint8_t *packed, size_t packed_size,
                         uint8_t *plain, size_t plain_size) {
    size_t written = 0U;
    if (plain_size == 0U) return true;
    return xx_dcl_decode_memory(packed, packed_size, plain, plain_size,
                                &written) &&
           written == plain_size;
}

void xx_twrx_init(xx_twrx *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TWRX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-twrx");
    xx_format_set_extension(&archive->format, "tzf");
    archive->format.check_is_valid = xx_twrx_check_is_valid;
    archive->format.handle_base_info = xx_twrx_handle_base_info;
    archive->format.get_format_size = xx_twrx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_twrx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_twrx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_twrx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_twrx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_twrx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_twrx_free_archive_records_reading;
}

xx_twrx *xx_twrx_create(xx_io_device *device, int64_t base_address) {
    xx_twrx *archive = (xx_twrx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_twrx_init(archive, device, base_address);
    return archive;
}

void xx_twrx_destroy(xx_twrx *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_twrx_free(xx_twrx *archive) {
    if (!archive) return;
    xx_twrx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_twrx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    twrx_stream *stream;
    (void)pd;
    if (!twrx_parse(format, &stream)) return false;
    twrx_stream_free(stream);
    return true;
}

bool xx_twrx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    twrx_stream *stream;
    xx_twrx *archive;
    (void)pd;
    if (!format || !twrx_parse(format, &stream)) return false;
    archive = (xx_twrx *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    twrx_stream_free(stream);
    return true;
}

int64_t xx_twrx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_twrx_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_twrx_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_twrx_handle_base_info(format, pd))
               ? ((xx_twrx *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_twrx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    twrx_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!twrx_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        twrx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = twrx_stream_free;
    state->total_records = stream->count;
    if (!twrx_copy_options(&state->options, options) ||
        !twrx_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_twrx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_twrx_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    twrx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (twrx_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = twrx_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_twrx_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    twrx_stream *stream;
    twrx_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size, plain_size, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (twrx_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->unsafe_name || !twrx_safe_output_name(member->name))
        return false;
    /* Sizes were capped at TWRX_MAX_MEMBER by the walk; an unknown method or
     * an impossible expansion is refused before anything is allocated. */
    if (member->packed_size < 0 || member->unpacked_size < 0 ||
        member->packed_size > TWRX_MAX_MEMBER ||
        member->unpacked_size > TWRX_MAX_MEMBER ||
        (uint64_t)member->packed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->unpacked_size > (uint64_t)SIZE_MAX ||
        !twrx_expansion_possible(member))
        return false;
    /* A DCL member is the only one with two buffers; together they stay
     * within the per-member cap.  Both sizes are capped, so this sum fits. */
    if (member->method == TWRX_METHOD_DCL &&
        member->packed_size + member->unpacked_size > TWRX_MAX_MEMBER)
        return false;
    packed_size = (size_t)member->packed_size;
    plain_size = (size_t)member->unpacked_size;
    /* The packed size is bounded by the file itself, so this allocation is
     * never larger than the input; the tail is the Deflate guard. */
    packed = (uint8_t *)xx_mem_alloc(packed_size + TWRX_DEFLATE_GUARD);
    if (!packed) return false;
    xx_rt_memset(packed + packed_size, 0, TWRX_DEFLATE_GUARD);
    if (packed_size != 0U &&
        !twrx_read_at(format->device, member->data_offset, packed,
                      packed_size))
        goto done;
    /* A zero check dword means the producer stored none. */
    if (member->check != 0U && twrx_crc(packed, packed_size) != member->check)
        goto done;
    if (member->method == TWRX_METHOD_STORED) {
        if (plain_size != packed_size) goto done;
        plain = packed;
        packed = NULL;
    } else if (member->method == TWRX_METHOD_DCL) {
        /* Measured before the output buffer exists: only a stream that
         * really decodes to the claimed size is given one. */
        if (!twrx_dcl_measure(packed, packed_size, plain_size)) goto done;
        plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
        if (!plain ||
            !twrx_explode(packed, packed_size, plain, plain_size))
            goto done;
        xx_mem_free(packed);
        packed = NULL;
    } else {
        /* Deflate is verified into no buffer at all; the bytes are produced
         * again, straight into the file, once it is known to be sound. */
        if (!twrx_inflate(packed, packed_size, NULL, plain_size, pd))
            goto done;
    }
    path_option = twrx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        if (member->method == TWRX_METHOD_DEFLATE)
            result = twrx_inflate(packed, packed_size, destination, plain_size,
                                  pd);
        while (plain && written < plain_size) {
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
    if (!result && path && created) xx_rt_remove(path);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_twrx_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
