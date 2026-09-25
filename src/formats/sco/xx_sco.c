/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SCO UNIX "compress -H" streams -- the LZH mode of the SCO compress(1)
 * variant, historically tagged by file(1) as "SCO compress -H (LZH) data".
 *
 *   +0  0x1F 0xA0   magic
 *   +2  ...         an LHA -lh5- coded stream, to the end of the file
 *
 * That is the whole container: two bytes. What follows is byte for byte the
 * block-structured Huffman stream LHA writes for -lh5-, with a 13-bit window
 * (8 KiB), fourteen position codes and a four-bit position-table count. The
 * layout was recovered from the U3 handler for this format: its recogniser
 * (0x004e52e0) tests the two magic bytes and a zero final word, and its
 * unpacker (0x004e5330) seeks to offset 2 and hands the rest to the shared
 * LHA decoder (0x004e5000) with dicbit 13 and pbit 4.
 *
 * There is no stored plaintext length and no checksum. A -lh5- stream
 * normally stops when the declared output size is reached; with no such size
 * the stream ends the way U3 ends it, on a block whose symbol count is zero.
 * That is the two zero bytes every sample in the corpus finishes with, and
 * check_is_valid requires them.
 *
 * Because nothing in the file asserts the plaintext, correctness here was
 * established against U3's own extraction of the corpus rather than against
 * anything the file says about itself.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sco/xx_sco.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef SCO
#define XX_SCO_FILE_TYPE XX_FILE_TYPE_SCO
#else
#define XX_SCO_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SCO_PAYLOAD_NAME "payload"
#define XX_SCO_MAGIC0 UINT8_C(0x1f)
#define XX_SCO_MAGIC1 UINT8_C(0xa0)
/* The magic is the entire header: the word behind it is already the first
 * block's symbol count and belongs to the coded stream. */
#define XX_SCO_HEADER_SIZE 2
/* Magic, one block header and the two-byte terminator. Eight is also what
 * the exclusion tests below need to look at. */
#define XX_SCO_MIN_SIZE 8
/* A runaway guard, not a format limit: nothing in the file bounds the
 * plaintext, so the decoder needs a ceiling of its own. */
#define XX_SCO_MAX_OUTPUT ((size_t)256 * 1024 * 1024)
#define XX_SCO_INITIAL_OUTPUT ((size_t)64 * 1024)

static void xx_sco_vtable_destroy(Abstractformat *self);
/* Recognition needs the decoder; the decoder is below. */
static bool xx_sco_trial_decode(Abstractformat *self, int64_t stream_offset,
                                int64_t stream_size, xx_pd_struct *pd);

static bool xx_sco_read_at(xx_io_device *device, int64_t offset, void *buffer,
                           size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Validates the header and the terminating flush and returns the extent of
 * the coded stream.  Every value handed back is bounded by the real device
 * size before it leaves this function. */
static bool xx_sco_scan(Abstractformat *self, uint32_t *header_word,
                        int64_t *stream_offset, int64_t *stream_size,
                        xx_pd_struct *pd) {
    uint8_t header[8];
    uint8_t tail[2];
    int64_t total_size;
    int64_t available;
    int64_t offset;
    int64_t size;
    uint32_t word;
    if (!self || !self->device || self->base_address < 0 || !header_word ||
        !stream_offset || !stream_size || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    available = total_size - self->base_address;
    if (available < (int64_t)XX_SCO_MIN_SIZE) return false;
    if (!xx_sco_read_at(self->device, self->base_address, header,
                        sizeof(header)) ||
        header[0] != XX_SCO_MAGIC0 || header[1] != XX_SCO_MAGIC1) {
        return false;
    }
    /* An LHA archive whose first member header happens to be 31 bytes long
     * with a checksum of 0xA0 opens with these same two bytes. It is a real
     * collision -- the corpus contains one -- and the method tag behind it
     * settles the question. The U3 recogniser carries an exclusion in this
     * same spot for the same reason, for a file starting 1F A0 45 A0 6B A0,
     * and that one is kept too. */
    if (header[4] == (uint8_t)'-' && header[7] == (uint8_t)'-' &&
        (header[5] == (uint8_t)'l' || header[5] == (uint8_t)'p')) {
        return false;
    }
    if (header[2] == 0x45U && header[3] == 0xA0U && header[4] == 0x6BU &&
        header[5] == 0xA0U) {
        return false;
    }
    /* The first block's symbol count, read the way the coder writes it. A
     * zero here would mean an empty stream that terminates before it starts,
     * which no encoder produces. */
    word = ((uint32_t)header[2] << 8U) | (uint32_t)header[3];
    if (word == 0U) return false;
    /* The stream ends on a block whose symbol count is zero, so the final
     * two bytes are always zero. */
    if (!xx_sco_read_at(self->device, self->base_address + available - 2,
                        tail, sizeof(tail)) ||
        tail[0] != 0U || tail[1] != 0U) {
        return false;
    }
    offset = self->base_address + (int64_t)XX_SCO_HEADER_SIZE;
    size = available - (int64_t)XX_SCO_HEADER_SIZE;
    /* Two bytes of magic would accept far too much. Decoding a few kilobytes
     * is what actually establishes that this is one of these files, and it is
     * also what keeps the reader from claiming a file it cannot extract. */
    if (!xx_sco_trial_decode(self, offset, size, pd)) return false;
    *header_word = word;
    *stream_offset = offset;
    *stream_size = size;
    return true;
}

/* ------------------------------------------------------------ -lh5- ----- */

/*
 * LHA -lh5-, decoded without knowing the plaintext length.
 *
 * The stream is a chain of blocks. Each one states how many symbols it codes,
 * then the three tables it codes them with, then the symbols:
 *
 *   u16  symbols in this block; zero ends the stream
 *   pre-table    19 lengths, run-length coded with a small prefix code
 *   literal tab  up to 510 lengths, coded with the pre-table
 *   position tab up to 14 lengths, with its own pre-table
 *
 * A symbol under 256 is a literal; at or above it the symbol is a match
 * length (symbol - 253) followed by a position code p, where p = 0 means
 * distance 1 and p > 0 means (1 << (p-1)) plus p-1 raw bits, plus one.
 *
 * Decoding is canonical rather than through the reference's index table plus
 * overflow tree: the reference hands out consecutive patterns per length in
 * symbol order, which is the definition of a canonical code, so the two agree
 * bit for bit.
 */

#define XX_SCO_MIN_MATCH 3
#define XX_SCO_MAX_MATCH 256
#define XX_SCO_LT_SIZE (256 + XX_SCO_MAX_MATCH - XX_SCO_MIN_MATCH + 1)
#define XX_SCO_PT_SIZE 19
#define XX_SCO_MAX_BITS 16
#define XX_SCO_WINDOW_BITS 13
/* dicbit + 1, which is what the U3 handler passes as the position count. */
#define XX_SCO_PT_COUNT (XX_SCO_WINDOW_BITS + 1)
#define XX_SCO_PT_COUNT_BITS 4
/* What a reference reaching back past the start of output resolves to: LHA
 * primes its window with spaces. */
#define XX_SCO_WINDOW_FILL 0x20U

typedef struct xx_sco_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t cache;
    int available;
    bool overrun;
} xx_sco_bits;

typedef struct xx_sco_huff_s {
    int count[XX_SCO_MAX_BITS + 1];
    int first_code[XX_SCO_MAX_BITS + 1];
    int first_index[XX_SCO_MAX_BITS + 1];
    uint16_t symbols[XX_SCO_LT_SIZE];
    int max_bits;
    bool single;
    uint16_t single_symbol;
} xx_sco_huff;

/* A plaintext of unknown length, grown as the stream produces it. */
typedef struct xx_sco_out_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
} xx_sco_out;

static void xx_sco_bits_init(xx_sco_bits *bits, const uint8_t *data,
                             size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0U;
    bits->cache = 0U;
    bits->available = 0;
    bits->overrun = false;
}

/* Past the end of input the reader yields zero bits and raises overrun; the
 * caller treats that as a truncated stream. */
static uint32_t xx_sco_read_bits(xx_sco_bits *bits, int count) {
    uint32_t result;

    if (count <= 0) return 0U;
    while (bits->available < count) {
        uint8_t next = 0U;
        if (bits->position < bits->size) {
            next = bits->data[bits->position++];
        } else {
            bits->overrun = true;
        }
        bits->cache = (bits->cache << 8) | next;
        bits->available += 8;
    }
    result = (bits->cache >> (bits->available - count)) &
             ((count >= 32) ? 0xFFFFFFFFU : ((1U << count) - 1U));
    bits->available -= count;
    return result;
}

static bool xx_sco_build(xx_sco_huff *table, const uint8_t *lengths,
                         int count) {
    int length;
    int index;
    int code = 0;
    int total = 0;
    int next_index[XX_SCO_MAX_BITS + 1];

    xx_mem_zero(table, sizeof(*table));
    for (index = 0; index < count; ++index) {
        if (lengths[index] > XX_SCO_MAX_BITS) return false;
        if (lengths[index] != 0U) {
            ++table->count[lengths[index]];
            ++total;
        }
    }
    if (total == 0) return false;
    if (total == 1) {
        for (index = 0; index < count; ++index) {
            if (lengths[index] != 0U) {
                table->single = true;
                table->single_symbol = (uint16_t)index;
                return true;
            }
        }
        return false;
    }
    for (length = 1; length <= XX_SCO_MAX_BITS; ++length) {
        table->first_code[length] = code;
        table->first_index[length] =
            (length == 1)
                ? 0
                : table->first_index[length - 1] + table->count[length - 1];
        next_index[length] = table->first_index[length];
        code += table->count[length];
        if (code > (1 << length)) return false;
        code <<= 1;
        if (table->count[length] != 0) table->max_bits = length;
    }
    /* The reference spells this as "the accumulated patterns must total
     * 0x10000": an incomplete literal or position table is a corrupt one. */
    if (code != (1 << (XX_SCO_MAX_BITS + 1))) return false;

    for (index = 0; index < count; ++index) {
        uint8_t value = lengths[index];
        if (value != 0U) table->symbols[next_index[value]++] = (uint16_t)index;
    }
    return true;
}

static int xx_sco_decode_symbol(xx_sco_bits *bits, const xx_sco_huff *table) {
    int length;
    int code = 0;

    if (table->single) return (int)table->single_symbol;
    for (length = 1; length <= table->max_bits; ++length) {
        code = (code << 1) | (int)xx_sco_read_bits(bits, 1);
        if (table->count[length] != 0 &&
            code - table->first_code[length] < table->count[length]) {
            return (int)table->symbols[table->first_index[length] +
                                       (code - table->first_code[length])];
        }
    }
    return -1;
}

/* The pre-table's own lengths use a prefix code: 0..6 in three bits, then
 * "111" followed by that many extra one bits and a zero, up to 16. */
static int xx_sco_read_pt_length(xx_sco_bits *bits) {
    int value = (int)xx_sco_read_bits(bits, 3);
    int extra = 0;

    if (value != 7) return value;
    while (extra < 10 && xx_sco_read_bits(bits, 1) != 0U) ++extra;
    if (extra >= 10) return -1;
    return 7 + extra;
}

static bool xx_sco_read_pt(xx_sco_bits *bits, xx_sco_huff *table, int size,
                           int count_bits, bool is_position) {
    uint8_t lengths[XX_SCO_PT_SIZE > 32 ? XX_SCO_PT_SIZE : 32];
    int available = (int)xx_sco_read_bits(bits, count_bits);
    int index = 0;

    if (available == 0) {
        /* No lengths: the table is one symbol, named outright. */
        int symbol = (int)xx_sco_read_bits(bits, count_bits);
        if (symbol >= size || bits->overrun) return false;
        xx_mem_zero(table, sizeof(*table));
        table->single = true;
        table->single_symbol = (uint16_t)symbol;
        return true;
    }
    if (available > size) return false;
    xx_mem_zero(lengths, sizeof(lengths));

    while (index < available) {
        int length = xx_sco_read_pt_length(bits);
        if (length < 0) return false;
        lengths[index++] = (uint8_t)length;
        /* Only the literal pre-table carries the three-entry escape: after
         * the first three lengths a two-bit count says how many are zero. */
        if (!is_position && index == 3) {
            int skip = (int)xx_sco_read_bits(bits, 2);
            if (skip > available - 3) return false;
            while (skip-- > 0) lengths[index++] = 0U;
        }
        if (bits->overrun) return false;
    }
    return xx_sco_build(table, lengths, available);
}

static bool xx_sco_read_literal(xx_sco_bits *bits, const xx_sco_huff *pre,
                                xx_sco_huff *table) {
    uint8_t lengths[XX_SCO_LT_SIZE];
    int available = (int)xx_sco_read_bits(bits, 9);
    int index = 0;

    if (available == 0) {
        int symbol = (int)xx_sco_read_bits(bits, 9);
        if (symbol >= XX_SCO_LT_SIZE || bits->overrun) return false;
        xx_mem_zero(table, sizeof(*table));
        table->single = true;
        table->single_symbol = (uint16_t)symbol;
        return true;
    }
    if (available > XX_SCO_LT_SIZE) return false;
    xx_mem_zero(lengths, sizeof(lengths));

    while (index < available) {
        int symbol = xx_sco_decode_symbol(bits, pre);
        if (symbol < 0 || bits->overrun) return false;
        if (symbol > 2) {
            lengths[index++] = (uint8_t)(symbol - 2);
        } else if (symbol == 0) {
            lengths[index++] = 0U;
        } else {
            int width = (symbol == 1) ? 4 : 9;
            int run = (int)xx_sco_read_bits(bits, width) +
                      ((width == 4) ? 3 : 20);
            if (index + run > available) return false;
            while (run-- > 0) lengths[index++] = 0U;
        }
    }
    return xx_sco_build(table, lengths, available);
}

static bool xx_sco_out_reserve(xx_sco_out *out, size_t extra) {
    size_t wanted;
    uint8_t *grown;

    if (extra > XX_SCO_MAX_OUTPUT - out->size) return false;
    wanted = out->size + extra;
    if (wanted <= out->capacity) return true;
    /* Double rather than fit exactly: the plaintext length is unknown and a
     * per-byte realloc over a megabyte of output is not acceptable. */
    {
        size_t capacity = out->capacity ? out->capacity : XX_SCO_INITIAL_OUTPUT;
        while (capacity < wanted) {
            if (capacity > XX_SCO_MAX_OUTPUT / 2U) {
                capacity = XX_SCO_MAX_OUTPUT;
                break;
            }
            capacity *= 2U;
        }
        if (capacity < wanted) return false;
        grown = (uint8_t *)xx_mem_realloc(out->data, capacity);
        if (!grown) return false;
        out->data = grown;
        out->capacity = capacity;
    }
    return true;
}

static bool xx_sco_out_byte(xx_sco_out *out, uint8_t value) {
    if (!xx_sco_out_reserve(out, 1U)) return false;
    out->data[out->size++] = value;
    return true;
}

/*
 * Decode the stream. @p limit caps how much plaintext is produced -- pass 0
 * for "no cap" -- so that validation can trial-decode a prefix without
 * materialising a whole file. @p complete reports whether the terminating
 * empty block was reached, which is the only thing that distinguishes a
 * finished stream from one that merely started plausibly.
 *
 * On success @p out owns the plaintext and the caller frees it with
 * xx_mem_free.
 */
static bool xx_sco_lzh_decode(const uint8_t *input, size_t input_size,
                              size_t limit, xx_sco_out *out, bool *complete,
                              xx_pd_struct *pd) {
    xx_sco_bits bits;
    xx_sco_huff pre;
    xx_sco_huff literal;
    xx_sco_huff position;

    out->data = NULL;
    out->size = 0U;
    out->capacity = 0U;
    if (complete) *complete = false;
    if (!input || input_size < 2U) return false;
    xx_sco_bits_init(&bits, input, input_size);

    for (;;) {
        int block_symbols;

        if (limit != 0U && out->size >= limit) return true;
        if (pd && xx_pd_is_stopped(pd)) return false;
        block_symbols = (int)xx_sco_read_bits(&bits, 16);
        if (bits.overrun) return false;
        /* The terminator: a block that codes nothing. With no stored
         * plaintext length this is the only end the stream has. */
        if (block_symbols == 0) break;

        if (!xx_sco_read_pt(&bits, &pre, XX_SCO_PT_SIZE, 5, false) ||
            !xx_sco_read_literal(&bits, &pre, &literal) ||
            !xx_sco_read_pt(&bits, &position, XX_SCO_PT_COUNT,
                            XX_SCO_PT_COUNT_BITS, true)) {
            return false;
        }

        while (block_symbols-- > 0) {
            int symbol;

            if (limit != 0U && out->size >= limit) return true;
            symbol = xx_sco_decode_symbol(&bits, &literal);
            if (symbol < 0 || bits.overrun) return false;
            if (symbol < 256) {
                if (!xx_sco_out_byte(out, (uint8_t)symbol)) return false;
                continue;
            }
            {
                int length = symbol - 256 + XX_SCO_MIN_MATCH;
                int code = xx_sco_decode_symbol(&bits, &position);
                size_t distance;
                int index;

                if (code < 0 || code >= XX_SCO_PT_COUNT || bits.overrun) {
                    return false;
                }
                if (code <= 1) {
                    distance = (size_t)code + 1U;
                } else {
                    distance = ((size_t)1U << (code - 1)) +
                               (size_t)xx_sco_read_bits(&bits, code - 1) + 1U;
                }
                if (bits.overrun) return false;
                if (distance > ((size_t)1U << XX_SCO_WINDOW_BITS)) {
                    return false;
                }
                if (!xx_sco_out_reserve(out, (size_t)length)) return false;
                for (index = 0; index < length; ++index) {
                    out->data[out->size] =
                        (distance > out->size)
                            ? (uint8_t)XX_SCO_WINDOW_FILL
                            : out->data[out->size - distance];
                    ++out->size;
                }
            }
        }
    }
    if (complete) *complete = true;
    return true;
}

/* Read the packed extent the reader already computed and decode it. */
static bool xx_sco_decode(Abstractformat *self, uint8_t **plain,
                          size_t *plain_size, xx_pd_struct *pd) {
    const xx_sco *archive;
    xx_sco_out out;
    uint8_t *packed;
    bool complete = false;

    *plain = NULL;
    *plain_size = 0U;
    if (!self || !self->is_valid || !self->base_info_handled) return false;
    archive = (const xx_sco *)self;
    if (archive->stream_offset < 0 || archive->stream_size < 2 ||
        archive->stream_size > (int64_t)XX_SCO_MAX_OUTPUT) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)archive->stream_size);
    if (!packed) return false;
    if (!xx_sco_read_at(self->device, archive->stream_offset, packed,
                        (size_t)archive->stream_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_sco_lzh_decode(packed, (size_t)archive->stream_size, 0U, &out,
                           &complete, pd) ||
        !complete) {
        xx_mem_free(out.data);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *plain = out.data;
    *plain_size = out.size;
    return true;
}

/*
 * How much of a candidate is decoded before it is believed, and how much of
 * it is read to do that. Two bytes of magic plus a zero final word is not
 * much of a gate: this is the rest of it.
 */
#define XX_SCO_TRIAL_INPUT ((size_t)64 * 1024)
#define XX_SCO_TRIAL_OUTPUT ((size_t)4 * 1024)

/* Decode the first few kilobytes of plaintext. A file that gets that far is
 * an -lh5- stream; one that does not is two bytes that happened to match. */
static bool xx_sco_trial_decode(Abstractformat *self, int64_t stream_offset,
                                int64_t stream_size, xx_pd_struct *pd) {
    xx_sco_out out;
    uint8_t *packed;
    size_t wanted;
    bool complete = false;
    bool result;

    if (stream_offset < 0 || stream_size < 2) return false;
    wanted = (stream_size > (int64_t)XX_SCO_TRIAL_INPUT)
                 ? XX_SCO_TRIAL_INPUT
                 : (size_t)stream_size;
    packed = (uint8_t *)xx_mem_alloc(wanted);
    if (!packed) return false;
    if (!xx_sco_read_at(self->device, stream_offset, packed, wanted)) {
        xx_mem_free(packed);
        return false;
    }
    /* Either the stream finished inside the prefix, or it produced the whole
     * trial quota without going wrong. Running out of bits partway is the
     * failure this is here to catch. */
    result = xx_sco_lzh_decode(packed, wanted, XX_SCO_TRIAL_OUTPUT, &out,
                               &complete, pd);
    xx_mem_free(out.data);
    xx_mem_free(packed);
    return result;
}

/* ----------------------------------------------------------- records ---- */

static bool xx_sco_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination) return false;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_sco_get_option(const xx_list_s *options,
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

static bool xx_sco_populate_record(Abstractformat *self,
                                   xx_archive_record *record) {
    const xx_sco *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    archive = (const xx_sco *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = XX_SCO_HEADER_SIZE;
    record->data_offset = archive->stream_offset;
    record->compressed_size = archive->stream_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_SCO_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)archive->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          archive->header_word) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_sco_init(xx_sco *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_SCO_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sco-compress");
    xx_format_set_extension(&archive->format, "Z");
    archive->format.check_is_valid = xx_sco_check_is_valid;
    archive->format.handle_base_info = xx_sco_handle_base_info;
    archive->format.get_format_size = xx_sco_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sco_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sco_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sco_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sco_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sco_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sco_free_archive_records_reading;
    archive->format.destroy = xx_sco_vtable_destroy;
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

xx_sco *xx_sco_create(xx_io_device *device, int64_t base_address) {
    xx_sco *archive = (xx_sco *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sco_init(archive, device, base_address);
    return archive;
}

void xx_sco_destroy(xx_sco *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->header_word = 0U;
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

static void xx_sco_vtable_destroy(Abstractformat *self) {
    xx_sco_destroy((xx_sco *)self);
}

void xx_sco_free(xx_sco *archive) {
    if (!archive) return;
    xx_sco_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sco_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint32_t header_word;
    int64_t stream_offset;
    int64_t stream_size;
    return xx_sco_scan(self, &header_word, &stream_offset, &stream_size, pd);
}

bool xx_sco_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sco *archive;
    uint32_t header_word;
    int64_t stream_offset;
    int64_t stream_size;
    if (!self) return false;
    archive = (xx_sco *)self;
    if (!xx_sco_scan(self, &header_word, &stream_offset, &stream_size, pd)) {
        archive->header_word = 0U;
        archive->stream_offset = -1;
        archive->stream_size = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->header_word = header_word;
    archive->stream_offset = stream_offset;
    archive->stream_size = stream_size;
    self->format_size = (int64_t)XX_SCO_HEADER_SIZE + stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_SCO_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_sco_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_sco_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

xx_archive_record_state *xx_sco_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_sco_copy_options(&state->options, options) ||
        !xx_sco_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_sco_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sco_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_sco_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }

    path_option = xx_sco_get_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the stream
         * without writing anything. */
        result = xx_sco_decode(self, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", XX_SCO_PAYLOAD_NAME);
    } else {
        target_path = xx_str_concat(base_path, XX_SCO_PAYLOAD_NAME);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_sco_decode(self, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
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

void xx_sco_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint32_t xx_sco_get_header_word(const xx_sco *archive) {
    return archive ? archive->header_word : 0U;
}

int64_t xx_sco_get_stream_offset(const xx_sco *archive) {
    return archive ? archive->stream_offset : -1;
}

int64_t xx_sco_get_stream_size(const xx_sco *archive) {
    return archive ? archive->stream_size : -1;
}
