/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Reader for CONCATENATED bare PKWARE Data Compression Library streams: a
 * file that is nothing but two or more implode streams laid back to back,
 * with no directory, no names, no lengths and no checksums between them.
 * DOS-era installers wrote their data volumes this way (GAMETEK.1 and
 * friends) and kept the file list in the installer itself, so the volume on
 * its own is a run of streams and nothing else.
 *
 * Each stream is the codec's own shape and nothing more:
 *
 *   0x00  u8  literal mode: 0 = fixed literals, 1 = coded literals
 *   0x01  u8  dictionary size selector: 4 (1 KiB), 5 (2 KiB) or 6 (4 KiB)
 *   0x02  n   the bit stream, ending in the length-519 end-of-stream code;
 *             the rest of that last byte is padding
 *
 * A SINGLE stream filling the file is the dclft reader's ("DCLStream"), and
 * this reader deliberately refuses it, so the two never claim the same file
 * and their order in the detector does not matter.
 *
 * Validity is decided by DECODING, exactly as for a single stream but
 * stronger: every stream must run to its own end marker, the next one must
 * start on the very next byte with a valid two-byte prelude, and the last one
 * must end on the last byte of the file. The streams carry no names, so the
 * records are numbered in file order.
 *
 * The file has no magic, so the detector runs this probe late, on files no
 * signature claimed. It must therefore reject garbage cheaply, and it must
 * never cost what the file's DECLARED contents would cost to inflate:
 *
 *   - two prelude bytes are checked before anything is allocated;
 *   - the streams are then MEASURED, not inflated: a small scanner walks the
 *     bit stream exactly as xx_dcl_decode_memory() would (same trees, same
 *     checks, same end rule, same byte accounting) but keeps only the output
 *     COUNT, so a length-518 match costs one addition instead of 518 copies;
 *   - input is pulled from the device in 64 KiB windows, first the window at
 *     the start of the file, so garbage behind a lucky prelude is rejected
 *     from that bounded prefix and the rest of the file is never read; only a
 *     file whose streams keep decoding is read further, window by window, and
 *     nothing ever holds the whole file in memory;
 *   - all the streams share one plaintext budget, so a file of many small
 *     bombs is refused as early as one big one.
 *
 * Extraction still inflates each member with xx_dcl_decode_memory() into a
 * buffer of exactly the measured size and requires it to be filled, so the
 * library decoder has the last word on every byte written.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dclraw/xx_dclraw.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef DCLRAW
#define XX_DCLRAW_FILE_TYPE XX_FILE_TYPE_DCLRAW
#else
#define XX_DCLRAW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Two prelude bytes plus the 16-bit end code: the shortest possible stream. */
#define DCLRAW_MIN_STREAM 4
/* Fewer than two streams is a single stream, which dclft owns. */
#define DCLRAW_MIN_MEMBERS 2U
#define DCLRAW_MAX_MEMBERS 65536U
/* A raw volume has no length field, so the file itself is the only bound on
 * how much the probe may have to scan, and on a member's packed size. */
#define DCLRAW_MAX_FILE ((int64_t)128 * 1024 * 1024)
/* Plaintext budget for the WHOLE file, shared by every stream. */
#define DCLRAW_MAX_OUTPUT ((size_t)512U * 1024U * 1024U)
#define DCLRAW_METHOD_DCL 1U
/* The scanner reads the file through a window of this size; the first read
 * is the bounded prefix that decides almost every non-DCL file. */
#define DCLRAW_WINDOW ((size_t)64U * 1024U)
#define DCLRAW_MAX_BITS 13U

typedef struct dclraw_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;
    bool folder;
} dclraw_member;

typedef struct dclraw_stream_s {
    dclraw_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} dclraw_stream;

/* The codec's fixed Huffman trees, in the DCL specification's run form (high
 * nibble = repeat count - 1, low nibble = code length), and its length
 * table: the same constants the library decoder uses. */
static const uint8_t dclraw_literal_runs[] = {
    11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,
    9,7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,
    7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,
    8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,
    44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,
    44,173
};
static const uint8_t dclraw_length_runs[] = { 2,35,36,53,38,23 };
static const uint8_t dclraw_distance_runs[] = { 2,20,53,230,247,151,248 };
static const uint16_t dclraw_length_base[16] = {
    3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264
};
static const uint8_t dclraw_length_extra[16] = {
    0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8
};

/* Longest code in each fixed tree; each tree is decoded through a table
 * indexed by that many upcoming stream bits. */
#define DCLRAW_LITERAL_BITS 13U
#define DCLRAW_LENGTH_BITS 7U
#define DCLRAW_DISTANCE_BITS 8U

typedef struct dclraw_tree_s {
    uint16_t count[DCLRAW_MAX_BITS + 1U];
    uint16_t symbol[256];
    unsigned symbols;
} dclraw_tree;

/* Measures streams straight from the device through one bounded window.
 * A decode-table entry is (code length << 8) | symbol; length 0 = no code. */
typedef struct dclraw_scanner_s {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t base;          /* device offset of the volume's first byte */
    int64_t size;          /* bytes in the volume */
    uint8_t *window;
    size_t window_size;
    int64_t window_start;  /* volume offset of window[0] */
    size_t window_fill;
    int64_t offset;        /* volume offset of the next byte to pull */
    uint64_t bits;         /* pulled, not yet used; LSB = next bit */
    unsigned count;        /* number of valid bits in `bits` */
    uint16_t *literal_table;  /* built on first literal-mode-1 stream */
    uint16_t length_table[1U << DCLRAW_LENGTH_BITS];
    uint16_t distance_table[1U << DCLRAW_DISTANCE_BITS];
} dclraw_scanner;

static void xx_dclraw_vtable_destroy(Abstractformat *self);

static bool dclraw_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool dclraw_prelude_ok(const uint8_t *bytes) {
    return bytes[0] <= 1U && bytes[1] >= 4U && bytes[1] <= 6U;
}

/* A member name that survives to the filesystem must be a plain relative
 * path; anything else makes the member invalid rather than renamed. */
static bool dclraw_safe_output_name(const char *name) {
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
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void dclraw_stream_free(void *opaque) {
    dclraw_stream *stream = (dclraw_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Geometric growth: a volume can hold thousands of tiny streams, and a
 * realloc per stream would make the probe quadratic. */
static bool dclraw_add_member(dclraw_stream *stream,
                              const dclraw_member *member) {
    if (!stream || !member || stream->count >= DCLRAW_MAX_MEMBERS) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        dclraw_member *grown;
        if (wanted > DCLRAW_MAX_MEMBERS) wanted = DCLRAW_MAX_MEMBERS;
        grown = (dclraw_member *)xx_mem_realloc(stream->items,
                                                wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Record geometry only; names are made once the stream count is final, so a
 * rejected file never allocates a single name. */
static bool dclraw_name_members(dclraw_stream *stream) {
    size_t index;
    for (index = 0U; index < stream->count; ++index) {
        char name[32];
        int length = xx_rt_snprintf(name, sizeof(name), "data_%05u",
                                    (unsigned)index);
        if (length <= 0 || (size_t)length >= sizeof(name)) return false;
        stream->items[index].name = xx_str_dup(name);
        if (!stream->items[index].name) return false;
    }
    return true;
}

/* ---- the measuring scanner ---------------------------------------------
 * A mirror of the library's DCL decode loop (xx_dcl.c, dcl_run) with the
 * output reduced to a counter. Every decision it makes -- tree shapes, bit
 * order, which codes are invalid, the distance and budget tests, the end
 * code, and how many input bytes a finished stream occupies -- is the
 * decoder's, so a stream it measures is one xx_dcl_decode_memory() inflates
 * to exactly the measured size.
 *
 * Only the mechanics differ, for speed: bits are pulled ahead into a 64-bit
 * cache, and a Huffman code is resolved by one table lookup on the next
 * (longest code length) bits instead of one bit at a time. Both are exact:
 *  - The decoder pulls a byte only when it needs a bit from it, so a stream
 *    it finishes after using U bits occupies ceil(U / 8) bytes; the scanner
 *    reports the bytes it pulled minus the whole bytes still cached, which is
 *    the same number.
 *  - The decoder fails for want of input exactly when a read needs more bits
 *    than remain before the end of the file; the scanner's cache holds every
 *    remaining bit up to its size, so "fewer cached bits than needed" is that
 *    same condition.
 *  - A code of length L is found in the table only when all L of its bits are
 *    real input (zeros stand in for bits past the end, and a code resolved
 *    with them is refused), and the codes are prefix-free, so the table
 *    answers exactly what the bit-by-bit walk would. */

static bool dclraw_build_tree(dclraw_tree *tree, const uint8_t *runs,
                              size_t run_count) {
    uint8_t lengths[256];
    unsigned offsets[DCLRAW_MAX_BITS + 1U];
    unsigned symbol_count = 0U;
    unsigned index, length;
    int free_codes = 1;
    xx_rt_memset(tree, 0, sizeof(*tree));
    for (index = 0U; index < run_count; ++index) {
        unsigned repeat = ((unsigned)runs[index] >> 4U) + 1U;
        length = (unsigned)runs[index] & 15U;
        if (length > DCLRAW_MAX_BITS ||
            repeat > (unsigned)sizeof(lengths) - symbol_count)
            return false;
        while (repeat--) lengths[symbol_count++] = (uint8_t)length;
    }
    if (symbol_count == 0U) return false;
    for (index = 0U; index < symbol_count; ++index) {
        if (lengths[index] == 0U) return false;
        ++tree->count[lengths[index]];
    }
    for (length = 1U; length <= DCLRAW_MAX_BITS; ++length) {
        free_codes = (free_codes << 1) - (int)tree->count[length];
        if (free_codes < 0) return false;
    }
    offsets[0] = 0U;
    offsets[1] = 0U;
    for (length = 1U; length < DCLRAW_MAX_BITS; ++length)
        offsets[length + 1U] = offsets[length] + tree->count[length];
    for (index = 0U; index < symbol_count; ++index) {
        length = lengths[index];
        tree->symbol[offsets[length]++] = (uint16_t)index;
    }
    tree->symbols = symbol_count;
    return true;
}

/* Expand a tree into a table indexed by the next @p table_bits stream bits
 * (first bit read = least significant). The codes are enumerated in the
 * order the decoder's canonical walk assigns them: at each length, codes
 * first .. first + count - 1 (bits inverted, most significant sent first)
 * map to consecutive entries of the symbol list. */
static bool dclraw_make_table(uint16_t *table, unsigned table_bits,
                              const uint8_t *runs, size_t run_count) {
    dclraw_tree tree;
    unsigned first = 0U, position = 0U, length;
    if (!dclraw_build_tree(&tree, runs, run_count)) return false;
    xx_rt_memset(table, 0, sizeof(*table) << table_bits);
    for (length = 1U; length <= DCLRAW_MAX_BITS; ++length) {
        unsigned index;
        if (length > table_bits && tree.count[length] != 0U) return false;
        for (index = 0U; index < tree.count[length]; ++index) {
            unsigned code = first + index, at = position + index;
            unsigned stream_bits = 0U, bit, fill;
            if (at >= tree.symbols) continue;
            for (bit = 0U; bit < length; ++bit)
                stream_bits |= (((code >> (length - 1U - bit)) & 1U) ^ 1U)
                               << bit;
            for (fill = stream_bits; fill < (1U << table_bits);
                 fill += 1U << length)
                table[fill] = (uint16_t)((length << 8U) | tree.symbol[at]);
        }
        position += tree.count[length];
        first = (first + tree.count[length]) << 1U;
    }
    return true;
}

static bool dclraw_scanner_open(dclraw_scanner *scanner,
                                xx_io_device *device, xx_pd_struct *pd,
                                int64_t base, int64_t size) {
    xx_mem_zero(scanner, sizeof(*scanner));
    scanner->device = device;
    scanner->pd = pd;
    scanner->base = base;
    scanner->size = size;
    scanner->window_size = size < (int64_t)DCLRAW_WINDOW ? (size_t)size
                                                          : DCLRAW_WINDOW;
    if (size <= 0 ||
        !dclraw_make_table(scanner->length_table, DCLRAW_LENGTH_BITS,
                           dclraw_length_runs, sizeof(dclraw_length_runs)) ||
        !dclraw_make_table(scanner->distance_table, DCLRAW_DISTANCE_BITS,
                           dclraw_distance_runs,
                           sizeof(dclraw_distance_runs)))
        return false;
    scanner->window = (uint8_t *)xx_mem_alloc(scanner->window_size);
    return scanner->window != NULL;
}

/* The literal tree is needed only by literal-mode-1 streams. */
static bool dclraw_scanner_literals(dclraw_scanner *scanner) {
    if (scanner->literal_table) return true;
    scanner->literal_table = (uint16_t *)xx_mem_alloc(
        sizeof(uint16_t) << DCLRAW_LITERAL_BITS);
    return scanner->literal_table &&
           dclraw_make_table(scanner->literal_table, DCLRAW_LITERAL_BITS,
                             dclraw_literal_runs,
                             sizeof(dclraw_literal_runs));
}

static void dclraw_scanner_close(dclraw_scanner *scanner) {
    if (scanner->window) xx_mem_free(scanner->window);
    if (scanner->literal_table) xx_mem_free(scanner->literal_table);
    scanner->window = NULL;
    scanner->literal_table = NULL;
}

/* Top the bit cache up to at least 57 bits, or with every byte left before
 * the end of the volume, reading the next window from the device when the
 * current one is used up. A read error or a stop request just leaves the
 * cache short, which fails the stream like running out of input. */
static void dclraw_refill(dclraw_scanner *scanner) {
    while (scanner->count <= 56U && scanner->offset < scanner->size) {
        size_t at;
        if (scanner->offset < scanner->window_start ||
            scanner->offset - scanner->window_start >=
                (int64_t)scanner->window_fill) {
            int64_t left = scanner->size - scanner->offset;
            size_t want = left < (int64_t)scanner->window_size
                              ? (size_t)left : scanner->window_size;
            scanner->window_fill = 0U;
            if ((scanner->pd && xx_pd_is_stopped(scanner->pd)) ||
                !dclraw_read_at(scanner->device,
                                scanner->base + scanner->offset,
                                scanner->window, want))
                return;
            scanner->window_start = scanner->offset;
            scanner->window_fill = want;
        }
        at = (size_t)(scanner->offset - scanner->window_start);
        while (scanner->count <= 56U && at < scanner->window_fill) {
            scanner->bits |= (uint64_t)scanner->window[at++] << scanner->count;
            scanner->count += 8U;
        }
        scanner->offset = scanner->window_start + (int64_t)at;
    }
}

/* Measure the stream starting at volume offset @p start: true when it
 * reaches its end code with at most @p limit bytes of output.
 *
 * The bit cache lives in locals for speed. Before each token the cache is
 * topped up to at least 32 bits (a token needs at most 30: flag 1, length
 * code 7, extra 8, distance code 8, distance bits 6); if it still holds
 * fewer, every bit left in the volume is already in it, so each read below
 * failing for want of bits is the decoder running out of input. */
#define DCLRAW_TAKE(n) (bits >>= (n), count -= (n))
static bool dclraw_scan_stream(dclraw_scanner *scanner, int64_t start,
                               size_t limit, int64_t *consumed,
                               size_t *produced) {
    const uint16_t *literal_table = NULL;
    uint64_t bits;
    unsigned count, literal_mode, dictionary_bits;
    size_t output_at = 0U;
    *consumed = 0;
    *produced = 0U;
    if (start < 0 || start > scanner->size || scanner->size - start < 3 ||
        limit == 0U)
        return false;
    scanner->offset = start;
    scanner->bits = 0U;
    scanner->count = 0U;
    dclraw_refill(scanner);
    bits = scanner->bits;
    count = scanner->count;
    if (count < 16U) return false;
    literal_mode = (unsigned)(bits & 0xFFU);
    dictionary_bits = (unsigned)((bits >> 8U) & 0xFFU);
    DCLRAW_TAKE(16U);
    if (literal_mode > 1U || dictionary_bits < 4U || dictionary_bits > 6U)
        return false;
    if (literal_mode == 1U) {
        if (!dclraw_scanner_literals(scanner)) return false;
        literal_table = scanner->literal_table;
    }
    for (;;) {
        unsigned entry, length;
        if (count < 32U) {
            scanner->bits = bits;
            scanner->count = count;
            dclraw_refill(scanner);
            bits = scanner->bits;
            count = scanner->count;
        }
        if (count < 1U) return false;
        if ((bits & 1U) == 0U) {                      /* literal */
            DCLRAW_TAKE(1U);
            if (!literal_table) {
                if (count < 8U) return false;
                DCLRAW_TAKE(8U);
            } else {
                entry = literal_table[bits & ((1U << DCLRAW_LITERAL_BITS) - 1U)];
                length = entry >> 8U;
                if (length == 0U || length > count) return false;
                DCLRAW_TAKE(length);
            }
            if (output_at >= limit) return false;
            ++output_at;
            continue;
        }
        DCLRAW_TAKE(1U);                              /* match */
        {
            unsigned length_symbol, extra_bits, distance_bits;
            size_t distance;
            entry = scanner->length_table[bits &
                                          ((1U << DCLRAW_LENGTH_BITS) - 1U)];
            length = entry >> 8U;
            if (length == 0U || length > count) return false;
            DCLRAW_TAKE(length);
            length_symbol = entry & 0xFFU;
            if (length_symbol >= 16U) return false;
            extra_bits = dclraw_length_extra[length_symbol];
            if (count < extra_bits) return false;
            length = dclraw_length_base[length_symbol] +
                     (unsigned)(bits & ((1U << extra_bits) - 1U));
            DCLRAW_TAKE(extra_bits);
            if (length == 519U) break;
            distance_bits = length == 2U ? 2U : dictionary_bits;
            entry = scanner->distance_table[bits &
                                            ((1U << DCLRAW_DISTANCE_BITS) - 1U)];
            if ((entry >> 8U) == 0U || (entry >> 8U) > count) return false;
            DCLRAW_TAKE(entry >> 8U);
            if (count < distance_bits) return false;
            distance = ((size_t)(entry & 0xFFU) << distance_bits) +
                       (size_t)(bits & ((1U << distance_bits) - 1U)) + 1U;
            DCLRAW_TAKE(distance_bits);
            /* The same two tests the decoder makes before it copies. */
            if (distance > output_at || length > limit - output_at)
                return false;
            output_at += length;
        }
    }
    /* Whole bytes still sitting in the bit cache belong to the next stream. */
    *consumed = (scanner->offset - start) - (int64_t)(count / 8U);
    *produced = output_at;
    return true;
}
#undef DCLRAW_TAKE

static bool dclraw_parse(Abstractformat *format, xx_pd_struct *pd,
                         dclraw_stream **result) {
    uint8_t prelude[2];
    dclraw_scanner scanner;
    dclraw_stream *stream = NULL;
    int64_t total, size, position = 0;
    size_t budget = DCLRAW_MAX_OUTPUT, produced_total = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    *result = NULL;
    if (pd && xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 2 * DCLRAW_MIN_STREAM || size > DCLRAW_MAX_FILE) return false;
    /* Two bytes decide almost every file before anything is allocated. */
    if (!dclraw_read_at(format->device, format->base_address, prelude,
                        sizeof(prelude)) ||
        !dclraw_prelude_ok(prelude))
        return false;
    if (!dclraw_scanner_open(&scanner, format->device, pd,
                             format->base_address, size)) {
        dclraw_scanner_close(&scanner);
        return false;
    }
    while (position < size) {
        dclraw_member member;
        int64_t left = size - position;
        int64_t consumed = 0;
        size_t produced = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (left < DCLRAW_MIN_STREAM) goto fail;
        /* Each stream must reach its own end marker inside the budget left
         * by the ones before it. The first one is measured from the first
         * window alone unless it keeps decoding past it. */
        if (!dclraw_scan_stream(&scanner, position, budget, &consumed,
                                &produced) ||
            consumed < DCLRAW_MIN_STREAM || consumed > left ||
            produced > budget)
            goto fail;
        /* A single stream that fills the file is dclft's, not ours. */
        if (position == 0 && consumed == left) goto fail;
        if (!stream) {
            stream = (dclraw_stream *)xx_mem_calloc(1U, sizeof(*stream));
            if (!stream) goto fail;
        }
        xx_mem_zero(&member, sizeof(member));
        member.header_offset = format->base_address + position;
        member.header_size = 2;
        member.data_offset = member.header_offset;
        member.packed_size = consumed;
        member.unpacked_size = produced;
        member.method = DCLRAW_METHOD_DCL;
        if (!dclraw_add_member(stream, &member)) goto fail;
        budget -= produced;
        produced_total += produced;
        position += consumed;
    }
    dclraw_scanner_close(&scanner);
    /* An all-empty run of end codes is not a volume of files. */
    if (!stream || stream->count < DCLRAW_MIN_MEMBERS ||
        produced_total == 0U || !dclraw_name_members(stream)) {
        dclraw_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    dclraw_scanner_close(&scanner);
    dclraw_stream_free(stream);
    return false;
}

static bool dclraw_copy_options(xx_list_s *destination,
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

static const xx_var *dclraw_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool dclraw_set_record(xx_archive_record *record,
                              const dclraw_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

/* The plaintext length was measured by the scan in parse, so the output
 * allocation is bounded by what the decoder itself produced, never by a
 * stored field. An empty member (a stream that is only its end code) decodes
 * to nothing and allocates nothing. */
static bool dclraw_decode_member(Abstractformat *format,
                                 const dclraw_member *member, uint8_t **plain,
                                 size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U, output_size;
    if (!format || !member || !plain || !plain_size ||
        member->packed_size < DCLRAW_MIN_STREAM ||
        member->packed_size > DCLRAW_MAX_FILE ||
        member->unpacked_size > (uint64_t)DCLRAW_MAX_OUTPUT)
        return false;
    *plain = NULL;
    *plain_size = 0U;
    if (member->unpacked_size == 0U) return true;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!packed || !output ||
        !dclraw_read_at(format->device, member->data_offset, packed,
                        (size_t)member->packed_size) ||
        !xx_dcl_decode_memory(packed, (size_t)member->packed_size, output,
                              output_size, &written) ||
        written != output_size)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_dclraw_init(xx_dclraw *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DCLRAW_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pkware-dcl");
    xx_format_set_extension(&archive->format, "dcl");
    archive->format.check_is_valid = xx_dclraw_check_is_valid;
    archive->format.handle_base_info = xx_dclraw_handle_base_info;
    archive->format.get_format_size = xx_dclraw_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dclraw_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dclraw_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dclraw_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dclraw_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dclraw_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dclraw_free_archive_records_reading;
    archive->format.destroy = xx_dclraw_vtable_destroy;
    archive->archive_end = -1;
}

xx_dclraw *xx_dclraw_create(xx_io_device *device, int64_t base_address) {
    xx_dclraw *archive = (xx_dclraw *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_dclraw_init(archive, device, base_address);
    return archive;
}

void xx_dclraw_destroy(xx_dclraw *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_dclraw_free(xx_dclraw *archive) {
    if (!archive) return;
    xx_dclraw_destroy(archive);
    xx_mem_free(archive);
}

static void xx_dclraw_vtable_destroy(Abstractformat *self) {
    xx_dclraw_destroy((xx_dclraw *)self);
}

bool xx_dclraw_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    dclraw_stream *stream;
    if (!dclraw_parse(format, pd, &stream)) return false;
    dclraw_stream_free(stream);
    return true;
}

bool xx_dclraw_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    dclraw_stream *stream;
    xx_dclraw *archive;
    if (!format) return false;
    format->base_info_handled = true;
    if (!dclraw_parse(format, pd, &stream)) {
        format->is_valid = false;
        format->format_size = 0;
        return false;
    }
    archive = (xx_dclraw *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    dclraw_stream_free(stream);
    return true;
}

int64_t xx_dclraw_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    if (!format ||
        (!format->base_info_handled && !xx_dclraw_handle_base_info(format, pd)))
        return 0;
    return format->is_valid ? format->format_size : 0;
}

uint64_t xx_dclraw_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    if (!format ||
        (!format->base_info_handled && !xx_dclraw_handle_base_info(format, pd)))
        return 0U;
    return format->is_valid ? ((xx_dclraw *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_dclraw_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dclraw_stream *stream;
    xx_archive_record_state *state;
    if (!dclraw_parse(format, pd, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        dclraw_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = dclraw_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!dclraw_copy_options(&state->options, options) ||
        !dclraw_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_dclraw_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_dclraw_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    dclraw_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (dclraw_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = dclraw_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dclraw_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    dclraw_stream *stream;
    dclraw_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (dclraw_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!dclraw_safe_output_name(member->name) ||
        !dclraw_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = dclraw_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member. */
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
        created = destination != NULL;
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
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_dclraw_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
