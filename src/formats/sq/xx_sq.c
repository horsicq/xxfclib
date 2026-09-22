/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SQ, the single-file container whose signature is 53 51 AC AE.
 *
 * This is NOT the CP/M Greenlaw squeeze (magic 0xFF76, the squeeze1 reader),
 * NOT Squeeze II (magic 0xFFFA, the squeeze2 reader) and NOT the Oracle
 * squeeze; it is a separate DOS-era container that happens to share the two
 * ASCII letters, and its codec is unrelated to all three.
 *
 * Recovered from U3 archive/609 (class tib, VMT 0x00682b68): the recognition
 * predicate is FUN_00682be0, the open handler FUN_00682dc0, the calendar
 * unpacker FUN_00682d50 and the decoder FUN_0059d670 with its helpers
 * FUN_0059cf50 (table init), FUN_0059d5a0 (symbol decode), FUN_0059d1c0 and
 * FUN_0059d080 (tree update) and FUN_0059d410 (bit reader).  The port is
 * verified byte for byte against U3's own output on all four corpus
 * samples.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sq/xx_sq.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef SQ
#define XX_SQ_FILE_TYPE XX_FILE_TYPE_SQ
#else
#define XX_SQ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SQ_MAX_MEMBERS 16U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct sq_member_s {
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
} sq_member;

typedef struct sq_stream_s {
    sq_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} sq_stream;

static uint16_t sq_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t sq_le32(const uint8_t *b) {
    return (uint32_t)sq_le16(b) | ((uint32_t)sq_le16(b + 2U) << 16U);
}

static uint64_t sq_le64(const uint8_t *b) {
    return (uint64_t)sq_le32(b) | ((uint64_t)sq_le32(b + 4U) << 32U);
}

static uint32_t sq_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t sq_be64(const uint8_t *b) {
    return ((uint64_t)sq_be32(b) << 32U) | (uint64_t)sq_be32(b + 4U);
}

static bool sq_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool sq_write_all(xx_io_device *device, const void *data, size_t size,
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
static bool sq_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!sq_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool sq_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!sq_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *sq_make_name(const char *prefix, int64_t index,
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
static char *sq_clean_name(const uint8_t *bytes, size_t size) {
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

static bool sq_safe_output_name(const char *name) {
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

static void sq_stream_free(void *opaque) {
    sq_stream *stream = (sq_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool sq_add_member(sq_stream *stream, const sq_member *member) {
    sq_member *grown;
    if (!stream || !member || stream->count >= SQ_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (sq_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define SQ_MAGIC_SIZE 4U
#define SQ_STAMP_SIZE 6U
#define SQ_MAX_NAME 128U
#define SQ_MAX_INPUT ((uint64_t)64U * 1024U * 1024U)
#define SQ_MAX_OUTPUT ((uint64_t)512U * 1024U * 1024U)

/* Coding constants, all recovered from U3's decoder at 0x0059d670 and the
 * table initialiser at 0x0059cf50.
 *
 * The alphabet is 629 symbols: 256 literals, an end marker at 0x100 and then
 * 6 * 62 match symbols.  Match symbol s gives length ((s - 0x101) % 62) + 3
 * and distance slot (s - 0x101) / 62; the slot contributes 4, 6, 8, 10, 12 or
 * 14 extra bits over the bases below, and the MATCH LENGTH IS FOLDED INTO THE
 * DISTANCE -- the copy source is pos - (extra + length + base), not
 * pos - distance.  That last detail is the one a reader cannot guess. */
#define SQ_SYMBOLS 0x275U
#define SQ_NODES 0x4eaU /* 2 * SQ_SYMBOLS, the complete-tree node count */
#define SQ_END_SYMBOL 0x100U
#define SQ_LENGTHS 62U
#define SQ_SLOTS 6U
#define SQ_WINDOW 0x8000U
#define SQ_RESCALE 2000U

typedef struct sq_sink_s {
    xx_io_device *destination;
    uint64_t produced;
    xx_pd_struct *pd;
} sq_sink;

/* The adaptive Huffman tree.  Node 1 is the root, node n's children are 2n
 * and 2n + 1 at init, and nodes SQ_SYMBOLS..SQ_NODES-1 are the leaves, so
 * leaf node L stands for symbol L - SQ_SYMBOLS.  The tree then reshapes
 * itself after every symbol. */
typedef struct sq_decoder_s {
    uint16_t freq[SQ_NODES];
    uint16_t parent[SQ_NODES];
    uint16_t left[SQ_SYMBOLS];
    uint16_t right[SQ_SYMBOLS];
    uint16_t dist_base[SQ_SLOTS];
    uint8_t dist_bits[SQ_SLOTS];
    const uint8_t *input;
    size_t input_size;
    size_t position;
    uint8_t bit_buffer;
    uint8_t bits_left;
    uint8_t window[SQ_WINDOW];
    uint32_t window_position;
} sq_decoder;

static void sq_decoder_init(sq_decoder *decoder, const uint8_t *input,
                            size_t input_size) {
    uint32_t at;
    uint32_t running = 0U;
    xx_mem_zero(decoder, sizeof(*decoder));
    for (at = 2U; at < SQ_NODES; ++at) {
        decoder->freq[at] = 1U;
        decoder->parent[at] = (uint16_t)(at >> 1U);
    }
    for (at = 1U; at < SQ_SYMBOLS; ++at) {
        decoder->left[at] = (uint16_t)(at * 2U);
        decoder->right[at] = (uint16_t)(at * 2U + 1U);
    }
    for (at = 0U; at < SQ_SLOTS; ++at) {
        decoder->dist_bits[at] = (uint8_t)(at * 2U + 4U);
        decoder->dist_base[at] = (uint16_t)running;
        running += 1U << decoder->dist_bits[at];
    }
    decoder->input = input;
    decoder->input_size = input_size;
}

/* One bit, MSB first within each byte. */
static int sq_read_bit(sq_decoder *decoder) {
    unsigned bit;
    if (decoder->bits_left == 0U) {
        if (decoder->position >= decoder->input_size) return -1;
        decoder->bit_buffer = decoder->input[decoder->position++];
        decoder->bits_left = 8U;
    }
    bit = (unsigned)(decoder->bit_buffer >> 7U);
    decoder->bit_buffer = (uint8_t)(decoder->bit_buffer << 1U);
    --decoder->bits_left;
    return (int)bit;
}

static int64_t sq_read_bits(sq_decoder *decoder, unsigned count) {
    uint32_t value = 0U;
    uint32_t mask = 1U;
    while (count-- != 0U) {
        int bit = sq_read_bit(decoder);
        if (bit < 0) return -1;
        if (bit != 0) value |= mask;
        mask <<= 1U;
    }
    return (int64_t)value;
}

/* Push the new weight of `node` (paired with `sibling`) up to the root, then
 * halve every weight once the root reaches SQ_RESCALE. */
static void sq_propagate(sq_decoder *decoder, uint32_t node,
                         uint32_t sibling) {
    for (;;) {
        uint32_t previous = node;
        node = decoder->parent[node];
        decoder->freq[node] =
            (uint16_t)(decoder->freq[previous] + decoder->freq[sibling]);
        if (node == 1U) break;
        {
            uint32_t grandparent = decoder->parent[node];
            sibling = decoder->left[grandparent];
            if (sibling == node) sibling = decoder->right[grandparent];
        }
    }
    if (decoder->freq[1] == SQ_RESCALE) {
        uint32_t at;
        for (at = 1U; at < SQ_NODES; ++at)
            decoder->freq[at] = (uint16_t)(decoder->freq[at] >> 1U);
    }
}

/* After a symbol is emitted its weight rises; while it outweighs its uncle the
 * two are swapped, which is what keeps the code lengths close to optimal. */
static void sq_update(sq_decoder *decoder, uint32_t node) {
    uint32_t parent, sibling, walk;
    ++decoder->freq[node];
    parent = decoder->parent[node];
    if (parent == 1U) return;
    sibling = decoder->left[parent];
    if (sibling == node) sibling = decoder->right[parent];
    sq_propagate(decoder, node, sibling);
    walk = parent;
    do {
        uint32_t grandparent = decoder->parent[walk];
        uint32_t first = decoder->left[grandparent];
        uint32_t uncle = first;
        if (walk == first) uncle = decoder->right[grandparent];
        if (decoder->freq[uncle] < decoder->freq[node]) {
            uint32_t moved;
            if (walk == first)
                decoder->right[grandparent] = (uint16_t)node;
            else
                decoder->left[grandparent] = (uint16_t)node;
            moved = decoder->left[walk];
            if (node == moved) {
                moved = decoder->right[walk];
                decoder->left[walk] = (uint16_t)uncle;
            } else {
                decoder->right[walk] = (uint16_t)uncle;
            }
            decoder->parent[uncle] = (uint16_t)walk;
            decoder->parent[node] = (uint16_t)grandparent;
            sq_propagate(decoder, uncle, moved);
            node = uncle;
        }
        node = decoder->parent[node];
        walk = decoder->parent[node];
    } while (walk != 1U);
}

static int32_t sq_decode_symbol(sq_decoder *decoder) {
    uint32_t node = 1U;
    uint32_t guard = 0U;
    do {
        int bit = sq_read_bit(decoder);
        if (bit < 0 || ++guard > SQ_NODES) return -1;
        node = bit != 0 ? decoder->right[node] : decoder->left[node];
        if (node == 0U || node >= SQ_NODES) return -1;
    } while (node < SQ_SYMBOLS);
    sq_update(decoder, node);
    return (int32_t)(node - SQ_SYMBOLS);
}

static bool sq_flush(sq_sink *sink, const uint8_t *data, size_t size) {
    if (sink->produced > SQ_MAX_OUTPUT - size) return false;
    sink->produced += size;
    return sq_write_all(sink->destination, data, size, sink->pd);
}

static bool sq_emit(sq_decoder *decoder, sq_sink *sink, uint8_t value) {
    decoder->window[decoder->window_position] = value;
    decoder->window_position = (decoder->window_position + 1U) & (SQ_WINDOW - 1U);
    if (decoder->window_position != 0U) return true;
    return sq_flush(sink, decoder->window, SQ_WINDOW);
}

/* The whole stream: literals, an end marker and 32 KiB LZ77 matches. */
static bool sq_decode(sq_decoder *decoder, sq_sink *sink) {
    for (;;) {
        int32_t symbol = sq_decode_symbol(decoder);
        uint32_t length, slot, source;
        int64_t extra;
        if (symbol < 0) return false;
        if ((uint32_t)symbol == SQ_END_SYMBOL) break;
        if ((uint32_t)symbol < SQ_END_SYMBOL) {
            if (!sq_emit(decoder, sink, (uint8_t)symbol)) return false;
            continue;
        }
        length = ((uint32_t)symbol - SQ_END_SYMBOL - 1U) % SQ_LENGTHS + 3U;
        slot = ((uint32_t)symbol - SQ_END_SYMBOL - 1U) / SQ_LENGTHS;
        if (slot >= SQ_SLOTS) return false;
        extra = sq_read_bits(decoder, decoder->dist_bits[slot]);
        if (extra < 0) return false;
        source = decoder->window_position -
                 ((uint32_t)extra + length + decoder->dist_base[slot]);
        while (length-- != 0U) {
            uint8_t value = decoder->window[source & (SQ_WINDOW - 1U)];
            source = (source & (SQ_WINDOW - 1U)) + 1U;
            if (!sq_emit(decoder, sink, value)) return false;
        }
    }
    return decoder->window_position == 0U ||
           sq_flush(sink, decoder->window, decoder->window_position);
}

/* Reads the packed extent once and either measures it or writes it out. */
static bool sq_run(Abstractformat *format, int64_t data_offset,
                   int64_t packed_size, xx_io_device *destination,
                   uint64_t *produced, xx_pd_struct *pd) {
    sq_decoder *decoder = NULL;
    uint8_t *input = NULL;
    sq_sink sink;
    bool result = false;
    if (!format || packed_size <= 0 || (uint64_t)packed_size > SQ_MAX_INPUT)
        return false;
    input = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    decoder = (sq_decoder *)xx_mem_alloc(sizeof(*decoder));
    if (!input || !decoder ||
        !sq_read_at(format->device, data_offset, input, (size_t)packed_size))
        goto done;
    sq_decoder_init(decoder, input, (size_t)packed_size);
    sink.destination = destination;
    sink.produced = 0U;
    sink.pd = pd;
    result = sq_decode(decoder, &sink);
    if (result && produced) *produced = sink.produced;
done:
    if (input) xx_mem_free(input);
    if (decoder) xx_mem_free(decoder);
    return result;
}

/* Header:
 *   u32 LE 0xAEAC5153   ("SQ" 0xAC 0xAE on disk)
 *   asciz  original DOS file name, every byte >= 0x20
 *   u8     month 1..12
 *   u8     day 1..31
 *   u8     year, biased by 1980
 *   u8     hour 0..23
 *   u8     minute 0..59
 *   u8     seconds / 2, 0..29
 * and then the coded stream to end of file.  There is no stored length, no
 * checksum and no method byte: the six calendar fields ARE the validation,
 * which is exactly what U3's recognition predicate FUN_00682be0 (the SQ VMT
 * slot 0 at 0x00682fe0) tests, and FUN_00682dc0 shows the same six bytes
 * being turned into the member's timestamp.
 *
 * Detection stays on the header alone.  The decoder is run once here to LEARN
 * the decoded size, because the container does not store it, but a stream that
 * will not decode leaves the size at zero instead of rejecting a file whose
 * header is unambiguous. */
static bool sq_parse(Abstractformat *format, sq_stream **result) {
    uint8_t buffer[SQ_MAGIC_SIZE + SQ_MAX_NAME + 1U + SQ_STAMP_SIZE + 1U];
    sq_stream *stream = NULL;
    sq_member member;
    int64_t total, size;
    size_t want, at, name_length;
    uint8_t month, day, year, hour, minute, half_second;
    uint64_t produced = 0U;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(SQ_MAGIC_SIZE + 2U + SQ_STAMP_SIZE)) return false;
    want = (size_t)size < sizeof(buffer) ? (size_t)size : sizeof(buffer);
    xx_mem_zero(buffer, sizeof(buffer));
    if (!sq_read_at(format->device, format->base_address, buffer, want) ||
        sq_le32(buffer) != UINT32_C(0xaeac5153))
        return false;

    name_length = 0U;
    for (at = SQ_MAGIC_SIZE; at < want; ++at) {
        if (buffer[at] == 0U) break;
        if (buffer[at] < 0x20U) return false;
        ++name_length;
    }
    if (at >= want || name_length == 0U || name_length > SQ_MAX_NAME)
        return false;
    /* at now indexes the terminator; the six calendar bytes follow it. */
    if (want - at - 1U < SQ_STAMP_SIZE) return false;
    month = buffer[at + 1U];
    day = buffer[at + 2U];
    year = buffer[at + 3U];
    hour = buffer[at + 4U];
    minute = buffer[at + 5U];
    half_second = buffer[at + 6U];
    if (month < 1U || month > 12U || day < 1U || day > 31U || hour > 23U ||
        minute > 59U || half_second > 29U)
        return false;

    stream = (sq_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;

    xx_mem_zero(&member, sizeof(member));
    member.name = sq_clean_name(buffer + SQ_MAGIC_SIZE, name_length);
    if (!member.name) goto fail;
    member.header_offset = format->base_address;
    member.header_size = (int64_t)(at + 1U + SQ_STAMP_SIZE);
    member.data_offset = format->base_address + member.header_size;
    member.packed_size = size - member.header_size;
    if (member.packed_size <= 0) {
        xx_mem_free(member.name);
        goto fail;
    }
    if (sq_run(format, member.data_offset, member.packed_size, NULL, &produced,
               NULL))
        member.unpacked_size = produced;
    member.method = 1U; /* adaptive Huffman + 32 KiB LZ77 */
    /* DOS-packed date and time, from the six calendar bytes. */
    member.timestamp =
        ((uint64_t)((((uint32_t)year) & 0x7fU) << 9U) |
         (uint64_t)(((uint32_t)month) << 5U) | (uint64_t)day) << 16U;
    member.timestamp |= (uint64_t)((((uint32_t)hour) << 11U) |
                                   (((uint32_t)minute) << 5U) |
                                   (uint32_t)half_second);
    if (!sq_add_member(stream, &member)) {
        xx_mem_free(member.name);
        goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    sq_stream_free(stream);
    return false;
}

static bool sq_write_member(Abstractformat *format, sq_stream *stream,
                            const sq_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint64_t produced = 0U;
    (void)stream;
    if (!format || !member) return false;
    return sq_run(format, member->data_offset, member->packed_size,
                  destination, &produced, pd) &&
           produced == member->unpacked_size;
}

static bool sq_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *sq_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sq_set_record(xx_archive_record *record,
                           const sq_member *member) {
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

void xx_sq_init(xx_sq *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SQ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sq");
    xx_format_set_extension(&archive->format, "sq");
    archive->format.check_is_valid = xx_sq_check_is_valid;
    archive->format.handle_base_info = xx_sq_handle_base_info;
    archive->format.get_format_size = xx_sq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sq_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sq_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sq_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sq_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sq_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sq_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_sq *xx_sq_create(xx_io_device *device, int64_t base_address) {
    xx_sq *archive = (xx_sq *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sq_init(archive, device, base_address);
    return archive;
}

void xx_sq_destroy(xx_sq *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sq_free(xx_sq *archive) {
    if (!archive) return;
    xx_sq_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sq_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    sq_stream *stream;
    (void)pd;
    if (!sq_parse(format, &stream)) return false;
    sq_stream_free(stream);
    return true;
}

bool xx_sq_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    sq_stream *stream;
    xx_sq *archive;
    (void)pd;
    if (!format || !sq_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_sq *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_SQ_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    sq_stream_free(stream);
    return true;
}

int64_t xx_sq_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sq_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sq_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sq_handle_base_info(format, pd))
               ? ((xx_sq *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_sq_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sq_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!sq_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sq_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sq_stream_free;
    state->total_records = stream->count;
    if (!sq_copy_options(&state->options, options) ||
        !sq_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sq_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sq_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    sq_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sq_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        sq_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sq_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    sq_stream *stream;
    sq_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sq_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!sq_safe_output_name(member->name)) return false;
    path_option = sq_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return sq_write_member(format, stream, member, NULL, pd);
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
    if (!destination) goto done;
    result = sq_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sq_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
