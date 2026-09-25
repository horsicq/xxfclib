/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Raw Deflate compressed data: a file that is exactly one RFC 1951 stream,
 * with no zlib / gzip / ZIP wrapper.  xx_raw_deflate_compressed_data.h
 * carries the layout.
 *
 * The format has no magic, no length field and no checksum.  The only proof
 * that a file is one is the stream itself, so validity is decided by walking
 * it to its end:
 *
 *   - the walk applies the acceptance rules of zlib's inflate, which is what
 *     every producer of raw Deflate is tested against: reserved block type 3,
 *     a stored block whose LEN is not the complement of NLEN, more than 286
 *     literal/length or 30 distance codes, an over-subscribed code, an
 *     incomplete code (other than a lone one-bit literal/length or distance
 *     code), a code-length repeat with nothing to repeat or running past the
 *     table, a dynamic block with no end-of-block code, literal/length
 *     symbols 286-287, distance symbols 30-31, and a distance reaching before
 *     the first output byte are all refused;
 *   - the final block must end in the last byte of the file, and the unused
 *     high bits of that byte must be zero (every Deflate encoder pads with
 *     zero bits);
 *   - the walk keeps only the output COUNT, never the output, so a
 *     258-byte match costs one addition; it needs no window and allocates a
 *     single 64 KiB read buffer.
 *
 * The file is magic-less, so the detector can only run this as a late probe
 * on files no signature claimed.  It stays cheap on garbage: the first block
 * header is checked from the first bytes before anything is allocated, the
 * first read is 4 KiB, and random data breaks the Huffman grammar within a
 * few hundred bytes, so the rest of the file is never read.  Only a file
 * that keeps decoding is read further.
 *
 * check_is_valid is that probe, and it asks for more than the grammar:
 *   - at least RDF_MIN_PROBE_INPUT bytes.  A short run of fixed-Huffman
 *     literals ending in the 7-bit end-of-block code is easy to hit by
 *     chance: measured over 10^6 buffers per size, uniformly random ones
 *     pass the full walk at about 2e-5 from 4 to 14 bytes and 3e-6 at 32,
 *     and mostly-zero 4-byte ones (a small integer) at 2.6e-3; at 64 bytes
 *     1 in 10^7 random buffers passes and none of 10^7 text-like or 10^7
 *     mostly-zero ones do;
 *   - a non-empty payload (a file of nothing but empty blocks, such as
 *     repeated sync-flush markers, is not compressed data);
 *   - at most RDF_MAX_INPUT bytes of input and RDF_MAX_OUTPUT of plaintext.
 * handle_base_info, which a caller reaches after choosing this reader,
 * applies the grammar alone, from RDF_MIN_INPUT bytes up to
 * RDF_MAX_OPEN_INPUT bytes of input and RDF_MAX_OPEN_OUTPUT of plaintext.
 *
 * Extraction hands exactly the measured stream to the library's Deflate
 * decoder (xx_deflate_unpack_device) and requires it to produce exactly the
 * measured number of bytes, so the library codec writes every output byte.
 *
 * Written from RFC 1951; no third-party code.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/raw_deflate_compressed_data/xx_raw_deflate_compressed_data.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef RAW_DEFLATE_COMPRESSED_DATA
#define XX_RAW_DEFLATE_COMPRESSED_DATA_FILE_TYPE \
    XX_FILE_TYPE_RAW_DEFLATE_COMPRESSED_DATA
#else
#define XX_RAW_DEFLATE_COMPRESSED_DATA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RDF_PAYLOAD_NAME "payload"
/* Deflate's method number in ZIP and gzip. */
#define RDF_METHOD_DEFLATE 8U
/* Shortest stream: a final fixed block holding only end-of-block (10 bits). */
#define RDF_MIN_INPUT 2
/* The probe's floor: below it chance streams are too common (see above). */
#define RDF_MIN_PROBE_INPUT 64
/* A raw stream carries no length, so the file is the only bound on how much
 * the probe may have to read; above this the file is refused unread. */
#define RDF_MAX_INPUT ((int64_t)128 * 1024 * 1024)
/* Plaintext bound for the probe (a Deflate bomb reaches about 1032:1). */
#define RDF_MAX_OUTPUT ((uint64_t)4 * 1024 * 1024 * 1024)
/* Bounds once a caller has chosen this reader. */
#define RDF_MAX_OPEN_INPUT ((int64_t)4 * 1024 * 1024 * 1024)
#define RDF_MAX_OPEN_OUTPUT ((uint64_t)16 * 1024 * 1024 * 1024)
/* Read buffer; the first read is only RDF_FIRST_READ bytes, which is where
 * almost every non-Deflate file is refused. */
#define RDF_WINDOW ((size_t)64U * 1024U)
#define RDF_FIRST_READ ((size_t)4096U)
/* Stop checks between progress polls. */
#define RDF_POLL_MASK 0xffffU

#define RDF_MAX_BITS 15U
#define RDF_LITLEN_CODES 288U
#define RDF_DIST_CODES 32U
#define RDF_CLEN_CODES 19U

/* A canonical Huffman code as per-length counts and symbols sorted by
 * (length, symbol) -- all a canonical code needs to be decoded. */
typedef struct rdf_huffman_s {
    uint16_t count[RDF_MAX_BITS + 1U];
    uint16_t symbol[RDF_LITLEN_CODES];
} rdf_huffman;

typedef struct rdf_scanner_s {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t base;          /* device offset of the stream's first byte */
    int64_t size;          /* bytes available from base to end of file */
    uint8_t *window;
    size_t window_fill;
    size_t window_pos;
    size_t next_read;      /* size of the next window read */
    int64_t window_start;  /* stream offset of window[0] */
    uint64_t bits;         /* pulled, not yet used; LSB = next bit */
    unsigned count;        /* valid bits in `bits`; higher bits are zero */
    uint64_t produced;     /* plaintext bytes so far */
    uint64_t output_limit;
    uint64_t blocks;
    uint32_t poll;
    rdf_huffman fixed_litlen;
    rdf_huffman fixed_dist;
    rdf_huffman litlen;
    rdf_huffman dist;
    rdf_huffman clen;
} rdf_scanner;

typedef struct rdf_context_s {
    int64_t stream_offset;
    int64_t stream_size;
    uint64_t unpacked_size;
    uint64_t block_count;
} rdf_context;

typedef struct rdf_stream_s {
    rdf_context context;
    size_t index;
    size_t count;
} rdf_stream;

static const uint16_t rdf_length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t rdf_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t rdf_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t rdf_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
/* Order in which a dynamic block stores the code-length code's lengths. */
static const uint8_t rdf_clen_order[RDF_CLEN_CODES] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static bool rdf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---- Huffman codes ------------------------------------------------------ */

/* Build a canonical code from its code lengths (each 0..15).  `strict` is
 * for the code-length code, which must be complete; a literal/length or
 * distance code may be incomplete only when it is a single one-bit code,
 * and may be empty (a block with no matches has no distance codes).  An
 * over-subscribed code is always refused.  These are zlib's rules. */
static bool rdf_huffman_build(rdf_huffman *code, const uint8_t *lengths,
                              unsigned n, bool strict) {
    uint16_t offsets[RDF_MAX_BITS + 2U];
    unsigned symbol, length, longest = 0U;
    int32_t left = 1;
    xx_rt_memset(code->count, 0, sizeof(code->count));
    for (symbol = 0U; symbol < n; ++symbol) {
        if (lengths[symbol] > RDF_MAX_BITS) return false;
        code->count[lengths[symbol]]++;
    }
    if (code->count[0] == n) {
        code->count[0] = 0U;
        return !strict;
    }
    code->count[0] = 0U;
    for (length = 1U; length <= RDF_MAX_BITS; ++length) {
        left = left * 2 - (int32_t)code->count[length];
        if (left < 0) return false;
        if (code->count[length] != 0U) longest = length;
    }
    if (left > 0 && (strict || longest != 1U)) return false;
    offsets[1] = 0U;
    for (length = 1U; length <= RDF_MAX_BITS; ++length)
        offsets[length + 1U] =
            (uint16_t)(offsets[length] + code->count[length]);
    for (symbol = 0U; symbol < n; ++symbol)
        if (lengths[symbol] != 0U)
            code->symbol[offsets[lengths[symbol]]++] = (uint16_t)symbol;
    return true;
}

static void rdf_build_fixed(rdf_scanner *scanner) {
    uint8_t lengths[RDF_LITLEN_CODES];
    unsigned symbol;
    for (symbol = 0U; symbol < 144U; ++symbol) lengths[symbol] = 8U;
    for (; symbol < 256U; ++symbol) lengths[symbol] = 9U;
    for (; symbol < 280U; ++symbol) lengths[symbol] = 7U;
    for (; symbol < RDF_LITLEN_CODES; ++symbol) lengths[symbol] = 8U;
    (void)rdf_huffman_build(&scanner->fixed_litlen, lengths, RDF_LITLEN_CODES,
                            false);
    for (symbol = 0U; symbol < RDF_DIST_CODES; ++symbol) lengths[symbol] = 5U;
    (void)rdf_huffman_build(&scanner->fixed_dist, lengths, RDF_DIST_CODES,
                            false);
}

/* ---- bit input ------------------------------------------------------------ */

static bool rdf_scanner_open(rdf_scanner *scanner, xx_io_device *device,
                             xx_pd_struct *pd, int64_t base, int64_t size,
                             uint64_t output_limit) {
    xx_mem_zero(scanner, sizeof(*scanner));
    scanner->device = device;
    scanner->pd = pd;
    scanner->base = base;
    scanner->size = size;
    scanner->output_limit = output_limit;
    scanner->next_read = RDF_FIRST_READ;
    scanner->window = (uint8_t *)xx_mem_alloc(RDF_WINDOW);
    if (!scanner->window) return false;
    rdf_build_fixed(scanner);
    return true;
}

static void rdf_scanner_close(rdf_scanner *scanner) {
    if (scanner->window) xx_mem_free(scanner->window);
    scanner->window = NULL;
}

/* Stream offset of the next byte not yet pulled into the bit cache. */
static int64_t rdf_position(const rdf_scanner *scanner) {
    return scanner->window_start + (int64_t)scanner->window_pos;
}

/* Load the window at the current position.  False at the end of the stream
 * or on a read error. */
static bool rdf_fill(rdf_scanner *scanner) {
    int64_t position = rdf_position(scanner);
    int64_t left = scanner->size - position;
    size_t want = scanner->next_read;
    if (left <= 0) return false;
    if ((int64_t)want > left) want = (size_t)left;
    scanner->window_start = position;
    scanner->window_pos = 0U;
    scanner->window_fill = 0U;
    if (!rdf_read_at(scanner->device, scanner->base + position,
                     scanner->window, want))
        return false;
    scanner->window_fill = want;
    if (scanner->next_read < RDF_WINDOW) scanner->next_read *= 4U;
    if (scanner->next_read > RDF_WINDOW) scanner->next_read = RDF_WINDOW;
    return true;
}

/* Top the cache up to at least 57 bits, or with every byte left. */
static void rdf_refill(rdf_scanner *scanner) {
    while (scanner->count <= 56U) {
        if (scanner->window_pos >= scanner->window_fill &&
            !rdf_fill(scanner))
            return;
        scanner->bits |= (uint64_t)scanner->window[scanner->window_pos++]
                         << scanner->count;
        scanner->count += 8U;
    }
}

static void rdf_drop(rdf_scanner *scanner, unsigned n) {
    scanner->bits = n >= 64U ? 0U : scanner->bits >> n;
    scanner->count -= n;
}

/* Take `n` (0..16) bits, LSB first.  False when the stream ends first. */
static bool rdf_bits(rdf_scanner *scanner, unsigned n, uint32_t *value) {
    if (scanner->count < n) rdf_refill(scanner);
    if (scanner->count < n) return false;
    *value = (uint32_t)(scanner->bits & ((UINT64_C(1) << n) - 1U));
    rdf_drop(scanner, n);
    return true;
}

/* Decode one symbol, one code bit at a time.  Canonical codes of a given
 * length are consecutive integers starting at `first`, so the running code
 * is a code of this length exactly when it lies in [first, first + count).
 * -1 for a bit pattern no code uses, or when the stream ends mid-code. */
static int rdf_decode(rdf_scanner *scanner, const rdf_huffman *code) {
    uint64_t bits;
    unsigned length, available;
    int32_t value = 0, first = 0, index = 0;
    if (scanner->count < RDF_MAX_BITS) rdf_refill(scanner);
    bits = scanner->bits;
    available = scanner->count;
    for (length = 1U; length <= RDF_MAX_BITS; ++length) {
        int32_t count = (int32_t)code->count[length];
        if (length > available) return -1;
        value |= (int32_t)(bits & 1U);
        bits >>= 1U;
        if (value - first < count) {
            rdf_drop(scanner, length);
            return (int)code->symbol[index + (value - first)];
        }
        index += count;
        first = (first + count) << 1;
        value <<= 1;
    }
    return -1;
}

/* Skip `n` bytes of a stored block.  The cache is byte aligned here. */
static bool rdf_skip(rdf_scanner *scanner, uint32_t n) {
    int64_t target;
    while (n != 0U && scanner->count >= 8U) {
        rdf_drop(scanner, 8U);
        --n;
    }
    if (n == 0U) return true;
    target = rdf_position(scanner) + (int64_t)n;
    if (target > scanner->size) return false;
    if (target <= scanner->window_start + (int64_t)scanner->window_fill) {
        scanner->window_pos = (size_t)(target - scanner->window_start);
    } else {
        /* Past the window: restart it at the target on the next pull. */
        scanner->window_start = target;
        scanner->window_pos = 0U;
        scanner->window_fill = 0U;
    }
    return true;
}

static bool rdf_add_output(rdf_scanner *scanner, uint64_t amount) {
    if (amount > scanner->output_limit - scanner->produced) return false;
    scanner->produced += amount;
    return true;
}

static bool rdf_poll(rdf_scanner *scanner) {
    if ((++scanner->poll & RDF_POLL_MASK) == 0U && scanner->pd &&
        xx_pd_is_stopped(scanner->pd))
        return false;
    return true;
}

/* ---- blocks ----------------------------------------------------------------- */

static bool rdf_stored_block(rdf_scanner *scanner) {
    uint32_t length, complement;
    rdf_drop(scanner, scanner->count & 7U);
    if (!rdf_bits(scanner, 16U, &length) ||
        !rdf_bits(scanner, 16U, &complement) ||
        length != (~complement & 0xffffU) ||
        !rdf_add_output(scanner, length))
        return false;
    return rdf_skip(scanner, length);
}

/* Read a dynamic block's header into scanner->litlen / scanner->dist. */
static bool rdf_dynamic_header(rdf_scanner *scanner) {
    uint8_t lengths[RDF_LITLEN_CODES + RDF_DIST_CODES];
    uint8_t clen_lengths[RDF_CLEN_CODES];
    uint32_t hlit, hdist, hclen, value;
    unsigned index, total;
    if (!rdf_bits(scanner, 5U, &hlit) || !rdf_bits(scanner, 5U, &hdist) ||
        !rdf_bits(scanner, 4U, &hclen))
        return false;
    hlit += 257U;
    hdist += 1U;
    hclen += 4U;
    if (hlit > 286U || hdist > 30U) return false;
    xx_rt_memset(clen_lengths, 0, sizeof(clen_lengths));
    for (index = 0U; index < hclen; ++index) {
        if (!rdf_bits(scanner, 3U, &value)) return false;
        clen_lengths[rdf_clen_order[index]] = (uint8_t)value;
    }
    if (!rdf_huffman_build(&scanner->clen, clen_lengths, RDF_CLEN_CODES,
                           true))
        return false;
    total = hlit + hdist;
    index = 0U;
    while (index < total) {
        int symbol = rdf_decode(scanner, &scanner->clen);
        uint32_t repeat;
        uint8_t fill = 0U;
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16) {
            if (index == 0U || !rdf_bits(scanner, 2U, &repeat)) return false;
            fill = lengths[index - 1U];
            repeat += 3U;
        } else if (symbol == 17) {
            if (!rdf_bits(scanner, 3U, &repeat)) return false;
            repeat += 3U;
        } else {
            if (!rdf_bits(scanner, 7U, &repeat)) return false;
            repeat += 11U;
        }
        if (repeat > total - index) return false;
        while (repeat-- != 0U) lengths[index++] = fill;
    }
    /* A block that cannot end is not a block. */
    if (lengths[256] == 0U) return false;
    return rdf_huffman_build(&scanner->litlen, lengths, hlit, false) &&
           rdf_huffman_build(&scanner->dist, lengths + hlit, hdist, false);
}

static bool rdf_huffman_block(rdf_scanner *scanner, const rdf_huffman *litlen,
                              const rdf_huffman *dist) {
    for (;;) {
        int symbol;
        uint32_t extra;
        uint64_t length, distance;
        if (!rdf_poll(scanner)) return false;
        symbol = rdf_decode(scanner, litlen);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (!rdf_add_output(scanner, 1U)) return false;
            continue;
        }
        if (symbol == 256) return true;
        symbol -= 257;
        if (symbol >= 29) return false;
        if (!rdf_bits(scanner, rdf_length_extra[symbol], &extra)) return false;
        length = (uint64_t)rdf_length_base[symbol] + extra;
        symbol = rdf_decode(scanner, dist);
        if (symbol < 0 || symbol >= 30) return false;
        if (!rdf_bits(scanner, rdf_dist_extra[symbol], &extra)) return false;
        distance = (uint64_t)rdf_dist_base[symbol] + extra;
        if (distance > scanner->produced ||
            !rdf_add_output(scanner, length))
            return false;
    }
}

/* Walk the whole stream.  On success the final block ended in the last byte
 * of the input with zero padding, and the context holds the measurements. */
static bool rdf_walk(rdf_scanner *scanner, rdf_context *context) {
    uint32_t final_block = 0U, type;
    int64_t consumed;
    do {
        if (!rdf_poll(scanner) || !rdf_bits(scanner, 1U, &final_block) ||
            !rdf_bits(scanner, 2U, &type))
            return false;
        ++scanner->blocks;
        switch (type) {
            case 0U:
                if (!rdf_stored_block(scanner)) return false;
                break;
            case 1U:
                if (!rdf_huffman_block(scanner, &scanner->fixed_litlen,
                                       &scanner->fixed_dist))
                    return false;
                break;
            case 2U:
                if (!rdf_dynamic_header(scanner) ||
                    !rdf_huffman_block(scanner, &scanner->litlen,
                                       &scanner->dist))
                    return false;
                break;
            default:
                return false;
        }
    } while (final_block == 0U);
    /* Whole bytes still cached were pulled but not used; the rest of the
     * current byte is padding and must be zero. */
    if ((scanner->bits & ((UINT64_C(1) << (scanner->count & 7U)) - 1U)) != 0U)
        return false;
    consumed = rdf_position(scanner) - (int64_t)(scanner->count / 8U);
    if (consumed != scanner->size) return false;
    context->stream_size = consumed;
    context->unpacked_size = scanner->produced;
    context->block_count = scanner->blocks;
    return true;
}

/* The first block header, from the first bytes: reserved type 3, a stored
 * block whose LEN / NLEN disagree and an oversized dynamic header are all
 * refused before anything is allocated. */
static bool rdf_first_block_plausible(const uint8_t *head, size_t size) {
    unsigned type = (head[0] >> 1U) & 3U;
    if (type == 3U) return false;
    if (type == 0U) {
        if (size < 5U) return false;
        return (uint16_t)(head[1] | (head[2] << 8U)) ==
               (uint16_t)~(uint16_t)(head[3] | (head[4] << 8U));
    }
    if (type == 2U) {
        unsigned hlit = (unsigned)(head[0] >> 3U) & 31U;
        unsigned hdist = (unsigned)head[1] & 31U;
        if (size < 3U) return false;
        return hlit <= 29U && hdist <= 29U;
    }
    return true;
}

/* `probe` selects check_is_valid's bounds and extra tests (see the top of
 * the file); otherwise the grammar alone decides, within the open bounds. */
static bool rdf_parse(Abstractformat *format, rdf_context *out, bool probe,
                      xx_pd_struct *pd) {
    uint8_t head[8];
    size_t head_size;
    rdf_scanner *scanner;
    rdf_context context;
    int64_t total, size;
    bool walked;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (probe ? (size < RDF_MIN_PROBE_INPUT || size > RDF_MAX_INPUT)
              : (size < RDF_MIN_INPUT || size > RDF_MAX_OPEN_INPUT))
        return false;
    head_size = size < (int64_t)sizeof(head) ? (size_t)size : sizeof(head);
    if (!rdf_read_at(format->device, format->base_address, head, head_size) ||
        !rdf_first_block_plausible(head, head_size))
        return false;
    scanner = (rdf_scanner *)xx_mem_alloc(sizeof(*scanner));
    if (!scanner) return false;
    xx_mem_zero(&context, sizeof(context));
    context.stream_offset = format->base_address;
    walked = rdf_scanner_open(scanner, format->device, pd,
                              format->base_address, size,
                              probe ? RDF_MAX_OUTPUT : RDF_MAX_OPEN_OUTPUT) &&
             rdf_walk(scanner, &context) &&
             (!probe || context.unpacked_size != 0U);
    rdf_scanner_close(scanner);
    xx_mem_free(scanner);
    if (!walked) return false;
    *out = context;
    return true;
}

/* ---- records ---------------------------------------------------------------- */

static bool rdf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *rdf_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool rdf_set_record(xx_archive_record *record,
                           const rdf_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->stream_offset;
    record->header_size = 0;
    record->data_offset = context->stream_offset;
    record->compressed_size = context->stream_size;
    return xx_archive_record_set_original_name(record, RDF_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          RDF_METHOD_DEFLATE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void rdf_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* A write-through device that counts what the decoder writes and refuses
 * anything past the measured size. */
typedef struct rdf_counter_s {
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} rdf_counter;

static ssize_t rdf_counter_write(xx_io_device *self, const void *data,
                                 size_t size) {
    rdf_counter *counter = self ? (rdf_counter *)self->priv : NULL;
    size_t done = 0U;
    if (!counter || (!data && size != 0U) ||
        (uint64_t)size > counter->limit - counter->written ||
        size > (((size_t)-1) >> 1U))
        return -1;
    while (done < size) {
        ssize_t amount = xx_io_write(counter->target,
                                     (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) return -1;
        done += (size_t)amount;
    }
    counter->written += (uint64_t)size;
    return (ssize_t)size;
}

static bool rdf_unpack_context(Abstractformat *format,
                               const rdf_context *context,
                               xx_io_device *destination, xx_pd_struct *pd) {
    xx_io_device device;
    rdf_counter counter;
    if (!format || !format->device || !context || !destination ||
        context->stream_size < RDF_MIN_INPUT ||
        context->stream_size > RDF_MAX_OPEN_INPUT ||
        context->unpacked_size > RDF_MAX_OPEN_OUTPUT)
        return false;
    xx_mem_zero(&device, sizeof(device));
    counter.target = destination;
    counter.written = 0U;
    counter.limit = context->unpacked_size;
    device.write = rdf_counter_write;
    device.priv = &counter;
    return xx_deflate_unpack_device(format->device, context->stream_offset,
                                    context->stream_size, &device, false,
                                    pd) &&
           counter.written == context->unpacked_size;
}

/* ---- public API ------------------------------------------------------------- */

void xx_raw_deflate_compressed_data_init(
    xx_raw_deflate_compressed_data *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RAW_DEFLATE_COMPRESSED_DATA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "deflate");
    archive->format.check_is_valid =
        xx_raw_deflate_compressed_data_check_is_valid;
    archive->format.handle_base_info =
        xx_raw_deflate_compressed_data_handle_base_info;
    archive->format.get_format_size =
        xx_raw_deflate_compressed_data_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_raw_deflate_compressed_data_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_raw_deflate_compressed_data_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_raw_deflate_compressed_data_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_raw_deflate_compressed_data_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_raw_deflate_compressed_data_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_raw_deflate_compressed_data_free_archive_records_reading;
}

xx_raw_deflate_compressed_data *xx_raw_deflate_compressed_data_create(
    xx_io_device *device, int64_t base_address) {
    xx_raw_deflate_compressed_data *archive =
        (xx_raw_deflate_compressed_data *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_raw_deflate_compressed_data_init(archive, device, base_address);
    return archive;
}

void xx_raw_deflate_compressed_data_destroy(
    xx_raw_deflate_compressed_data *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_raw_deflate_compressed_data_free(
    xx_raw_deflate_compressed_data *archive) {
    if (!archive) return;
    xx_raw_deflate_compressed_data_destroy(archive);
    xx_mem_free(archive);
}

bool xx_raw_deflate_compressed_data_check_is_valid(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    rdf_context context;
    return rdf_parse(format, &context, true, pd);
}

bool xx_raw_deflate_compressed_data_handle_base_info(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    rdf_context context;
    xx_raw_deflate_compressed_data *archive;
    if (!format) return false;
    if (!rdf_parse(format, &context, false, pd)) {
        format->is_valid = false;
        format->format_size = 0;
        return false;
    }
    archive = (xx_raw_deflate_compressed_data *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = context.unpacked_size;
    archive->stream_size = context.stream_size;
    archive->block_count = context.block_count;
    format->number_of_archive_records = 1U;
    format->format_size = context.stream_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_raw_deflate_compressed_data_get_format_size(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    if (!format ||
        (!format->base_info_handled &&
         !xx_raw_deflate_compressed_data_handle_base_info(format, pd)))
        return 0;
    return format->is_valid ? format->format_size : 0;
}

uint64_t xx_raw_deflate_compressed_data_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    if (!format ||
        (!format->base_info_handled &&
         !xx_raw_deflate_compressed_data_handle_base_info(format, pd)))
        return 0U;
    return format->is_valid
               ? ((xx_raw_deflate_compressed_data *)format)->number_of_records
               : 0U;
}

/* The measurements handle_base_info cached, or a fresh walk. */
static bool rdf_measured(Abstractformat *format, rdf_context *context,
                         xx_pd_struct *pd) {
    xx_raw_deflate_compressed_data *archive =
        (xx_raw_deflate_compressed_data *)format;
    if (!format) return false;
    if (format->base_info_handled && format->is_valid &&
        archive->number_of_records == 1U) {
        xx_mem_zero(context, sizeof(*context));
        context->stream_offset = format->base_address;
        context->stream_size = archive->stream_size;
        context->unpacked_size = archive->unpacked_size;
        context->block_count = archive->block_count;
        return true;
    }
    return rdf_parse(format, context, false, pd);
}

bool xx_raw_deflate_compressed_data_unpack_to_device(
    xx_raw_deflate_compressed_data *archive, xx_io_device *destination,
    xx_pd_struct *pd) {
    rdf_context context;
    if (!archive || !destination ||
        !rdf_measured(&archive->format, &context, pd))
        return false;
    return rdf_unpack_context(&archive->format, &context, destination, pd);
}

xx_archive_record_state *
xx_raw_deflate_compressed_data_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    rdf_stream *stream;
    xx_archive_record_state *state;
    rdf_context context;
    if (!rdf_measured(format, &context, pd)) return NULL;
    stream = (rdf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rdf_stream_free;
    state->total_records = 1;
    if (!rdf_copy_options(&state->options, options) ||
        !rdf_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *
xx_raw_deflate_compressed_data_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_raw_deflate_compressed_data_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    rdf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (rdf_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) {
            xx_archive_record_cleanup(&state->current_record);
            xx_archive_record_init(&state->current_record);
            state->has_record = false;
        }
        return false;
    }
    return false;
}

bool xx_raw_deflate_compressed_data_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    rdf_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (rdf_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = rdf_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: the walk already proved the stream decodes. */
        rdf_context context;
        return rdf_parse(format, &context, false, pd);
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
               ? xx_str_concat3(base, "/", RDF_PAYLOAD_NAME)
               : xx_str_concat(base, RDF_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = rdf_unpack_context(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_raw_deflate_compressed_data_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
