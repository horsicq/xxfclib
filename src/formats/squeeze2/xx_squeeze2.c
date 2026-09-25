/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Squeeze II (magic 0xFFFA), the DOS descendant of the CP/M Greenlaw
 * squeeze.
 *
 * IT IS A RELATIVE OF squeeze1, NOT A NEW CODEC.  The decode tree, the
 * LSB-first bit order, the SPEOF symbol at 256 and the 0x90 repeat escape
 * are byte for byte what src/formats/squeeze1 already implements; only the
 * header differs, so the codec here is that reader's, re-stated against the
 * new field layout.  It is kept as its own reader because the two magics,
 * the two header shapes and the two corpora are disjoint.
 *
 * Header layout confirmed against U3's recognition predicate FUN_004f3400
 * (archive/49, class wja, VMT 0x004f3348, slot 0 at 0x004f3860) and against
 * the corpus in F:\ARC\ARC\SQUEEZE2.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/squeeze2/xx_squeeze2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef SQUEEZE2
#define XX_SQUEEZE2_FILE_TYPE XX_FILE_TYPE_SQUEEZE2
#else
#define XX_SQUEEZE2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SQUEEZE2_MAX_MEMBERS 16U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct squeeze2_member_s {
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
} squeeze2_member;

typedef struct squeeze2_stream_s {
    squeeze2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} squeeze2_stream;

static uint16_t squeeze2_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t squeeze2_le32(const uint8_t *b) {
    return (uint32_t)squeeze2_le16(b) | ((uint32_t)squeeze2_le16(b + 2U) << 16U);
}

static uint64_t squeeze2_le64(const uint8_t *b) {
    return (uint64_t)squeeze2_le32(b) | ((uint64_t)squeeze2_le32(b + 4U) << 32U);
}

static uint32_t squeeze2_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t squeeze2_be64(const uint8_t *b) {
    return ((uint64_t)squeeze2_be32(b) << 32U) | (uint64_t)squeeze2_be32(b + 4U);
}

static bool squeeze2_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool squeeze2_write_all(xx_io_device *device, const void *data, size_t size,
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
static bool squeeze2_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
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
        if (!squeeze2_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool squeeze2_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!squeeze2_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *squeeze2_make_name(const char *prefix, int64_t index,
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
static char *squeeze2_clean_name(const uint8_t *bytes, size_t size) {
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

static bool squeeze2_safe_output_name(const char *name) {
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

static void squeeze2_stream_free(void *opaque) {
    squeeze2_stream *stream = (squeeze2_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool squeeze2_add_member(squeeze2_stream *stream, const squeeze2_member *member) {
    squeeze2_member *grown;
    if (!stream || !member || stream->count >= SQUEEZE2_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (squeeze2_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define SQUEEZE2_MAX_NAME 64U
#define SQUEEZE2_MAX_NODES 256U
#define SQUEEZE2_SPEOF 256U
#define SQUEEZE2_RLE_ESCAPE 0x90U
#define SQUEEZE2_MAX_INPUT ((uint64_t)64U * 1024U * 1024U)
#define SQUEEZE2_MAX_OUTPUT ((uint64_t)256U * 1024U * 1024U)

typedef struct squeeze2_sink_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool materialize;
    uint16_t checksum;
} squeeze2_sink;

typedef struct squeeze2_bit_reader_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t current;
    unsigned bits_left;
} squeeze2_bit_reader;

typedef struct squeeze2_header_s {
    char file_name[SQUEEZE2_MAX_NAME];
    char stamp[SQUEEZE2_MAX_NAME];
    uint16_t checksum;
    uint16_t dos_date;
    uint16_t dos_time;
    uint16_t node_count;
    size_t tree_offset;
    size_t data_offset;
} squeeze2_header;

static int32_t squeeze2_read_signed16le(const uint8_t *data) {
    uint16_t value = squeeze2_le16(data);
    return (value & UINT16_C(0x8000)) != 0U ? (int32_t)value - INT32_C(65536)
                                            : (int32_t)value;
}

/* A NUL-terminated printable field.  Anything outside the safe set becomes
 * '_' in the copy; a control byte makes the file invalid rather than being
 * silently repaired, which is what keeps a random FA FF file out. */
static bool squeeze2_read_field(const uint8_t *input, size_t input_size,
                                size_t offset, char *out, size_t out_size,
                                size_t *next_offset, bool allow_empty) {
    size_t length = 0U;
    size_t index;
    if (!input || !out || !next_offset || offset >= input_size) return false;
    while (offset + length < input_size && length < out_size - 1U &&
           input[offset + length] != 0U) {
        uint8_t ch = input[offset + length];
        if (ch < 0x20U || ch >= 0x7fU) return false;
        ++length;
    }
    if ((length == 0U && !allow_empty) || offset + length >= input_size ||
        input[offset + length] != 0U)
        return false;
    for (index = 0U; index < length; ++index) {
        uint8_t ch = input[offset + index];
        bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                    ch == '-';
        out[index] = safe ? (char)ch : '_';
    }
    out[length] = '\0';
    if (xx_rt_strcmp(out, ".") == 0 || xx_rt_strcmp(out, "..") == 0)
        xx_rt_memcpy(out, "payload", sizeof("payload"));
    *next_offset = offset + length + 1U;
    return true;
}

/* Header (all little endian):
 *   u16   0xFFFA
 *   asciz original file name
 *   asciz "MM/DD/YY" stamp
 *   u8    0x00        an empty trailing field
 *   u8    0x1A        the CP/M end-of-text byte
 *   u16   checksum    sum of every decoded byte, modulo 65536
 *   u16   DOS date    u16 DOS time
 *   u16   node count  then node count * 2 signed child words
 * The tree, the bitstream and the 0x90 repeat escape are IDENTICAL to the
 * Greenlaw squeeze the squeeze1 reader already implements; only the header
 * differs, which is why this is a sibling of that reader and not a new codec.
 * Field layout confirmed against U3's recognition predicate FUN_004f3400 (the
 * Squeeze2 VMT slot 0 at 0x004f3860) and against the corpus. */
static bool squeeze2_parse_header(const uint8_t *input, size_t input_size,
                                  squeeze2_header *header) {
    size_t offset;
    size_t table_size;
    size_t index;
    if (!input || !header || input_size < 12U || input[0] != 0xfaU ||
        input[1] != 0xffU)
        return false;
    if (!squeeze2_read_field(input, input_size, 2U, header->file_name,
                             sizeof(header->file_name), &offset, false))
        return false;
    if (!squeeze2_read_field(input, input_size, offset, header->stamp,
                             sizeof(header->stamp), &offset, true))
        return false;
    if (input_size - offset < 10U) return false;
    if (input[offset] != 0x00U || input[offset + 1U] != 0x1aU) return false;
    header->checksum = squeeze2_le16(input + offset + 2U);
    header->dos_date = squeeze2_le16(input + offset + 4U);
    header->dos_time = squeeze2_le16(input + offset + 6U);
    header->node_count = squeeze2_le16(input + offset + 8U);
    offset += 10U;
    if (header->node_count == 0U || header->node_count > SQUEEZE2_MAX_NODES)
        return false;
    table_size = (size_t)header->node_count * 4U;
    if (table_size > input_size - offset) return false;
    /* Bound every child before anything indexes the table. */
    for (index = 0U; index < (size_t)header->node_count * 2U; ++index) {
        int32_t child = squeeze2_read_signed16le(input + offset + index * 2U);
        if (child >= (int32_t)header->node_count ||
            (child < 0 && (uint32_t)(-child - 1) > SQUEEZE2_SPEOF))
            return false;
    }
    if (input_size - offset - table_size == 0U) return false;
    header->tree_offset = offset;
    header->data_offset = offset + table_size;
    return true;
}

static bool squeeze2_sink_put(squeeze2_sink *sink, uint8_t value) {
    if (!sink || (uint64_t)sink->size >= SQUEEZE2_MAX_OUTPUT) return false;
    if (sink->materialize) {
        if (sink->size == sink->capacity) {
            size_t wanted = sink->capacity ? sink->capacity * 2U : 65536U;
            uint8_t *grown;
            if ((uint64_t)wanted > SQUEEZE2_MAX_OUTPUT)
                wanted = (size_t)SQUEEZE2_MAX_OUTPUT;
            if (wanted <= sink->size) return false;
            grown = (uint8_t *)xx_mem_realloc(sink->data, wanted);
            if (!grown) return false;
            sink->data = grown;
            sink->capacity = wanted;
        }
        sink->data[sink->size] = value;
    }
    ++sink->size;
    sink->checksum = (uint16_t)(sink->checksum + value);
    return true;
}

static bool squeeze2_read_bit(squeeze2_bit_reader *reader, uint32_t *bit) {
    if (!reader || !bit) return false;
    if (reader->bits_left == 0U) {
        if (reader->position >= reader->size) return false;
        reader->current = reader->data[reader->position++];
        reader->bits_left = 8U;
    }
    *bit = (uint32_t)(reader->current & 1U);
    reader->current = (uint8_t)(reader->current >> 1U);
    --reader->bits_left;
    return true;
}

static bool squeeze2_decode_symbol(squeeze2_bit_reader *reader,
                                   const uint8_t *tree, uint16_t node_count,
                                   uint32_t *symbol) {
    uint16_t node = 0U;
    unsigned guard = 0U;
    if (!reader || !tree || node_count == 0U || !symbol) return false;
    for (;;) {
        uint32_t bit;
        int32_t child;
        if (++guard > node_count || !squeeze2_read_bit(reader, &bit))
            return false;
        child = squeeze2_read_signed16le(tree + (size_t)node * 4U +
                                         (size_t)bit * 2U);
        if (child < 0) {
            *symbol = (uint32_t)(-child - 1);
            return true;
        }
        if ((uint32_t)child >= node_count) return false;
        node = (uint16_t)child;
    }
}

/* The Huffman walk plus the 0x90 repeat stage, run to SPEOF.  There is no
 * stored plaintext length, so the header checksum is the only integrity
 * anchor and it is ALWAYS verified before a stream is accepted. */
static bool squeeze2_decode(const uint8_t *input, size_t input_size,
                            const squeeze2_header *header, bool materialize,
                            uint8_t **output, uint64_t *output_size,
                            xx_pd_struct *pd) {
    squeeze2_sink sink;
    squeeze2_bit_reader reader;
    bool repeat_pending = false;
    bool has_last = false;
    bool finished = false;
    uint8_t last = 0U;
    unsigned tick = 0U;
    if (!input || !header || !output_size) return false;
    if (output) *output = NULL;
    *output_size = 0U;
    if (header->data_offset > input_size) return false;
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.materialize = materialize;
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input + header->data_offset;
    reader.size = input_size - header->data_offset;
    while (!finished) {
        uint32_t symbol;
        if ((++tick & 0xffffU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        if (!squeeze2_decode_symbol(&reader, input + header->tree_offset,
                                    header->node_count, &symbol))
            goto fail;
        if (symbol == SQUEEZE2_SPEOF) {
            if (repeat_pending) goto fail;
            finished = true;
            break;
        }
        if (repeat_pending) {
            uint32_t count;
            repeat_pending = false;
            if (symbol == 0U) {
                if (!squeeze2_sink_put(&sink, SQUEEZE2_RLE_ESCAPE)) goto fail;
                last = SQUEEZE2_RLE_ESCAPE;
                has_last = true;
                continue;
            }
            if (!has_last) goto fail;
            for (count = 1U; count < symbol; ++count)
                if (!squeeze2_sink_put(&sink, last)) goto fail;
            continue;
        }
        if (symbol == SQUEEZE2_RLE_ESCAPE) {
            repeat_pending = true;
            continue;
        }
        if (!squeeze2_sink_put(&sink, (uint8_t)symbol)) goto fail;
        last = (uint8_t)symbol;
        has_last = true;
    }
    if (sink.checksum != header->checksum) goto fail;
    *output_size = (uint64_t)sink.size;
    if (materialize && output) {
        *output = sink.data;
        sink.data = NULL;
    } else if (sink.data) {
        xx_mem_free(sink.data);
    }
    return true;
fail:
    if (sink.data) xx_mem_free(sink.data);
    return false;
}

static bool squeeze2_run(Abstractformat *format, squeeze2_header *header,
                         bool materialize, uint8_t **plain,
                         uint64_t *plain_size, xx_pd_struct *pd) {
    int64_t total, size;
    uint8_t *input = NULL;
    uint64_t produced = 0U;
    bool result = false;
    if (plain) *plain = NULL;
    if (plain_size) *plain_size = 0U;
    if (!format || !format->device || format->base_address < 0 || !header)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 12 || (uint64_t)size > SQUEEZE2_MAX_INPUT) return false;
    input = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!input ||
        !squeeze2_read_at(format->device, format->base_address, input,
                          (size_t)size) ||
        !squeeze2_parse_header(input, (size_t)size, header) ||
        !squeeze2_decode(input, (size_t)size, header, materialize, plain,
                         &produced, pd))
        goto cleanup;
    if (plain_size) *plain_size = produced;
    result = true;
cleanup:
    if (input) xx_mem_free(input);
    return result;
}

static bool squeeze2_parse(Abstractformat *format, squeeze2_stream **result) {
    squeeze2_header header;
    squeeze2_stream *stream = NULL;
    squeeze2_member member;
    uint64_t produced = 0U;
    int64_t total, size;

    if (!format || !format->device || !result) return false;
    xx_mem_zero(&header, sizeof(header));
    if (!squeeze2_run(format, &header, false, NULL, &produced, NULL))
        return false;
    total = xx_io_total_size(format->device);
    size = total - format->base_address;

    stream = (squeeze2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;

    xx_mem_zero(&member, sizeof(member));
    member.name = squeeze2_clean_name((const uint8_t *)header.file_name,
                                      xx_str_len(header.file_name));
    if (!member.name) goto fail;
    member.header_offset = format->base_address;
    member.header_size = (int64_t)header.data_offset;
    member.data_offset = format->base_address + (int64_t)header.data_offset;
    member.packed_size = size - (int64_t)header.data_offset;
    member.unpacked_size = produced;
    member.crc32 = header.checksum;
    member.method = 1U; /* static Huffman + 0x90 repeat escape */
    member.timestamp = ((uint64_t)header.dos_date << 16U) | header.dos_time;
    member.flags = header.node_count;
    if (!squeeze2_add_member(stream, &member)) {
        xx_mem_free(member.name);
        goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    squeeze2_stream_free(stream);
    return false;
}

static bool squeeze2_write_member(Abstractformat *format,
                                  squeeze2_stream *stream,
                                  const squeeze2_member *member,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    squeeze2_header header;
    uint8_t *plain = NULL;
    uint64_t plain_size = 0U;
    bool result;
    (void)stream;
    if (!format || !member) return false;
    xx_mem_zero(&header, sizeof(header));
    if (!squeeze2_run(format, &header, true, &plain, &plain_size, pd))
        return false;
    result = plain_size == member->unpacked_size &&
             plain_size <= (uint64_t)SIZE_MAX &&
             squeeze2_write_all(destination, plain, (size_t)plain_size, pd);
    if (plain) xx_mem_free(plain);
    return result;
}

static bool squeeze2_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *squeeze2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool squeeze2_set_record(xx_archive_record *record,
                           const squeeze2_member *member) {
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

void xx_squeeze2_init(xx_squeeze2 *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SQUEEZE2_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-squeezed");
    xx_format_set_extension(&archive->format, "sq2");
    archive->format.check_is_valid = xx_squeeze2_check_is_valid;
    archive->format.handle_base_info = xx_squeeze2_handle_base_info;
    archive->format.get_format_size = xx_squeeze2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_squeeze2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_squeeze2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_squeeze2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_squeeze2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_squeeze2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_squeeze2_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_squeeze2 *xx_squeeze2_create(xx_io_device *device, int64_t base_address) {
    xx_squeeze2 *archive = (xx_squeeze2 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_squeeze2_init(archive, device, base_address);
    return archive;
}

void xx_squeeze2_destroy(xx_squeeze2 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_squeeze2_free(xx_squeeze2 *archive) {
    if (!archive) return;
    xx_squeeze2_destroy(archive);
    xx_mem_free(archive);
}

bool xx_squeeze2_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    squeeze2_stream *stream;
    (void)pd;
    if (!squeeze2_parse(format, &stream)) return false;
    squeeze2_stream_free(stream);
    return true;
}

bool xx_squeeze2_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    squeeze2_stream *stream;
    xx_squeeze2 *archive;
    (void)pd;
    if (!format || !squeeze2_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_squeeze2 *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_SQUEEZE2_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    squeeze2_stream_free(stream);
    return true;
}

int64_t xx_squeeze2_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_squeeze2_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_squeeze2_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_squeeze2_handle_base_info(format, pd))
               ? ((xx_squeeze2 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_squeeze2_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    squeeze2_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!squeeze2_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        squeeze2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = squeeze2_stream_free;
    state->total_records = stream->count;
    if (!squeeze2_copy_options(&state->options, options) ||
        !squeeze2_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_squeeze2_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_squeeze2_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    squeeze2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (squeeze2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        squeeze2_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_squeeze2_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    squeeze2_stream *stream;
    squeeze2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (squeeze2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!squeeze2_safe_output_name(member->name)) return false;
    path_option = squeeze2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return squeeze2_write_member(format, stream, member, NULL, pd);
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
    result = squeeze2_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_squeeze2_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
