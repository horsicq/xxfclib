/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Shockwave Flash movies (FWS, CWS, ZWS) as containers of their embedded
 * media.  xx_swf.h carries the header and tag tables and the member list.
 *
 * Written from the SWF File Format Specification's structure tables.  The
 * member set follows what the reference extractors produce: 7-Zip's SWFc
 * handler (the decompressed movie), swfextract and unar (JPEG with the
 * tables merged and the FF D9 FF D8 seams removed, PNG/GIF payloads, MP3
 * sounds and streams, PCM as WAV, lossless bitmaps as PNG, binary data).
 * No code was taken from any of them.
 *
 * A compressed movie is decompressed once, into 1 MiB chunks, by
 * handle_base_info; the chunks live as long as the format object.  The tag
 * walk then reads either those chunks or, for FWS, the device itself, and
 * records each member as a chain of byte ranges of the uncompressed movie.
 * Everything a member needs is decided at listing time, so the sizes the
 * listing reports are the sizes extraction writes.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/swf/xx_swf.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as SWF is registered there. */
#ifdef SWF
#define XX_SWF_FILE_TYPE XX_FILE_TYPE_SWF
#else
#define XX_SWF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SWF_HEADER 8U
#define SWF_ZLIB_HEADER 2U
#define SWF_ZWS_HEADER 17U
#define SWF_MAX_VERSION 63U
/* Movie bytes (past the 8-byte header) the probe looks at: the widest RECT
 * (17 bytes), frame rate and count, and a long tag header fit easily. */
#define SWF_PREFIX 64U
#define SWF_CHUNK_SHIFT 20U
#define SWF_CHUNK ((uint32_t)1U << SWF_CHUNK_SHIFT)
#define SWF_MAX_CHUNKS (XX_SWF_MAX_CACHED >> SWF_CHUNK_SHIFT)
/* A CWS stream is decoded from memory (which tells how many input bytes it
 * used, and so the format size) up to this much compressed input. */
#define SWF_MAX_EXACT_INPUT ((int64_t)64 * 1024 * 1024)
#define SWF_MAX_TAGS (1U << 24)
#define SWF_MAX_ITEMS (1U << 20)
#define SWF_MAX_RANGES (1U << 22)
/* Lossless bitmaps: Flash itself never exceeds 16.7 million pixels. */
#define SWF_MAX_PIXELS ((uint64_t)1U << 26)
#define SWF_NONE 0xFFFFFFFFU
#define SWF_IO_BLOCK 65536U
#define SWF_WINDOW 4096U
#define SWF_STORED_MAX 65535U
#define SWF_WAV_HEADER 44U

enum {
    SWF_TAG_END = 0,
    SWF_TAG_DEFINE_BITS = 6,
    SWF_TAG_JPEG_TABLES = 8,
    SWF_TAG_DEFINE_SOUND = 14,
    SWF_TAG_STREAM_HEAD = 18,
    SWF_TAG_STREAM_BLOCK = 19,
    SWF_TAG_LOSSLESS = 20,
    SWF_TAG_JPEG2 = 21,
    SWF_TAG_JPEG3 = 35,
    SWF_TAG_LOSSLESS2 = 36,
    SWF_TAG_SPRITE = 39,
    SWF_TAG_STREAM_HEAD2 = 45,
    SWF_TAG_BINARY = 87,
    SWF_TAG_JPEG4 = 90
};

/* Sound formats (top four bits of the format byte). */
enum {
    SWF_SOUND_PCM_NATIVE = 0,
    SWF_SOUND_MP3 = 2,
    SWF_SOUND_PCM_LE = 3
};

typedef enum swf_kind_e {
    SWF_KIND_MOVIE = 0,  /* decompressed movie, FWS header in front */
    SWF_KIND_COPY,       /* ranges concatenated */
    SWF_KIND_JPEG,       /* ranges concatenated, FF D9 FF D8 removed */
    SWF_KIND_WAV,        /* RIFF header, ranges, pad byte */
    SWF_KIND_PNG         /* one range: zlib bitmap data, converted */
} swf_kind;

typedef enum swf_stem_e {
    SWF_STEM_MOVIE = 0,
    SWF_STEM_IMAGE,
    SWF_STEM_SOUND,
    SWF_STEM_BINARY,
    SWF_STEM_STREAM
} swf_stem;

static const char *const swf_stems[] = {"movie", "image", "sound", "binary",
                                        "stream"};
static const char *const swf_extensions[] = {"swf", "jpg", "png", "gif",
                                             "mp3", "wav", "bin"};
enum { SWF_EXT_SWF, SWF_EXT_JPG, SWF_EXT_PNG, SWF_EXT_GIF, SWF_EXT_MP3,
       SWF_EXT_WAV, SWF_EXT_BIN };

typedef struct swf_range_s {
    uint32_t offset; /**< Uncompressed movie offset. */
    uint32_t length;
    uint32_t next;
} swf_range;

typedef struct swf_item_s {
    uint8_t kind;
    uint8_t stem;
    uint8_t ext;
    uint8_t sound_flags;   /**< WAV: the SWF sound format byte. */
    uint8_t bitmap_format; /**< PNG: 3, 4 or 5. */
    bool alpha;            /**< PNG: DefineBitsLossless2. */
    bool duplicate;
    uint16_t id;
    uint16_t width;
    uint16_t height;
    uint16_t colors;       /**< PNG format 3: colour table entries. */
    uint32_t number;       /**< Stream number, or 1-based ordinal. */
    uint32_t first;
    uint32_t last;
    uint32_t tag_offset;   /**< Movie offset of the defining tag's body. */
    uint64_t data_size;
    uint64_t size;
} swf_item;

typedef struct swf_parsed_s {
    uint8_t signature;
    uint8_t version;
    uint32_t file_length;
    int64_t input_size;
    int64_t format_size;
    uint64_t movie_size;   /**< Readable movie bytes, header included. */
    bool movie_complete;
    bool media_listed;
    uint8_t header[SWF_HEADER]; /**< FWS header of the uncompressed movie. */
    uint8_t **chunks;
    uint32_t chunk_count;
    swf_item *items;
    uint32_t item_count;
    uint32_t item_capacity;
    swf_range *ranges;
    uint32_t range_count;
    uint32_t range_capacity;
    bool overflow;
    uint8_t seen[3][8192]; /**< Character ids met, per image/sound/binary. */
} swf_parsed;

typedef struct swf_header_s {
    uint8_t signature;
    uint8_t version;
    uint32_t file_length;
    int64_t input_size;
    int64_t data_offset;  /**< Device offset of the (compressed) body. */
    int64_t data_size;    /**< Bytes available to the decoder. */
    int64_t format_size;  /**< Extent known without decoding. */
    uint8_t props[XX_LZMA_PROPS_SIZE];
} swf_header;

static uint32_t swf_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t swf_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void swf_put_le16(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void swf_put_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void swf_put_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static bool swf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool swf_write_all(xx_io_device *device, const void *data,
                          size_t size) {
    size_t done = 0U;
    if (!device) return true; /* verification only */
    while (done < size) {
        ssize_t amount = xx_io_write(device, (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Header and probe                                                        */

static bool swf_parse_header(Abstractformat *format, swf_header *out) {
    uint8_t head[SWF_ZWS_HEADER + 2U];
    swf_header h;
    int64_t total, size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)SWF_HEADER + 1 ||
        !swf_read_at(format->device, format->base_address, head, SWF_HEADER))
        return false;
    if ((head[0] != 'F' && head[0] != 'C' && head[0] != 'Z') ||
        head[1] != 'W' || head[2] != 'S' || head[3] == 0U ||
        head[3] > SWF_MAX_VERSION)
        return false;
    xx_mem_zero(&h, sizeof(h));
    h.signature = head[0];
    h.version = head[3];
    h.file_length = swf_le32(head + 4U);
    h.input_size = size;
    /* The smallest movie is the header, a one-byte RECT, frame rate and
     * frame count. */
    if (h.file_length < SWF_HEADER + 5U ||
        h.file_length >= XX_SWF_MAX_FILE_LENGTH)
        return false;
    if (h.signature == 'F') {
        h.data_offset = format->base_address + (int64_t)SWF_HEADER;
        h.format_size = size < (int64_t)h.file_length ? size
                                                      : (int64_t)h.file_length;
        h.data_size = h.format_size - (int64_t)SWF_HEADER;
    } else if (h.signature == 'C') {
        if (size < (int64_t)(SWF_HEADER + SWF_ZLIB_HEADER + 1U) ||
            !swf_read_at(format->device, format->base_address + SWF_HEADER,
                         head + SWF_HEADER, SWF_ZLIB_HEADER) ||
            !xx_zlib_stream_header_is_valid(head + SWF_HEADER,
                                            SWF_ZLIB_HEADER))
            return false;
        h.data_offset =
            format->base_address + (int64_t)(SWF_HEADER + SWF_ZLIB_HEADER);
        h.data_size = size - (int64_t)(SWF_HEADER + SWF_ZLIB_HEADER);
        h.format_size = size;
    } else {
        uint32_t packed, property;
        int64_t available;
        /* Properties, the range coder's leading zero byte and four code
         * bytes: an LZMA stream is never shorter than five bytes. */
        if (size < (int64_t)(SWF_ZWS_HEADER + 5U) ||
            !swf_read_at(format->device, format->base_address + SWF_HEADER,
                         head + SWF_HEADER,
                         SWF_ZWS_HEADER + 2U - SWF_HEADER))
            return false;
        packed = swf_le32(head + 8U);
        property = head[12];
        if (property >= 9U * 5U * 5U) return false;
        /* lc + lp above 4 is legal LZMA but no SWF writer uses it and the
         * library's decoder refuses it. */
        if ((property % 9U) + ((property / 9U) % 5U) > 4U) return false;
        if (head[SWF_ZWS_HEADER] != 0U) return false;
        xx_rt_memcpy(h.props, head + 12U, XX_LZMA_PROPS_SIZE);
        available = size - (int64_t)SWF_ZWS_HEADER;
        h.data_offset = format->base_address + (int64_t)SWF_ZWS_HEADER;
        if (packed >= 5U && (int64_t)packed <= available) {
            h.data_size = (int64_t)packed;
            h.format_size = (int64_t)SWF_ZWS_HEADER + (int64_t)packed;
        } else {
            h.data_size = available;
            h.format_size = size;
        }
    }
    *out = h;
    return true;
}

/* The movie body behind the header: a RECT whose fields are ordered, frame
 * rate and count, and a first tag that fits inside the declared movie. */
static bool swf_check_prefix(const uint8_t *body, size_t size,
                             uint32_t file_length) {
    uint32_t nbits, rect_bytes, bit = 5U, field, index;
    int32_t values[4];
    uint64_t movie = (uint64_t)file_length - SWF_HEADER, position, length;
    uint32_t header;
    if (!body || size == 0U) return false;
    nbits = body[0] >> 3U;
    rect_bytes = (5U + 4U * nbits + 7U) / 8U;
    if ((size_t)rect_bytes + 6U > size ||
        (uint64_t)rect_bytes + 6U > movie)
        return false;
    for (field = 0U; field < 4U; ++field) {
        uint32_t value = 0U;
        for (index = 0U; index < nbits; ++index, ++bit)
            value = (value << 1U) |
                    ((uint32_t)(body[bit >> 3U] >> (7U - (bit & 7U))) & 1U);
        if (nbits != 0U && (value >> (nbits - 1U)) != 0U && nbits < 32U)
            value |= ~((UINT32_C(1) << nbits) - 1U);
        values[field] = (int32_t)value;
    }
    if (values[0] > values[1] || values[2] > values[3]) return false;
    position = (uint64_t)rect_bytes + 4U;
    header = swf_le16(body + position);
    position += 2U;
    length = header & 0x3FU;
    if (length == 0x3FU) {
        if (position + 4U > size) return position + 4U <= movie;
        length = swf_le32(body + position);
        position += 4U;
    }
    return length <= movie - position;
}

/* ---------------------------------------------------------------------- */
/* Decoder sinks: custom output devices the codecs write into              */

typedef enum swf_sink_mode_e {
    SWF_SINK_PREFIX = 0,
    SWF_SINK_CACHE,
    SWF_SINK_FORWARD,
    SWF_SINK_PNG
} swf_sink_mode;

struct swf_png_s;

typedef struct swf_sink_s {
    xx_io_device device;
    swf_sink_mode mode;
    uint64_t limit;
    uint64_t received;
    bool full;
    bool failed;
    uint8_t *prefix;
    swf_parsed *parsed;
    xx_io_device *out;
    struct swf_png_s *png;
} swf_sink;

static bool swf_png_feed(struct swf_png_s *png, const uint8_t *data,
                         size_t size);

static bool swf_cache_put(swf_parsed *parsed, uint64_t position,
                          const uint8_t *data, size_t size) {
    while (size) {
        uint32_t index = (uint32_t)(position >> SWF_CHUNK_SHIFT);
        uint32_t within = (uint32_t)(position & (SWF_CHUNK - 1U));
        size_t amount = SWF_CHUNK - within;
        if (amount > size) amount = size;
        if (index >= SWF_MAX_CHUNKS) return false;
        if (index >= parsed->chunk_count) {
            if (index != parsed->chunk_count) return false;
            parsed->chunks[index] = (uint8_t *)xx_mem_alloc(SWF_CHUNK);
            if (!parsed->chunks[index]) return false;
            parsed->chunk_count = index + 1U;
        }
        xx_rt_memcpy(parsed->chunks[index] + within, data, amount);
        data += amount;
        size -= amount;
        position += amount;
    }
    return true;
}

static ssize_t swf_sink_write(xx_io_device *self, const void *buffer,
                              size_t size) {
    swf_sink *sink = self ? (swf_sink *)self->priv : NULL;
    const uint8_t *data = (const uint8_t *)buffer;
    uint64_t room;
    size_t take;
    bool ok = true;
    if (!sink || sink->failed || sink->full || (!buffer && size != 0U))
        return -1;
    if (size == 0U) return 0;
    room = sink->limit - sink->received;
    take = (uint64_t)size < room ? size : (size_t)room;
    switch (sink->mode) {
    case SWF_SINK_PREFIX:
        xx_rt_memcpy(sink->prefix + sink->received, data, take);
        break;
    case SWF_SINK_CACHE:
        ok = swf_cache_put(sink->parsed, sink->received, data, take);
        break;
    case SWF_SINK_FORWARD:
        ok = swf_write_all(sink->out, data, take);
        break;
    case SWF_SINK_PNG:
        ok = swf_png_feed(sink->png, data, take);
        break;
    default:
        ok = false;
        break;
    }
    if (!ok) {
        sink->failed = true;
        return -1;
    }
    sink->received += take;
    if (sink->received == sink->limit) sink->full = true;
    /* Output past the wanted amount stops the decoder; the caller sees
     * `full` and keeps what arrived, as 7-Zip does for an over-long body. */
    return take == size ? (ssize_t)size : -1;
}

static void swf_sink_init(swf_sink *sink, swf_sink_mode mode, uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->mode = mode;
    sink->limit = limit;
    sink->device.write = swf_sink_write;
    sink->device.priv = sink;
    if (limit == 0U) sink->full = true;
}

/* Feed movie bytes 8 .. 8 + wanted of the body described by @p h into
 * @p sink.  Returns true when the codec finished cleanly or the sink got
 * everything it asked for.  @p consumed receives the deflate input used
 * when it could be measured (-1 otherwise). */
static bool swf_decode_body(Abstractformat *format, const swf_header *h,
                            swf_sink *sink, bool measure, int64_t *consumed,
                            xx_pd_struct *pd) {
    bool ok = false;
    if (consumed) *consumed = -1;
    if (sink->full) return true;
    if (h->signature == 'F') {
        uint8_t *block = (uint8_t *)xx_mem_alloc(SWF_IO_BLOCK);
        int64_t position = h->data_offset;
        int64_t remaining = h->data_size;
        if (!block) return false;
        ok = true;
        while (remaining > 0 && !sink->full) {
            size_t amount = remaining < (int64_t)SWF_IO_BLOCK
                                ? (size_t)remaining : SWF_IO_BLOCK;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !swf_read_at(format->device, position, block, amount) ||
                swf_sink_write(&sink->device, block, amount) < 0) {
                ok = false;
                break;
            }
            position += (int64_t)amount;
            remaining -= (int64_t)amount;
        }
        xx_mem_free(block);
    } else if (h->signature == 'C') {
        if (measure && h->data_size <= SWF_MAX_EXACT_INPUT) {
            uint8_t *input = (uint8_t *)xx_mem_alloc((size_t)h->data_size);
            size_t used = 0U;
            if (!input) return false;
            if (swf_read_at(format->device, h->data_offset, input,
                            (size_t)h->data_size)) {
                ok = xx_deflate_unpack_memory_to_device_ex(
                    input, (size_t)h->data_size, &sink->device, &used, false,
                    pd);
                if (ok && consumed) *consumed = (int64_t)used;
            }
            xx_mem_free(input);
        } else {
            ok = xx_deflate_unpack_device(format->device, h->data_offset,
                                          h->data_size, &sink->device, false,
                                          pd);
        }
    } else {
        uint8_t props[XX_LZMA_PROPS_SIZE];
        uint32_t dictionary;
        xx_rt_memcpy(props, h->props, sizeof(props));
        /* Matches never reach further back than the output produced so
         * far, so a dictionary as large as the wanted output is enough
         * (7-Zip clamps the same way).  This also bounds the allocation. */
        dictionary = swf_le32(props + 1U);
        if ((uint64_t)dictionary > sink->limit) {
            dictionary = (uint32_t)sink->limit;
            swf_put_le32(props + 1U, dictionary);
        }
        ok = xx_lzma_unpack_device(format->device, h->data_offset,
                                   h->data_size, props, sizeof(props),
                                   (int64_t)sink->limit, &sink->device, pd);
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    return !sink->failed && (ok || sink->full);
}

static bool swf_probe(Abstractformat *format, swf_header *h) {
    uint8_t prefix[SWF_PREFIX];
    uint64_t wanted;
    swf_sink sink;
    if (!swf_parse_header(format, h)) return false;
    wanted = (uint64_t)h->file_length - SWF_HEADER;
    if (wanted > SWF_PREFIX) wanted = SWF_PREFIX;
    swf_sink_init(&sink, SWF_SINK_PREFIX, wanted);
    sink.prefix = prefix;
    (void)swf_decode_body(format, h, &sink, false, NULL, NULL);
    if (sink.failed) return false;
    return swf_check_prefix(prefix, (size_t)sink.received, h->file_length);
}

/* ---------------------------------------------------------------------- */
/* Uncompressed movie access                                               */

static bool swf_movie_read(const swf_parsed *parsed, Abstractformat *format,
                           uint64_t offset, uint8_t *buffer, size_t size) {
    if (offset > parsed->movie_size || size > parsed->movie_size - offset)
        return false;
    if (parsed->signature == 'F')
        return swf_read_at(format->device,
                           format->base_address + (int64_t)offset, buffer,
                           size);
    while (size) {
        if (offset < SWF_HEADER) {
            *buffer++ = parsed->header[offset++];
            --size;
        } else {
            uint64_t body = offset - SWF_HEADER;
            uint32_t index = (uint32_t)(body >> SWF_CHUNK_SHIFT);
            uint32_t within = (uint32_t)(body & (SWF_CHUNK - 1U));
            size_t amount = SWF_CHUNK - within;
            if (amount > size) amount = size;
            if (index >= parsed->chunk_count) return false;
            xx_rt_memcpy(buffer, parsed->chunks[index] + within, amount);
            buffer += amount;
            size -= amount;
            offset += amount;
        }
    }
    return true;
}

typedef struct swf_cursor_s {
    const swf_parsed *parsed;
    Abstractformat *format;
    uint64_t start;
    size_t length;
    uint8_t buffer[SWF_WINDOW];
} swf_cursor;

static bool swf_fetch(swf_cursor *cursor, uint64_t offset, uint8_t *out,
                      size_t size) {
    const swf_parsed *parsed = cursor->parsed;
    if (size > SWF_WINDOW || offset > parsed->movie_size ||
        size > parsed->movie_size - offset)
        return false;
    if (offset < cursor->start || offset - cursor->start > cursor->length ||
        size > cursor->length - (size_t)(offset - cursor->start)) {
        uint64_t available = parsed->movie_size - offset;
        size_t want = available < SWF_WINDOW ? (size_t)available : SWF_WINDOW;
        cursor->length = 0U;
        if (!swf_movie_read(parsed, cursor->format, offset, cursor->buffer,
                            want))
            return false;
        cursor->start = offset;
        cursor->length = want;
    }
    xx_rt_memcpy(out, cursor->buffer + (size_t)(offset - cursor->start),
                 size);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Listing                                                                 */

static bool swf_add_item(swf_parsed *parsed, uint8_t kind, uint8_t stem,
                         uint8_t ext, uint16_t id, uint32_t tag_offset,
                         uint32_t *index) {
    swf_item *item;
    if (parsed->item_count >= SWF_MAX_ITEMS) {
        parsed->overflow = true;
        return false;
    }
    if (parsed->item_count == parsed->item_capacity) {
        uint32_t capacity = parsed->item_capacity ? parsed->item_capacity * 2U
                                                  : 64U;
        swf_item *grown;
        if (capacity > SWF_MAX_ITEMS) capacity = SWF_MAX_ITEMS;
        grown = (swf_item *)xx_mem_realloc(parsed->items,
                                           (size_t)capacity * sizeof(*grown));
        if (!grown) {
            parsed->overflow = true;
            return false;
        }
        parsed->items = grown;
        parsed->item_capacity = capacity;
    }
    item = &parsed->items[parsed->item_count];
    xx_mem_zero(item, sizeof(*item));
    item->kind = kind;
    item->stem = stem;
    item->ext = ext;
    item->id = id;
    item->tag_offset = tag_offset;
    item->first = SWF_NONE;
    item->last = SWF_NONE;
    *index = parsed->item_count++;
    return true;
}

static bool swf_add_range(swf_parsed *parsed, uint32_t item_index,
                          uint64_t offset, uint64_t length) {
    swf_item *item = &parsed->items[item_index];
    swf_range *range;
    if (length == 0U) return true;
    if (parsed->range_count >= SWF_MAX_RANGES ||
        offset + length > parsed->movie_size) {
        parsed->overflow = true;
        return false;
    }
    if (parsed->range_count == parsed->range_capacity) {
        uint32_t capacity = parsed->range_capacity
                                ? parsed->range_capacity * 2U : 256U;
        swf_range *grown;
        if (capacity > SWF_MAX_RANGES) capacity = SWF_MAX_RANGES;
        grown = (swf_range *)xx_mem_realloc(
            parsed->ranges, (size_t)capacity * sizeof(*grown));
        if (!grown) {
            parsed->overflow = true;
            return false;
        }
        parsed->ranges = grown;
        parsed->range_capacity = capacity;
    }
    range = &parsed->ranges[parsed->range_count];
    range->offset = (uint32_t)offset;
    range->length = (uint32_t)length;
    range->next = SWF_NONE;
    if (item->last == SWF_NONE)
        item->first = parsed->range_count;
    else
        parsed->ranges[item->last].next = parsed->range_count;
    item->last = parsed->range_count++;
    item->data_size += length;
    return true;
}

/* JPEG, PNG or GIF: DefineBitsJPEG2/3/4 may hold any of the three. */
static void swf_add_image(swf_parsed *parsed, swf_cursor *cursor, uint16_t id,
                          uint32_t tag_offset, uint64_t data,
                          uint64_t length) {
    static const uint8_t png_signature[8] = {0x89U, 'P', 'N', 'G',
                                             0x0DU, 0x0AU, 0x1AU, 0x0AU};
    uint8_t head[8] = {0};
    uint8_t kind = SWF_KIND_JPEG, ext = SWF_EXT_JPG;
    uint32_t index;
    if (length == 0U) return;
    if (!swf_fetch(cursor, data, head, length < 8U ? (size_t)length : 8U))
        return;
    if (length >= 8U && xx_rt_memcmp(head, png_signature, 8U) == 0) {
        kind = SWF_KIND_COPY;
        ext = SWF_EXT_PNG;
    } else if (length >= 6U && xx_rt_memcmp(head, "GIF8", 4U) == 0 &&
               (head[4] == '7' || head[4] == '9') && head[5] == 'a') {
        kind = SWF_KIND_COPY;
        ext = SWF_EXT_GIF;
    }
    if (swf_add_item(parsed, kind, SWF_STEM_IMAGE, ext, id, tag_offset,
                     &index))
        (void)swf_add_range(parsed, index, data, length);
}

typedef struct swf_walk_s {
    uint64_t tables_offset;
    uint64_t tables_length;
    uint32_t main_stream;
    uint32_t sprite_stream;
} swf_walk_state;

static void swf_stream_head(swf_parsed *parsed, const uint8_t *body,
                            uint64_t length, uint32_t tag_offset,
                            uint32_t *current) {
    uint32_t format, index;
    *current = SWF_NONE;
    if (length < 4U) return;
    format = (uint32_t)body[1] >> 4U;
    if (format == SWF_SOUND_MP3) {
        if (swf_add_item(parsed, SWF_KIND_COPY, SWF_STEM_STREAM, SWF_EXT_MP3,
                         0U, tag_offset, &index))
            *current = index;
    } else if (format == SWF_SOUND_PCM_NATIVE || format == SWF_SOUND_PCM_LE) {
        if (swf_add_item(parsed, SWF_KIND_WAV, SWF_STEM_STREAM, SWF_EXT_WAV,
                         0U, tag_offset, &index)) {
            parsed->items[index].sound_flags = body[1];
            *current = index;
        }
    }
}

static void swf_stream_block(swf_parsed *parsed, uint32_t current,
                             uint64_t body, uint64_t length) {
    if (current == SWF_NONE) return;
    if (parsed->items[current].kind == SWF_KIND_WAV) {
        (void)swf_add_range(parsed, current, body, length);
    } else if (length > 4U) {
        /* MP3STREAMSOUNDDATA: sample count and seek samples, then frames. */
        (void)swf_add_range(parsed, current, body + 4U, length - 4U);
    }
}

static void swf_top_tag(swf_parsed *parsed, swf_cursor *cursor,
                        swf_walk_state *walk, uint32_t code, uint64_t body,
                        uint64_t length) {
    uint8_t head[16] = {0};
    uint32_t index, tag_offset = (uint32_t)body;
    uint16_t id;
    if (!swf_fetch(cursor, body, head, length < 16U ? (size_t)length : 16U))
        return;
    id = (uint16_t)swf_le16(head);
    switch (code) {
    case SWF_TAG_JPEG_TABLES:
        walk->tables_offset = body;
        walk->tables_length = length;
        break;
    case SWF_TAG_DEFINE_BITS:
        if (length <= 2U) break;
        if (swf_add_item(parsed, SWF_KIND_JPEG, SWF_STEM_IMAGE, SWF_EXT_JPG,
                         id, tag_offset, &index)) {
            /* Tables then image: the tables' EOI and the image's SOI form
             * the FF D9 FF D8 seam the JPEG filter removes. */
            (void)swf_add_range(parsed, index, walk->tables_offset,
                                walk->tables_length);
            (void)swf_add_range(parsed, index, body + 2U, length - 2U);
        }
        break;
    case SWF_TAG_JPEG2:
        if (length > 2U)
            swf_add_image(parsed, cursor, id, tag_offset, body + 2U,
                          length - 2U);
        break;
    case SWF_TAG_JPEG3:
    case SWF_TAG_JPEG4: {
        uint64_t skip = code == SWF_TAG_JPEG3 ? 6U : 8U, image;
        if (length <= skip) break;
        /* The colour data ends at AlphaDataOffset; an offset past the tag
         * is clamped to it. */
        image = swf_le32(head + 2U);
        if (image > length - skip) image = length - skip;
        swf_add_image(parsed, cursor, id, tag_offset, body + skip, image);
        break;
    }
    case SWF_TAG_LOSSLESS:
    case SWF_TAG_LOSSLESS2: {
        uint32_t bitmap = head[2], width, height, colors = 0U;
        uint64_t data = body + 7U;
        uint8_t zlib_head[2];
        if (length < 7U) break;
        width = swf_le16(head + 3U);
        height = swf_le16(head + 5U);
        if (bitmap == 3U) {
            if (length < 8U) break;
            colors = (uint32_t)head[7] + 1U;
            data = body + 8U;
        } else if (bitmap == 4U) {
            if (code == SWF_TAG_LOSSLESS2) break; /* not defined */
        } else if (bitmap != 5U) {
            break;
        }
        if (width == 0U || height == 0U ||
            (uint64_t)width * height > SWF_MAX_PIXELS ||
            body + length - data < 3U ||
            !swf_fetch(cursor, data, zlib_head, 2U) ||
            !xx_zlib_stream_header_is_valid(zlib_head, 2U))
            break;
        if (swf_add_item(parsed, SWF_KIND_PNG, SWF_STEM_IMAGE, SWF_EXT_PNG,
                         id, tag_offset, &index)) {
            swf_item *item = &parsed->items[index];
            item->bitmap_format = (uint8_t)bitmap;
            item->alpha = code == SWF_TAG_LOSSLESS2;
            item->width = (uint16_t)width;
            item->height = (uint16_t)height;
            item->colors = (uint16_t)colors;
            (void)swf_add_range(parsed, index, data, body + length - data);
        }
        break;
    }
    case SWF_TAG_DEFINE_SOUND: {
        uint32_t format;
        if (length < 7U) break;
        format = (uint32_t)head[2] >> 4U;
        if (format == SWF_SOUND_MP3) {
            /* MP3SOUNDDATA: seek samples, then frames. */
            if (length <= 9U) break;
            if (swf_add_item(parsed, SWF_KIND_COPY, SWF_STEM_SOUND,
                             SWF_EXT_MP3, id, tag_offset, &index))
                (void)swf_add_range(parsed, index, body + 9U, length - 9U);
        } else if (format == SWF_SOUND_PCM_NATIVE ||
                   format == SWF_SOUND_PCM_LE) {
            if (length <= 7U) break;
            if (swf_add_item(parsed, SWF_KIND_WAV, SWF_STEM_SOUND,
                             SWF_EXT_WAV, id, tag_offset, &index)) {
                parsed->items[index].sound_flags = head[2];
                (void)swf_add_range(parsed, index, body + 7U, length - 7U);
            }
        }
        break;
    }
    case SWF_TAG_STREAM_HEAD:
    case SWF_TAG_STREAM_HEAD2:
        swf_stream_head(parsed, head, length, tag_offset, &walk->main_stream);
        break;
    case SWF_TAG_STREAM_BLOCK:
        swf_stream_block(parsed, walk->main_stream, body, length);
        break;
    case SWF_TAG_BINARY:
        if (length < 6U) break;
        if (swf_add_item(parsed, SWF_KIND_COPY, SWF_STEM_BINARY, SWF_EXT_BIN,
                         id, tag_offset, &index))
            (void)swf_add_range(parsed, index, body + 6U, length - 6U);
        break;
    default:
        break;
    }
}

/* Walk the tag chain; sprites nest one level (their own sound stream). */
static bool swf_walk(swf_parsed *parsed, Abstractformat *format,
                     xx_pd_struct *pd) {
    swf_cursor *cursor;
    swf_walk_state walk;
    uint8_t head[6];
    uint64_t position, sprite_end = 0U, end = parsed->movie_size;
    uint32_t tags = 0U;
    bool in_sprite = false, result = true;
    cursor = (swf_cursor *)xx_mem_calloc(1U, sizeof(*cursor));
    if (!cursor) return false;
    cursor->parsed = parsed;
    cursor->format = format;
    xx_mem_zero(&walk, sizeof(walk));
    walk.main_stream = SWF_NONE;
    walk.sprite_stream = SWF_NONE;
    if (!swf_fetch(cursor, SWF_HEADER, head, 1U)) {
        xx_mem_free(cursor);
        return false;
    }
    position = SWF_HEADER + (5U + 4U * (uint64_t)(head[0] >> 3U) + 7U) / 8U +
               4U;
    if (position > end) {
        xx_mem_free(cursor);
        return false;
    }
    while (!parsed->overflow) {
        uint64_t level_end = in_sprite ? sprite_end : end, body, length;
        uint32_t code, header;
        if (position + 2U > level_end) {
            if (!in_sprite) break;
            in_sprite = false;
            position = sprite_end;
            walk.sprite_stream = SWF_NONE;
            continue;
        }
        if (++tags > SWF_MAX_TAGS) break;
        if ((tags & 0xFFFU) == 0U && pd && xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        if (!swf_fetch(cursor, position, head, 2U)) break;
        header = swf_le16(head);
        code = header >> 6U;
        length = header & 0x3FU;
        position += 2U;
        if (length == 0x3FU) {
            if (position + 4U > level_end ||
                !swf_fetch(cursor, position, head + 2U, 4U))
                length = UINT64_MAX;
            else
                length = swf_le32(head + 2U);
            position += 4U;
        }
        if (position > level_end || length > level_end - position) {
            /* A tag running past its sprite ends the sprite; past the
             * movie it ends the walk (a truncated movie stops here). */
            if (!in_sprite) break;
            in_sprite = false;
            position = sprite_end;
            walk.sprite_stream = SWF_NONE;
            continue;
        }
        body = position;
        position += length;
        if (code == SWF_TAG_END) {
            if (!in_sprite) break;
            in_sprite = false;
            position = sprite_end;
            walk.sprite_stream = SWF_NONE;
            continue;
        }
        if (in_sprite) {
            if (code == SWF_TAG_STREAM_HEAD || code == SWF_TAG_STREAM_HEAD2) {
                uint8_t sound[4] = {0};
                if (length >= 4U && swf_fetch(cursor, body, sound, 4U))
                    swf_stream_head(parsed, sound, length, (uint32_t)body,
                                    &walk.sprite_stream);
                else
                    walk.sprite_stream = SWF_NONE;
            } else if (code == SWF_TAG_STREAM_BLOCK) {
                swf_stream_block(parsed, walk.sprite_stream, body, length);
            }
            continue;
        }
        if (code == SWF_TAG_SPRITE) {
            /* u16 id, u16 frame count, then the sprite's own chain.  A
             * sprite inside a sprite is not defined and is skipped. */
            if (length >= 4U) {
                in_sprite = true;
                sprite_end = position;
                position = body + 4U;
                walk.sprite_stream = SWF_NONE;
            }
            continue;
        }
        swf_top_tag(parsed, cursor, &walk, code, body, length);
    }
    xx_mem_free(cursor);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Output writers                                                          */

typedef struct swf_writer_s {
    xx_io_device *out;
    uint64_t written;
    /* JPEG filter: bytes that may start an FF D9 FF D8 run. */
    bool filter;
    uint8_t pending[4];
    size_t pending_count;
    uint8_t *buffer;
    size_t buffer_used;
    bool failed;
} swf_writer;

static const uint8_t swf_seam[4] = {0xFFU, 0xD9U, 0xFFU, 0xD8U};

static bool swf_writer_flush(swf_writer *writer) {
    if (writer->buffer_used &&
        !swf_write_all(writer->out, writer->buffer, writer->buffer_used))
        writer->failed = true;
    writer->written += writer->buffer_used;
    writer->buffer_used = 0U;
    return !writer->failed;
}

static bool swf_writer_emit(swf_writer *writer, uint8_t value) {
    if (writer->buffer_used == SWF_IO_BLOCK && !swf_writer_flush(writer))
        return false;
    writer->buffer[writer->buffer_used++] = value;
    return true;
}

static bool swf_writer_put(swf_writer *writer, const uint8_t *data,
                           size_t size) {
    size_t index;
    if (writer->failed) return false;
    if (!writer->filter) {
        while (size) {
            size_t room = SWF_IO_BLOCK - writer->buffer_used, amount;
            if (room == 0U) {
                if (!swf_writer_flush(writer)) return false;
                continue;
            }
            amount = size < room ? size : room;
            xx_rt_memcpy(writer->buffer + writer->buffer_used, data, amount);
            writer->buffer_used += amount;
            data += amount;
            size -= amount;
        }
        return true;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t value = data[index];
        if (writer->pending_count == 0U && value != 0xFFU) {
            if (!swf_writer_emit(writer, value)) return false;
            continue;
        }
        writer->pending[writer->pending_count++] = value;
        while (writer->pending_count &&
               xx_rt_memcmp(writer->pending, swf_seam,
                            writer->pending_count) != 0) {
            if (!swf_writer_emit(writer, writer->pending[0])) return false;
            xx_rt_memmove(writer->pending, writer->pending + 1U,
                          writer->pending_count - 1U);
            --writer->pending_count;
        }
        if (writer->pending_count == sizeof(swf_seam))
            writer->pending_count = 0U;
    }
    return true;
}

static bool swf_writer_finish(swf_writer *writer) {
    size_t index;
    for (index = 0U; index < writer->pending_count; ++index)
        if (!swf_writer_emit(writer, writer->pending[index])) return false;
    writer->pending_count = 0U;
    return swf_writer_flush(writer);
}

static bool swf_writer_ranges(const swf_parsed *parsed, Abstractformat *format,
                              const swf_item *item, swf_writer *writer,
                              xx_pd_struct *pd) {
    uint8_t *block = (uint8_t *)xx_mem_alloc(SWF_IO_BLOCK);
    uint32_t range = item->first;
    uint32_t guard = 0U;
    bool ok = block != NULL;
    while (ok && range != SWF_NONE) {
        const swf_range *r;
        uint64_t offset, remaining;
        if (range >= parsed->range_count || ++guard > parsed->range_count) {
            ok = false;
            break;
        }
        r = &parsed->ranges[range];
        offset = r->offset;
        remaining = r->length;
        while (remaining) {
            size_t amount = remaining < SWF_IO_BLOCK ? (size_t)remaining
                                                     : SWF_IO_BLOCK;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !swf_movie_read(parsed, format, offset, block, amount) ||
                !swf_writer_put(writer, block, amount)) {
                ok = false;
                break;
            }
            offset += amount;
            remaining -= amount;
        }
        range = r->next;
    }
    if (block) xx_mem_free(block);
    return ok;
}

/* Run the ranges through the writer; out NULL only measures. */
static bool swf_emit_ranges(const swf_parsed *parsed, Abstractformat *format,
                            const swf_item *item, xx_io_device *out,
                            bool filter, uint64_t *written,
                            xx_pd_struct *pd) {
    swf_writer writer;
    bool ok;
    xx_mem_zero(&writer, sizeof(writer));
    writer.out = out;
    writer.filter = filter;
    writer.buffer = (uint8_t *)xx_mem_alloc(SWF_IO_BLOCK);
    if (!writer.buffer) return false;
    ok = swf_writer_ranges(parsed, format, item, &writer, pd) &&
         swf_writer_finish(&writer) && !writer.failed;
    xx_mem_free(writer.buffer);
    if (written) *written = writer.written;
    return ok;
}

static void swf_wav_header(uint8_t *out, uint8_t flags, uint64_t data_size) {
    static const uint32_t rates[4] = {5512U, 11025U, 22050U, 44100U};
    uint32_t rate = rates[(flags >> 2U) & 3U];
    uint32_t bits = (flags & 2U) ? 16U : 8U;
    uint32_t channels = (flags & 1U) ? 2U : 1U;
    uint32_t align = channels * bits / 8U;
    uint32_t pad = (uint32_t)(data_size & 1U);
    xx_rt_memcpy(out, "RIFF", 4U);
    swf_put_le32(out + 4U, (uint32_t)(36U + data_size + pad));
    xx_rt_memcpy(out + 8U, "WAVEfmt ", 8U);
    swf_put_le32(out + 16U, 16U);
    swf_put_le16(out + 20U, 1U);
    swf_put_le16(out + 22U, channels);
    swf_put_le32(out + 24U, rate);
    swf_put_le32(out + 28U, rate * align);
    swf_put_le16(out + 32U, align);
    swf_put_le16(out + 34U, bits);
    xx_rt_memcpy(out + 36U, "data", 4U);
    swf_put_le32(out + 40U, (uint32_t)data_size);
}

/* ---------------------------------------------------------------------- */
/* Lossless bitmap -> PNG (stored deflate blocks, streamed row by row)     */

typedef struct swf_png_s {
    xx_io_device *out;
    uint64_t written;
    bool failed;
    uint8_t format;
    bool alpha;
    uint32_t width;
    uint32_t height;
    uint32_t colors;
    uint32_t palette_bytes;
    uint32_t palette_have;
    uint8_t palette[256U * 4U];
    uint32_t in_stride;
    uint32_t row_have;
    uint32_t rows_done;
    uint8_t *row;
    uint32_t out_stride;
    uint8_t *out_row;
    uint64_t raw_total;
    uint64_t raw_done;
    uint8_t *block;
    uint32_t block_have;
    uint32_t adler;
    bool first_block;
} swf_png;

static uint32_t swf_png_channels(uint8_t format, bool alpha) {
    return format == 3U ? 1U : (format == 5U && alpha) ? 4U : 3U;
}

static uint64_t swf_png_size(const swf_item *item) {
    uint64_t stride = 1U + (uint64_t)item->width *
                               swf_png_channels(item->bitmap_format,
                                                item->alpha);
    uint64_t raw = stride * item->height;
    uint64_t blocks = (raw + SWF_STORED_MAX - 1U) / SWF_STORED_MAX;
    uint64_t size = 8U + 25U + blocks * (12U + 5U) + raw + 2U + 4U + 12U;
    if (item->bitmap_format == 3U) {
        size += 12U + 768U;
        if (item->alpha) size += 12U + 256U;
    }
    return size;
}

static bool swf_png_chunk(swf_png *png, const char *type,
                          const uint8_t *prefix, size_t prefix_size,
                          const uint8_t *data, size_t size,
                          const uint8_t *suffix, size_t suffix_size) {
    uint8_t head[8], tail[4];
    uint32_t crc;
    uint64_t length = (uint64_t)prefix_size + size + suffix_size;
    swf_put_be32(head, (uint32_t)length);
    xx_rt_memcpy(head + 4U, type, 4U);
    crc = xx_crc32_calc(0U, head + 4U, 4U);
    if (prefix_size) crc = xx_crc32_calc(crc, prefix, prefix_size);
    if (size) crc = xx_crc32_calc(crc, data, size);
    if (suffix_size) crc = xx_crc32_calc(crc, suffix, suffix_size);
    swf_put_be32(tail, crc);
    if (!swf_write_all(png->out, head, 8U) ||
        (prefix_size && !swf_write_all(png->out, prefix, prefix_size)) ||
        (size && !swf_write_all(png->out, data, size)) ||
        (suffix_size && !swf_write_all(png->out, suffix, suffix_size)) ||
        !swf_write_all(png->out, tail, 4U)) {
        png->failed = true;
        return false;
    }
    png->written += 12U + length;
    return true;
}

/* One stored deflate block per IDAT chunk; the zlib header rides in the
 * first chunk and the Adler-32 in the last. */
static bool swf_png_emit_block(swf_png *png, bool final) {
    uint8_t prefix[2U + 5U], suffix[4];
    size_t prefix_size = 0U;
    if (png->first_block) {
        prefix[prefix_size++] = 0x78U;
        prefix[prefix_size++] = 0x01U;
        png->first_block = false;
    }
    prefix[prefix_size++] = final ? 1U : 0U;
    swf_put_le16(prefix + prefix_size, png->block_have);
    swf_put_le16(prefix + prefix_size + 2U, ~png->block_have & 0xFFFFU);
    prefix_size += 4U;
    swf_put_be32(suffix, png->adler);
    if (!swf_png_chunk(png, "IDAT", prefix, prefix_size, png->block,
                       png->block_have, suffix, final ? 4U : 0U))
        return false;
    png->block_have = 0U;
    return true;
}

static bool swf_png_raw(swf_png *png, const uint8_t *data, size_t size) {
    png->adler = xx_adler32_update(png->adler, data, size);
    while (size) {
        size_t room = SWF_STORED_MAX - png->block_have;
        size_t amount = size < room ? size : room;
        xx_rt_memcpy(png->block + png->block_have, data, amount);
        png->block_have += (uint32_t)amount;
        png->raw_done += amount;
        data += amount;
        size -= amount;
        if (png->block_have == SWF_STORED_MAX &&
            !swf_png_emit_block(png, png->raw_done == png->raw_total))
            return false;
    }
    return true;
}

static uint8_t swf_unmultiply(uint32_t value, uint32_t alpha) {
    uint32_t result;
    if (alpha == 0U) return 0U;
    if (alpha == 255U) return (uint8_t)value;
    result = (value * 255U + alpha / 2U) / alpha;
    return (uint8_t)(result > 255U ? 255U : result);
}

static bool swf_png_start(swf_png *png) {
    static const uint8_t signature[8] = {0x89U, 'P', 'N', 'G',
                                         0x0DU, 0x0AU, 0x1AU, 0x0AU};
    uint8_t ihdr[13];
    uint8_t color_type = png->format == 3U ? 3U
                         : (png->format == 5U && png->alpha) ? 6U : 2U;
    if (!swf_write_all(png->out, signature, sizeof(signature))) {
        png->failed = true;
        return false;
    }
    png->written += sizeof(signature);
    swf_put_be32(ihdr, png->width);
    swf_put_be32(ihdr + 4U, png->height);
    ihdr[8] = 8U;
    ihdr[9] = color_type;
    ihdr[10] = 0U;
    ihdr[11] = 0U;
    ihdr[12] = 0U;
    return swf_png_chunk(png, "IHDR", NULL, 0U, ihdr, sizeof(ihdr), NULL, 0U);
}

/* The palette is always written with 256 entries, so that no index in the
 * pixel data can fall outside it. */
static bool swf_png_palette(swf_png *png) {
    uint8_t plte[768], trns[256];
    uint32_t index, width = png->alpha ? 4U : 3U;
    xx_mem_zero(plte, sizeof(plte));
    xx_mem_zero(trns, sizeof(trns));
    for (index = 0U; index < png->colors; ++index) {
        const uint8_t *entry = png->palette + index * width;
        uint32_t alpha = png->alpha ? entry[3] : 255U;
        plte[index * 3U] = png->alpha ? swf_unmultiply(entry[0], alpha)
                                      : entry[0];
        plte[index * 3U + 1U] = png->alpha ? swf_unmultiply(entry[1], alpha)
                                           : entry[1];
        plte[index * 3U + 2U] = png->alpha ? swf_unmultiply(entry[2], alpha)
                                           : entry[2];
        trns[index] = (uint8_t)alpha;
    }
    if (!swf_png_chunk(png, "PLTE", NULL, 0U, plte, sizeof(plte), NULL, 0U))
        return false;
    return !png->alpha ||
           swf_png_chunk(png, "tRNS", NULL, 0U, trns, sizeof(trns), NULL, 0U);
}

static bool swf_png_row(swf_png *png) {
    const uint8_t *in = png->row;
    uint8_t *out = png->out_row;
    uint32_t x;
    *out++ = 0U; /* filter: none */
    if (png->format == 3U) {
        xx_rt_memcpy(out, in, png->width);
    } else if (png->format == 4U) {
        for (x = 0U; x < png->width; ++x) {
            uint32_t value = ((uint32_t)in[2U * x] << 8U) | in[2U * x + 1U];
            uint32_t r = (value >> 10U) & 31U, g = (value >> 5U) & 31U,
                     b = value & 31U;
            *out++ = (uint8_t)((r << 3U) | (r >> 2U));
            *out++ = (uint8_t)((g << 3U) | (g >> 2U));
            *out++ = (uint8_t)((b << 3U) | (b >> 2U));
        }
    } else if (png->alpha) {
        for (x = 0U; x < png->width; ++x, in += 4U) {
            uint32_t alpha = in[0];
            *out++ = swf_unmultiply(in[1], alpha);
            *out++ = swf_unmultiply(in[2], alpha);
            *out++ = swf_unmultiply(in[3], alpha);
            *out++ = (uint8_t)alpha;
        }
    } else {
        for (x = 0U; x < png->width; ++x, in += 4U) {
            *out++ = in[1];
            *out++ = in[2];
            *out++ = in[3];
        }
    }
    png->row_have = 0U;
    ++png->rows_done;
    return swf_png_raw(png, png->out_row, png->out_stride);
}

static bool swf_png_feed(swf_png *png, const uint8_t *data, size_t size) {
    while (size && !png->failed) {
        if (png->palette_have < png->palette_bytes) {
            size_t amount = png->palette_bytes - png->palette_have;
            if (amount > size) amount = size;
            xx_rt_memcpy(png->palette + png->palette_have, data, amount);
            png->palette_have += (uint32_t)amount;
            data += amount;
            size -= amount;
            if (png->palette_have == png->palette_bytes &&
                !swf_png_palette(png))
                return false;
            continue;
        }
        if (png->rows_done >= png->height) return true;
        {
            size_t amount = png->in_stride - png->row_have;
            if (amount > size) amount = size;
            xx_rt_memcpy(png->row + png->row_have, data, amount);
            png->row_have += (uint32_t)amount;
            data += amount;
            size -= amount;
            if (png->row_have == png->in_stride && !swf_png_row(png))
                return false;
        }
    }
    return !png->failed;
}

static bool swf_write_png(const swf_parsed *parsed, Abstractformat *format,
                          const swf_item *item, xx_io_device *out,
                          uint64_t *written, xx_pd_struct *pd) {
    swf_png *png;
    swf_sink sink;
    const swf_range *range;
    uint64_t expected;
    bool ok = false;
    *written = 0U;
    if (item->first == SWF_NONE || item->first >= parsed->range_count)
        return false;
    range = &parsed->ranges[item->first];
    if (range->length < 3U) return false;
    png = (swf_png *)xx_mem_calloc(1U, sizeof(*png));
    if (!png) return false;
    png->out = out;
    png->format = item->bitmap_format;
    png->alpha = item->alpha;
    png->width = item->width;
    png->height = item->height;
    png->colors = item->colors;
    png->first_block = true;
    png->adler = 1U;
    if (png->format == 3U) {
        png->palette_bytes = png->colors * (png->alpha ? 4U : 3U);
        png->in_stride = (png->width + 3U) & ~3U;
    } else if (png->format == 4U) {
        png->in_stride = (2U * png->width + 3U) & ~3U;
    } else {
        png->in_stride = 4U * png->width;
    }
    png->out_stride = 1U + png->width * swf_png_channels(png->format,
                                                         png->alpha);
    png->raw_total = (uint64_t)png->out_stride * png->height;
    expected = png->palette_bytes + (uint64_t)png->in_stride * png->height;
    png->row = (uint8_t *)xx_mem_alloc(png->in_stride);
    png->out_row = (uint8_t *)xx_mem_alloc(png->out_stride);
    png->block = (uint8_t *)xx_mem_alloc(SWF_STORED_MAX);
    if (!png->row || !png->out_row || !png->block || !swf_png_start(png))
        goto done;
    swf_sink_init(&sink, SWF_SINK_PNG, expected);
    sink.png = png;
    if (parsed->signature == 'F') {
        (void)xx_deflate_unpack_device(
            format->device,
            format->base_address + (int64_t)range->offset + SWF_ZLIB_HEADER,
            (int64_t)range->length - SWF_ZLIB_HEADER, &sink.device, false,
            pd);
    } else {
        uint8_t *input = (uint8_t *)xx_mem_alloc(range->length);
        if (!input) goto done;
        if (swf_movie_read(parsed, format, range->offset, input,
                           range->length))
            (void)xx_deflate_unpack_memory_to_device(
                input + SWF_ZLIB_HEADER, range->length - SWF_ZLIB_HEADER,
                &sink.device, false, pd);
        xx_mem_free(input);
    }
    if (sink.failed || png->failed || !sink.full ||
        png->rows_done != png->height || (pd && xx_pd_is_stopped(pd)))
        goto done;
    if (png->block_have && !swf_png_emit_block(png, true)) goto done;
    ok = png->raw_done == png->raw_total &&
         swf_png_chunk(png, "IEND", NULL, 0U, NULL, 0U, NULL, 0U);
done:
    *written = png->written;
    if (png->row) xx_mem_free(png->row);
    if (png->out_row) xx_mem_free(png->out_row);
    if (png->block) xx_mem_free(png->block);
    xx_mem_free(png);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Whole items                                                             */

static bool swf_header_from(const swf_parsed *parsed, swf_header *h,
                            Abstractformat *format) {
    return swf_parse_header(format, h) && h->signature == parsed->signature &&
           h->file_length == parsed->file_length;
}

static bool swf_write_movie(const swf_parsed *parsed, Abstractformat *format,
                            xx_io_device *out, uint64_t *written,
                            xx_pd_struct *pd) {
    swf_header h;
    swf_sink sink;
    *written = 0U;
    if (!swf_header_from(parsed, &h, format)) return false;
    if (h.signature == 'F' && h.data_size + (int64_t)SWF_HEADER !=
                                  (int64_t)h.file_length)
        return false;
    if (!swf_write_all(out, parsed->header, SWF_HEADER)) return false;
    *written = SWF_HEADER;
    swf_sink_init(&sink, SWF_SINK_FORWARD, (uint64_t)h.file_length -
                                               SWF_HEADER);
    sink.out = out;
    (void)swf_decode_body(format, &h, &sink, false, NULL, pd);
    *written += sink.received;
    return !sink.failed && sink.full && !(pd && xx_pd_is_stopped(pd));
}

static bool swf_write_item(const swf_parsed *parsed, Abstractformat *format,
                           const swf_item *item, xx_io_device *out,
                           xx_pd_struct *pd) {
    uint64_t written = 0U, part = 0U;
    bool ok;
    switch (item->kind) {
    case SWF_KIND_MOVIE:
        ok = swf_write_movie(parsed, format, out, &written, pd);
        break;
    case SWF_KIND_COPY:
    case SWF_KIND_JPEG:
        ok = swf_emit_ranges(parsed, format, item, out,
                             item->kind == SWF_KIND_JPEG, &written, pd);
        break;
    case SWF_KIND_WAV: {
        uint8_t header[SWF_WAV_HEADER];
        swf_wav_header(header, item->sound_flags, item->data_size);
        ok = swf_write_all(out, header, sizeof(header)) &&
             swf_emit_ranges(parsed, format, item, out, false, &part, pd);
        written = sizeof(header) + part;
        if (ok && (item->data_size & 1U)) {
            static const uint8_t pad = 0U;
            ok = swf_write_all(out, &pad, 1U);
            ++written;
        }
        break;
    }
    case SWF_KIND_PNG:
        ok = swf_write_png(parsed, format, item, out, &written, pd);
        break;
    default:
        ok = false;
        break;
    }
    return ok && written == item->size;
}

/* ---------------------------------------------------------------------- */
/* Parse                                                                   */

static void swf_parsed_free(swf_parsed *parsed) {
    uint32_t index;
    if (!parsed) return;
    if (parsed->chunks) {
        for (index = 0U; index < parsed->chunk_count; ++index)
            xx_mem_free(parsed->chunks[index]);
        xx_mem_free(parsed->chunks);
    }
    if (parsed->items) xx_mem_free(parsed->items);
    if (parsed->ranges) xx_mem_free(parsed->ranges);
    xx_mem_free(parsed);
}

/* Drop streams that never got a block, number the streams, mark repeated
 * character ids and work out every member's output size. */
static bool swf_finish(swf_parsed *parsed, Abstractformat *format,
                       xx_pd_struct *pd) {
    uint32_t read, write = 0U, streams = 0U, media = 0U;
    for (read = 0U; read < parsed->item_count; ++read) {
        swf_item item = parsed->items[read];
        if (item.stem == SWF_STEM_STREAM && item.data_size == 0U) continue;
        parsed->items[write++] = item;
    }
    parsed->item_count = write;
    for (read = 0U; read < parsed->item_count; ++read) {
        swf_item *item = &parsed->items[read];
        if (item->kind == SWF_KIND_MOVIE) {
            item->size = parsed->file_length;
            continue;
        }
        ++media;
        if (item->stem == SWF_STEM_STREAM) {
            item->number = ++streams;
        } else {
            uint8_t *seen = parsed->seen[item->stem - SWF_STEM_IMAGE];
            uint8_t bit = (uint8_t)(1U << (item->id & 7U));
            item->number = media;
            item->duplicate = (seen[item->id >> 3U] & bit) != 0U;
            seen[item->id >> 3U] |= bit;
        }
        switch (item->kind) {
        case SWF_KIND_COPY:
            item->size = item->data_size;
            break;
        case SWF_KIND_JPEG:
            if (!swf_emit_ranges(parsed, format, item, NULL, true,
                                 &item->size, pd))
                return false;
            break;
        case SWF_KIND_WAV:
            item->size = SWF_WAV_HEADER + item->data_size +
                         (item->data_size & 1U);
            break;
        case SWF_KIND_PNG:
            item->size = swf_png_size(item);
            break;
        default:
            break;
        }
    }
    return true;
}

static bool swf_parse(Abstractformat *format, swf_parsed **out,
                      xx_pd_struct *pd) {
    swf_header h;
    swf_parsed *parsed;
    uint32_t index;
    *out = NULL;
    if (!swf_probe(format, &h)) return false;
    parsed = (swf_parsed *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!parsed) return false;
    parsed->signature = h.signature;
    parsed->version = h.version;
    parsed->file_length = h.file_length;
    parsed->input_size = h.input_size;
    parsed->format_size = h.format_size;
    parsed->header[0] = 'F';
    parsed->header[1] = 'W';
    parsed->header[2] = 'S';
    parsed->header[3] = h.version;
    swf_put_le32(parsed->header + 4U, h.file_length);
    if (h.signature == 'F') {
        parsed->movie_size = (uint64_t)h.format_size;
        parsed->movie_complete = h.format_size == (int64_t)h.file_length;
        parsed->media_listed = true;
    } else {
        if (!swf_add_item(parsed, SWF_KIND_MOVIE, SWF_STEM_MOVIE,
                          SWF_EXT_SWF, 0U, 0U, &index))
            goto fail;
        if (h.file_length <= XX_SWF_MAX_CACHED) {
            swf_sink sink;
            int64_t consumed = -1;
            parsed->chunks = (uint8_t **)xx_mem_calloc(
                SWF_MAX_CHUNKS, sizeof(*parsed->chunks));
            if (!parsed->chunks) goto fail;
            swf_sink_init(&sink, SWF_SINK_CACHE,
                          (uint64_t)h.file_length - SWF_HEADER);
            sink.parsed = parsed;
            (void)swf_decode_body(format, &h, &sink, true, &consumed, pd);
            if (sink.failed || (pd && xx_pd_is_stopped(pd))) goto fail;
            parsed->movie_size = SWF_HEADER + sink.received;
            parsed->movie_complete = sink.full;
            parsed->media_listed = true;
            if (h.signature == 'C' && consumed >= 0) {
                int64_t end = (int64_t)(SWF_HEADER + SWF_ZLIB_HEADER) +
                              consumed;
                /* The Adler-32 trailer, when it is there. */
                end += h.input_size - end >= 4 ? 4 : h.input_size - end;
                parsed->format_size = end;
            }
        } else {
            /* Too large to hold: only the movie itself is listed. */
            parsed->movie_size = SWF_HEADER;
            parsed->movie_complete = true;
        }
    }
    if (parsed->media_listed) {
        if (!swf_walk(parsed, format, pd)) {
            if (h.signature == 'F' || (pd && xx_pd_is_stopped(pd))) goto fail;
        }
        parsed->overflow = false;
    }
    if (!swf_finish(parsed, format, pd)) goto fail;
    *out = parsed;
    return true;
fail:
    swf_parsed_free(parsed);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

typedef struct swf_stream_s {
    uint32_t index;
    uint32_t count;
} swf_stream;

static void swf_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static void swf_item_name(const swf_item *item, char *name, size_t size) {
    const char *stem = swf_stems[item->stem];
    const char *ext = swf_extensions[item->ext];
    if (item->kind == SWF_KIND_MOVIE)
        (void)xx_rt_snprintf(name, size, "movie.%s", ext);
    else if (item->stem == SWF_STEM_STREAM)
        (void)xx_rt_snprintf(name, size, "%s_%u.%s", stem,
                             (unsigned)item->number, ext);
    else if (item->duplicate)
        (void)xx_rt_snprintf(name, size, "%s_%05u_%u.%s", stem,
                             (unsigned)item->id, (unsigned)item->number, ext);
    else
        (void)xx_rt_snprintf(name, size, "%s_%05u.%s", stem,
                             (unsigned)item->id, ext);
}

static bool swf_copy_options(xx_list_s *destination,
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

static const xx_var *swf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool swf_set_record(xx_archive_record *record, Abstractformat *format,
                           const swf_parsed *parsed, const swf_item *item) {
    char name[64];
    swf_item_name(item, name, sizeof(name));
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = -1;
    record->compressed_size = (int64_t)(item->kind == SWF_KIND_MOVIE
                                            ? (uint64_t)parsed->format_size
                                            : item->data_size);
    if (parsed->signature == 'F' && item->first != SWF_NONE) {
        record->header_offset = format->base_address + item->tag_offset;
        record->data_offset = format->base_address +
                              parsed->ranges[item->first].offset;
    }
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          item->size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

static void swf_vtable_destroy(Abstractformat *format) {
    xx_swf *archive = (xx_swf *)format;
    if (!archive) return;
    swf_parsed_free(archive->parsed);
    archive->parsed = NULL;
}

void xx_swf_init(xx_swf *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SWF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-shockwave-flash");
    xx_format_set_extension(&archive->format, "swf");
    archive->format.check_is_valid = xx_swf_check_is_valid;
    archive->format.handle_base_info = xx_swf_handle_base_info;
    archive->format.get_format_size = xx_swf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_swf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_swf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_swf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_swf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_swf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_swf_free_archive_records_reading;
    archive->format.destroy = swf_vtable_destroy;
}

xx_swf *xx_swf_create(xx_io_device *device, int64_t base_address) {
    xx_swf *archive = (xx_swf *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_swf_init(archive, device, base_address);
    return archive;
}

void xx_swf_destroy(xx_swf *archive) {
    if (!archive) return;
    swf_vtable_destroy(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_swf_free(xx_swf *archive) {
    if (!archive) return;
    xx_swf_destroy(archive);
    xx_mem_free(archive);
}

bool xx_swf_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    swf_header h;
    (void)pd;
    return swf_probe(format, &h);
}

bool xx_swf_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_swf *archive = (xx_swf *)format;
    swf_parsed *parsed;
    if (!format) return false;
    if (!archive->parsed) {
        if (!swf_parse(format, &parsed, pd)) return false;
        archive->parsed = parsed;
    }
    parsed = archive->parsed;
    archive->signature = parsed->signature;
    archive->version = parsed->version;
    archive->file_length = parsed->file_length;
    archive->movie_complete = parsed->movie_complete;
    archive->number_of_records = parsed->item_count;
    format->number_of_archive_records = parsed->item_count;
    format->format_size = parsed->format_size;
    (void)xx_rt_snprintf(format->version, sizeof(format->version), "%u",
                         (unsigned)parsed->version);
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_swf_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_swf_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_swf_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_swf_handle_base_info(format, pd))
               ? ((xx_swf *)format)->number_of_records : 0U;
}

bool xx_swf_unpack_movie_to_device(xx_swf *archive,
                                   xx_io_device *destination,
                                   xx_pd_struct *pd) {
    uint64_t written = 0U;
    if (!archive || !destination ||
        (!archive->parsed &&
         !xx_swf_handle_base_info(&archive->format, pd)))
        return false;
    return swf_write_movie(archive->parsed, &archive->format, destination,
                           &written, pd) &&
           written == archive->parsed->file_length;
}

xx_archive_record_state *xx_swf_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_swf *archive = (xx_swf *)format;
    swf_stream *stream;
    xx_archive_record_state *state;
    if (!format ||
        (!archive->parsed && !xx_swf_handle_base_info(format, pd)) ||
        !archive->parsed)
        return NULL;
    stream = (swf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->count = archive->parsed->item_count;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = swf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!swf_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count) {
        if (!swf_set_record(&state->current_record, format, archive->parsed,
                            &archive->parsed->items[0])) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_swf_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_swf_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_swf *archive = (xx_swf *)format;
    swf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !archive->parsed ||
        !(stream = (swf_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count ||
        stream->count > archive->parsed->item_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!swf_set_record(&state->current_record, format, archive->parsed,
                        &archive->parsed->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_swf_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_swf *archive = (xx_swf *)format;
    swf_stream *stream;
    const swf_item *item;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char name[64];
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !archive->parsed ||
        !(stream = (swf_stream *)state->internal_state) ||
        stream->index >= stream->count ||
        stream->index >= archive->parsed->item_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    item = &archive->parsed->items[stream->index];
    path_option = swf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return swf_write_item(archive->parsed, format, item, NULL, pd);
    /* Member names are synthesized from numbers and fixed words only, so
     * they are always safe and never collide. */
    swf_item_name(item, name, sizeof(name));
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
               ? xx_str_concat3(base, "/", name)
               : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = swf_write_item(archive->parsed, format, item, destination,
                                pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_swf_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
