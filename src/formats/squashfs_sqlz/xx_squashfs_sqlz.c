/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SquashFS v2/v3 with the vendor magic 'sqlz' (big endian) or 'zlqs' (little
 * endian).  The field table is in xx_squashfs_sqlz.h.
 *
 * The metadata stream, inode, directory and fragment handling follows the
 * library's own SquashFS reader (src/formats/squashfs/xx_squashfs.c, MIT,
 * same project), reduced to the v2/v3 layouts.  What differs is the block
 * codec: these images are LZMA compressed by vendor tools that write five
 * props bytes, no size field and no end marker, so a block has to be decoded
 * to its known size (a full data block, a file's short last block) or to the
 * clean end of its input (metadata blocks, fragment blocks).  The LZMA
 * decoder below is written for that from the LZMA format description.
 *
 * On top of the SquashFS rules, member names that are Windows device names
 * are refused, and a name that repeats an earlier one (ASCII
 * case-insensitively) is refused instead of overwriting the first.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/squashfs_sqlz/xx_squashfs_sqlz.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef SQUASHFS_SQLZ
#define XX_SQUASHFS_SQLZ_FILE_TYPE XX_FILE_TYPE_SQUASHFS_SQLZ
#else
#define XX_SQUASHFS_SQLZ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SQLZ_SUPER_V2 0x3F
#define SQLZ_SUPER_V3 0x77
#define SQLZ_META_MAX 8192U
#define SQLZ_MIN_BLOCK 4096
#define SQLZ_MAX_BLOCK (1024 * 1024)
#define SQLZ_MAX_DEPTH 64U
#define SQLZ_MAX_MEMBERS 500000U
#define SQLZ_MAX_CHUNKS 200000U
#define SQLZ_MAX_NAME 256U
#define SQLZ_MAX_PATH 4096U
#define SQLZ_MAX_UNCOMPRESSED INT64_C(0x40000000)
#define SQLZ_CACHE_BUDGET (16U * 1024U * 1024U)
#define SQLZ_META_GUARD 0x10000U
#define SQLZ_MAX_SEEN (1U << 20)
#define SQLZ_MAX_VISITS (2U * SQLZ_MAX_MEMBERS)

#define SQLZ_KIND_STORED 0U
#define SQLZ_KIND_COMPRESSED 1U
#define SQLZ_KIND_SPARSE 2U

#define SQLZ_INODE_DIR 1
#define SQLZ_INODE_FILE 2
#define SQLZ_INODE_LDIR 8
#define SQLZ_INODE_LREG 9

#define SQLZ_METHOD_LZMA 2U

/* ================================================================ LZMA === */

#define LZ_PROB_BITS 11U
#define LZ_PROB_INIT 1024U
#define LZ_MOVE_BITS 5U
#define LZ_STATES 12U
#define LZ_POS_STATES_MAX 16U
#define LZ_MAX_LCLP 8U /* literal table 0x300 << 8 probabilities at most */

typedef uint16_t lz_prob;

typedef struct lz_len_s {
    lz_prob choice;
    lz_prob choice2;
    lz_prob low[LZ_POS_STATES_MAX][8];
    lz_prob mid[LZ_POS_STATES_MAX][8];
    lz_prob high[256];
} lz_len;

typedef struct lz_model_s {
    lz_prob is_match[LZ_STATES][LZ_POS_STATES_MAX];
    lz_prob is_rep[LZ_STATES];
    lz_prob is_rep_g0[LZ_STATES];
    lz_prob is_rep_g1[LZ_STATES];
    lz_prob is_rep_g2[LZ_STATES];
    lz_prob is_rep0_long[LZ_STATES][LZ_POS_STATES_MAX];
    lz_prob pos_slot[4][64];
    lz_prob pos_special[115];
    lz_prob align[16];
    lz_len len;
    lz_len rep_len;
} lz_model;

typedef struct lz_rc_s {
    const uint8_t *in;
    size_t size;
    size_t pos;
    uint32_t range;
    uint32_t code;
    bool overrun;
} lz_rc;

static void lz_fill(lz_prob *p, size_t n) {
    size_t i;
    for (i = 0U; i < n; ++i) p[i] = (lz_prob)LZ_PROB_INIT;
}

static uint8_t lz_next(lz_rc *rc) {
    if (rc->pos < rc->size) return rc->in[rc->pos++];
    rc->overrun = true;
    return 0U;
}

static void lz_normalize(lz_rc *rc) {
    if (rc->range < (1U << 24)) {
        rc->range <<= 8;
        rc->code = (rc->code << 8) | lz_next(rc);
    }
}

static unsigned lz_bit(lz_rc *rc, lz_prob *p) {
    uint32_t bound = (rc->range >> LZ_PROB_BITS) * (uint32_t)(*p);
    unsigned bit;
    if (rc->code < bound) {
        rc->range = bound;
        *p = (lz_prob)(*p + (((1U << LZ_PROB_BITS) - *p) >> LZ_MOVE_BITS));
        bit = 0U;
    } else {
        rc->range -= bound;
        rc->code -= bound;
        *p = (lz_prob)(*p - (*p >> LZ_MOVE_BITS));
        bit = 1U;
    }
    lz_normalize(rc);
    return bit;
}

static uint32_t lz_tree(lz_rc *rc, lz_prob *probs, unsigned bits) {
    uint32_t m = 1U;
    unsigned i;
    for (i = 0U; i < bits; ++i) m = (m << 1) | lz_bit(rc, probs + m);
    return m - (1U << bits);
}

static uint32_t lz_reverse(lz_rc *rc, lz_prob *probs, unsigned bits) {
    uint32_t m = 1U;
    uint32_t symbol = 0U;
    unsigned i;
    for (i = 0U; i < bits; ++i) {
        unsigned bit = lz_bit(rc, probs + m);
        m = (m << 1) | bit;
        symbol |= (uint32_t)bit << i;
    }
    return symbol;
}

static uint32_t lz_direct(lz_rc *rc, unsigned bits) {
    uint32_t result = 0U;
    unsigned i;
    for (i = 0U; i < bits; ++i) {
        rc->range >>= 1;
        if (rc->code >= rc->range) {
            rc->code -= rc->range;
            result = (result << 1) | 1U;
        } else {
            result <<= 1;
        }
        lz_normalize(rc);
    }
    return result;
}

static uint32_t lz_length(lz_rc *rc, lz_len *len, unsigned pos_state) {
    if (lz_bit(rc, &len->choice) == 0U) return lz_tree(rc, len->low[pos_state], 3U);
    if (lz_bit(rc, &len->choice2) == 0U) {
        return 8U + lz_tree(rc, len->mid[pos_state], 3U);
    }
    return 16U + lz_tree(rc, len->high, 8U);
}

bool xx_squashfs_sqlz_lzma_decode(const uint8_t *input, size_t input_size,
                                  uint8_t props_byte, uint8_t *output,
                                  size_t output_size, bool exact,
                                  size_t *written) {
    lz_model *model = NULL;
    lz_prob *literal = NULL;
    lz_rc rc;
    unsigned lc;
    unsigned lp;
    unsigned pb;
    unsigned state = 0U;
    uint32_t rep0 = 0U, rep1 = 0U, rep2 = 0U, rep3 = 0U;
    size_t pos = 0U;
    size_t literal_count;
    bool ok = false;

    if (written) *written = 0U;
    if (!input || !output || output_size == 0U || !written) return false;
    if (props_byte >= 9U * 5U * 5U) return false;
    lc = props_byte % 9U;
    lp = (props_byte / 9U) % 5U;
    pb = props_byte / 45U;
    if (lc + lp > LZ_MAX_LCLP) return false;
    if (input_size < 5U || input[0] != 0U) return false;

    model = (lz_model *)xx_mem_alloc(sizeof(*model));
    literal_count = (size_t)0x300U << (lc + lp);
    literal = (lz_prob *)xx_mem_alloc(literal_count * sizeof(lz_prob));
    if (!model || !literal) goto done;
    lz_fill((lz_prob *)model, sizeof(*model) / sizeof(lz_prob));
    lz_fill(literal, literal_count);

    rc.in = input;
    rc.size = input_size;
    rc.pos = 1U;
    rc.range = 0xFFFFFFFFU;
    rc.code = 0U;
    rc.overrun = false;
    {
        unsigned i;
        for (i = 0U; i < 4U; ++i) rc.code = (rc.code << 8) | lz_next(&rc);
    }
    if (rc.code == rc.range) goto done;

    while (pos < output_size) {
        unsigned pos_state = (unsigned)(pos & ((1U << pb) - 1U));
        uint32_t len;
        if (!exact && rc.pos >= rc.size && rc.code == 0U) break;
        if (rc.overrun) goto done;

        if (lz_bit(&rc, &model->is_match[state][pos_state]) == 0U) {
            unsigned prev = pos ? output[pos - 1U] : 0U;
            lz_prob *probs =
                literal + 0x300U * (((pos & ((1U << lp) - 1U)) << lc) +
                                    (prev >> (8U - lc)));
            uint32_t symbol = 1U;
            if (state >= 7U) {
                unsigned match_byte;
                if ((size_t)rep0 >= pos) goto done;
                match_byte = output[pos - rep0 - 1U];
                do {
                    unsigned match_bit = (match_byte >> 7) & 1U;
                    unsigned bit;
                    match_byte <<= 1;
                    bit = lz_bit(&rc, probs + ((1U + match_bit) << 8) + symbol);
                    symbol = (symbol << 1) | bit;
                    if (match_bit != bit) break;
                } while (symbol < 0x100U);
            }
            while (symbol < 0x100U) symbol = (symbol << 1) | lz_bit(&rc, probs + symbol);
            output[pos++] = (uint8_t)(symbol - 0x100U);
            state = (state < 4U) ? 0U : (state < 10U ? state - 3U : state - 6U);
            continue;
        }

        if (lz_bit(&rc, &model->is_rep[state]) != 0U) {
            if (pos == 0U) goto done;
            if (lz_bit(&rc, &model->is_rep_g0[state]) == 0U) {
                if (lz_bit(&rc, &model->is_rep0_long[state][pos_state]) == 0U) {
                    if ((size_t)rep0 >= pos) goto done;
                    state = (state < 7U) ? 9U : 11U;
                    output[pos] = output[pos - rep0 - 1U];
                    ++pos;
                    continue;
                }
            } else {
                uint32_t dist;
                if (lz_bit(&rc, &model->is_rep_g1[state]) == 0U) {
                    dist = rep1;
                } else {
                    if (lz_bit(&rc, &model->is_rep_g2[state]) == 0U) {
                        dist = rep2;
                    } else {
                        dist = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = lz_length(&rc, &model->rep_len, pos_state);
            state = (state < 7U) ? 8U : 11U;
        } else {
            unsigned len_state;
            uint32_t slot;
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            len = lz_length(&rc, &model->len, pos_state);
            state = (state < 7U) ? 7U : 10U;
            len_state = (len < 3U) ? (unsigned)len : 3U;
            slot = lz_tree(&rc, model->pos_slot[len_state], 6U);
            if (slot < 4U) {
                rep0 = slot;
            } else {
                unsigned direct = (unsigned)((slot >> 1) - 1U);
                uint32_t dist = (2U | (slot & 1U)) << direct;
                if (slot < 14U) {
                    dist += lz_reverse(&rc, model->pos_special + dist - slot,
                                       direct);
                } else {
                    dist += lz_direct(&rc, direct - 4U) << 4;
                    dist += lz_reverse(&rc, model->align, 4U);
                }
                rep0 = dist;
            }
            if (rep0 == 0xFFFFFFFFU) {
                /* End marker: accepted unless an exact size was demanded
                 * and not reached. */
                if (rc.overrun || exact) goto done;
                break;
            }
        }
        if (rc.overrun) goto done;
        if ((size_t)rep0 >= pos) goto done;
        len += 2U;
        {
            size_t left = output_size - pos;
            size_t count = (len < left) ? (size_t)len : left;
            size_t from = pos - rep0 - 1U;
            size_t i;
            for (i = 0U; i < count; ++i) output[pos + i] = output[from + i];
            pos += count;
        }
    }
    if (rc.overrun) goto done;
    if (exact && pos != output_size) goto done;
    if (pos == 0U) goto done;
    *written = pos;
    ok = true;

done:
    if (model) xx_mem_free(model);
    if (literal) xx_mem_free(literal);
    return ok;
}

/* ============================================================ structures === */

typedef struct sqlz_super_s {
    int32_t major;
    int32_t minor;
    bool big_endian;
    uint32_t flags;
    int64_t inodes;
    int64_t block_size;
    int64_t bytes_used;
    int64_t root_inode;
    int64_t inode_table;
    int64_t directory_table;
    int64_t fragment_table;
    int64_t fragments;
} sqlz_super;

typedef struct sqlz_chunk_s {
    int64_t offset;
    int64_t on_disk;
    int64_t out_size;
    int64_t skip;
    uint32_t kind;
    bool fragment; /* a fragment block: its size is not known in advance */
} sqlz_chunk;

typedef struct sqlz_member_s {
    char *name;
    int64_t uncompressed_size;
    int64_t span_offset;
    int64_t span_size;
    sqlz_chunk *chunks;
    size_t chunk_count;
} sqlz_member;

typedef struct sqlz_meta_block_s {
    int64_t base;
    int64_t block;
    int64_t next;
    uint8_t *data;
    size_t size;
} sqlz_meta_block;

typedef struct sqlz_private_s {
    sqlz_super super;
    sqlz_member *members;
    size_t count;
    size_t capacity;
    sqlz_meta_block *cache;
    size_t cache_count;
    size_t cache_capacity;
    size_t cache_bytes;
    int64_t image_size;
} sqlz_private;

typedef struct sqlz_walk_s {
    Abstractformat *format;
    sqlz_private *parsed;
    xx_pd_struct *pd;
    size_t visits; /* bounds the whole walk */
    /* The directory inodes on the current path, to cut directory loops. */
    int64_t ancestor_block[SQLZ_MAX_DEPTH + 2U];
    int64_t ancestor_offset[SQLZ_MAX_DEPTH + 2U];
} sqlz_walk;

typedef struct sqlz_stream_s {
    sqlz_walk *walk;
    int64_t base;
    int64_t block;
    int64_t offset;
} sqlz_stream;

typedef struct sqlz_seen_s {
    char **slots;
    size_t capacity;
    size_t count;
} sqlz_seen;

typedef struct sqlz_session_s {
    sqlz_private parsed;
    size_t index;
    sqlz_seen seen;
    /* One decoded fragment block, reused by the members that share it. */
    int64_t frag_offset;
    uint8_t *frag_data;
    size_t frag_size;
} sqlz_session;

static void xx_squashfs_sqlz_vtable_destroy(Abstractformat *self);

/* ============================================================== helpers === */

static uint16_t sqlz_u16(const uint8_t *p, bool be) {
    return be ? (uint16_t)(((unsigned)p[0] << 8U) | p[1])
              : (uint16_t)(p[0] | ((unsigned)p[1] << 8U));
}

static uint32_t sqlz_u32(const uint8_t *p, bool be) {
    return be ? (((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
                 ((uint32_t)p[2] << 8U) | (uint32_t)p[3])
              : ((uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
                 ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U));
}

static uint64_t sqlz_u64(const uint8_t *p, bool be) {
    return be ? (((uint64_t)sqlz_u32(p, true) << 32U) | sqlz_u32(p + 4, true))
              : ((uint64_t)sqlz_u32(p, false) |
                 ((uint64_t)sqlz_u32(p + 4, false) << 32U));
}

/* Image-relative read. */
static bool sqlz_read_at(Abstractformat *self, int64_t offset, void *data,
                         size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!self || !self->device || (!data && size != 0U) || offset < 0 ||
        self->base_address < 0 || offset > INT64_MAX - self->base_address) {
        return false;
    }
    if (xx_io_seek64(self->device, self->base_address + offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(self->device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/*
 * v1..v3 pack several values into one word as C bitfields; a big-endian image
 * was written by a big-endian compiler, which allocates them from the most
 * significant bit down.  `lsb_shift` is the field's offset in the
 * little-endian layout.
 */
static uint32_t sqlz_bits(uint32_t word, unsigned total_bits,
                          unsigned lsb_shift, unsigned width, bool be) {
    unsigned shift = be ? (total_bits - lsb_shift - width) : lsb_shift;
    uint32_t mask = (width >= 32U) ? 0xFFFFFFFFU : ((1U << width) - 1U);
    return (word >> shift) & mask;
}

/* ============================================================ blocks === */

/*
 * Decodes one compressed block.  `exact` true means the output must be
 * exactly `output_cap` bytes (a full data block or a file's short last
 * block); false means "at most output_cap" (metadata and fragment blocks).
 */
static bool sqlz_decompress(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_cap, bool exact,
                            size_t *written) {
    size_t produced = 0U;
    if (!input || input_size == 0U || !output || output_cap == 0U || !written) {
        return false;
    }
    *written = 0U;

    /* zlib: CMF 0x78 and a valid FCHECK. */
    if (input_size >= 2U && input[0] == 0x78U &&
        (((unsigned)input[0] << 8) | input[1]) % 31U == 0U) {
        if (xx_zlib_stream_decode_memory(input, input_size, output, output_cap,
                                         &produced) &&
            produced != 0U && (!exact || produced == output_cap)) {
            *written = produced;
            return true;
        }
    }

    if (input[0] < 9U * 5U * 5U && input_size > 5U) {
        /* The 13-byte "alone" header: its u64 size is either unknown
         * (all ones) or fits the block, and its first stream byte is 0. */
        if (input_size > 13U && input[13] == 0U) {
            uint64_t size = sqlz_u64(input + 5, false);
            if (size == UINT64_MAX ||
                (size != 0U && size <= (uint64_t)output_cap)) {
                bool known = size != UINT64_MAX;
                size_t want = known ? (size_t)size : output_cap;
                if ((!exact || !known || want == output_cap) &&
                    xx_squashfs_sqlz_lzma_decode(input + 13, input_size - 13U,
                                                 input[0], output, want,
                                                 known || exact, &produced) &&
                    (!exact || produced == output_cap)) {
                    *written = produced;
                    return true;
                }
            }
        }
        /* Five props bytes and no size field. */
        if (input[5] == 0U &&
            xx_squashfs_sqlz_lzma_decode(input + 5, input_size - 5U, input[0],
                                         output, output_cap, exact,
                                         &produced)) {
            *written = produced;
            return true;
        }
    }
    return false;
}

/* ========================================================== metadata === */

static bool sqlz_cache_insert(sqlz_private *parsed, int64_t base, int64_t block,
                              int64_t next, const uint8_t *data, size_t size) {
    sqlz_meta_block *grown;
    uint8_t *copy = NULL;
    size_t capacity;
    if (!parsed || parsed->cache_bytes > SQLZ_CACHE_BUDGET) return false;
    if (parsed->cache_count == parsed->cache_capacity) {
        capacity = parsed->cache_capacity ? parsed->cache_capacity * 2U : 64U;
        if (capacity > SIZE_MAX / sizeof(*parsed->cache)) return false;
        grown = (sqlz_meta_block *)xx_mem_realloc(
            parsed->cache, capacity * sizeof(*parsed->cache));
        if (!grown) return false;
        parsed->cache = grown;
        parsed->cache_capacity = capacity;
    }
    if (size != 0U) {
        copy = (uint8_t *)xx_mem_alloc(size);
        if (!copy) return false;
        xx_rt_memcpy(copy, data, size);
    }
    parsed->cache[parsed->cache_count].base = base;
    parsed->cache[parsed->cache_count].block = block;
    parsed->cache[parsed->cache_count].next = next;
    parsed->cache[parsed->cache_count].data = copy;
    parsed->cache[parsed->cache_count].size = size;
    ++parsed->cache_count;
    parsed->cache_bytes += size;
    return true;
}

static const sqlz_meta_block *sqlz_cache_find(const sqlz_private *parsed,
                                              int64_t base, int64_t block) {
    size_t index;
    for (index = parsed->cache_count; index > 0U; --index) {
        const sqlz_meta_block *entry = &parsed->cache[index - 1U];
        if (entry->base == base && entry->block == block) return entry;
    }
    return NULL;
}

/* One metadata block: an 8 KiB payload behind a u16 header whose 0x8000 bit
 * marks it stored.  `plain` is the caller's fallback buffer. */
static bool sqlz_meta_get_block(sqlz_walk *walk, int64_t base, int64_t block,
                                uint8_t *plain, const uint8_t **data,
                                size_t *size, int64_t *next) {
    const sqlz_meta_block *cached;
    uint8_t header_bytes[2];
    uint8_t packed[SQLZ_META_MAX];
    uint16_t header;
    size_t length;
    size_t produced = 0U;
    int64_t position;
    if (base < 0 || block < 0 || block > INT64_MAX - base) return false;
    cached = sqlz_cache_find(walk->parsed, base, block);
    if (cached) {
        *data = cached->data;
        *size = cached->size;
        *next = cached->next;
        return true;
    }
    position = base + block;
    if (position > walk->parsed->image_size - 2) return false;
    if (!sqlz_read_at(walk->format, position, header_bytes, 2U)) return false;
    header = sqlz_u16(header_bytes, walk->parsed->super.big_endian);
    length = (size_t)(header & 0x7FFFU);
    if (length > SQLZ_META_MAX) return false;
    if ((int64_t)length > walk->parsed->image_size - position - 2) return false;
    *next = block + 2 + (int64_t)length;
    if (length == 0U) {
        *data = plain;
        *size = 0U;
        (void)sqlz_cache_insert(walk->parsed, base, block, *next, plain, 0U);
        return true;
    }
    if (!sqlz_read_at(walk->format, position + 2, packed, length)) return false;
    if ((header & 0x8000U) != 0U) {
        xx_rt_memcpy(plain, packed, length);
        produced = length;
    } else if (!sqlz_decompress(packed, length, plain, SQLZ_META_MAX, false,
                                &produced)) {
        return false;
    }
    (void)sqlz_cache_insert(walk->parsed, base, block, *next, plain, produced);
    cached = sqlz_cache_find(walk->parsed, base, block);
    *data = cached ? cached->data : plain;
    *size = produced;
    return true;
}

static bool sqlz_meta_read(sqlz_stream *stream, size_t size, uint8_t *out) {
    uint8_t plain[SQLZ_META_MAX];
    size_t done = 0U;
    unsigned guard = 0U;
    while (done < size) {
        const uint8_t *data = NULL;
        size_t block_size = 0U;
        int64_t next = 0;
        int64_t available;
        size_t take;
        if (++guard > SQLZ_META_GUARD) return false;
        if (!sqlz_meta_get_block(stream->walk, stream->base, stream->block,
                                 plain, &data, &block_size, &next)) {
            return false;
        }
        available = (int64_t)block_size - stream->offset;
        if (available <= 0) {
            if (next == stream->block) return false;
            stream->block = next;
            stream->offset = 0;
            continue;
        }
        take = (size_t)available;
        if (take > size - done) take = size - done;
        if (data) xx_rt_memcpy(out + done, data + stream->offset, take);
        done += take;
        stream->offset += (int64_t)take;
        if (stream->offset >= (int64_t)block_size) {
            stream->block = next;
            stream->offset = 0;
        }
    }
    return true;
}

/* ============================================================ inodes === */

static bool sqlz_base_inode(sqlz_walk *walk, sqlz_stream *stream,
                            int32_t *type) {
    uint8_t data[4];
    bool be = walk->parsed->super.big_endian;
    if (!sqlz_meta_read(stream, 4U, data)) return false;
    *type = (int32_t)sqlz_bits(sqlz_u16(data, be), 16U, 0U, 4U, be);
    return true;
}

static bool sqlz_dir_inode(sqlz_walk *walk, sqlz_stream *stream, bool extended,
                           int64_t *size, int64_t *offset,
                           int64_t *start_block) {
    uint8_t data[16];
    uint32_t word;
    bool be = walk->parsed->super.big_endian;

    if (walk->parsed->super.major == 2) {
        if (extended) {
            /* file_size:27 offset:13 - five bytes */
            if (!sqlz_meta_read(stream, 4U, data)) return false;
            word = sqlz_u32(data, be);
            if (!sqlz_meta_read(stream, 1U, data + 4)) return false;
            if (be) {
                *size = (int64_t)(word >> 5);
                *offset = ((int64_t)(word & 0x1FU) << 8) | data[4];
            } else {
                *size = (int64_t)(word & 0x7FFFFFFU);
                *offset = (int64_t)(word >> 27) | ((int64_t)data[4] << 5);
            }
        } else {
            if (!sqlz_meta_read(stream, 4U, data)) return false;
            word = sqlz_u32(data, be);
            *size = (int64_t)sqlz_bits(word, 32U, 0U, 19U, be);
            *offset = (int64_t)sqlz_bits(word, 32U, 19U, 13U, be);
        }
        if (!sqlz_meta_read(stream, 4U, data)) return false; /* mtime */
        if (!sqlz_meta_read(stream, 3U, data)) return false;
        *start_block = be ? (((int64_t)data[0] << 16) | ((int64_t)data[1] << 8) |
                             (int64_t)data[2])
                          : ((int64_t)data[0] | ((int64_t)data[1] << 8) |
                             ((int64_t)data[2] << 16));
        return true;
    }

    /* v3: mtime, inode number, nlink, then the size/offset word(s) */
    if (!sqlz_meta_read(stream, 12U, data)) return false;
    if (extended) {
        if (!sqlz_meta_read(stream, 4U, data)) return false;
        word = sqlz_u32(data, be);
        if (!sqlz_meta_read(stream, 1U, data + 4)) return false;
        if (be) {
            *size = (int64_t)(word >> 5);
            *offset = ((int64_t)(word & 0x1FU) << 8) | data[4];
        } else {
            *size = (int64_t)(word & 0x7FFFFFFU);
            *offset = (int64_t)(word >> 27) | ((int64_t)data[4] << 5);
        }
    } else {
        if (!sqlz_meta_read(stream, 4U, data)) return false;
        word = sqlz_u32(data, be);
        *size = (int64_t)sqlz_bits(word, 32U, 0U, 19U, be);
        *offset = (int64_t)sqlz_bits(word, 32U, 19U, 13U, be);
    }
    if (!sqlz_meta_read(stream, 4U, data)) return false;
    *start_block = (int64_t)sqlz_u32(data, be);
    *size -= 3; /* v3 counts the implicit "." and ".." */
    return true;
}

static bool sqlz_file_inode(sqlz_walk *walk, sqlz_stream *stream,
                            bool extended, int64_t *start, int64_t *fragment,
                            int64_t *block_offset, int64_t *size) {
    uint8_t data[40];
    bool be = walk->parsed->super.big_endian;
    if (walk->parsed->super.major == 2) {
        if (!sqlz_meta_read(stream, 20U, data)) return false;
        *start = (int64_t)sqlz_u32(data + 4U, be);
        *fragment = (int64_t)sqlz_u32(data + 8U, be);
        *block_offset = (int64_t)sqlz_u32(data + 12U, be);
        *size = (int64_t)sqlz_u32(data + 16U, be);
        return true;
    }
    if (extended) {
        uint64_t s;
        uint64_t n;
        if (!sqlz_meta_read(stream, 36U, data)) return false;
        s = sqlz_u64(data + 12U, be);
        n = sqlz_u64(data + 28U, be);
        if (s > (uint64_t)INT64_MAX || n > (uint64_t)INT64_MAX) return false;
        *start = (int64_t)s;
        *fragment = (int64_t)sqlz_u32(data + 20U, be);
        *block_offset = (int64_t)sqlz_u32(data + 24U, be);
        *size = (int64_t)n;
    } else {
        uint64_t s;
        if (!sqlz_meta_read(stream, 28U, data)) return false;
        s = sqlz_u64(data + 8U, be);
        if (s > (uint64_t)INT64_MAX) return false;
        *start = (int64_t)s;
        *fragment = (int64_t)sqlz_u32(data + 16U, be);
        *block_offset = (int64_t)sqlz_u32(data + 20U, be);
        *size = (int64_t)sqlz_u32(data + 24U, be);
    }
    return true;
}

/* A fragment index entry is an ABSOLUTE (image-relative) metadata block
 * offset, hence the stream on base 0. */
static bool sqlz_fragment(sqlz_walk *walk, int64_t index, int64_t *start,
                          int64_t *size) {
    sqlz_stream stream;
    uint8_t entry[16];
    uint8_t pointer[8];
    int64_t block;
    int64_t entry_offset;
    int64_t index_position;
    bool be = walk->parsed->super.big_endian;
    if (index < 0 || index >= walk->parsed->super.fragments) return false;
    if (walk->parsed->super.major == 2) {
        index_position = walk->parsed->super.fragment_table + (index >> 10) * 4;
        if (index_position < 0 || index_position > walk->parsed->image_size - 4 ||
            !sqlz_read_at(walk->format, index_position, pointer, 4U)) {
            return false;
        }
        block = (int64_t)sqlz_u32(pointer, be);
        entry_offset = (index & 0x3FF) * 8;
    } else {
        uint64_t raw;
        index_position = walk->parsed->super.fragment_table + (index >> 9) * 8;
        if (index_position < 0 || index_position > walk->parsed->image_size - 8 ||
            !sqlz_read_at(walk->format, index_position, pointer, 8U)) {
            return false;
        }
        raw = sqlz_u64(pointer, be);
        if (raw > (uint64_t)INT64_MAX) return false;
        block = (int64_t)raw;
        entry_offset = (index & 0x1FF) * 16;
    }
    stream.walk = walk;
    stream.base = 0;
    stream.block = block;
    stream.offset = entry_offset;
    if (walk->parsed->super.major == 2) {
        if (!sqlz_meta_read(&stream, 8U, entry)) return false;
        *start = (int64_t)sqlz_u32(entry, be);
        *size = (int64_t)sqlz_u32(entry + 4U, be);
        return true;
    }
    if (!sqlz_meta_read(&stream, 16U, entry)) return false;
    {
        uint64_t raw = sqlz_u64(entry, be);
        if (raw > (uint64_t)INT64_MAX) return false;
        *start = (int64_t)raw;
    }
    *size = (int64_t)sqlz_u32(entry + 8U, be);
    return true;
}

/* ======================================================== collections === */

static void sqlz_member_cleanup(sqlz_member *member) {
    if (member->name) xx_str_free(member->name);
    if (member->chunks) xx_mem_free(member->chunks);
    xx_rt_memset(member, 0, sizeof(*member));
}

static void sqlz_private_cleanup(sqlz_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        sqlz_member_cleanup(&parsed->members[index]);
    }
    if (parsed->members) xx_mem_free(parsed->members);
    for (index = 0U; index < parsed->cache_count; ++index) {
        if (parsed->cache[index].data) xx_mem_free(parsed->cache[index].data);
    }
    if (parsed->cache) xx_mem_free(parsed->cache);
    xx_rt_memset(parsed, 0, sizeof(*parsed));
}

static bool sqlz_append_member(sqlz_private *parsed, sqlz_member *member) {
    if (parsed->count >= SQLZ_MAX_MEMBERS) return false;
    if (parsed->count == parsed->capacity) {
        size_t capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        sqlz_member *grown;
        if (capacity > SIZE_MAX / sizeof(*parsed->members)) return false;
        grown = (sqlz_member *)xx_mem_realloc(parsed->members,
                                              capacity * sizeof(*parsed->members));
        if (!grown) return false;
        parsed->members = grown;
        parsed->capacity = capacity;
    }
    parsed->members[parsed->count++] = *member;
    xx_rt_memset(member, 0, sizeof(*member));
    return true;
}

static bool sqlz_append_chunk(sqlz_chunk **chunks, size_t *count,
                              size_t *capacity, const sqlz_chunk *chunk) {
    if (*count >= SQLZ_MAX_CHUNKS) return false;
    if (*count == *capacity) {
        size_t grown_capacity = *capacity ? *capacity * 2U : 16U;
        sqlz_chunk *grown;
        if (grown_capacity > SIZE_MAX / sizeof(**chunks)) return false;
        grown = (sqlz_chunk *)xx_mem_realloc(*chunks,
                                             grown_capacity * sizeof(**chunks));
        if (!grown) return false;
        *chunks = grown;
        *capacity = grown_capacity;
    }
    (*chunks)[(*count)++] = *chunk;
    return true;
}

/* ============================================================== names === */

static char sqlz_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$, with or without
 * an extension (and trailing spaces before it). */
static bool sqlz_component_is_device(const char *s, size_t n) {
    static const char *const names[] = {"con", "prn", "aux", "nul",
                                        "conin$", "conout$", "clock$"};
    size_t stem = 0U;
    size_t index;
    while (stem < n && s[stem] != '.') ++stem;
    while (stem > 0U && s[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        size_t len = xx_str_len(names[index]);
        size_t k;
        if (len != stem) continue;
        for (k = 0U; k < len; ++k) {
            if (sqlz_lower(s[k]) != names[index][k]) break;
        }
        if (k == len) return true;
    }
    if (stem == 4U) {
        char a = sqlz_lower(s[0]);
        char b = sqlz_lower(s[1]);
        char c = sqlz_lower(s[2]);
        if (((a == 'c' && b == 'o' && c == 'm') ||
             (a == 'l' && b == 'p' && c == 't')) &&
            s[3] >= '0' && s[3] <= '9') {
            return true;
        }
    }
    return false;
}

/* Relative, no traversal, no characters or names Windows cannot carry. */
static bool sqlz_name_is_safe(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || ch == 127U || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' || component[length - 1U] == '.' ||
                sqlz_component_is_device(component, length)) {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static char *sqlz_join_name(const char *prefix, const uint8_t *name,
                            size_t name_size) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    char *combined;
    size_t index;
    if (name_size == 0U || name_size > SQLZ_MAX_NAME ||
        prefix_size >= SQLZ_MAX_PATH ||
        name_size > SQLZ_MAX_PATH - prefix_size - 1U) {
        return NULL;
    }
    for (index = 0U; index < name_size; ++index) {
        if (name[index] == 0U || name[index] == '/' || name[index] == '\\') {
            return NULL;
        }
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size + 2U);
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_rt_memcpy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_rt_memcpy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_rt_memcpy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

static size_t sqlz_hash(const char *s) {
    uint32_t h = 2166136261U;
    for (; *s; ++s) {
        char c = (*s == '\\') ? '/' : sqlz_lower(*s);
        h ^= (uint8_t)c;
        h *= 16777619U;
    }
    return (size_t)h;
}

static bool sqlz_same(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char ca = (*a == '\\') ? '/' : sqlz_lower(*a);
        char cb = (*b == '\\') ? '/' : sqlz_lower(*b);
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
}

static void sqlz_seen_cleanup(sqlz_seen *seen) {
    size_t index;
    for (index = 0U; index < seen->capacity; ++index) {
        if (seen->slots[index]) xx_str_free(seen->slots[index]);
    }
    if (seen->slots) xx_mem_free(seen->slots);
    xx_rt_memset(seen, 0, sizeof(*seen));
}

static void sqlz_seen_place(char **slots, size_t capacity, char *name) {
    size_t index = sqlz_hash(name) & (capacity - 1U);
    while (slots[index]) index = (index + 1U) & (capacity - 1U);
    slots[index] = name;
}

/* True when `name` is new and now recorded; false for a repeat or when it
 * cannot be recorded (the member is then refused, never overwritten). */
static bool sqlz_seen_add(sqlz_seen *seen, const char *name) {
    char *copy;
    if (seen->capacity != 0U) {
        size_t index = sqlz_hash(name) & (seen->capacity - 1U);
        while (seen->slots[index]) {
            if (sqlz_same(seen->slots[index], name)) return false;
            index = (index + 1U) & (seen->capacity - 1U);
        }
    }
    if (seen->count >= SQLZ_MAX_SEEN) return false;
    if ((seen->count + 1U) * 2U > seen->capacity) {
        size_t capacity = seen->capacity ? seen->capacity * 2U : 64U;
        char **slots;
        size_t index;
        slots = (char **)xx_mem_calloc(capacity, sizeof(char *));
        if (!slots) return false;
        for (index = 0U; index < seen->capacity; ++index) {
            if (seen->slots[index]) sqlz_seen_place(slots, capacity, seen->slots[index]);
        }
        if (seen->slots) xx_mem_free(seen->slots);
        seen->slots = slots;
        seen->capacity = capacity;
    }
    copy = xx_str_create(name);
    if (!copy) return false;
    sqlz_seen_place(seen->slots, seen->capacity, copy);
    ++seen->count;
    return true;
}

/* ============================================================== walk === */

static bool sqlz_walk_node(sqlz_walk *walk, int64_t block, int64_t offset,
                           int32_t want, const char *name, unsigned depth);

static bool sqlz_file_data(sqlz_walk *walk, sqlz_stream *stream, bool extended,
                           const char *name) {
    sqlz_member member;
    sqlz_chunk *chunks = NULL;
    size_t chunk_count = 0U;
    size_t chunk_capacity = 0U;
    int64_t start = 0, fragment = 0, block_offset = 0, size = 0;
    int64_t block_size = walk->parsed->super.block_size;
    int64_t block_count;
    int64_t tail;
    int64_t position;
    int64_t min_offset = -1;
    int64_t max_end = -1;
    size_t index;
    bool be = walk->parsed->super.big_endian;

    if (!sqlz_file_inode(walk, stream, extended, &start, &fragment,
                         &block_offset, &size)) {
        return false;
    }
    if (size < 0 || size > SQLZ_MAX_UNCOMPRESSED || block_offset < 0 ||
        start < 0) {
        return false;
    }
    if (fragment == INT64_C(0xFFFFFFFF)) {
        block_count = (size + block_size - 1) / block_size;
        tail = 0;
    } else {
        block_count = size / block_size;
        tail = size % block_size;
    }
    if (block_count > (int64_t)SQLZ_MAX_CHUNKS) return false;

    position = start;
    for (index = 0U; index < (size_t)block_count; ++index) {
        uint8_t entry_bytes[4];
        uint32_t entry;
        int64_t on_disk;
        int64_t out_size = block_size;
        int64_t remaining = size - (int64_t)index * block_size;
        sqlz_chunk chunk;
        if (walk->pd && xx_pd_is_stopped(walk->pd)) goto fail;
        if (!sqlz_meta_read(stream, 4U, entry_bytes)) goto fail;
        entry = sqlz_u32(entry_bytes, be);
        on_disk = (int64_t)(entry & 0xFFFFFFU);
        if (remaining < out_size) out_size = remaining;
        xx_rt_memset(&chunk, 0, sizeof(chunk));
        chunk.out_size = out_size;
        if (entry == 0U) {
            chunk.kind = SQLZ_KIND_SPARSE;
        } else {
            if (on_disk > walk->parsed->image_size - position) goto fail;
            chunk.offset = position;
            chunk.on_disk = on_disk;
            chunk.kind = (entry & 0x1000000U) ? SQLZ_KIND_STORED
                                              : SQLZ_KIND_COMPRESSED;
        }
        if (!sqlz_append_chunk(&chunks, &chunk_count, &chunk_capacity, &chunk)) {
            goto fail;
        }
        position += on_disk;
    }

    if (tail != 0) {
        int64_t fragment_start = 0;
        int64_t fragment_size = 0;
        int64_t on_disk;
        sqlz_chunk chunk;
        if (!sqlz_fragment(walk, fragment, &fragment_start, &fragment_size)) {
            goto fail;
        }
        on_disk = fragment_size & 0xFFFFFF;
        if (fragment_start < 0 || on_disk == 0 ||
            on_disk > walk->parsed->image_size - fragment_start ||
            block_offset >= block_size || tail > block_size - block_offset) {
            goto fail;
        }
        xx_rt_memset(&chunk, 0, sizeof(chunk));
        chunk.offset = fragment_start;
        chunk.on_disk = on_disk;
        chunk.out_size = tail;
        chunk.skip = block_offset;
        chunk.fragment = true;
        chunk.kind = (fragment_size & 0x1000000) ? SQLZ_KIND_STORED
                                                 : SQLZ_KIND_COMPRESSED;
        if (!sqlz_append_chunk(&chunks, &chunk_count, &chunk_capacity, &chunk)) {
            goto fail;
        }
    }

    for (index = 0U; index < chunk_count; ++index) {
        int64_t from;
        int64_t to;
        if (chunks[index].kind == SQLZ_KIND_SPARSE) continue;
        from = chunks[index].offset;
        to = from + chunks[index].on_disk;
        if (min_offset < 0 || from < min_offset) min_offset = from;
        if (max_end < 0 || to > max_end) max_end = to;
    }

    xx_rt_memset(&member, 0, sizeof(member));
    member.name = xx_str_create(name);
    if (!member.name) goto fail;
    member.uncompressed_size = size;
    member.span_offset = (min_offset < 0) ? 0 : min_offset;
    member.span_size = (max_end < 0) ? 0 : (max_end - member.span_offset);
    member.chunks = chunks;
    member.chunk_count = chunk_count;
    if (!sqlz_append_member(walk->parsed, &member)) {
        sqlz_member_cleanup(&member);
        return false;
    }
    return true;

fail:
    if (chunks) xx_mem_free(chunks);
    return false;
}

static bool sqlz_walk_dir(sqlz_walk *walk, int64_t start_block, int64_t offset,
                          int64_t size, const char *prefix, unsigned depth) {
    sqlz_stream stream;
    int64_t remaining = size;
    bool v3 = walk->parsed->super.major == 3;
    bool be = walk->parsed->super.big_endian;
    if (depth > SQLZ_MAX_DEPTH) return false;
    stream.walk = walk;
    stream.base = walk->parsed->super.directory_table;
    stream.block = start_block;
    stream.offset = offset;

    while (remaining > 0) {
        uint8_t header[8];
        int64_t count;
        int64_t entry_block;
        int64_t index;
        if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;
        if (!v3) {
            uint32_t word;
            if (remaining < 4) return false;
            remaining -= 4;
            if (!sqlz_meta_read(&stream, 4U, header)) return false;
            word = sqlz_u32(header, be);
            count = (int64_t)sqlz_bits(word, 32U, 0U, 8U, be) + 1;
            entry_block = (int64_t)sqlz_bits(word, 32U, 8U, 24U, be);
        } else {
            if (remaining < 9) return false;
            remaining -= 9;
            if (!sqlz_meta_read(&stream, 1U, header)) return false;
            count = (int64_t)header[0] + 1;
            if (!sqlz_meta_read(&stream, 8U, header)) return false;
            entry_block = (int64_t)sqlz_u32(header, be);
        }

        for (index = 0; index < count; ++index) {
            uint8_t entry[4];
            uint8_t name[SQLZ_MAX_NAME];
            uint32_t word;
            int64_t entry_offset;
            int64_t entry_type;
            int64_t name_size;
            int64_t need = v3 ? 5 : 3;
            if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;
            if (remaining < need) return false;
            remaining -= need;
            if (!sqlz_meta_read(&stream, 2U, entry)) return false;
            word = sqlz_u16(entry, be);
            entry_offset = (int64_t)sqlz_bits(word, 16U, 0U, 13U, be);
            entry_type = (int64_t)sqlz_bits(word, 16U, 13U, 3U, be);
            if (!sqlz_meta_read(&stream, 1U, entry)) return false;
            name_size = (int64_t)entry[0] + 1;
            if (v3 && !sqlz_meta_read(&stream, 2U, entry)) return false;
            if (name_size > remaining) return false;
            remaining -= name_size;
            if (!sqlz_meta_read(&stream, (size_t)name_size, name)) return false;

            if (entry_type == SQLZ_INODE_DIR || entry_type == SQLZ_INODE_FILE) {
                /* A name that cannot be joined (embedded '/', NUL, too long)
                 * and a child whose inode is damaged are skipped; the
                 * listing itself stays in step, so its siblings still load. */
                char *child = sqlz_join_name(prefix, name, (size_t)name_size);
                if (!child) continue;
                (void)sqlz_walk_node(walk, entry_block, entry_offset,
                                     (int32_t)entry_type, child, depth + 1U);
                xx_str_free(child);
            }
        }
    }
    return remaining == 0;
}

static bool sqlz_walk_node(sqlz_walk *walk, int64_t block, int64_t offset,
                           int32_t want, const char *name, unsigned depth) {
    sqlz_stream stream;
    int32_t type = 0;
    if (depth > SQLZ_MAX_DEPTH || ++walk->visits > SQLZ_MAX_VISITS) return false;
    if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;
    stream.walk = walk;
    stream.base = walk->parsed->super.inode_table;
    stream.block = block;
    stream.offset = offset;
    if (!sqlz_base_inode(walk, &stream, &type)) return false;
    if (type != want && !(type == SQLZ_INODE_LDIR && want == SQLZ_INODE_DIR) &&
        !(type == SQLZ_INODE_LREG && want == SQLZ_INODE_FILE)) {
        return false;
    }
    /* v2 has no extended regular inode. */
    if (type == SQLZ_INODE_LREG && walk->parsed->super.major == 2) return false;
    if (type == SQLZ_INODE_DIR || type == SQLZ_INODE_LDIR) {
        int64_t dir_size = 0, dir_offset = 0, dir_start = 0;
        unsigned level;
        for (level = 0U; level < depth; ++level) {
            if (walk->ancestor_block[level] == block &&
                walk->ancestor_offset[level] == offset) {
                return true; /* a directory inside itself: not walked again */
            }
        }
        walk->ancestor_block[depth] = block;
        walk->ancestor_offset[depth] = offset;
        if (!sqlz_dir_inode(walk, &stream, type == SQLZ_INODE_LDIR, &dir_size,
                            &dir_offset, &dir_start)) {
            return false;
        }
        if (dir_size < 0 || dir_offset >= (int64_t)SQLZ_META_MAX) return false;
        return sqlz_walk_dir(walk, dir_start, dir_offset, dir_size, name, depth);
    }
    return sqlz_file_data(walk, &stream, type == SQLZ_INODE_LREG, name);
}

/* ========================================================== superblock === */

static bool sqlz_parse_super(const uint8_t *h, int64_t image_size,
                             sqlz_super *super) {
    unsigned block_log;
    bool be;
    xx_rt_memset(super, 0, sizeof(*super));
    if (h[0] == 0x73U && h[1] == 0x71U && h[2] == 0x6CU && h[3] == 0x7AU) {
        be = true; /* 'sqlz' */
    } else if (h[0] == 0x7AU && h[1] == 0x6CU && h[2] == 0x71U && h[3] == 0x73U) {
        be = false; /* 'zlqs' */
    } else {
        return false;
    }
    super->big_endian = be;
    super->major = (int32_t)sqlz_u16(h + 0x1C, be);
    super->minor = (int32_t)sqlz_u16(h + 0x1E, be);
    if (super->major != 2 && super->major != 3) return false;
    if (super->major == 3 && image_size < SQLZ_SUPER_V3) return false;

    block_log = sqlz_u16(h + 0x22, be);
    super->flags = h[0x24];
    super->inodes = (int64_t)sqlz_u32(h + 0x04, be);
    super->block_size = (int64_t)sqlz_u32(h + 0x33, be);
    super->fragments = (int64_t)sqlz_u32(h + 0x37, be);
    super->root_inode = (int64_t)(sqlz_u64(h + 0x2B, be) & UINT64_C(0xFFFFFFFFFFFF));
    if (block_log < 12U || block_log > 20U ||
        super->block_size != (INT64_C(1) << block_log)) {
        return false;
    }
    if (super->major == 2) {
        super->bytes_used = (int64_t)sqlz_u32(h + 0x08, be);
        super->inode_table = (int64_t)sqlz_u32(h + 0x14, be);
        super->directory_table = (int64_t)sqlz_u32(h + 0x18, be);
        super->fragment_table = (int64_t)sqlz_u32(h + 0x3B, be);
    } else {
        uint64_t used = sqlz_u64(h + 0x3F, be);
        uint64_t it = sqlz_u64(h + 0x57, be);
        uint64_t dt = sqlz_u64(h + 0x5F, be);
        uint64_t ft = sqlz_u64(h + 0x67, be);
        if (used > (uint64_t)INT64_MAX || it > (uint64_t)INT64_MAX ||
            dt > (uint64_t)INT64_MAX) {
            return false;
        }
        super->bytes_used = (int64_t)used;
        super->inode_table = (int64_t)it;
        super->directory_table = (int64_t)dt;
        super->fragment_table = (ft > (uint64_t)INT64_MAX) ? -1 : (int64_t)ft;
    }
    if (super->inode_table < (super->major == 2 ? SQLZ_SUPER_V2 : SQLZ_SUPER_V3) ||
        super->directory_table <= super->inode_table ||
        super->directory_table >= image_size ||
        super->bytes_used <= super->directory_table) {
        return false;
    }
    if (super->fragments != 0 &&
        (super->fragment_table <= super->directory_table ||
         super->fragment_table >= image_size)) {
        return false;
    }
    return true;
}

static bool sqlz_parse(Abstractformat *self, sqlz_private *parsed, bool full,
                       xx_pd_struct *pd) {
    uint8_t header[SQLZ_SUPER_V3];
    sqlz_walk walk;
    int64_t total;
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    parsed->image_size = total - self->base_address;
    if (parsed->image_size < SQLZ_SUPER_V2) return false;
    xx_rt_memset(header, 0, sizeof(header));
    if (!sqlz_read_at(self, 0,
                      header, parsed->image_size < SQLZ_SUPER_V3
                                  ? (size_t)SQLZ_SUPER_V2
                                  : (size_t)SQLZ_SUPER_V3)) {
        return false;
    }
    if (!sqlz_parse_super(header, parsed->image_size, &parsed->super)) {
        return false;
    }
    if (!full) return true;

    walk.format = self;
    walk.parsed = parsed;
    walk.pd = pd;
    walk.visits = 0U;
    (void)sqlz_walk_node(&walk, parsed->super.root_inode >> 16,
                         parsed->super.root_inode & 0xFFFF, SQLZ_INODE_DIR, "",
                         0U);
    if (parsed->count == 0U) {
        sqlz_private_cleanup(parsed);
        return false;
    }
    return true;
}

/* ========================================================== extraction === */

static bool sqlz_write_all(xx_io_device *destination, const uint8_t *data,
                           size_t size) {
    return size == 0U || xx_io_write(destination, data, size) == (ssize_t)size;
}

static bool sqlz_extract_member(Abstractformat *self, sqlz_session *session,
                                const sqlz_member *member,
                                xx_io_device *destination, xx_pd_struct *pd) {
    const sqlz_private *parsed = &session->parsed;
    size_t block_size = (size_t)parsed->super.block_size;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    int64_t produced_total = 0;
    size_t index;
    bool result = false;

    packed = (uint8_t *)xx_mem_alloc(block_size + 64U);
    plain = (uint8_t *)xx_mem_alloc(block_size);
    if (!packed || !plain) goto cleanup;

    for (index = 0U; index < member->chunk_count; ++index) {
        const sqlz_chunk *chunk = &member->chunks[index];
        const uint8_t *emit;
        size_t emit_size;
        size_t produced = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (chunk->out_size < 0 || chunk->out_size > (int64_t)block_size ||
            chunk->skip < 0) {
            goto cleanup;
        }
        if (chunk->kind == SQLZ_KIND_SPARSE) {
            xx_rt_memset(plain, 0, (size_t)chunk->out_size);
            if (!sqlz_write_all(destination, plain, (size_t)chunk->out_size)) {
                goto cleanup;
            }
            produced_total += chunk->out_size;
            continue;
        }
        /* A block never grows past the block size, except by a codec
         * header; anything larger is not a block of this image. */
        if (chunk->on_disk <= 0 || chunk->on_disk > (int64_t)block_size + 64) {
            goto cleanup;
        }
        if (chunk->fragment && chunk->kind == SQLZ_KIND_COMPRESSED &&
            session->frag_data && session->frag_offset == chunk->offset) {
            emit = session->frag_data;
            emit_size = session->frag_size;
        } else {
            if (!sqlz_read_at(self, chunk->offset, packed,
                              (size_t)chunk->on_disk)) {
                goto cleanup;
            }
            if (chunk->kind == SQLZ_KIND_STORED) {
                emit = packed;
                emit_size = (size_t)chunk->on_disk;
            } else {
                bool exact = !chunk->fragment;
                size_t want = exact ? (size_t)chunk->out_size : block_size;
                if (!sqlz_decompress(packed, (size_t)chunk->on_disk, plain, want,
                                     exact, &produced)) {
                    goto cleanup;
                }
                emit = plain;
                emit_size = produced;
                if (chunk->fragment) {
                    uint8_t *copy = (uint8_t *)xx_mem_alloc(produced);
                    if (copy) {
                        xx_rt_memcpy(copy, plain, produced);
                        if (session->frag_data) xx_mem_free(session->frag_data);
                        session->frag_data = copy;
                        session->frag_size = produced;
                        session->frag_offset = chunk->offset;
                    }
                }
            }
        }
        if (chunk->skip > 0) {
            if ((size_t)chunk->skip >= emit_size) goto cleanup;
            emit += chunk->skip;
            emit_size -= (size_t)chunk->skip;
        }
        if (emit_size < (size_t)chunk->out_size) goto cleanup;
        emit_size = (size_t)chunk->out_size;
        if (!sqlz_write_all(destination, emit, emit_size)) goto cleanup;
        produced_total += (int64_t)emit_size;
    }
    result = produced_total == member->uncompressed_size;

cleanup:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

/* ===================================================== record plumbing === */

static bool sqlz_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
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

static const xx_var *sqlz_find_option(const xx_list_s *options,
                                      uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool sqlz_populate_record(xx_archive_record *record,
                                 const sqlz_member *member, int64_t base) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = -1;
    record->data_offset = base + member->span_offset;
    record->compressed_size = member->span_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->span_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          (uint64_t)SQLZ_METHOD_LZMA) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void sqlz_session_free(void *pointer) {
    sqlz_session *session = (sqlz_session *)pointer;
    if (!session) return;
    sqlz_private_cleanup(&session->parsed);
    sqlz_seen_cleanup(&session->seen);
    if (session->frag_data) xx_mem_free(session->frag_data);
    xx_mem_free(session);
}

/* =========================================================== lifecycle === */

void xx_squashfs_sqlz_init(xx_squashfs_sqlz *archive, xx_io_device *device,
                           int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_SQUASHFS_SQLZ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-squashfs");
    xx_format_set_extension(&archive->format, "squashfs");
    archive->format.check_is_valid = xx_squashfs_sqlz_check_is_valid;
    archive->format.handle_base_info = xx_squashfs_sqlz_handle_base_info;
    archive->format.get_format_size = xx_squashfs_sqlz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_squashfs_sqlz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_squashfs_sqlz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_squashfs_sqlz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_squashfs_sqlz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_squashfs_sqlz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_squashfs_sqlz_free_archive_records_reading;
    archive->format.destroy = xx_squashfs_sqlz_vtable_destroy;
}

xx_squashfs_sqlz *xx_squashfs_sqlz_create(xx_io_device *device,
                                          int64_t base_address) {
    xx_squashfs_sqlz *archive =
        (xx_squashfs_sqlz *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_squashfs_sqlz_init(archive, device, base_address);
    return archive;
}

void xx_squashfs_sqlz_destroy(xx_squashfs_sqlz *archive) {
    if (!archive) return;
    archive->internal = NULL;
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void xx_squashfs_sqlz_vtable_destroy(Abstractformat *self) {
    xx_squashfs_sqlz_destroy((xx_squashfs_sqlz *)self);
}

void xx_squashfs_sqlz_free(xx_squashfs_sqlz *archive) {
    if (!archive) return;
    xx_squashfs_sqlz_destroy(archive);
    xx_mem_free(archive);
}

/* ============================================================== vtable === */

bool xx_squashfs_sqlz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    sqlz_private parsed;
    bool result = sqlz_parse(self, &parsed, false, pd);
    sqlz_private_cleanup(&parsed);
    return result;
}

bool xx_squashfs_sqlz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_squashfs_sqlz *archive = (xx_squashfs_sqlz *)self;
    sqlz_private parsed;
    int64_t total;
    int64_t archive_size;
    if (!self) return false;
    self->is_valid = false;
    self->base_info_handled = false;
    if (!sqlz_parse(self, &parsed, true, pd)) return false;

    archive->number_of_records = parsed.count;
    archive->version_major = (uint32_t)parsed.super.major;
    archive->version_minor = (uint32_t)parsed.super.minor;
    archive->block_size = (uint32_t)parsed.super.block_size;
    archive->inode_count = (uint64_t)parsed.super.inodes;
    archive->fragment_count = (uint64_t)parsed.super.fragments;
    archive->bytes_used = parsed.super.bytes_used;
    archive->big_endian = parsed.super.big_endian;
    self->endian = parsed.super.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    (void)xx_rt_snprintf(self->version, sizeof(self->version), "%d.%d",
                         (int)parsed.super.major, (int)parsed.super.minor);

    archive_size = parsed.image_size;
    if (parsed.super.bytes_used > 0 &&
        parsed.super.bytes_used <= parsed.image_size) {
        archive_size = parsed.super.bytes_used;
    }
    self->format_size = archive_size;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + archive_size) {
        self->overlay_offset = self->base_address + archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed.count;
    self->is_valid = true;
    self->base_info_handled = true;
    sqlz_private_cleanup(&parsed);
    return true;
}

int64_t xx_squashfs_sqlz_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_squashfs_sqlz_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_squashfs_sqlz *)self)->number_of_records;
}

xx_archive_record_state *xx_squashfs_sqlz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    sqlz_session *session;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    session = (sqlz_session *)xx_mem_calloc(1U, sizeof(*session));
    if (!state || !session) {
        if (state) xx_mem_free(state);
        if (session) xx_mem_free(session);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = session;
    state->free_internal = sqlz_session_free;
    session->frag_offset = -1;
    if (!sqlz_copy_options(&state->options, options) ||
        !sqlz_parse(self, &session->parsed, true, pd)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->total_records = (int64_t)session->parsed.count;
    if (session->parsed.count != 0U &&
        sqlz_populate_record(&state->current_record, &session->parsed.members[0],
                             self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_squashfs_sqlz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_squashfs_sqlz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    sqlz_session *session;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    session = (sqlz_session *)state->internal_state;
    ++session->index;
    if (session->index >= session->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!sqlz_populate_record(&state->current_record,
                              &session->parsed.members[session->index],
                              self->base_address)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_squashfs_sqlz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    sqlz_session *session;
    const sqlz_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    session = (sqlz_session *)state->internal_state;
    if (session->index >= session->parsed.count) return false;
    member = &session->parsed.members[session->index];
    if (!sqlz_name_is_safe(member->name)) return false;

    option = sqlz_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        return member->span_offset >= 0 && member->span_size >= 0 &&
               member->span_offset <= session->parsed.image_size &&
               member->span_size <=
                   session->parsed.image_size - member->span_offset;
    }
    /* A repeated name is refused so it can never overwrite the first. */
    if (!sqlz_seen_add(&session->seen, member->name)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination_path = xx_str_concat3(base, "/", member->name);
    } else {
        destination_path = xx_str_concat(base, member->name);
    }
    if (!destination_path) goto cleanup;
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    if (!destination) goto cleanup;
    created = true;
    result = sqlz_extract_member(self, session, member, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_squashfs_sqlz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
