/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tarma Installer 5 / InstallMate "pre-setup loader" executables (the "tiz3"
 * payload).  xx_tarma_installer.h carries the layout.
 *
 * Only the PE headers are parsed, to find the overlay; nothing in the
 * executable is run or emulated.  The section test ("tiz3" at +0x10 and the
 * two dwords at +0x40 whose XOR is 0x35BC6F82), the optional 16-byte
 * separator and the "tzf3" block walk follow what U3's "SFX Tarma 3" handler
 * reads and what the loader itself checks; both corpus installers were
 * compared member by member with U3's output.
 *
 * The LZMA decoder below is a pull decoder: it keeps the range coder, the
 * model and a pending match between calls, so members are delivered one
 * after another from a single pass over each solid section instead of
 * decoding the section again for every member.  Its range-coder arithmetic
 * and model follow the library's own src/algo/lzma/xx_lzma_dec.c (MIT, this
 * library), which is itself a reimplementation of the public-domain LZMA
 * algorithm.  The window grows on demand up to the stream's dictionary size,
 * so a small section never allocates the full dictionary.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tarma_installer/xx_tarma_installer.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested; this
 * picks up the real file type as soon as the format is registered. */
#ifdef TARMA_INSTALLER
#define XX_TARMA_INSTALLER_FILE_TYPE XX_FILE_TYPE_TARMA_INSTALLER
#else
#define XX_TARMA_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---- limits ------------------------------------------------------------ */

#define TZ_MIN_FILE 0x200
#define TZ_MAX_PE_SECTIONS 96U
#define TZ_PE_ROW 40U
#define TZ_MAX_OPTIONAL 0x1000U

#define TZ_HEADER 0x48
#define TZ_PROPS 5
#define TZ_STREAM 0x4D /* first range-coder byte */
/* Header, properties and the five range-coder bytes an end marker needs. */
#define TZ_MIN_SECTION (TZ_STREAM + 5)
#define TZ_SEPARATOR 16
#define TZ_MAX_SECTIONS 4096U
#define TZ_XOR_KEY UINT32_C(0x35BC6F82)

#define TZ_BLOCK 0x40
#define TZ_MAX_BLOCKS UINT64_C(1000000)

/* Tarma writes an 8 MiB dictionary; 64 MiB is the LZMA SDK's largest
 * preset.  The window is grown on demand, never allocated up front. */
#define TZ_MAX_DICT (64U * 1024U * 1024U)
#define TZ_DICT_FIRST (64U * 1024U)
/* Output budget of one section: LZMA cannot exceed roughly 7000:1, so the
 * ratio term never binds on a real stream; the absolute cap bounds time. */
#define TZ_MAX_OUTPUT (INT64_C(1) << 36)
#define TZ_RATIO INT64_C(8192)
#define TZ_RATIO_SLACK (INT64_C(64) << 20)

#define TZ_IN_BUFFER 65536U
#define TZ_COPY_BUFFER 65536U
#define TZ_NAME_MAX 24U

/* ---- helpers ----------------------------------------------------------- */

static uint32_t tz_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}

static uint32_t tz_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint64_t tz_le64(const uint8_t *p) {
    return (uint64_t)tz_le32(p) | ((uint64_t)tz_le32(p + 4U) << 32U);
}

static bool tz_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool tz_write_all(xx_io_device *device, const uint8_t *data,
                         size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---- LZMA pull decoder ------------------------------------------------- */

#define TZ_STATES 12U
#define TZ_POS_STATES 16U
#define TZ_PROB_INIT 1024U

typedef struct tz_len_s {
    uint16_t choice;
    uint16_t choice2;
    uint16_t low[TZ_POS_STATES][8];
    uint16_t mid[TZ_POS_STATES][8];
    uint16_t high[256];
} tz_len;

typedef struct tz_lzma_s {
    xx_io_device *device;
    int64_t in_pos; /* next device byte to buffer */
    int64_t in_end; /* end of this stream on the device */
    uint8_t *in_buf;
    size_t in_len;
    size_t in_idx;
    uint32_t range;
    uint32_t code;
    bool bad;
    bool finished; /* the end marker was decoded */

    uint32_t lc, lp, pb;
    uint32_t dict_size; /* farthest distance the stream may use */
    uint8_t *dict;
    uint32_t dict_cap; /* grows to dict_size, then the window wraps */
    uint32_t dict_pos;
    uint64_t total; /* bytes produced */
    uint64_t limit; /* output budget */

    uint32_t state;
    uint32_t rep0, rep1, rep2, rep3;
    uint32_t pending; /* bytes of the current match still to copy */

    uint16_t *literal; /* 0x300 << (lc + lp) */
    uint16_t is_match[TZ_STATES][TZ_POS_STATES];
    uint16_t is_rep[TZ_STATES];
    uint16_t is_rep_g0[TZ_STATES];
    uint16_t is_rep_g1[TZ_STATES];
    uint16_t is_rep_g2[TZ_STATES];
    uint16_t is_rep0_long[TZ_STATES][TZ_POS_STATES];
    uint16_t pos_slot[4][64];
    /* Distance models of slots 4..13; one leading pad entry keeps every
     * reverse-tree base pointer inside the array. */
    uint16_t pos_special[1U + 114U];
    uint16_t align[16];
    tz_len len;
    tz_len rep_len;
} tz_lzma;

static void tz_probs(uint16_t *p, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) p[index] = TZ_PROB_INIT;
}

static void tz_lzma_free(tz_lzma *z) {
    if (!z) return;
    if (z->literal) xx_mem_free(z->literal);
    if (z->dict) xx_mem_free(z->dict);
    if (z->in_buf) xx_mem_free(z->in_buf);
    xx_mem_free(z);
}

static uint8_t tz_in_byte(tz_lzma *z) {
    if (z->in_idx >= z->in_len) {
        int64_t left = z->in_end - z->in_pos;
        size_t want;
        ssize_t got;
        if (z->bad || left <= 0) {
            z->bad = true;
            return 0U;
        }
        want = left < (int64_t)TZ_IN_BUFFER ? (size_t)left : TZ_IN_BUFFER;
        if (xx_io_seek64(z->device, z->in_pos, SEEK_SET) != 0) {
            z->bad = true;
            return 0U;
        }
        got = xx_io_read(z->device, z->in_buf, want);
        if (got <= 0 || (size_t)got > want) {
            z->bad = true;
            return 0U;
        }
        z->in_pos += (int64_t)got;
        z->in_len = (size_t)got;
        z->in_idx = 0U;
    }
    return z->in_buf[z->in_idx++];
}

static void tz_normalize(tz_lzma *z) {
    if (z->range < (UINT32_C(1) << 24)) {
        z->range <<= 8U;
        z->code = (z->code << 8U) | tz_in_byte(z);
    }
}

static uint32_t tz_bit(tz_lzma *z, uint16_t *prob) {
    uint32_t bound = (z->range >> 11U) * (uint32_t)*prob;
    uint32_t bit;
    if (z->code < bound) {
        z->range = bound;
        *prob = (uint16_t)(*prob + ((2048U - *prob) >> 5U));
        bit = 0U;
    } else {
        z->range -= bound;
        z->code -= bound;
        *prob = (uint16_t)(*prob - (*prob >> 5U));
        bit = 1U;
    }
    tz_normalize(z);
    return bit;
}

static uint32_t tz_tree(tz_lzma *z, uint16_t *probs, unsigned bits) {
    uint32_t m = 1U;
    unsigned index;
    for (index = 0U; index < bits; ++index)
        m = (m << 1U) | tz_bit(z, probs + m);
    return m - (UINT32_C(1) << bits);
}

static uint32_t tz_tree_reverse(tz_lzma *z, uint16_t *probs, unsigned bits) {
    uint32_t m = 1U, symbol = 0U;
    unsigned index;
    for (index = 0U; index < bits; ++index) {
        uint32_t bit = tz_bit(z, probs + m);
        m = (m << 1U) | bit;
        symbol |= bit << index;
    }
    return symbol;
}

static uint32_t tz_direct(tz_lzma *z, unsigned bits) {
    uint32_t result = 0U;
    while (bits-- > 0U) {
        z->range >>= 1U;
        if (z->code >= z->range) {
            z->code -= z->range;
            result = (result << 1U) | 1U;
        } else {
            result <<= 1U;
        }
        tz_normalize(z);
    }
    return result;
}

static uint32_t tz_length(tz_lzma *z, tz_len *model, uint32_t pos_state) {
    if (!tz_bit(z, &model->choice))
        return 2U + tz_tree(z, model->low[pos_state], 3U);
    if (!tz_bit(z, &model->choice2))
        return 10U + tz_tree(z, model->mid[pos_state], 3U);
    return 18U + tz_tree(z, model->high, 8U);
}

static void tz_len_init(tz_len *model) {
    model->choice = TZ_PROB_INIT;
    model->choice2 = TZ_PROB_INIT;
    tz_probs(&model->low[0][0], sizeof(model->low) / sizeof(uint16_t));
    tz_probs(&model->mid[0][0], sizeof(model->mid) / sizeof(uint16_t));
    tz_probs(model->high, 256U);
}

/* The byte distance + 1 back.  Callers have checked distance < total and
 * distance < dict_size, so the index is inside the filled window. */
static uint8_t tz_back(const tz_lzma *z, uint32_t distance) {
    uint32_t back = distance + 1U;
    return z->dict[z->dict_pos >= back ? z->dict_pos - back
                                       : z->dict_pos + z->dict_cap - back];
}

static bool tz_put(tz_lzma *z, uint8_t value) {
    if (z->total >= z->limit) {
        z->bad = true;
        return false;
    }
    z->dict[z->dict_pos++] = value;
    ++z->total;
    if (z->dict_pos == z->dict_cap) {
        if (z->dict_cap < z->dict_size) {
            uint32_t grown = z->dict_cap > z->dict_size / 2U
                                 ? z->dict_size : z->dict_cap * 2U;
            uint8_t *window = (uint8_t *)xx_mem_realloc(z->dict, grown);
            if (!window) {
                z->bad = true;
                return false;
            }
            z->dict = window;
            z->dict_cap = grown;
        } else {
            z->dict_pos = 0U;
        }
    }
    return true;
}

static bool tz_distance_ok(const tz_lzma *z, uint32_t distance) {
    return (uint64_t)distance < z->total && distance < z->dict_size;
}

static tz_lzma *tz_lzma_open(xx_io_device *device, int64_t offset,
                             int64_t size, const uint8_t *props,
                             uint64_t limit) {
    tz_lzma *z;
    uint32_t d = props[0], dict = tz_le32(props + 1U);
    size_t literals;
    unsigned index;
    if (d >= 9U * 5U * 5U || dict > TZ_MAX_DICT || size < 5) return NULL;
    z = (tz_lzma *)xx_mem_calloc(1U, sizeof(*z));
    if (!z) return NULL;
    z->pb = d / 45U;
    d %= 45U;
    z->lp = d / 9U;
    z->lc = d % 9U;
    z->dict_size = dict < 4096U ? 4096U : dict;
    z->dict_cap = z->dict_size < TZ_DICT_FIRST ? z->dict_size : TZ_DICT_FIRST;
    literals = (size_t)0x300U << (z->lc + z->lp);
    z->literal = (uint16_t *)xx_mem_alloc(literals * sizeof(uint16_t));
    z->dict = (uint8_t *)xx_mem_alloc(z->dict_cap);
    z->in_buf = (uint8_t *)xx_mem_alloc(TZ_IN_BUFFER);
    if (!z->literal || !z->dict || !z->in_buf) {
        tz_lzma_free(z);
        return NULL;
    }
    tz_probs(z->literal, literals);
    tz_probs(&z->is_match[0][0], TZ_STATES * TZ_POS_STATES);
    tz_probs(z->is_rep, TZ_STATES);
    tz_probs(z->is_rep_g0, TZ_STATES);
    tz_probs(z->is_rep_g1, TZ_STATES);
    tz_probs(z->is_rep_g2, TZ_STATES);
    tz_probs(&z->is_rep0_long[0][0], TZ_STATES * TZ_POS_STATES);
    tz_probs(&z->pos_slot[0][0], 4U * 64U);
    tz_probs(z->pos_special, sizeof(z->pos_special) / sizeof(uint16_t));
    tz_probs(z->align, 16U);
    tz_len_init(&z->len);
    tz_len_init(&z->rep_len);
    z->device = device;
    z->in_pos = offset;
    z->in_end = offset + size;
    z->limit = limit;
    z->range = UINT32_MAX;
    /* A range-coder stream starts with a zero byte, then the 32-bit code. */
    if (tz_in_byte(z) != 0U) z->bad = true;
    for (index = 0U; index < 4U; ++index)
        z->code = (z->code << 8U) | tz_in_byte(z);
    if (z->bad || z->code == UINT32_MAX) {
        tz_lzma_free(z);
        return NULL;
    }
    return z;
}

/* Decode one LZMA symbol.  A literal or short rep produces one byte into
 * *out (when out is not NULL) and sets *made; a match only arms pending. */
static bool tz_step(tz_lzma *z, uint8_t *out, size_t *made) {
    uint32_t pos_state = (uint32_t)z->total & ((1U << z->pb) - 1U);
    uint32_t state = z->state;
    uint32_t length;
    *made = 0U;
    if (!tz_bit(z, &z->is_match[state][pos_state])) {
        uint32_t lp_mask = (1U << z->lp) - 1U;
        uint32_t previous = z->total ? tz_back(z, 0U) : 0U;
        uint16_t *probs =
            z->literal +
            (size_t)0x300U * (((((uint32_t)z->total) & lp_mask) << z->lc) +
                              (previous >> (8U - z->lc)));
        uint32_t symbol;
        if (state < 7U) {
            symbol = tz_tree(z, probs, 8U);
        } else {
            uint32_t match_byte;
            if (!tz_distance_ok(z, z->rep0)) return false;
            match_byte = tz_back(z, z->rep0);
            symbol = 1U;
            do {
                uint32_t match_bit = (match_byte >> 7U) & 1U;
                uint32_t bit;
                match_byte <<= 1U;
                bit = tz_bit(z, probs + ((1U + match_bit) << 8U) + symbol);
                symbol = (symbol << 1U) | bit;
                if (match_bit != bit) {
                    while (symbol < 0x100U)
                        symbol = (symbol << 1U) | tz_bit(z, probs + symbol);
                    break;
                }
            } while (symbol < 0x100U);
            symbol &= 0xFFU;
        }
        if (z->bad || !tz_put(z, (uint8_t)symbol)) return false;
        if (out) *out = (uint8_t)symbol;
        *made = 1U;
        z->state = state < 4U ? 0U : (state < 10U ? state - 3U : state - 6U);
        return true;
    }
    if (!tz_bit(z, &z->is_rep[state])) {
        uint32_t distance, slot;
        length = tz_length(z, &z->len, pos_state);
        slot = tz_tree(z, z->pos_slot[length - 2U < 4U ? length - 2U : 3U], 6U);
        if (slot < 4U) {
            distance = slot;
        } else {
            unsigned direct = (unsigned)(slot >> 1U) - 1U;
            distance = (2U | (slot & 1U)) << direct;
            if (slot < 14U) {
                distance += tz_tree_reverse(
                    z, z->pos_special + (distance - slot), direct);
            } else {
                distance += tz_direct(z, direct - 4U) << 4U;
                distance += tz_tree_reverse(z, z->align, 4U);
                if (distance == UINT32_MAX) {
                    /* End marker. */
                    if (z->bad) return false;
                    z->finished = true;
                    return true;
                }
            }
        }
        z->rep3 = z->rep2;
        z->rep2 = z->rep1;
        z->rep1 = z->rep0;
        z->rep0 = distance;
        z->state = state < 7U ? 7U : 10U;
    } else {
        if (!tz_bit(z, &z->is_rep_g0[state])) {
            if (!tz_bit(z, &z->is_rep0_long[state][pos_state])) {
                uint8_t value;
                if (z->bad || !tz_distance_ok(z, z->rep0)) return false;
                z->state = state < 7U ? 9U : 11U;
                value = tz_back(z, z->rep0);
                if (!tz_put(z, value)) return false;
                if (out) *out = value;
                *made = 1U;
                return true;
            }
        } else {
            uint32_t distance;
            if (!tz_bit(z, &z->is_rep_g1[state])) {
                distance = z->rep1;
            } else {
                if (!tz_bit(z, &z->is_rep_g2[state])) {
                    distance = z->rep2;
                } else {
                    distance = z->rep3;
                    z->rep3 = z->rep2;
                }
                z->rep2 = z->rep1;
            }
            z->rep1 = z->rep0;
            z->rep0 = distance;
        }
        length = tz_length(z, &z->rep_len, pos_state);
        z->state = state < 7U ? 8U : 11U;
    }
    if (z->bad || !tz_distance_ok(z, z->rep0)) return false;
    z->pending = length;
    return true;
}

/* Produce up to @p want bytes (into @p out unless NULL).  Fewer bytes come
 * back only when the end marker has been reached. */
static bool tz_lzma_read(tz_lzma *z, uint8_t *out, size_t want,
                         size_t *got) {
    size_t done = 0U;
    *got = 0U;
    while (done < want) {
        if (z->bad) return false;
        if (z->pending) {
            size_t count = want - done;
            if (count > z->pending) count = z->pending;
            z->pending -= (uint32_t)count;
            while (count-- > 0U) {
                uint8_t value = tz_back(z, z->rep0);
                if (!tz_put(z, value)) return false;
                if (out) out[done] = value;
                ++done;
            }
            continue;
        }
        if (z->finished) break;
        {
            size_t made;
            if (!tz_step(z, out ? out + done : NULL, &made)) {
                z->bad = true;
                return false;
            }
            done += made;
        }
    }
    *got = done;
    return !z->bad;
}

/* ---- PE overlay and section chain -------------------------------------- */

typedef struct tz_section_s {
    int64_t offset;   /* base-relative header */
    int64_t size;     /* bytes present (clipped to the file) */
    int64_t declared; /* size field at +0x20 */
    uint8_t props[TZ_PROPS];
} tz_section;

typedef struct tz_layout_s {
    int64_t size;    /* file bytes from the base */
    int64_t overlay; /* first section */
    int64_t end;     /* end of the chain */
    uint32_t count;
    uint32_t major, minor;
    bool truncated;
} tz_layout;

/* End of the last section's raw data and the file alignment, both from the
 * headers only: DOS header, NT signature, file header, optional-header
 * magic and alignment, and the section table. */
static bool tz_pe_overlay(Abstractformat *format, int64_t size,
                          int64_t *overlay, uint32_t *alignment) {
    uint8_t header[64];
    uint8_t nt[24];
    uint8_t optional[40];
    uint8_t table[TZ_MAX_PE_SECTIONS * TZ_PE_ROW];
    int64_t base = format->base_address;
    int64_t nt_offset, table_offset, end = 0;
    uint32_t sections, optional_size, index, magic;
    if (size < TZ_MIN_FILE || !tz_read_at(format->device, base, header, 64U) ||
        header[0] != 'M' || header[1] != 'Z')
        return false;
    nt_offset = (int64_t)tz_le32(header + 0x3CU);
    if (nt_offset < 4 || nt_offset > size - 64 ||
        !tz_read_at(format->device, base + nt_offset, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    sections = tz_le16(nt + 6U);
    optional_size = tz_le16(nt + 20U);
    if (sections == 0U || sections > TZ_MAX_PE_SECTIONS ||
        optional_size < sizeof(optional) || optional_size > TZ_MAX_OPTIONAL ||
        !tz_read_at(format->device, base + nt_offset + 24, optional,
                    sizeof(optional)))
        return false;
    magic = tz_le16(optional);
    if (magic != 0x10BU && magic != 0x20BU) return false;
    *alignment = tz_le32(optional + 36U);
    table_offset = nt_offset + 24 + (int64_t)optional_size;
    if (table_offset > size ||
        (int64_t)(sections * TZ_PE_ROW) > size - table_offset ||
        !tz_read_at(format->device, base + table_offset, table,
                    sections * TZ_PE_ROW))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * TZ_PE_ROW;
        int64_t raw_size = (int64_t)tz_le32(row + 16U);
        int64_t raw_offset = (int64_t)tz_le32(row + 20U);
        if (raw_size == 0) continue;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    if (end < table_offset + (int64_t)(sections * TZ_PE_ROW) || end >= size)
        return false;
    *overlay = end;
    return true;
}

static bool tz_section_header_ok(const uint8_t *h) {
    return xx_rt_memcmp(h + 0x10U, "tiz3", 4U) == 0 &&
           (tz_le32(h + 0x40U) ^ tz_le32(h + 0x44U)) == TZ_XOR_KEY &&
           h[0x48] < 9U * 5U * 5U && tz_le32(h + 0x49U) <= TZ_MAX_DICT &&
           h[TZ_STREAM] == 0U;
}

/* Walk the section chain.  @p out (may be NULL) receives up to
 * TZ_MAX_SECTIONS entries. */
static bool tz_locate(Abstractformat *format, tz_layout *layout,
                      tz_section *out) {
    uint8_t h[TZ_STREAM + 1];
    uint8_t separator[TZ_SEPARATOR];
    int64_t total, size, position = -1, candidates[2];
    uint32_t alignment = 0U, index;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_mem_zero(layout, sizeof(*layout));
    layout->size = size;
    if (!tz_pe_overlay(format, size, &candidates[0], &alignment)) return false;
    /* The payload normally starts right at the raw end; a file-aligned
     * start is accepted as well. */
    candidates[1] = candidates[0];
    if (alignment >= 0x200U && alignment <= 0x10000U &&
        (alignment & (alignment - 1U)) == 0U)
        candidates[1] = (candidates[0] + (int64_t)alignment - 1) &
                        ~((int64_t)alignment - 1);
    for (index = 0U; index < 2U && position < 0; ++index) {
        int64_t at = candidates[index];
        if (index == 1U && at == candidates[0]) break;
        if (at > size - (int64_t)sizeof(h) - 5) continue;
        if (tz_read_at(format->device, format->base_address + at, h,
                       sizeof(h)) &&
            tz_section_header_ok(h))
            position = at;
    }
    if (position < 0) return false;
    layout->overlay = position;
    layout->minor = tz_le16(h + 0x14U);
    layout->major = tz_le16(h + 0x16U);
    for (;;) {
        int64_t declared, present;
        if (layout->count >= TZ_MAX_SECTIONS ||
            position > size - (int64_t)sizeof(h) ||
            !tz_read_at(format->device, format->base_address + position, h,
                        sizeof(h)) ||
            !tz_section_header_ok(h))
            break;
        declared = (int64_t)tz_le64(h + 0x20U);
        if (declared < TZ_MIN_SECTION) break;
        present = declared > size - position ? size - position : declared;
        if (out) {
            tz_section *section = &out[layout->count];
            section->offset = position;
            section->size = present;
            section->declared = declared;
            xx_rt_memcpy(section->props, h + TZ_HEADER, TZ_PROPS);
        }
        ++layout->count;
        if (present < declared) {
            layout->truncated = true;
            position = size;
            break;
        }
        position += declared;
        if (position <= size - TZ_SEPARATOR &&
            tz_read_at(format->device, format->base_address + position,
                       separator, sizeof(separator)) &&
            (tz_le32(separator) ^ tz_le32(separator + 4U)) == UINT32_MAX &&
            tz_le64(separator + 8U) == 0U)
            position += TZ_SEPARATOR;
    }
    if (layout->count == 0U) return false;
    layout->end = position;
    return true;
}

/* ---- block cursor ------------------------------------------------------ */

typedef struct tz_cursor_s {
    Abstractformat *format;
    tz_layout layout;
    tz_section *sections;
    uint32_t section; /* index of the open (or next) section */
    tz_lzma *z;
    uint8_t *scratch;
    bool have_block;
    bool error;
    uint64_t listed;    /* block headers delivered */
    uint64_t completed; /* blocks whose data decoded completely */
    uint64_t data_start;
    uint64_t data_size;
    uint8_t header[TZ_BLOCK];
} tz_cursor;

static void tz_cursor_free(tz_cursor *c) {
    if (!c) return;
    tz_lzma_free(c->z);
    if (c->sections) xx_mem_free(c->sections);
    if (c->scratch) xx_mem_free(c->scratch);
    xx_mem_free(c);
}

static tz_cursor *tz_cursor_create(Abstractformat *format) {
    tz_cursor *c = (tz_cursor *)xx_mem_calloc(1U, sizeof(*c));
    tz_layout layout;
    if (!c) return NULL;
    c->format = format;
    c->sections =
        (tz_section *)xx_mem_calloc(TZ_MAX_SECTIONS, sizeof(tz_section));
    c->scratch = (uint8_t *)xx_mem_alloc(TZ_COPY_BUFFER);
    if (!c->sections || !c->scratch || !tz_locate(format, &layout, c->sections)) {
        tz_cursor_free(c);
        return NULL;
    }
    c->layout = layout;
    return c;
}

static bool tz_cursor_open(tz_cursor *c, uint32_t index) {
    const tz_section *section;
    int64_t stream, budget;
    tz_lzma_free(c->z);
    c->z = NULL;
    if (index >= c->layout.count) return false;
    section = &c->sections[index];
    stream = section->size - TZ_STREAM;
    budget = TZ_MAX_OUTPUT;
    if (stream < (TZ_MAX_OUTPUT - TZ_RATIO_SLACK) / TZ_RATIO)
        budget = stream * TZ_RATIO + TZ_RATIO_SLACK;
    c->z = tz_lzma_open(c->format->device,
                        c->format->base_address + section->offset + TZ_STREAM,
                        stream, section->props, (uint64_t)budget);
    return c->z != NULL;
}

static bool tz_cursor_skip_to(tz_cursor *c, uint64_t target,
                              xx_pd_struct *pd) {
    while (c->z && c->z->total < target) {
        uint64_t left = target - c->z->total;
        size_t want = left < TZ_COPY_BUFFER ? (size_t)left : TZ_COPY_BUFFER;
        size_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!tz_lzma_read(c->z, c->scratch, want, &got) || got != want)
            return false;
    }
    return c->z != NULL;
}

/* Advance to the next block header, finishing the current block's data
 * first.  Returns false at the clean end and on damage (c->error). */
static bool tz_cursor_next(tz_cursor *c, xx_pd_struct *pd) {
    if (c->error) return false;
    if (c->have_block) {
        c->have_block = false;
        if (!tz_cursor_skip_to(c, c->data_start + c->data_size, pd)) {
            c->error = true;
            return false;
        }
        ++c->completed;
    }
    for (;;) {
        size_t got;
        int64_t size;
        if (!c->z) {
            if (c->section >= c->layout.count) return false;
            if (!tz_cursor_open(c, c->section)) {
                c->error = true;
                return false;
            }
        }
        if (pd && xx_pd_is_stopped(pd)) {
            c->error = true;
            return false;
        }
        if (!tz_lzma_read(c->z, c->header, TZ_BLOCK, &got)) {
            c->error = true;
            return false;
        }
        if (got == 0U && c->z->finished) {
            tz_lzma_free(c->z);
            c->z = NULL;
            ++c->section;
            continue;
        }
        size = (int64_t)tz_le64(c->header + 0x10U);
        if (got != TZ_BLOCK || xx_rt_memcmp(c->header, "tzf3", 4U) != 0 ||
            size < 0 || (uint64_t)size > c->z->limit - c->z->total ||
            c->listed >= TZ_MAX_BLOCKS) {
            c->error = true;
            return false;
        }
        c->data_start = c->z->total;
        c->data_size = (uint64_t)size;
        c->have_block = true;
        ++c->listed;
        return true;
    }
}

/* Deliver the current block's data to @p destination (NULL only decodes).
 * A block already passed is re-reached by restarting its section. */
static bool tz_cursor_unpack(tz_cursor *c, xx_io_device *destination,
                             xx_pd_struct *pd) {
    uint64_t end;
    if (!c->have_block || c->error) return false;
    if (!c->z || c->z->total > c->data_start) {
        if (!tz_cursor_open(c, c->section)) return false;
    }
    if (!tz_cursor_skip_to(c, c->data_start, pd)) return false;
    end = c->data_start + c->data_size;
    while (c->z->total < end) {
        uint64_t left = end - c->z->total;
        size_t want = left < TZ_COPY_BUFFER ? (size_t)left : TZ_COPY_BUFFER;
        size_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!tz_lzma_read(c->z, c->scratch, want, &got) || got != want)
            return false;
        if (destination && !tz_write_all(destination, c->scratch, want))
            return false;
    }
    return true;
}

/* ---- records ----------------------------------------------------------- */

typedef struct tz_stream_s {
    tz_cursor *cursor;
} tz_stream;

static void tz_stream_free(void *opaque) {
    tz_stream *stream = (tz_stream *)opaque;
    if (!stream) return;
    tz_cursor_free(stream->cursor);
    xx_mem_free(stream);
}

static bool tz_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *tz_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* Members are numbered in stream order, as U3 names them. */
static void tz_member_name(uint64_t number, char *name) {
    char digits[TZ_NAME_MAX];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + (int)(number % 10U));
        number /= 10U;
    } while (number != 0U && count < sizeof(digits) - 1U);
    for (index = 0U; index < count; ++index)
        name[index] = digits[count - 1U - index];
    name[count] = 0;
}

static bool tz_set_record(xx_archive_record *record, const tz_cursor *c) {
    const tz_section *section = &c->sections[c->section];
    char name[TZ_NAME_MAX];
    uint64_t filetime = tz_le64(c->header + 0x20U);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    tz_member_name(c->listed, name);
    record->header_offset = c->format->base_address + section->offset;
    record->header_size = TZ_STREAM;
    record->data_offset =
        c->format->base_address + section->offset + TZ_STREAM;
    record->compressed_size = section->size - TZ_STREAM;
    if (!xx_archive_record_set_original_name(record, name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        c->data_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        XX_TARMA_INSTALLER_METHOD_LZMA) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (filetime != 0U &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, filetime))
        return false;
    return true;
}

/* ---- public API -------------------------------------------------------- */

void xx_tarma_installer_init(xx_tarma_installer *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TARMA_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-tarma-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_tarma_installer_check_is_valid;
    archive->format.handle_base_info = xx_tarma_installer_handle_base_info;
    archive->format.get_format_size = xx_tarma_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tarma_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tarma_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tarma_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tarma_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tarma_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tarma_installer_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
}

xx_tarma_installer *xx_tarma_installer_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_tarma_installer *archive =
        (xx_tarma_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_tarma_installer_init(archive, device, base_address);
    return archive;
}

void xx_tarma_installer_destroy(xx_tarma_installer *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_tarma_installer_free(xx_tarma_installer *archive) {
    if (!archive) return;
    xx_tarma_installer_destroy(archive);
    xx_mem_free(archive);
}

bool xx_tarma_installer_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    tz_layout layout;
    (void)pd;
    return tz_locate(format, &layout, NULL);
}

bool xx_tarma_installer_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    xx_tarma_installer *archive;
    tz_cursor *c;
    uint64_t unpacked = 0U;
    char version[32];
    bool result = false;
    if (!format) return false;
    c = tz_cursor_create(format);
    if (!c) return false;
    while (tz_cursor_next(c, pd)) {
        if (c->data_size > UINT64_MAX - unpacked) {
            c->error = true;
            break;
        }
        unpacked += c->data_size;
    }
    if (pd && xx_pd_is_stopped(pd)) goto done;
    /* A damaged section still yields the blocks before the damage; with
     * none at all there is nothing to publish. */
    if (c->error && c->listed == 0U) goto done;
    archive = (xx_tarma_installer *)format;
    archive->number_of_records = c->listed;
    archive->unpacked_size = unpacked;
    archive->number_of_sections = c->layout.count;
    archive->payload_offset = c->layout.overlay;
    archive->payload_end = c->layout.end;
    archive->truncated = c->layout.truncated;
    archive->damaged = c->error;
    (void)xx_rt_snprintf(version, sizeof(version), "%u.%u",
                         (unsigned)c->layout.major, (unsigned)c->layout.minor);
    xx_format_set_version(format, version);
    format->number_of_archive_records = c->listed;
    format->format_size = c->layout.end;
    format->overlay_offset = c->layout.end < c->layout.size
                                 ? format->base_address + c->layout.end : -1;
    format->overlay_size = c->layout.size - c->layout.end;
    format->is_valid = true;
    format->base_info_handled = true;
    result = true;
done:
    tz_cursor_free(c);
    return result;
}

int64_t xx_tarma_installer_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_tarma_installer_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_tarma_installer_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_tarma_installer_handle_base_info(format, pd))
               ? ((xx_tarma_installer *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_tarma_installer_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    tz_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (tz_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->cursor = tz_cursor_create(format);
    if (!stream->cursor) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        tz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = tz_stream_free;
    state->total_records =
        format->base_info_handled
            ? (int64_t)((xx_tarma_installer *)format)->number_of_records : -1;
    if (!tz_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (tz_cursor_next(stream->cursor, pd)) {
        if (!tz_set_record(&state->current_record, stream->cursor)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_tarma_installer_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_tarma_installer_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    tz_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (tz_stream *)state->internal_state) ||
        !tz_cursor_next(stream->cursor, pd) ||
        !tz_set_record(&state->current_record, stream->cursor)) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_tarma_installer_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    tz_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char name[TZ_NAME_MAX];
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (tz_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = tz_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return tz_cursor_unpack(stream->cursor, NULL, pd);
    /* The name is the member's decimal number, so it is always a plain,
     * non-reserved file name and never collides with another member. */
    tz_member_name(stream->cursor->listed, name);
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
        created = destination != NULL;
        if (!destination) goto done;
        result = tz_cursor_unpack(stream->cursor, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

bool xx_tarma_installer_unpack_current_to_device(
    Abstractformat *format, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd) {
    tz_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (tz_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    return tz_cursor_unpack(stream->cursor, destination, pd);
}

void xx_tarma_installer_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
