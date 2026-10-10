/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * StuffIt X (.sitx) archives.  xx_stuffitx.h carries the element layout.
 *
 * Written from the format's structure.  The element stream semantics (P2
 * integers, bit/byte alignment rules, element and catalog tags, the 6-bit
 * HDIST deflate variant) were studied in The Unarchiver's XADMaster
 * (LGPL-2.1) for understanding only; no code was taken from it.  Every
 * sample was produced by an independent Python generator and cross-checked
 * with unar 1.8.1.  The canonical-Huffman decoder is the textbook
 * count/symbol method of RFC 1951 section 3.2.2.
 *
 * Emission order follows the reference reader: at the first data element
 * every entry that has no fork yet is published (folders and empty files),
 * then each data element publishes its forks in fork-index order.  Entries
 * that were never published by the end of the stream and never received a
 * fork are published at the end (the reference drops them when an archive
 * has no data element at all).
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/stuffitx/xx_stuffitx.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: picks up the real file type as soon as
 * STUFFITX is registered in xxfc_defs.h. */
#ifdef STUFFITX
#define XX_STUFFITX_FILE_TYPE XX_FILE_TYPE_STUFFITX
#else
#define XX_STUFFITX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SX_MAGIC_SIZE 8U
#define SX_MAX_KEYS 256U /* attribute / algorithm pairs per element */
#define SX_MAX_ENTRIES 1000000U
#define SX_MAX_FORKS 1000000U
#define SX_MAX_NAME_BYTES (64U * 1024U * 1024U) /* all paths together */
#define SX_MAX_DATA 1000000U
#define SX_MAX_RECORDS 1000000U
#define SX_MAX_CATALOG (64U * 1024U * 1024U)
#define SX_MAX_STRING 65536U
#define SX_MAX_COMPONENT 1024U
#define SX_MAX_PATH 4096U
#define SX_MAX_TAG10 65536U
#define SX_MAX_SUFFIX 100000U
#define SX_WINDOW 65536U
#define SX_NONE UINT32_MAX

#define SX_METHOD_NONE (-1)
#define SX_METHOD_DEFLATE 3
#define SX_METHOD_RC4 5
#define SX_PREPROCESS_X86 2

/* ---------------------------------------------------------------------- */
/* Bit / byte source                                                       */

typedef struct sx_src_s {
    xx_io_device *device;
    const uint8_t *memory;
    int64_t pos; /**< Next byte to fetch (absolute). */
    int64_t end;
    uint8_t *buffer;
    size_t capacity;
    int64_t buffer_pos;
    size_t buffer_len;
    uint32_t current;
    uint32_t left; /**< Unread bits of the current byte. */
    bool eof;      /**< A read ran past the end. */
    bool bad;      /**< A malformed P2 code. */
} sx_src;

static bool sx_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool sx_fetch(sx_src *s, uint8_t *out)
{
    if (s->pos >= s->end) {
        s->eof = true;
        return false;
    }
    if (s->memory) {
        *out = s->memory[s->pos++];
        return true;
    }
    if (s->buffer_len == 0U || s->pos < s->buffer_pos || s->pos >= s->buffer_pos + (int64_t)s->buffer_len) {
        int64_t available = s->end - s->pos;
        size_t amount = available < (int64_t)s->capacity ? (size_t)available : s->capacity;
        if (!sx_read_at(s->device, s->pos, s->buffer, amount)) {
            s->eof = true;
            s->buffer_len = 0U;
            return false;
        }
        s->buffer_pos = s->pos;
        s->buffer_len = amount;
    }
    *out = s->buffer[s->pos - s->buffer_pos];
    s->pos++;
    return true;
}

static bool sx_bit(sx_src *s, uint32_t *bit)
{
    if (s->left == 0U) {
        uint8_t byte;
        if (!sx_fetch(s, &byte)) return false;
        s->current = byte;
        s->left = 8U;
    }
    *bit = s->current & 1U;
    s->current >>= 1U;
    s->left--;
    return true;
}

static bool sx_bits(sx_src *s, unsigned count, uint64_t *value)
{
    uint64_t result = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index) {
        uint32_t bit;
        if (!sx_bit(s, &bit)) return false;
        result |= (uint64_t)bit << index;
    }
    *value = result;
    return true;
}

/* Bytes assembled most significant first, each byte read as 8 bits. */
static bool sx_bits_be(sx_src *s, unsigned bytes, uint64_t *value)
{
    uint64_t result = 0U;
    unsigned index;
    for (index = 0U; index < bytes; ++index) {
        uint64_t byte;
        if (!sx_bits(s, 8U, &byte)) return false;
        result = (result << 8U) | byte;
    }
    *value = result;
    return true;
}

static void sx_flush(sx_src *s)
{
    s->left = 0U;
}

/* A byte-level skip; like every byte-level read it drops the unread bits
 * of the current byte, unless it moves nowhere. */
static bool sx_skip(sx_src *s, uint64_t count)
{
    if (count == 0U) return true;
    s->left = 0U;
    if (count > (uint64_t)(s->end - s->pos)) {
        s->pos = s->end;
        s->eof = true;
        return false;
    }
    s->pos += (int64_t)count;
    return true;
}

static bool sx_bytes(sx_src *s, uint8_t *out, size_t count)
{
    size_t index;
    if (count == 0U) return true;
    s->left = 0U;
    for (index = 0U; index < count; ++index)
        if (!sx_fetch(s, out + index)) return false;
    return true;
}

/* P2: a unary count N, then bits (least significant first) until N one bits
 * were seen; the value is that number minus one. */
static bool sx_p2(sx_src *s, uint64_t *value)
{
    uint32_t bit;
    unsigned ones = 1U;
    unsigned position = 0U;
    uint64_t result = 0U;
    for (;;) {
        if (!sx_bit(s, &bit)) return false;
        if (!bit) break;
        if (++ones >= 64U) {
            s->bad = true;
            return false;
        }
    }
    while (ones) {
        if (position >= 64U) {
            s->bad = true;
            return false;
        }
        if (!sx_bit(s, &bit)) return false;
        if (bit) {
            result |= (uint64_t)1U << position;
            --ones;
        }
        ++position;
    }
    *value = result - 1U;
    return true;
}

static void sx_src_device(sx_src *s, xx_io_device *device, int64_t pos, int64_t end, uint8_t *buffer, size_t capacity)
{
    xx_mem_zero(s, sizeof(*s));
    s->device = device;
    s->pos = pos;
    s->end = end;
    s->buffer = buffer;
    s->capacity = capacity;
}

static void sx_src_memory(sx_src *s, const uint8_t *memory, size_t size)
{
    xx_mem_zero(s, sizeof(*s));
    s->memory = memory;
    s->end = (int64_t)size;
}

/* ---------------------------------------------------------------------- */
/* Elements                                                                */

typedef struct sx_element_s {
    uint64_t type;
    uint64_t attr[10];
    uint64_t alg[6];
    uint32_t attr_set;
    uint32_t alg_set;
} sx_element;

static bool sx_read_element(sx_src *s, sx_element *e)
{
    uint32_t bit;
    unsigned count;
    xx_mem_zero(e, sizeof(*e));
    if (!sx_bit(s, &bit) || !sx_p2(s, &e->type)) return false;
    for (count = 0U;; ++count) {
        uint64_t key, value;
        if (count > SX_MAX_KEYS) {
            s->bad = true;
            return false;
        }
        if (!sx_p2(s, &key)) return false;
        if (key == 0U) break;
        if (!sx_p2(s, &value)) return false;
        if (key <= 10U) {
            e->attr[key - 1U] = value;
            e->attr_set |= 1U << (key - 1U);
        }
    }
    for (count = 0U;; ++count) {
        uint64_t key, value, extra;
        if (count > SX_MAX_KEYS) {
            s->bad = true;
            return false;
        }
        if (!sx_p2(s, &key)) return false;
        if (key == 0U) break;
        if (!sx_p2(s, &value)) return false;
        if (key <= 6U) {
            e->alg[key - 1U] = value;
            e->alg_set |= 1U << (key - 1U);
        }
        if (key == 4U && !sx_p2(s, &extra)) return false;
    }
    return true;
}

static bool sx_has_attr(const sx_element *e, unsigned key)
{
    return (e->attr_set & (1U << (key - 1U))) != 0U;
}

static int64_t sx_alg(const sx_element *e, unsigned key)
{
    if (!(e->alg_set & (1U << (key - 1U)))) return -1;
    return e->alg[key - 1U] > (uint64_t)INT32_MAX ? INT32_MAX : (int64_t)e->alg[key - 1U];
}

/* The blocks of a data-carrying element and its trailing chunks (a first
 * chunk of 4 bytes is the CRC-32). */
static bool sx_scan(sx_src *s, int64_t *data_offset, bool *has_crc, uint32_t *crc)
{
    uint64_t length;
    sx_flush(s);
    *data_offset = s->pos;
    *has_crc = false;
    *crc = 0U;
    for (;;) {
        if (!sx_p2(s, &length)) return false;
        if (length == 0U) break;
        if (!sx_skip(s, length)) return false;
    }
    sx_flush(s);
    if (!sx_p2(s, &length)) return false;
    if (length == 4U) {
        uint8_t bytes[4];
        if (!sx_bytes(s, bytes, 4U)) return false;
        *crc = ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | bytes[3];
        *has_crc = true;
        if (!sx_p2(s, &length)) return false;
    }
    while (length) {
        if (!sx_skip(s, length) || !sx_p2(s, &length)) return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Stream decoder: blocks -> method -> bytes                               */

typedef struct sx_huff_s {
    uint16_t count[16];
    uint16_t symbol[352];
} sx_huff;

typedef struct sx_data_s {
    int64_t data_offset;
    int64_t packed_size;
    uint64_t actual_size;
    int64_t method;
    int64_t checksum;
    int64_t preprocess;
    int64_t cipher;
    uint32_t crc;
    bool has_crc;
} sx_data;

typedef struct sx_dec_s {
    sx_src src;
    uint8_t *io;
    uint64_t block_left;
    bool blocks_done;
    int64_t method;
    int64_t preprocess;
    uint64_t x86_size, x86_pos;
    int64_t x86_last_hit;
    uint8_t x86_bitfield;
    uint8_t x86_queue[4], x86_pending[4];
    uint8_t x86_queued, x86_pending_at, x86_pending_left;
    /* RC4 */
    uint8_t rc4[256];
    uint32_t ri, rj;
    /* inflate */
    uint32_t bitbuf, bitcnt;
    int mode; /* 0 block header, 1 stored, 2 codes, 3 done */
    bool last;
    uint32_t stored_left;
    uint32_t match_len, match_dist;
    sx_huff lit, dist;
    uint8_t window[SX_WINDOW];
    uint32_t wpos;
    uint64_t history; /* bytes the inflater has produced */
    /* consumer side */
    uint64_t produced;
    uint32_t crc;
    uint32_t data_index;
    bool active;
    bool failed;
} sx_dec;

static bool sx_method_supported(const sx_data *d)
{
    return d->cipher < 0 && (d->preprocess < 0 || d->preprocess == SX_PREPROCESS_X86) &&
           (d->method == SX_METHOD_NONE || d->method == SX_METHOD_DEFLATE || d->method == SX_METHOD_RC4);
}

static bool sx_block_byte(sx_dec *dec, uint8_t *out)
{
    while (dec->block_left == 0U) {
        uint64_t size;
        if (dec->blocks_done) return false;
        if (!sx_p2(&dec->src, &size)) return false;
        if (size == 0U) {
            dec->blocks_done = true;
            return false;
        }
        dec->block_left = size;
    }
    if (!sx_bytes(&dec->src, out, 1U)) return false;
    dec->block_left--;
    return true;
}

static bool sx_need(sx_dec *dec, uint32_t count)
{
    while (dec->bitcnt < count) {
        uint8_t byte;
        if (!sx_block_byte(dec, &byte)) return false;
        dec->bitbuf |= (uint32_t)byte << dec->bitcnt;
        dec->bitcnt += 8U;
    }
    return true;
}

static bool sx_getbits(sx_dec *dec, uint32_t count, uint32_t *value)
{
    if (count == 0U) {
        *value = 0U;
        return true;
    }
    if (!sx_need(dec, count)) return false;
    *value = dec->bitbuf & ((1U << count) - 1U);
    dec->bitbuf >>= count;
    dec->bitcnt -= count;
    return true;
}

static bool sx_huff_build(sx_huff *h, const uint8_t *lengths, unsigned n)
{
    uint16_t offsets[16];
    unsigned symbol, length;
    int left = 1;
    xx_mem_zero(h->count, sizeof(h->count));
    for (symbol = 0U; symbol < n; ++symbol) h->count[lengths[symbol]]++;
    for (length = 1U; length < 16U; ++length) {
        left <<= 1;
        left -= (int)h->count[length];
        if (left < 0) return false;
    }
    offsets[1] = 0U;
    for (length = 1U; length < 15U; ++length) offsets[length + 1U] = (uint16_t)(offsets[length] + h->count[length]);
    for (symbol = 0U; symbol < n; ++symbol)
        if (lengths[symbol]) h->symbol[offsets[lengths[symbol]]++] = (uint16_t)symbol;
    return true;
}

static bool sx_huff_decode(sx_dec *dec, const sx_huff *h, unsigned *out)
{
    int code = 0, first = 0, index = 0;
    unsigned length;
    for (length = 1U; length < 16U; ++length) {
        uint32_t bit;
        int count;
        if (!sx_getbits(dec, 1U, &bit)) return false;
        code |= (int)bit;
        count = (int)h->count[length];
        if (code - first < count) {
            *out = h->symbol[index + (code - first)];
            return true;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return false;
}

static const uint16_t SX_LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t SX_LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
/* Distance codes 30 and 31 exist in this variant (64 KiB distances). */
static const uint32_t SX_DIST_BASE[32] = {1,   2,   3,   4,   5,    7,    9,    13,   17,   25,   33,   49,    65,    97,    129,   193,
                                          257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153};
static const uint8_t SX_DIST_EXTRA[32] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14};

static bool sx_block_header(sx_dec *dec)
{
    uint32_t final_bit, type;
    if (!sx_getbits(dec, 1U, &final_bit) || !sx_getbits(dec, 2U, &type)) return false;
    dec->last = final_bit != 0U;
    if (type == 0U) {
        uint32_t length, inverse, drop = dec->bitcnt & 7U;
        dec->bitbuf >>= drop;
        dec->bitcnt -= drop;
        if (!sx_getbits(dec, 16U, &length) || !sx_getbits(dec, 16U, &inverse) || length != (~inverse & 0xFFFFU)) return false;
        dec->stored_left = length;
        dec->mode = 1;
        return true;
    }
    if (type == 1U) {
        uint8_t lengths[288];
        unsigned index;
        for (index = 0U; index < 144U; ++index) lengths[index] = 8U;
        for (; index < 256U; ++index) lengths[index] = 9U;
        for (; index < 280U; ++index) lengths[index] = 7U;
        for (; index < 288U; ++index) lengths[index] = 8U;
        if (!sx_huff_build(&dec->lit, lengths, 288U)) return false;
        for (index = 0U; index < 32U; ++index) lengths[index] = 5U;
        if (!sx_huff_build(&dec->dist, lengths, 32U)) return false;
        dec->mode = 2;
        return true;
    }
    if (type == 2U) {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint8_t lengths[288 + 64];
        uint8_t meta[19];
        uint32_t hlit, hdist, hclen, value;
        unsigned index = 0U, total;
        sx_huff metacode;
        if (!sx_getbits(dec, 5U, &hlit) || !sx_getbits(dec, 6U, &hdist) || !sx_getbits(dec, 4U, &hclen)) return false;
        hlit += 257U;
        hdist += 1U;
        hclen += 4U;
        xx_mem_zero(meta, sizeof(meta));
        for (index = 0U; index < hclen; ++index) {
            if (!sx_getbits(dec, 3U, &value)) return false;
            meta[order[index]] = (uint8_t)value;
        }
        if (!sx_huff_build(&metacode, meta, 19U)) return false;
        total = hlit + hdist;
        index = 0U;
        while (index < total) {
            unsigned symbol, repeat;
            uint8_t fill = 0U;
            if (!sx_huff_decode(dec, &metacode, &symbol)) return false;
            if (symbol < 16U) {
                lengths[index++] = (uint8_t)symbol;
                continue;
            }
            if (symbol == 16U) {
                if (index == 0U || !sx_getbits(dec, 2U, &value)) return false;
                fill = lengths[index - 1U];
                repeat = 3U + value;
            } else if (symbol == 17U) {
                if (!sx_getbits(dec, 3U, &value)) return false;
                repeat = 3U + value;
            } else {
                if (!sx_getbits(dec, 7U, &value)) return false;
                repeat = 11U + value;
            }
            if (repeat > total - index) return false;
            while (repeat--) lengths[index++] = fill;
        }
        if (lengths[256] == 0U) return false;
        if (!sx_huff_build(&dec->lit, lengths, hlit) || !sx_huff_build(&dec->dist, lengths + hlit, hdist)) return false;
        dec->mode = 2;
        return true;
    }
    return false;
}

static void sx_put(sx_dec *dec, uint8_t byte)
{
    dec->window[dec->wpos] = byte;
    dec->wpos = (dec->wpos + 1U) & (SX_WINDOW - 1U);
    dec->history++;
}

static bool sx_inflate_byte(sx_dec *dec, uint8_t *out)
{
    for (;;) {
        if (dec->match_len) {
            uint8_t byte = dec->window[(dec->wpos - dec->match_dist) & (SX_WINDOW - 1U)];
            dec->match_len--;
            sx_put(dec, byte);
            *out = byte;
            return true;
        }
        if (dec->mode == 0) {
            if (!sx_block_header(dec)) return false;
            continue;
        }
        if (dec->mode == 1) {
            uint32_t value;
            if (dec->stored_left == 0U) {
                dec->mode = dec->last ? 3 : 0;
                continue;
            }
            if (!sx_getbits(dec, 8U, &value)) return false;
            dec->stored_left--;
            sx_put(dec, (uint8_t)value);
            *out = (uint8_t)value;
            return true;
        }
        if (dec->mode == 2) {
            unsigned symbol;
            uint32_t extra;
            if (!sx_huff_decode(dec, &dec->lit, &symbol)) return false;
            if (symbol < 256U) {
                sx_put(dec, (uint8_t)symbol);
                *out = (uint8_t)symbol;
                return true;
            }
            if (symbol == 256U) {
                dec->mode = dec->last ? 3 : 0;
                continue;
            }
            symbol -= 257U;
            if (symbol >= 29U || !sx_getbits(dec, SX_LEN_EXTRA[symbol], &extra)) return false;
            dec->match_len = SX_LEN_BASE[symbol] + extra;
            if (!sx_huff_decode(dec, &dec->dist, &symbol) || symbol >= 32U || !sx_getbits(dec, SX_DIST_EXTRA[symbol], &extra)) return false;
            dec->match_dist = SX_DIST_BASE[symbol] + extra;
            if (dec->match_dist > SX_WINDOW || (uint64_t)dec->match_dist > dec->history) return false;
            continue;
        }
        return false; /* past the last block */
    }
}

static bool sx_dec_start(sx_dec *dec, xx_io_device *device, int64_t end, const sx_data *data, uint32_t data_index)
{
    uint8_t byte, key;
    uint8_t *io = dec->io;
    size_t capacity = dec->src.capacity;
    unsigned index;
    uint32_t j = 0U;
    dec->active = false;
    dec->failed = false;
    if (!sx_method_supported(data)) return false;
    sx_src_device(&dec->src, device, data->data_offset, end, io, capacity);
    dec->block_left = 0U;
    dec->blocks_done = false;
    dec->method = data->method;
    dec->preprocess = data->preprocess;
    dec->x86_size = data->actual_size;
    dec->x86_pos = 0U;
    dec->x86_last_hit = -6;
    dec->x86_bitfield = 0U;
    dec->x86_queued = dec->x86_pending_at = dec->x86_pending_left = 0U;
    dec->bitbuf = dec->bitcnt = 0U;
    dec->mode = 0;
    dec->last = false;
    dec->stored_left = dec->match_len = dec->match_dist = 0U;
    dec->wpos = 0U;
    dec->history = 0U;
    dec->produced = 0U;
    dec->crc = 0U;
    dec->data_index = data_index;
    if (data->method == SX_METHOD_DEFLATE) {
        if (!sx_block_byte(dec, &byte) || byte != 15U) return false;
    } else if (data->method == SX_METHOD_RC4) {
        if (!sx_block_byte(dec, &byte) || !sx_block_byte(dec, &byte) || !sx_block_byte(dec, &key)) return false;
        for (index = 0U; index < 256U; ++index) dec->rc4[index] = (uint8_t)index;
        for (index = 0U; index < 256U; ++index) {
            uint8_t swap;
            j = (j + dec->rc4[index] + key) & 0xFFU;
            swap = dec->rc4[index];
            dec->rc4[index] = dec->rc4[j];
            dec->rc4[j] = swap;
        }
        dec->ri = dec->rj = 0U;
    }
    dec->active = true;
    return true;
}

static bool sx_dec_core_byte(sx_dec *dec, uint8_t *out)
{
    uint8_t byte, swap;
    switch (dec->method) {
        case SX_METHOD_NONE: return sx_block_byte(dec, out);
        case SX_METHOD_DEFLATE: return sx_inflate_byte(dec, out);
        case SX_METHOD_RC4:
            if (!sx_block_byte(dec, &byte)) return false;
            dec->ri = (dec->ri + 1U) & 0xFFU;
            dec->rj = (dec->rj + dec->rc4[dec->ri]) & 0xFFU;
            swap = dec->rc4[dec->ri];
            dec->rc4[dec->ri] = dec->rc4[dec->rj];
            dec->rc4[dec->rj] = swap;
            *out = byte ^ dec->rc4[(dec->rc4[dec->ri] + dec->rc4[dec->rj]) & 0xFFU];
            return true;
        default: return false;
    }
}

/* StuffIt X's x86 filter reverses absolute-to-relative CALL/JMP address
 * conversion.  Four-byte lookahead is kept in the decoder, rather than
 * seeking the compressed stream, so it also works across block boundaries. */
static bool sx_x86_pop(sx_dec *dec, uint8_t *out)
{
    unsigned i;
    if (!dec->x86_queued) return sx_dec_core_byte(dec, out);
    *out = dec->x86_queue[0];
    for (i = 1U; i < dec->x86_queued; ++i) dec->x86_queue[i - 1U] = dec->x86_queue[i];
    --dec->x86_queued;
    return true;
}

static bool sx_dec_byte(sx_dec *dec, uint8_t *out)
{
    static const uint8_t accept[8] = {1, 1, 1, 0, 1, 0, 0, 0};
    static const uint8_t shifts[8] = {24, 16, 8, 8, 0, 0, 0, 0};
    uint8_t byte;
    uint64_t pos;
    unsigned i;
    if (dec->preprocess != SX_PREPROCESS_X86) return sx_dec_core_byte(dec, out);
    if (dec->x86_pos >= dec->x86_size) return false;
    if (dec->x86_pending_left) {
        *out = dec->x86_pending[dec->x86_pending_at++];
        --dec->x86_pending_left;
        ++dec->x86_pos;
        return true;
    }
    if (!sx_x86_pop(dec, &byte)) return false;
    pos = dec->x86_pos++;
    if (byte != 0xE8U && byte != 0xE9U) {
        *out = byte;
        return true;
    }
    {
        uint64_t distance = pos - (uint64_t)dec->x86_last_hit;
        dec->x86_last_hit = (int64_t)pos;
        if (distance > 5U) dec->x86_bitfield = 0U;
        else
            for (i = 0U; i < (unsigned)distance; ++i) dec->x86_bitfield = (uint8_t)((dec->x86_bitfield & 0x77U) << 1U);
    }
    if (dec->x86_size - pos - 1U < 4U) {
        *out = byte;
        return true;
    }
    while (dec->x86_queued < 4U) {
        if (!sx_dec_core_byte(dec, &dec->x86_queue[dec->x86_queued])) return false;
        ++dec->x86_queued;
    }
    if (dec->x86_queue[3] == 0U || dec->x86_queue[3] == 0xFFU) {
        unsigned state = dec->x86_bitfield >> 1U;
        if (state < 8U && accept[state] && state <= 15U) {
            uint32_t absolute =
                (uint32_t)dec->x86_queue[0] | ((uint32_t)dec->x86_queue[1] << 8U) | ((uint32_t)dec->x86_queue[2] << 16U) | ((uint32_t)dec->x86_queue[3] << 24U);
            uint32_t relative;
            unsigned attempt;
            for (attempt = 0U; attempt < 16U; ++attempt) {
                uint32_t marker, mask;
                relative = absolute - (uint32_t)pos - 6U;
                if (!dec->x86_bitfield) break;
                marker = (relative >> shifts[state]) & 0xFFU;
                if (marker != 0U && marker != 0xFFU) break;
                mask = shifts[state] == 24U ? UINT32_MAX : (UINT32_C(1) << (shifts[state] + 8U)) - 1U;
                absolute = relative ^ mask;
            }
            if (attempt == 16U) return false;
            relative &= 0x1FFFFFFU;
            if (relative >= 0x1000000U) relative |= 0xFF000000U;
            for (i = 0U; i < 4U; ++i) dec->x86_pending[i] = (uint8_t)(relative >> (8U * i));
            dec->x86_pending_at = 0U;
            dec->x86_pending_left = 4U;
            dec->x86_queued = 0U;
            dec->x86_bitfield = 0U;
            *out = byte;
            return true;
        }
        dec->x86_bitfield |= 0x11U;
    } else {
        dec->x86_bitfield |= 0x01U;
    }
    *out = byte;
    return true;
}

/* Produce @p count bytes into @p out (NULL discards), updating the CRC. */
static bool sx_dec_read(sx_dec *dec, uint8_t *out, size_t count)
{
    uint8_t scratch[4096];
    size_t done = 0U;
    while (done < count) {
        size_t chunk = count - done, index;
        uint8_t *target;
        if (!out && chunk > sizeof(scratch)) chunk = sizeof(scratch);
        target = out ? out + done : scratch;
        for (index = 0U; index < chunk; ++index) {
            if (!sx_dec_byte(dec, target + index)) {
                dec->failed = true;
                return false;
            }
        }
        dec->crc = xx_crc32_calc(dec->crc, target, chunk);
        dec->produced += chunk;
        done += chunk;
    }
    return true;
}

static sx_dec *sx_dec_create(void)
{
    size_t capacity = xx_get_file_buffer_size();
    sx_dec *dec;
    if (capacity == 0U || capacity > SIZE_MAX - sizeof(*dec)) return NULL;
    dec = (sx_dec *)xx_mem_calloc(1U, sizeof(*dec) + capacity);
    if (!dec) return NULL;
    dec->io = (uint8_t *)(dec + 1);
    dec->src.capacity = capacity;
    return dec;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

/* Mac Roman 0x80..0xFF as Unicode. */
static const uint16_t SX_MAC_ROMAN[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED,
    0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022,
    0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F,
    0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0,
    0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01,
    0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2,
    0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

static size_t sx_put_utf8(char *out, uint32_t code)
{
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

static size_t sx_utf8_sequence(const uint8_t *s, size_t available)
{
    uint32_t code;
    size_t length, index;
    if (available == 0U) return 0U;
    if (s[0] < 0x80U) return 1U;
    if (s[0] >= 0xC2U && s[0] <= 0xDFU) {
        length = 2U;
        code = s[0] & 0x1FU;
    } else if (s[0] >= 0xE0U && s[0] <= 0xEFU) {
        length = 3U;
        code = s[0] & 0x0FU;
    } else if (s[0] >= 0xF0U && s[0] <= 0xF4U) {
        length = 4U;
        code = s[0] & 0x07U;
    } else {
        return 0U;
    }
    if (length > available) return 0U;
    for (index = 1U; index < length; ++index) {
        if ((s[index] & 0xC0U) != 0x80U) return 0U;
        code = (code << 6U) | (s[index] & 0x3FU);
    }
    if ((length == 3U && code < 0x800U) || (length == 4U && code < 0x10000U) || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) return 0U;
    return length;
}

static bool sx_is_utf8(const uint8_t *s, size_t size)
{
    size_t at = 0U;
    while (at < size) {
        size_t length = sx_utf8_sequence(s + at, size - at);
        if (length == 0U) return false;
        at += length;
    }
    return true;
}

static char sx_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool sx_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || sx_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

static bool sx_is_device_name(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t length = xx_str_len(name), stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (sx_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((sx_upper(name[0]) == 'C' && sx_upper(name[1]) == 'O' && sx_upper(name[2]) == 'M') ||
            (sx_upper(name[0]) == 'L' && sx_upper(name[1]) == 'P' && sx_upper(name[2]) == 'T'));
}

/* One path component as UTF-8 safe on any file system: well-formed UTF-8
 * is kept, anything else is read as Mac Roman; separators, control and
 * reserved characters become '_', trailing dots and spaces are dropped and
 * device names get a '_' prefix. */
static char *sx_component(const uint8_t *bytes, size_t size)
{
    char *name;
    size_t in, out = 1U;
    bool utf8;
    if (size > SX_MAX_COMPONENT) size = SX_MAX_COMPONENT;
    utf8 = sx_is_utf8(bytes, size);
    name = (char *)xx_mem_alloc(size * 3U + 3U);
    if (!name) return NULL;
    for (in = 0U; in < size; ++in) {
        uint8_t c = bytes[in];
        if (c >= 0x80U) {
            if (utf8) name[out++] = (char)c;
            else out += sx_put_utf8(name + out, SX_MAC_ROMAN[c - 0x80U]);
        } else if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            name[out++] = '_';
        } else {
            name[out++] = (char)c;
        }
    }
    while (out > 1U && (name[out - 1U] == ' ' || name[out - 1U] == '.')) --out;
    if (out == 1U) name[out++] = '_';
    name[out] = 0;
    if (sx_is_device_name(name + 1U)) {
        name[0] = '_';
        return name;
    }
    xx_rt_memmove(name, name + 1U, out);
    return name;
}

static uint32_t sx_fold_next(const char **cursor)
{
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t code = s[0];
    size_t used = 1U;
    if (code == 0U) return 0U;
    if ((code & 0xE0U) == 0xC0U && s[1] != 0U) {
        code = ((code & 0x1FU) << 6U) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((code & 0xF0U) == 0xE0U && s[1] != 0U && s[2] != 0U) {
        code = ((code & 0x0FU) << 12U) | ((uint32_t)(s[1] & 0x3FU) << 6U) | (s[2] & 0x3FU);
        used = 3U;
    } else if ((code & 0xF8U) == 0xF0U && s[1] != 0U && s[2] != 0U && s[3] != 0U) {
        code = ((code & 0x07U) << 18U) | ((uint32_t)(s[1] & 0x3FU) << 12U) | ((uint32_t)(s[2] & 0x3FU) << 6U) | (s[3] & 0x3FU);
        used = 4U;
    }
    *cursor += used;
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    if (code == 0xFFU) return 0x178U;
    if (code == 0x153U) return 0x152U;
    if (code == '\\') return '/';
    return code;
}

static uint32_t sx_fold_hash(const char *name)
{
    uint32_t hash = UINT32_C(2166136261), code;
    while ((code = sx_fold_next(&name)) != 0U) {
        hash ^= code;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool sx_fold_equal(const char *a, const char *b)
{
    for (;;) {
        uint32_t x = sx_fold_next(&a), y = sx_fold_next(&b);
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

typedef struct sx_names_s {
    const char **slots;
    size_t mask;
    size_t used;
} sx_names;

static bool sx_names_contains(const sx_names *names, const char *name)
{
    size_t slot;
    if (!names->slots) return false;
    slot = (size_t)sx_fold_hash(name) & names->mask;
    while (names->slots[slot]) {
        if (sx_fold_equal(names->slots[slot], name)) return true;
        slot = (slot + 1U) & names->mask;
    }
    return false;
}

static bool sx_names_insert(sx_names *names, const char *name)
{
    size_t slot;
    if (!names->slots || (names->used + 1U) * 2U > names->mask + 1U) {
        size_t size = names->slots ? (names->mask + 1U) * 2U : 64U, index;
        const char **grown;
        if (size > SIZE_MAX / sizeof(*grown)) return false;
        grown = (const char **)xx_mem_calloc(size, sizeof(*grown));
        if (!grown) return false;
        for (index = 0U; names->slots && index <= names->mask; ++index) {
            const char *old = names->slots[index];
            if (old) {
                slot = (size_t)sx_fold_hash(old) & (size - 1U);
                while (grown[slot]) slot = (slot + 1U) & (size - 1U);
                grown[slot] = old;
            }
        }
        if (names->slots) xx_mem_free((void *)names->slots);
        names->slots = grown;
        names->mask = size - 1U;
    }
    slot = (size_t)sx_fold_hash(name) & names->mask;
    while (names->slots[slot]) slot = (slot + 1U) & names->mask;
    names->slots[slot] = name;
    names->used++;
    return true;
}

static bool sx_safe_output_name(const char *name)
{
    const char *segment, *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' || name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == 0x7FU || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') || (length == 2U && segment[0] == '.' && segment[1] == '.')) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Archive model                                                           */

typedef struct sx_map_s {
    uint64_t *keys;
    uint32_t *values; /* SX_NONE = empty slot */
    size_t mask;
    size_t used;
} sx_map;

static size_t sx_map_slot(uint64_t key, size_t mask)
{
    key ^= key >> 33U;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33U;
    return (size_t)key & mask;
}

static bool sx_map_get(const sx_map *map, uint64_t key, uint32_t *value)
{
    size_t slot;
    if (!map->values) return false;
    slot = sx_map_slot(key, map->mask);
    while (map->values[slot] != SX_NONE) {
        if (map->keys[slot] == key) {
            *value = map->values[slot];
            return true;
        }
        slot = (slot + 1U) & map->mask;
    }
    return false;
}

static bool sx_map_put(sx_map *map, uint64_t key, uint32_t value)
{
    size_t slot;
    if (!map->values || (map->used + 1U) * 2U > map->mask + 1U) {
        size_t size = map->values ? (map->mask + 1U) * 2U : 64U, index;
        uint64_t *keys;
        uint32_t *values;
        if (size > SIZE_MAX / sizeof(uint64_t)) return false;
        keys = (uint64_t *)xx_mem_alloc(size * sizeof(uint64_t));
        values = (uint32_t *)xx_mem_alloc(size * sizeof(uint32_t));
        if (!keys || !values) {
            if (keys) xx_mem_free(keys);
            if (values) xx_mem_free(values);
            return false;
        }
        for (index = 0U; index < size; ++index) values[index] = SX_NONE;
        for (index = 0U; map->values && index <= map->mask; ++index) {
            if (map->values[index] != SX_NONE) {
                slot = sx_map_slot(map->keys[index], size - 1U);
                while (values[slot] != SX_NONE) slot = (slot + 1U) & (size - 1U);
                keys[slot] = map->keys[index];
                values[slot] = map->values[index];
            }
        }
        if (map->keys) xx_mem_free(map->keys);
        if (map->values) xx_mem_free(map->values);
        map->keys = keys;
        map->values = values;
        map->mask = size - 1U;
    }
    slot = sx_map_slot(key, map->mask);
    while (map->values[slot] != SX_NONE) {
        if (map->keys[slot] == key) {
            map->values[slot] = value;
            return true;
        }
        slot = (slot + 1U) & map->mask;
    }
    map->keys[slot] = key;
    map->values[slot] = value;
    map->used++;
    return true;
}

static void sx_map_free(sx_map *map)
{
    if (map->keys) xx_mem_free(map->keys);
    if (map->values) xx_mem_free(map->values);
    map->keys = NULL;
    map->values = NULL;
}

typedef struct sx_entry_s {
    uint64_t id;
    uint64_t parent;
    char *path;     /* from the catalog, NULL until named */
    uint64_t mtime; /* FILETIME, 0 when absent */
    bool has_parent;
    bool folder;
    bool forked;
    bool published;
} sx_entry;

typedef struct sx_fork_s {
    uint64_t entry;
    uint64_t stream;
    uint64_t index;
    uint64_t length;
    uint64_t type;
    uint32_t next; /* next fork of the same stream */
    uint32_t sequence;
} sx_fork;

typedef struct sx_member_s {
    char *name;
    uint32_t data; /* SX_NONE: folder or empty file */
    uint64_t offset;
    uint64_t length;
    uint64_t mtime;
    bool folder;
    bool resource;
} sx_member;

typedef struct sx_stream_s {
    sx_entry *entries;
    size_t entry_count, entry_capacity;
    sx_fork *forks;
    size_t fork_count, fork_capacity;
    sx_data *datas;
    size_t data_count, data_capacity;
    sx_member *items;
    size_t count, capacity;
    size_t index;
    sx_map entry_map;  /* id -> latest entry */
    sx_map stream_map; /* stream id -> first fork of its chain */
    sx_names names;
    sx_map suffixes; /* name hash -> last " (N)" handed out */
    size_t name_bytes;
    int64_t archive_size;
    size_t catalogs;
    bool first_data_seen;
    bool ended;
    bool truncated;
    bool names_missing;
    sx_dec *dec;
    xx_io_device *device;
    int64_t device_end;
} sx_stream;

static void sx_stream_free(void *opaque)
{
    sx_stream *stream = (sx_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->entry_count; ++index)
        if (stream->entries[index].path) xx_str_free(stream->entries[index].path);
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->entries) xx_mem_free(stream->entries);
    if (stream->forks) xx_mem_free(stream->forks);
    if (stream->datas) xx_mem_free(stream->datas);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->names.slots) xx_mem_free((void *)stream->names.slots);
    sx_map_free(&stream->entry_map);
    sx_map_free(&stream->stream_map);
    sx_map_free(&stream->suffixes);
    if (stream->dec) xx_mem_free(stream->dec);
    xx_mem_free(stream);
}

static bool sx_grow(void **array, size_t *capacity, size_t count, size_t element, size_t limit)
{
    size_t size;
    void *grown;
    if (count < *capacity) return true;
    if (count >= limit) return false;
    size = *capacity ? *capacity * 2U : 16U;
    if (size > limit) size = limit;
    if (size > SIZE_MAX / element) return false;
    grown = xx_mem_realloc(*array, size * element);
    if (!grown) return false;
    *array = grown;
    *capacity = size;
    return true;
}

/* Take ownership of @p candidate and return a path no earlier record uses,
 * appending " (2)", " (3)", ... when needed.  The last suffix handed out
 * per name is remembered, so many equal names stay linear. */
static char *sx_unique_name(sx_stream *stream, char *candidate)
{
    uint32_t suffix, start = 2U;
    uint64_t key;
    if (!candidate) return NULL;
    if (!sx_names_contains(&stream->names, candidate)) return candidate;
    key = sx_fold_hash(candidate);
    if (sx_map_get(&stream->suffixes, key, &suffix) && suffix >= 2U) start = suffix + 1U;
    for (suffix = start; suffix < SX_MAX_SUFFIX; ++suffix) {
        char number[24];
        char *next;
        (void)xx_rt_snprintf(number, sizeof(number), " (%u)", (unsigned)suffix);
        next = xx_str_concat(candidate, number);
        if (!next) break;
        if (!sx_names_contains(&stream->names, next)) {
            xx_str_free(candidate);
            if (!sx_map_put(&stream->suffixes, key, suffix)) {
                xx_str_free(next);
                return NULL;
            }
            return next;
        }
        xx_str_free(next);
    }
    xx_str_free(candidate);
    return NULL;
}

/* The path an entry is published under: its catalog path, or "entry_<id>"
 * when no catalog named it. */
static char *sx_entry_path(sx_stream *stream, const sx_entry *entry, bool resource)
{
    char *base;
    if (entry->path) {
        base = xx_str_dup(entry->path);
    } else {
        char synthetic[40];
        (void)xx_rt_snprintf(synthetic, sizeof(synthetic), "entry_%llu", (unsigned long long)entry->id);
        base = xx_str_dup(synthetic);
        stream->names_missing = true;
    }
    if (base && resource) {
        char *joined = xx_str_concat(base, ".rsrc");
        xx_str_free(base);
        base = joined;
    }
    return base;
}

static bool sx_publish(sx_stream *stream, const sx_entry *entry, bool resource, uint32_t data, uint64_t offset, uint64_t length)
{
    sx_member member;
    char *name;
    if (!sx_grow((void **)&stream->items, &stream->capacity, stream->count, sizeof(sx_member), SX_MAX_RECORDS)) return false;
    name = sx_unique_name(stream, sx_entry_path(stream, entry, resource));
    if (!name) return false;
    if (xx_str_len(name) > SX_MAX_NAME_BYTES - stream->name_bytes || !sx_names_insert(&stream->names, name)) {
        xx_str_free(name);
        return false;
    }
    stream->name_bytes += xx_str_len(name);
    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.data = data;
    member.offset = offset;
    member.length = length;
    member.mtime = entry->mtime;
    member.folder = entry->folder;
    member.resource = resource;
    stream->items[stream->count++] = member;
    return true;
}

static bool sx_publish_unforked(sx_stream *stream)
{
    size_t index;
    for (index = 0U; index < stream->entry_count; ++index) {
        sx_entry *entry = &stream->entries[index];
        if (entry->published || entry->forked) continue;
        entry->published = true;
        if (!sx_publish(stream, entry, false, SX_NONE, 0U, 0U)) return false;
    }
    return true;
}

static sx_entry *sx_find_entry(sx_stream *stream, uint64_t id)
{
    uint32_t index;
    if (!sx_map_get(&stream->entry_map, id, &index) || index >= stream->entry_count) return NULL;
    return &stream->entries[index];
}

/* ---------------------------------------------------------------------- */
/* Catalog                                                                 */

static bool sx_catalog_string(sx_src *s, uint8_t *buffer, size_t *size)
{
    uint64_t length;
    if (!sx_p2(s, &length)) return false;
    if (length > SX_MAX_STRING) {
        s->bad = true;
        return false;
    }
    if (buffer) {
        if (!sx_bytes(s, buffer, (size_t)length)) return false;
    } else if (!sx_skip(s, length)) {
        return false;
    }
    sx_flush(s);
    if (size) *size = (size_t)length;
    return true;
}

static bool sx_catalog_entry(sx_stream *stream, sx_src *s, sx_entry *entry, uint8_t *text)
{
    unsigned keys;
    for (keys = 0U;; ++keys) {
        uint64_t key, value;
        if (keys > SX_MAX_KEYS) return false;
        if (!sx_p2(s, &key)) return false;
        if (key == 0U) break;
        switch (key) {
            case 1: {
                size_t size;
                char *component, *path;
                sx_entry *parent = NULL;
                if (!sx_catalog_string(s, text, &size)) return false;
                component = sx_component(text, size);
                if (!component) return false;
                if (entry->has_parent) parent = sx_find_entry(stream, entry->parent);
                if (parent && parent != entry && parent->path) {
                    path = xx_str_concat3(parent->path, "/", component);
                    xx_str_free(component);
                } else {
                    path = component;
                }
                if (!path) return false;
                size = xx_str_len(path);
                if (size > SX_MAX_PATH || size > SX_MAX_NAME_BYTES - stream->name_bytes) {
                    xx_str_free(path);
                    return false;
                }
                stream->name_bytes += size;
                if (entry->path) xx_str_free(entry->path);
                entry->path = path;
                break;
            }
            case 2:
                if (!sx_bits_be(s, 8U, &entry->mtime)) return false;
                break;
            case 8:
                if (!sx_bits_be(s, 8U, &value)) return false;
                break;
            case 3:
                if (!sx_bits_be(s, 4U, &value)) return false;
                break;
            case 4:
            case 5: {
                unsigned index;
                for (index = 0U; index < 4U; ++index)
                    if (!sx_bits_be(s, 8U, &value)) return false;
                break;
            }
            case 6: /* owner flag, permissions, then user and group */
                if (!sx_bits(s, 8U, &value) || !sx_bits_be(s, 4U, &key)) return false;
                if (value && (!sx_bits_be(s, 4U, &key) || !sx_bits_be(s, 4U, &key))) return false;
                break;
            case 7:
                if (!sx_p2(s, &value)) return false;
                break;
            case 9:
            case 11:
            case 12:
                if (!sx_catalog_string(s, NULL, NULL)) return false;
                break;
            case 10: {
                uint64_t index;
                if (!sx_p2(s, &value) || value > SX_MAX_TAG10) return false;
                for (index = 0U; index < value; ++index)
                    if (!sx_catalog_string(s, NULL, NULL)) return false;
                break;
            }
            default: return false;
        }
    }
    sx_flush(s);
    return true;
}

static void sx_catalog(sx_stream *stream, const uint8_t *data, size_t size)
{
    sx_src s;
    uint8_t *text = (uint8_t *)xx_mem_alloc(SX_MAX_STRING);
    size_t index;
    if (!text) return;
    sx_src_memory(&s, data, size);
    for (index = 0U; index < stream->entry_count; ++index)
        if (!sx_catalog_entry(stream, &s, &stream->entries[index], text)) break;
    xx_mem_free(text);
}

/* ---------------------------------------------------------------------- */
/* Parser                                                                  */

static int sx_fork_compare(const void *left, const void *right)
{
    const sx_fork *a = *(const sx_fork *const *)left;
    const sx_fork *b = *(const sx_fork *const *)right;
    if (a->index != b->index) return a->index < b->index ? -1 : 1;
    return a->sequence < b->sequence ? -1 : (a->sequence > b->sequence ? 1 : 0);
}

typedef enum sx_outcome_e {
    SX_ENDED,
    SX_EOF,
    SX_BAD
} sx_outcome;

static bool sx_data_element(sx_stream *stream, sx_src *s, const sx_element *e, bool *bad)
{
    sx_data data;
    sx_fork **chain = NULL;
    size_t chain_count = 0U, index;
    uint32_t head, data_index;
    uint64_t actual = 0U, offset = 0U;
    bool result = false;
    xx_mem_zero(&data, sizeof(data));
    if (!sx_scan(s, &data.data_offset, &data.has_crc, &data.crc)) return false;
    data.packed_size = s->pos - data.data_offset;
    data.method = sx_alg(e, 1U);
    data.checksum = sx_alg(e, 2U);
    data.preprocess = sx_alg(e, 3U);
    data.cipher = sx_alg(e, 4U);

    /* Forks of this stream, in fork-index order (ties: declaration order). */
    if (sx_has_attr(e, 1U) && sx_map_get(&stream->stream_map, e->attr[0], &head)) {
        uint32_t at;
        for (at = head; at != SX_NONE; at = stream->forks[at].next) ++chain_count;
        chain = (sx_fork **)xx_mem_alloc(chain_count * sizeof(*chain));
        if (!chain) return false;
        chain_count = 0U;
        for (at = head; at != SX_NONE; at = stream->forks[at].next) chain[chain_count++] = &stream->forks[at];
        xx_rt_qsort(chain, chain_count, sizeof(*chain), sx_fork_compare);
    }
    /* Fork indexes must run 0, 1, 2, ... with no gap; several entries may
     * share one index (the same bytes), but only with the same length. */
    for (index = 0U; index < chain_count; ++index) {
        const sx_fork *fork = chain[index];
        const sx_fork *previous = index ? chain[index - 1U] : NULL;
        if (previous && previous->index == fork->index) {
            if (previous->length != fork->length) {
                *bad = true;
                goto done;
            }
            continue;
        }
        if (fork->index != (previous ? previous->index + 1U : 0U) || fork->length > UINT64_MAX - actual) {
            *bad = true;
            goto done;
        }
        actual += fork->length;
    }
    data.actual_size = actual;

    if (!stream->first_data_seen) {
        stream->first_data_seen = true;
        for (index = 0U; index < stream->fork_count; ++index) {
            sx_entry *entry = sx_find_entry(stream, stream->forks[index].entry);
            if (entry) entry->forked = true;
        }
        if (!sx_publish_unforked(stream)) goto done;
    }

    if (!sx_grow((void **)&stream->datas, &stream->data_capacity, stream->data_count, sizeof(sx_data), SX_MAX_DATA)) goto done;
    data_index = (uint32_t)stream->data_count;
    stream->datas[stream->data_count++] = data;

    for (index = 0U; index < chain_count; ++index) {
        const sx_fork *fork = chain[index];
        sx_entry *entry;
        if (index && chain[index - 1U]->index != fork->index) offset += chain[index - 1U]->length;
        if (fork->type > 1U) continue;
        entry = sx_find_entry(stream, fork->entry);
        if (!entry || entry->folder) continue;
        entry->published = true;
        if (!sx_publish(stream, entry, fork->type == 1U, data_index, offset, fork->length)) goto done;
    }
    result = true;
done:
    if (chain) xx_mem_free(chain);
    return result;
}

static bool sx_catalog_element(sx_stream *stream, sx_src *s, const sx_element *e, bool *bad)
{
    sx_data data;
    uint8_t *buffer = NULL;
    size_t size;
    xx_mem_zero(&data, sizeof(data));
    if (!sx_has_attr(e, 5U)) {
        *bad = true;
        return false;
    }
    if (!sx_scan(s, &data.data_offset, &data.has_crc, &data.crc)) return false;
    stream->catalogs++;
    data.method = sx_alg(e, 1U);
    data.checksum = -1;
    data.preprocess = sx_alg(e, 3U);
    data.cipher = sx_alg(e, 4U);
    data.actual_size = e->attr[4];
    if (data.actual_size > SX_MAX_CATALOG || !sx_method_supported(&data)) return true; /* listed with synthetic names */
    size = (size_t)data.actual_size;
    if (!stream->dec && !(stream->dec = sx_dec_create())) return false;
    if (sx_dec_start(stream->dec, stream->device, stream->device_end, &data, SX_NONE)) {
        /* The buffer grows with what actually decodes, so a declared size
         * cannot force a large allocation; a catalog stream that stops
         * early still names what it covers. */
        size_t got = 0U, capacity = 0U;
        uint8_t byte;
        while (got < size && sx_dec_byte(stream->dec, &byte)) {
            if (got == capacity) {
                size_t grown = capacity ? capacity * 2U : 4096U;
                uint8_t *next;
                if (grown > size) grown = size;
                next = (uint8_t *)xx_mem_realloc(buffer, grown);
                if (!next) break;
                buffer = next;
                capacity = grown;
            }
            buffer[got++] = byte;
        }
        if (buffer) sx_catalog(stream, buffer, got);
    }
    stream->dec->active = false;
    if (buffer) xx_mem_free(buffer);
    return true;
}

static sx_outcome sx_walk(sx_stream *stream, sx_src *s, xx_pd_struct *pd)
{
    unsigned long elements = 0UL;
    for (;;) {
        sx_element e;
        bool bad = false;
        if ((++elements & 0x3FFUL) == 0UL && pd && xx_pd_is_stopped(pd)) return SX_BAD;
        sx_flush(s);
        if (!sx_read_element(s, &e)) return s->bad ? SX_BAD : SX_EOF;
        switch (e.type) {
            case 0: return SX_ENDED;
            case 1:
                if (!sx_data_element(stream, s, &e, &bad)) return (bad || s->bad || !s->eof) ? SX_BAD : SX_EOF;
                break;
            case 2:
            case 4: {
                sx_entry entry;
                if (!sx_has_attr(&e, 1U)) return SX_BAD;
                if (!sx_grow((void **)&stream->entries, &stream->entry_capacity, stream->entry_count, sizeof(sx_entry), SX_MAX_ENTRIES)) return SX_BAD;
                xx_mem_zero(&entry, sizeof(entry));
                entry.id = e.attr[0];
                entry.has_parent = sx_has_attr(&e, 2U);
                entry.parent = e.attr[1];
                entry.folder = e.type == 4U;
                if (!sx_map_put(&stream->entry_map, entry.id, (uint32_t)stream->entry_count)) return SX_BAD;
                stream->entries[stream->entry_count++] = entry;
                break;
            }
            case 3: {
                sx_fork fork;
                uint32_t head;
                uint64_t type;
                if (!sx_p2(s, &type)) return s->bad ? SX_BAD : SX_EOF;
                if (!sx_has_attr(&e, 2U) || !sx_has_attr(&e, 3U) || !sx_has_attr(&e, 4U) || !sx_has_attr(&e, 5U) || e.attr[3] >= SX_MAX_FORKS) return SX_BAD;
                if (!sx_grow((void **)&stream->forks, &stream->fork_capacity, stream->fork_count, sizeof(sx_fork), SX_MAX_FORKS)) return SX_BAD;
                fork.entry = e.attr[1];
                fork.stream = e.attr[2];
                fork.index = e.attr[3];
                fork.length = e.attr[4];
                fork.type = type;
                fork.sequence = (uint32_t)stream->fork_count;
                fork.next = sx_map_get(&stream->stream_map, fork.stream, &head) ? head : SX_NONE;
                if (!sx_map_put(&stream->stream_map, fork.stream, (uint32_t)stream->fork_count)) return SX_BAD;
                stream->forks[stream->fork_count++] = fork;
                break;
            }
            case 5:
                if (!sx_catalog_element(stream, s, &e, &bad)) return (bad || s->bad || !s->eof) ? SX_BAD : SX_EOF;
                break;
            case 6:
                if (!sx_has_attr(&e, 5U)) return SX_BAD;
                if (!sx_skip(s, e.attr[4])) return SX_EOF;
                break;
            case 7: {
                uint64_t value;
                if (!sx_p2(s, &value)) return s->bad ? SX_BAD : SX_EOF;
                break;
            }
            case 8:
            case 9: break;
            case 10: return SX_BAD;
            default: {
                int64_t offset;
                bool has_crc;
                uint32_t crc;
                if (!sx_scan(s, &offset, &has_crc, &crc)) return s->bad ? SX_BAD : SX_EOF;
                break;
            }
        }
    }
}

/* Parse the whole element stream.  An archive is accepted when it reaches
 * its end element, or runs out of data after at least one catalog; either
 * way it must declare at least one entry. */
static bool sx_parse(Abstractformat *format, xx_pd_struct *pd, sx_stream **result)
{
    uint8_t magic[SX_MAGIC_SIZE];
    sx_stream *stream = NULL;
    sx_src src;
    uint8_t *buffer = NULL;
    size_t capacity = xx_get_file_buffer_size();
    int64_t total, base;
    sx_outcome outcome;
    if (!format || !format->device || !result || format->base_address < 0 || capacity == 0U) return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < 0 || base > total || total - base < (int64_t)SX_MAGIC_SIZE + 1 || !sx_read_at(format->device, base, magic, sizeof(magic)) ||
        xx_rt_memcmp(magic, "StuffIt!", SX_MAGIC_SIZE) != 0)
        return false;
    stream = (sx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!stream || !buffer) goto fail;
    stream->device = format->device;
    stream->device_end = total;
    sx_src_device(&src, format->device, base + (int64_t)SX_MAGIC_SIZE, total, buffer, capacity);
    outcome = sx_walk(stream, &src, pd);
    if (outcome == SX_BAD || (outcome == SX_EOF && stream->catalogs == 0U) || stream->entry_count == 0U) goto fail;
    if (!sx_publish_unforked(stream)) goto fail;
    stream->ended = outcome == SX_ENDED;
    stream->truncated = outcome == SX_EOF;
    stream->archive_size = stream->ended ? src.pos - base : total - base;
    xx_mem_free(buffer);
    *result = stream;
    return true;
fail:
    if (buffer) xx_mem_free(buffer);
    sx_stream_free(stream);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool sx_write(xx_io_device *device, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t wrote = xx_io_write(device, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

static bool sx_extract(sx_stream *stream, size_t item, xx_io_device *destination, xx_pd_struct *pd)
{
    const sx_member *member = &stream->items[item];
    const sx_data *data;
    sx_dec *dec;
    uint8_t *chunk;
    uint64_t left;
    bool last_of_stream;
    if (member->folder) return true;
    if (member->data == SX_NONE) return true;
    if (member->data >= stream->data_count) return false;
    data = &stream->datas[member->data];
    if (!sx_method_supported(data) || member->offset > data->actual_size || member->length > data->actual_size - member->offset) return false;
    if (!stream->dec && !(stream->dec = sx_dec_create())) return false;
    dec = stream->dec;
    if (!dec->active || dec->failed || dec->data_index != member->data || dec->produced > member->offset) {
        if (!sx_dec_start(dec, stream->device, stream->device_end, data, member->data)) return false;
    }
    while (dec->produced < member->offset) {
        uint64_t skip = member->offset - dec->produced;
        if (skip > 0x10000U) skip = 0x10000U;
        if (!sx_dec_read(dec, NULL, (size_t)skip)) return false;
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    chunk = (uint8_t *)xx_mem_alloc(0x10000U);
    if (!chunk) return false;
    for (left = member->length; left;) {
        size_t amount = left > 0x10000U ? 0x10000U : (size_t)left;
        if (!sx_dec_read(dec, chunk, amount) || (destination && !sx_write(destination, chunk, amount)) || (pd && xx_pd_is_stopped(pd))) {
            xx_mem_free(chunk);
            return false;
        }
        left -= amount;
    }
    xx_mem_free(chunk);
    /* The CRC covers the whole unpacked stream: the stream's last record
     * drains what is left and checks it. */
    last_of_stream = item + 1U >= stream->count || stream->items[item + 1U].data != member->data;
    if (last_of_stream) {
        while (dec->produced < data->actual_size) {
            uint64_t skip = data->actual_size - dec->produced;
            if (skip > 0x10000U) skip = 0x10000U;
            if (!sx_dec_read(dec, NULL, (size_t)skip)) return false;
            if (pd && xx_pd_is_stopped(pd)) return false;
        }
    }
    if (dec->produced == data->actual_size && data->has_crc && data->checksum == 0 && dec->crc != data->crc) {
        dec->failed = true;
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* API                                                                     */

static bool sx_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *sx_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool sx_set_record(xx_archive_record *record, const sx_stream *stream, const sx_member *member)
{
    const sx_data *data = member->data != SX_NONE && member->data < stream->data_count ? &stream->datas[member->data] : NULL;
    uint64_t packed = 0U;
    int64_t method = -1;
    bool encrypted = false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (data) {
        packed = (uint64_t)data->packed_size;
        if (data->actual_size && member->length <= data->actual_size && (packed == 0U || member->length <= UINT64_MAX / packed))
            packed = member->length * packed / data->actual_size;
        method = data->method;
        encrypted = data->cipher >= 0;
        record->data_offset = data->data_offset;
    }
    record->header_offset = -1;
    record->compressed_size = (int64_t)packed;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, (uint64_t)(method + 1)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->mtime) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->folder);
}

void xx_stuffitx_init(xx_stuffitx *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_STUFFITX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stuffitx");
    xx_format_set_extension(&archive->format, "sitx");
    archive->format.check_is_valid = xx_stuffitx_check_is_valid;
    archive->format.handle_base_info = xx_stuffitx_handle_base_info;
    archive->format.get_format_size = xx_stuffitx_get_format_size;
    archive->format.get_number_of_archive_records = xx_stuffitx_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_stuffitx_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_stuffitx_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_stuffitx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_stuffitx_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_stuffitx_free_archive_records_reading;
    archive->archive_size = -1;
}

xx_stuffitx *xx_stuffitx_create(xx_io_device *device, int64_t base_address)
{
    xx_stuffitx *archive = (xx_stuffitx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_stuffitx_init(archive, device, base_address);
    return archive;
}

void xx_stuffitx_destroy(xx_stuffitx *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_stuffitx_free(xx_stuffitx *archive)
{
    if (!archive) return;
    xx_stuffitx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_stuffitx_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    sx_stream *stream;
    if (!sx_parse(format, pd, &stream)) return false;
    sx_stream_free(stream);
    return true;
}

bool xx_stuffitx_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    sx_stream *stream;
    xx_stuffitx *archive;
    if (!format || !sx_parse(format, pd, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_stuffitx *)format;
    archive->number_of_records = stream->count;
    archive->archive_size = stream->archive_size;
    archive->truncated = stream->truncated;
    archive->names_missing = stream->names_missing;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_STUFFITX_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    sx_stream_free(stream);
    return true;
}

int64_t xx_stuffitx_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_stuffitx_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_stuffitx_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_stuffitx_handle_base_info(format, pd)) ? ((xx_stuffitx *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_stuffitx_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    sx_stream *stream;
    xx_archive_record_state *state;
    if (!sx_parse(format, pd, &stream)) return NULL;
    if (stream->count == 0U) {
        sx_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sx_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!sx_copy_options(&state->options, options) || !sx_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_stuffitx_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_stuffitx_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (sx_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = sx_set_record(&state->current_record, stream, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_stuffitx_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    sx_stream *stream;
    const sx_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record || !(stream = (sx_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!sx_safe_output_name(member->name)) return false;
    path_option = sx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return sx_extract(stream, stream->index, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = sx_extract(stream, stream->index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && !member->folder && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_stuffitx_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
