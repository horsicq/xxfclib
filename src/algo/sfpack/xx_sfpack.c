/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SFPack (.sfpack) SoundFont rebuilder.  Ported one-for-one from the XArchive
 * reference decoder (XArchive/Algos/xsfpackdecoder.cpp): the LZW chunk codec,
 * the predictive sample codec, the shdr patch-up and the RIFF layout are all
 * reproduced exactly.  The only structural change is that the reference builds
 * growable QByteArrays while this writes straight into the caller's buffer,
 * which is why the output size is computed up front (the same computation the
 * reference's measure() performs) instead of falling out of the append calls.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/sfpack/xx_sfpack.h"

#define SFPACK_LZW_TABLE 0x1000
#define SFPACK_LZW_LAST 0x0fff
#define SFPACK_MAX_BLOCK 0x800
#define SFPACK_MAX_UNARY 4096
#define SFPACK_MAX_CHUNK ((int64_t)0x08000000)
#define SFPACK_MAX_UNCOMPRESSED ((int64_t)0x40000000)
#define SFPACK_SHDR_RECORD_SIZE 0x2e

/* ------------------------------------------------------------------ misc - */

static uint32_t sf_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t sf_read16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static void sf_write32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xffU);
    data[1] = (uint8_t)((value >> 8) & 0xffU);
    data[2] = (uint8_t)((value >> 16) & 0xffU);
    data[3] = (uint8_t)((value >> 24) & 0xffU);
}

/* The reference reads these container fields through (qint32) casts, so a word
 * with bit 31 set becomes a negative number that the range tests then reject.
 * Reproduced: an unsigned read here would accept files the reference refuses. */
static int64_t sf_read32_signed(const uint8_t *data) {
    return (int64_t)(int32_t)sf_read32(data);
}

static bool sf_has_range(int64_t buffer_size, int64_t offset, int64_t size) {
    return (offset >= 0) && (size >= 0) && (offset <= buffer_size) &&
           (size <= (buffer_size - offset));
}

/* --------------------------------------------------- codec 1: chunk LZW --- */

/* mode 1: refill one byte, hand out bits MSB first.  -1 on exhaustion. */
typedef struct sf_bits_byte_s {
    const uint8_t *data;
    int64_t size;
    int64_t position;
    uint8_t current;
    int32_t count;
} sf_bits_byte;

static int32_t sf_byte_get(sf_bits_byte *bits, int32_t width) {
    int32_t value = 0;
    int32_t i;
    for (i = 0; i < width; ++i) {
        if (bits->count == 0) {
            if (bits->position >= bits->size) return -1;
            bits->current = bits->data[bits->position];
            ++bits->position;
            bits->count = 8;
        }
        value = (value << 1) | ((bits->current >> 7) & 1);
        bits->current = (uint8_t)((uint32_t)bits->current << 1);
        --bits->count;
    }
    return value;
}

typedef struct sfpack_lzw_s {
    uint16_t prefix[SFPACK_LZW_TABLE];
    uint8_t suffix[SFPACK_LZW_TABLE];
    uint8_t stack[SFPACK_LZW_TABLE];
} sfpack_lzw;

/* Textbook LZW with NO CONTROL CODES AT ALL: the first free code is 0x100, so
 * the 256 literals are the whole initial alphabet.  Two details are load
 * bearing and must not be "fixed": the width grows when free + 1 reaches the
 * width (one code EARLY compared with a textbook coder), and when free reaches
 * 0xfff the entire table is thrown away, the width drops back to 9 and the
 * next code is a fresh 9-bit literal -- a full reset, not a partial clear. */
static bool sfpack_decode_chunk(const uint8_t *packed, int64_t packed_size,
                                int64_t unpacked_size, uint8_t *out,
                                sfpack_lzw *table) {
    sf_bits_byte bits;
    int64_t left = unpacked_size;
    size_t produced = 0U;

    if ((unpacked_size < 0) || (unpacked_size > SFPACK_MAX_CHUNK)) return false;
    if (unpacked_size == 0) return true;
    if ((packed_size < 0) || (!packed && (packed_size > 0))) return false;

    bits.data = packed;
    bits.size = packed_size;
    bits.position = 0;
    bits.current = 0U;
    bits.count = 0;

    for (;;) { /* one pass per full-table reset */
        int32_t width = 9;
        int32_t code;
        int32_t first;
        int32_t previous;
        int32_t free_code;
        bool reset = false;

        xx_rt_memset(table->prefix, 0, sizeof(table->prefix));
        xx_rt_memset(table->suffix, 0, sizeof(table->suffix));

        code = sf_byte_get(&bits, 9);
        if ((code < 0) || (code > 0xff)) return false;
        out[produced++] = (uint8_t)code;
        --left;
        first = code;
        previous = code;
        free_code = 0x100;

        while (!reset) {
            int32_t current;
            int32_t last = 0;
            bool grow;

            if (left < 1) return (left == 0);
            if (free_code == SFPACK_LZW_LAST) {
                reset = true; /* table full - start over from scratch */
                break;
            }

            current = sf_byte_get(&bits, width);
            if (current < 0) return false;

            if (current < 0x100) {
                out[produced++] = (uint8_t)current;
                --left;
                last = current;
            } else {
                int32_t stack_size = 0;
                int32_t c = current;
                int32_t i;
                if (c >= free_code) {
                    if (c > free_code) return false;
                    table->stack[stack_size++] = (uint8_t)first;
                    c = previous;
                }
                while (c > 0xff) {
                    if ((c >= SFPACK_LZW_TABLE) ||
                        (stack_size >= SFPACK_LZW_TABLE)) {
                        return false;
                    }
                    table->stack[stack_size++] = table->suffix[c];
                    c = (int32_t)table->prefix[c];
                }
                if (stack_size >= SFPACK_LZW_TABLE) return false;
                table->stack[stack_size++] = (uint8_t)c;
                last = c;
                /* The reference lets its remaining-byte counter go negative
                 * here and rejects at the top of the next iteration; writing
                 * past the caller's buffer is not an option, so the identical
                 * rejection happens one step earlier.  Same accept set. */
                if ((int64_t)stack_size > left) return false;
                left -= stack_size;
                for (i = stack_size - 1; i >= 0; --i) {
                    out[produced++] = table->stack[i];
                }
            }

            table->prefix[free_code] = (uint16_t)previous;
            table->suffix[free_code] = (uint8_t)last;
            /* one code EARLY: the step happens when free + 1 reaches the width */
            grow = ((free_code + 2) == (1 << width));
            ++free_code;
            first = last;
            previous = current;
            if (grow) ++width;
        }
    }
}

/* ------------------------------------------------- codec 2: the samples --- */

/* mode 4: refill a 32-bit LITTLE-ENDIAN word, hand out bits from bit 31 down.
 * A plain MSB-first byte reader desynchronises immediately. */
typedef struct sf_bits_word_s {
    const uint8_t *data;
    int64_t size;
    int64_t position;
    uint32_t current;
    int32_t count;
} sf_bits_word;

/* -1 means the stream is exhausted */
static int64_t sf_word_get(sf_bits_word *bits, int32_t width) {
    int64_t value = 0;
    int32_t i;
    if (width == 0) return 0;
    for (i = 0; i < width; ++i) {
        if (bits->count == 0) {
            if ((bits->position < 0) || ((bits->position + 4) > bits->size)) {
                return -1;
            }
            bits->current = sf_read32(bits->data + bits->position);
            bits->position += 4;
            bits->count = 32;
        }
        value = (value << 1) | (int64_t)((bits->current >> 31) & 1U);
        bits->current = (uint32_t)(bits->current << 1);
        --bits->count;
    }
    return value;
}

/* gamma(k): q leading zero bits (the terminating 1 is consumed), then k raw
 * bits r; value = (q << k) | r.  Results are carried as int64 because a
 * legitimate value can use all 32 bits and -1 is the failure code. */
static int64_t sf_gamma(sf_bits_word *bits, int32_t k) {
    int32_t q = 0;
    int64_t rest;
    int64_t value;
    for (;;) {
        const int64_t bit = sf_word_get(bits, 1);
        if (bit < 0) return -1;
        if (bit) break;
        ++q;
        /* the reference has no bound here at all; this only stops a runaway on
         * a desynced stream and must stay loose, because a cold-start residual
         * with a small k routinely needs a q in the tens */
        if (q > SFPACK_MAX_UNARY) return -1;
    }
    rest = sf_word_get(bits, k);
    if (rest < 0) return -1;
    value = rest | ((int64_t)q << k);
    if (value > 0xffffffffLL) return -1;
    return value;
}

/* sgamma(k): v = gamma(k + 1); zigzag - v even -> v >> 1, v odd -> ~(v >> 1) */
static bool sf_sgamma(sf_bits_word *bits, int32_t k, int32_t *result) {
    const int64_t value = sf_gamma(bits, k + 1);
    uint32_t half;
    if (value < 0) return false;
    half = (uint32_t)(value >> 1);
    if (value & 1) half = ~half;
    *result = (int32_t)half;
    return true;
}

/* uval(): k = gamma(2); return gamma(k) */
static int64_t sf_uval(sf_bits_word *bits) {
    const int64_t k = sf_gamma(bits, 2);
    if ((k < 0) || (k > 32)) return -1;
    return sf_gamma(bits, (int32_t)k);
}

typedef struct sfpack_sample_s {
    int32_t *buffer;
    int32_t *coefficients;
} sfpack_sample;

/* A lossless predictive audio coder over 32-bit accumulators emitting 16-bit
 * little-endian PCM.  All the arithmetic is deliberately done on uint32 so the
 * wraparound the reference relies on is well defined here. */
static bool sfpack_decode_sample(const uint8_t *file, int64_t file_size,
                                 int64_t offset, uint8_t *out, size_t out_cap,
                                 size_t *out_len) {
    sf_bits_word bits;
    sfpack_sample scratch;
    int64_t block_size;
    int64_t max_order;
    int64_t dc;
    int32_t max_count;
    int32_t order_max;
    int32_t history;
    uint32_t acc1 = 0U;
    uint32_t acc2;
    int32_t int_mode = 0;
    int32_t shift = 0;
    int32_t count;
    size_t produced = 0U;
    bool ok = false;

    *out_len = 0U;
    if ((offset < 0) || (offset >= file_size)) return false;

    bits.data = file;
    bits.size = file_size;
    bits.position = offset;
    bits.current = 0U;
    bits.count = 0;

    block_size = sf_uval(&bits);
    if (block_size < 0) return false;
    max_order = sf_uval(&bits);
    if (max_order < 0) return false;
    if ((block_size > SFPACK_MAX_BLOCK) || (max_order >= block_size)) return false;
    dc = sf_uval(&bits);
    if (dc < 0) return false;

    max_count = (int32_t)block_size;
    order_max = (int32_t)max_order;
    history = (order_max >= 3) ? order_max : 3;
    acc2 = (uint32_t)dc - 33000U;
    count = max_count;

    scratch.buffer = (int32_t *)xx_mem_alloc(
        (size_t)(history + max_count) * sizeof(int32_t));
    scratch.coefficients = (int32_t *)xx_mem_alloc(
        (size_t)((order_max > 0) ? order_max : 1) * sizeof(int32_t));
    if (!scratch.buffer || !scratch.coefficients) goto done;
    xx_rt_memset(scratch.buffer, 0,
                 (size_t)(history + max_count) * sizeof(int32_t));
    xx_rt_memset(scratch.coefficients, 0,
                 (size_t)((order_max > 0) ? order_max : 1) * sizeof(int32_t));

    for (;;) {
        int32_t k = 0;
        int32_t i;
        const int64_t command = sf_gamma(&bits, 2);
        if (command < 0) goto done;
        if (command == 9) {
            ok = true;
            goto done;
        }

        if ((command > 4) && (command != 7)) {
            int64_t value;
            if (command == 5) {
                value = sf_gamma(&bits, 0);
                if (value < 0) goto done;
                int_mode = (value > 0x7fffffffLL) ? 0x7fffffff : (int32_t)value;
            } else if (command == 6) {
                value = sf_gamma(&bits, 2);
                if ((value < 0) || (value > 31)) goto done;
                shift = (int32_t)value;
            } else if (command == 8) {
                value = sf_uval(&bits);
                if ((value < 0) || (value > max_count)) goto done;
                count = (int32_t)value;
            } else {
                goto done;
            }
            continue;
        }

        if (command != 7) {
            const int64_t value = sf_gamma(&bits, 3);
            if ((value < 0) || (value > 31)) goto done;
            k = (int32_t)value;
        }

        if (command == 0) {
            for (i = 0; i < count; ++i) {
                int32_t residual;
                if (!sf_sgamma(&bits, k, &residual)) goto done;
                scratch.buffer[history + i] = residual;
            }
        } else if (command == 1) {
            for (i = 0; i < count; ++i) {
                int32_t residual;
                if (!sf_sgamma(&bits, k, &residual)) goto done;
                scratch.buffer[history + i] = (int32_t)(
                    (uint32_t)residual + (uint32_t)scratch.buffer[history + i - 1]);
            }
        } else if (command == 2) {
            for (i = 0; i < count; ++i) {
                int32_t residual;
                if (!sf_sgamma(&bits, k, &residual)) goto done;
                scratch.buffer[history + i] = (int32_t)(
                    (uint32_t)residual +
                    2U * (uint32_t)scratch.buffer[history + i - 1] -
                    (uint32_t)scratch.buffer[history + i - 2]);
            }
        } else if (command == 3) {
            for (i = 0; i < count; ++i) {
                int32_t residual;
                uint32_t delta;
                if (!sf_sgamma(&bits, k, &residual)) goto done;
                delta = (uint32_t)scratch.buffer[history + i - 1] -
                        (uint32_t)scratch.buffer[history + i - 2];
                scratch.buffer[history + i] = (int32_t)(
                    (uint32_t)residual +
                    (uint32_t)scratch.buffer[history + i - 3] + 3U * delta);
            }
        } else if (command == 4) {
            int32_t order;
            int32_t j;
            const int64_t order_value = sf_gamma(&bits, 3);
            if ((order_value < 0) || (order_value > max_order)) goto done;
            order = (int32_t)order_value;
            for (j = 0; j < order; ++j) {
                int32_t coefficient;
                if (!sf_sgamma(&bits, 5, &coefficient)) goto done;
                scratch.coefficients[j] = coefficient;
            }
            for (i = 0; i < count; ++i) {
                uint32_t sum = 32U;
                int32_t residual;
                for (j = 0; j < order; ++j) {
                    sum += (uint32_t)scratch.coefficients[j] *
                           (uint32_t)scratch.buffer[history + i - 1 - j];
                }
                if (!sf_sgamma(&bits, k, &residual)) goto done;
                /* arithmetic shift, so it floors */
                scratch.buffer[history + i] =
                    (int32_t)((uint32_t)residual + (uint32_t)((int32_t)sum >> 5));
            }
        } else { /* command == 7: a block of n zeros */
            for (i = 0; i < count; ++i) scratch.buffer[history + i] = 0;
        }

        /* THE PREDICTOR HISTORY IS SNAPSHOTTED BEFORE THE SHIFT AND THE
         * INTEGRATION: the predictor works in the residual domain, so taking
         * the history after integration silently produces plausible but wrong
         * audio.  Deliberate - do not move this below. */
        for (i = 0; i < history; ++i) {
            scratch.buffer[i] = scratch.buffer[i + count];
        }
        if (shift) {
            for (i = 0; i < count; ++i) {
                scratch.buffer[history + i] = (int32_t)(
                    (uint32_t)scratch.buffer[history + i] << shift);
            }
        }
        if (int_mode == 0) {
            for (i = 0; i < count; ++i) {
                acc1 = (uint32_t)scratch.buffer[history + i];
                acc2 = acc1 + acc2;
                scratch.buffer[history + i] = (int32_t)acc2;
            }
        } else if (int_mode == 1) {
            for (i = 0; i < count; ++i) {
                acc1 = (uint32_t)scratch.buffer[history + i] + acc1;
                acc2 = acc1 + acc2;
                scratch.buffer[history + i] = (int32_t)acc2;
            }
        }

        /* The reference only caps this against its 1 GiB ceiling because it
         * appends to a growable buffer; running out of the caller's capacity
         * is the same rejection, just earlier - a sample longer than its slot
         * makes the reference's final total check fail anyway. */
        if ((size_t)count > ((out_cap - produced) / 2U)) goto done;
        for (i = 0; i < count; ++i) {
            const uint32_t sample = (uint32_t)scratch.buffer[history + i];
            out[produced++] = (uint8_t)(sample & 0xffU);
            out[produced++] = (uint8_t)((sample >> 8) & 0xffU);
        }
    }

done:
    xx_mem_free(scratch.buffer);
    xx_mem_free(scratch.coefficients);
    if (ok) *out_len = produced;
    return ok;
}

/* ------------------------------------------------------------ container --- */

typedef struct sfpack_header_s {
    int64_t info_offset; /* first byte of the INFO codec-1 payload */
    int64_t info_packed;
    int64_t info_size; /* unpacked INFO length */
    int64_t pdta_offset;
    int64_t pdta_packed;
    int64_t pdta_size;
    int64_t table_offset; /* the i32 sample stream offsets */
    int64_t structure_size;
    int64_t declared_size; /* header +8; unreliable, see the header comment */
    int32_t sample_count;
    uint16_t flags;
} sfpack_header;

static bool sfpack_parse_header(const uint8_t *buffer, int64_t size,
                                sfpack_header *header) {
    static const char sf_magic[4] = {'S', 'F', 'P', 'K'};
    static const char sf_info[4] = {'I', 'N', 'F', 'O'};
    static const char sf_pdta[4] = {'p', 'd', 't', 'a'};
    int64_t position;
    int64_t skip;
    int64_t table_bytes;
    int32_t chunk;
    int32_t i;

    if (size < 16) return false;
    if (xx_rt_memcmp(buffer, sf_magic, 4) != 0) return false;

    xx_rt_memset(header, 0, sizeof(*header));
    header->flags = sf_read16(buffer + 6);
    /* encrypted; the reference refuses it too */
    if (header->flags & 4) return false;
    header->declared_size = sf_read32_signed(buffer + 8);

    position = 16;
    for (chunk = 0; chunk < 2; ++chunk) {
        int64_t a;
        int64_t unpacked;
        int64_t next;
        if (!sf_has_range(size, position, 12)) return false;
        a = sf_read32_signed(buffer + position);
        unpacked = sf_read32_signed(buffer + position + 8);
        if (xx_rt_memcmp(buffer + position + 4,
                         (chunk == 0) ? sf_info : sf_pdta, 4) != 0) {
            return false;
        }
        if ((a <= 8) || (unpacked <= 0) || (unpacked > SFPACK_MAX_CHUNK)) {
            return false;
        }
        /* A is a byte count measured from +0x04, so the next chunk starts at
         * chunkStart + 4 + A and the compressed payload is A - 8 bytes long. */
        next = position + 4 + a;
        if ((next <= position) || (next > size)) return false;
        if (chunk == 0) {
            header->info_offset = position + 12;
            header->info_packed = a - 8;
            header->info_size = unpacked;
        } else {
            header->pdta_offset = position + 12;
            header->pdta_packed = a - 8;
            header->pdta_size = unpacked;
        }
        position = next;
    }

    if (!sf_has_range(size, position, 4)) return false;
    skip = sf_read32_signed(buffer + position);
    position += 4;
    if (skip <= 0) return false;
    if (skip > (size - position)) return false;
    position += skip;

    if (!sf_has_range(size, position, 4)) return false;
    table_bytes = sf_read32_signed(buffer + position);
    position += 4;
    if ((table_bytes <= 0) || (table_bytes & 3)) return false;
    if (table_bytes > (size - position)) return false;

    header->table_offset = position;
    header->sample_count = (int32_t)(table_bytes >> 2);
    header->structure_size = position + table_bytes;

    for (i = 0; i < header->sample_count; ++i) {
        const int64_t sample_offset =
            sf_read32_signed(buffer + header->table_offset + (int64_t)i * 4);
        if ((sample_offset < 1) || (sample_offset >= size)) return false;
    }

    return true;
}

/* walk the decompressed pdta looking for the shdr sub-chunk */
static int64_t sfpack_find_shdr(const uint8_t *pdta, int64_t pdta_size,
                                int32_t count) {
    static const char sf_shdr[4] = {'s', 'h', 'd', 'r'};
    int64_t position = 0;
    while ((pdta_size - position) >= 8) {
        const int64_t size = sf_read32_signed(pdta + position + 4);
        if ((size < 0) || (size > (pdta_size - position - 8))) return -1;
        if (xx_rt_memcmp(pdta + position, sf_shdr, 4) == 0) {
            if (size != ((int64_t)count + 1) * SFPACK_SHDR_RECORD_SIZE) return -1;
            return position + 8;
        }
        position += 8 + size;
    }
    return -1;
}

/* smplWords = sum over non-ROM samples of (dwEnd - dwStart + 23), where the 23
 * is the 46 appended zero bytes.  A sample whose sfSampleType (shdr +0x2c) has
 * bit 15 set is ROM: it is skipped entirely and its shdr record is left
 * alone. */
static bool sfpack_sample_words(const uint8_t *pdta, int64_t pdta_size,
                                int64_t shdr, int32_t count, int64_t *words) {
    int64_t total = 0;
    int32_t i;
    for (i = 0; i < count; ++i) {
        const int64_t record = shdr + (int64_t)i * SFPACK_SHDR_RECORD_SIZE;
        int64_t start;
        int64_t end;
        uint16_t type;
        if ((record < 0) || ((record + SFPACK_SHDR_RECORD_SIZE) > pdta_size)) {
            return false;
        }
        type = sf_read16(pdta + record + 0x2c);
        if (type & 0x8000) continue;
        start = sf_read32_signed(pdta + record + 0x14);
        end = sf_read32_signed(pdta + record + 0x18);
        total += (end - start) + 23;
        if ((total < 0) || (total > (SFPACK_MAX_UNCOMPRESSED / 2))) return false;
    }
    *words = total;
    return true;
}

/* Header + pdta only: decompresses the pdta chunk into a scratch block the
 * caller owns and reports the exact rebuilt size. */
static bool sfpack_prepare(const uint8_t *input, int64_t input_size,
                           sfpack_header *header, uint8_t **pdta,
                           int64_t *shdr, int64_t *words, int64_t *total) {
    sfpack_lzw *table;
    uint8_t *scratch;
    bool ok = false;

    *pdta = NULL;
    if (!sfpack_parse_header(input, input_size, header)) return false;
    if (!sf_has_range(input_size, header->pdta_offset, header->pdta_packed)) {
        return false;
    }

    scratch = (uint8_t *)xx_mem_alloc((size_t)header->pdta_size);
    table = (sfpack_lzw *)xx_mem_alloc(sizeof(sfpack_lzw));
    if (!scratch || !table) goto done;
    if (!sfpack_decode_chunk(input + header->pdta_offset, header->pdta_packed,
                             header->pdta_size, scratch, table)) {
        goto done;
    }

    *shdr = sfpack_find_shdr(scratch, header->pdta_size, header->sample_count);
    if (*shdr < 0) goto done;
    if (!sfpack_sample_words(scratch, header->pdta_size, *shdr,
                             header->sample_count, words)) {
        goto done;
    }

    *total = 56 + header->info_size + 2 * (*words) + header->pdta_size;
    if ((*total < 0) || (*total > SFPACK_MAX_UNCOMPRESSED)) goto done;
    ok = true;

done:
    xx_mem_free(table);
    if (ok) {
        *pdta = scratch;
    } else {
        xx_mem_free(scratch);
    }
    return ok;
}

bool xx_sfpack_scan_memory(const uint8_t *input, size_t input_size,
                           size_t max_output, size_t *consumed,
                           size_t *produced) {
    sfpack_header header;
    uint8_t *pdta = NULL;
    int64_t shdr = 0;
    int64_t words = 0;
    int64_t total = 0;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input || (input_size < 16U)) return false;
    if (input_size > (size_t)0x7fffffffU) return false;

    if (!sfpack_prepare(input, (int64_t)input_size, &header, &pdta, &shdr,
                        &words, &total)) {
        return false;
    }
    xx_mem_free(pdta);

    if ((max_output > 0U) && ((uint64_t)total > (uint64_t)max_output)) {
        return false;
    }
    /* The sample table holds absolute file offsets, so the codec owns the
     * whole file: there is no way to tell where the last sample stream ends. */
    if (consumed) *consumed = input_size;
    if (produced) *produced = (size_t)total;
    return true;
}

bool xx_sfpack_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written) {
    static const char sf_riff[4] = {'R', 'I', 'F', 'F'};
    static const char sf_sfbk[4] = {'s', 'f', 'b', 'k'};
    static const char sf_list[4] = {'L', 'I', 'S', 'T'};
    static const char sf_info[4] = {'I', 'N', 'F', 'O'};
    static const char sf_sdta[4] = {'s', 'd', 't', 'a'};
    static const char sf_smpl[4] = {'s', 'm', 'p', 'l'};
    static const char sf_pdta[4] = {'p', 'd', 't', 'a'};
    sfpack_header header;
    sfpack_lzw *table = NULL;
    uint8_t *pdta = NULL;
    int64_t shdr = 0;
    int64_t words = 0;
    int64_t total = 0;
    int64_t samples_start;
    int64_t sample_base = 0;
    int32_t i;
    bool ok = false;

    if (written) *written = 0U;
    if (!input || !output || (input_size < 16U)) return false;
    if (input_size > (size_t)0x7fffffffU) return false;

    if (!sfpack_prepare(input, (int64_t)input_size, &header, &pdta, &shdr,
                        &words, &total)) {
        return false;
    }
    if (!sf_has_range((int64_t)input_size, header.info_offset,
                      header.info_packed)) {
        goto done;
    }
    if ((uint64_t)total > (uint64_t)output_size) goto done;

    table = (sfpack_lzw *)xx_mem_alloc(sizeof(sfpack_lzw));
    if (!table) goto done;

    /* Layout of the rebuilt file.  The pieces are written in place instead of
     * being appended, so the INFO chunk is decompressed straight into its slot;
     * the reference decompresses it before the pdta, which changes nothing
     * because both must succeed for the rebuild to succeed. */
    samples_start = 44 + header.info_size;

    xx_rt_memcpy(output, sf_riff, 4);
    sf_write32(output + 4, (uint32_t)(header.info_size + 2 * words +
                                      header.pdta_size + 0x30));
    xx_rt_memcpy(output + 8, sf_sfbk, 4);

    xx_rt_memcpy(output + 12, sf_list, 4);
    sf_write32(output + 16, (uint32_t)(header.info_size + 4));
    xx_rt_memcpy(output + 20, sf_info, 4);
    if (!sfpack_decode_chunk(input + header.info_offset, header.info_packed,
                             header.info_size, output + 24, table)) {
        goto done;
    }

    xx_rt_memcpy(output + 24 + header.info_size, sf_list, 4);
    sf_write32(output + 28 + header.info_size, (uint32_t)(2 * words + 0x0c));
    xx_rt_memcpy(output + 32 + header.info_size, sf_sdta, 4);
    xx_rt_memcpy(output + 36 + header.info_size, sf_smpl, 4);
    sf_write32(output + 40 + header.info_size, (uint32_t)(2 * words));

    for (i = 0; i < header.sample_count; ++i) {
        const int64_t record = shdr + (int64_t)i * SFPACK_SHDR_RECORD_SIZE;
        const uint16_t type = sf_read16(pdta + record + 0x2c);
        int64_t sample_offset;
        int64_t old_start;
        uint32_t delta;
        size_t sample_length = 0U;
        int32_t j;
        static const int32_t fields[4] = {0x14, 0x18, 0x1c, 0x20};

        if (type & 0x8000) continue; /* ROM sample: skipped entirely */

        sample_offset =
            sf_read32_signed(input + header.table_offset + (int64_t)i * 4);
        if (!sfpack_decode_sample(input, (int64_t)input_size, sample_offset,
                                  output + samples_start + sample_base,
                                  (size_t)(2 * words - sample_base),
                                  &sample_length)) {
            goto done;
        }
        if (((int64_t)sample_length + SFPACK_SHDR_RECORD_SIZE) >
            (2 * words - sample_base)) {
            goto done;
        }
        /* the 46 zero bytes SFPack appends after every sample */
        xx_rt_memset(output + samples_start + sample_base + sample_length, 0,
                     (size_t)SFPACK_SHDR_RECORD_SIZE);

        old_start = sf_read32_signed(pdta + record + 0x14);
        delta = (uint32_t)(int32_t)((sample_base / 2) - old_start);
        for (j = 0; j < 4; ++j) {
            const uint32_t value = sf_read32(pdta + record + fields[j]);
            sf_write32(pdta + record + fields[j], value + delta);
        }

        sample_base += (int64_t)sample_length + SFPACK_SHDR_RECORD_SIZE;
    }
    if (sample_base != (2 * words)) goto done;

    xx_rt_memcpy(output + samples_start + sample_base, sf_list, 4);
    sf_write32(output + samples_start + sample_base + 4,
               (uint32_t)(header.pdta_size + 4));
    xx_rt_memcpy(output + samples_start + sample_base + 8, sf_pdta, 4);
    xx_rt_memcpy(output + samples_start + sample_base + 12, pdta,
                 (size_t)header.pdta_size);
    ok = true;

done:
    xx_mem_free(table);
    xx_mem_free(pdta);
    if (ok) {
        if (written) *written = (size_t)total;
        return true;
    }
    if (written) *written = 0U;
    return false;
}
