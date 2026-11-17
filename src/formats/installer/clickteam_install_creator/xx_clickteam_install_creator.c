/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Clickteam Install Creator setups (1.x and 2.x).  xx_clickteam_install_creator.h
 * describes the container.  The executable is parsed only as far as its
 * section table, to find where the PE overlay starts; nothing in it is run.
 *
 * Sources of the layout knowledge:
 *  - XArchive installers/xclickteam.cpp (MIT, hors): the 1.x chunk chain,
 *    the 1.x directory entry, and the description of Clickteam-Deflate (its
 *    block header, stored-block length and code-length order).  The decoder
 *    below is a streaming re-implementation of that description, not a copy.
 *  - The other 1.x directory layouts, the component lists, the member
 *    checksum, the delta ("patch") members and their XOR mask were measured
 *    on corpus installers (Install Creator and Patch Maker setups) and are
 *    verified by the checksum every member carries.
 *  - The 2.x file-list node layouts per builder version (20/24/30/35/40) were
 *    taken from the behaviour of cicdec (Bioruebe) and confirmed on the v40
 *    corpus samples; no code was taken from it.
 * Everything else (the 0x7F7F data chunk, the {method, stream} member
 * records, the zlib trailer) was measured on the corpus.
 */

#include "xxfclib/codepages/xx_codepage.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installer/clickteam_install_creator/xx_clickteam_install_creator.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared, so the alias macro that
 * sits next to the enumerator is tested instead. */
#ifdef CLICKTEAM_INSTALL_CREATOR
#define XX_CLICKTEAM_INSTALL_CREATOR_FILE_TYPE XX_FILE_TYPE_CLICKTEAM_INSTALL_CREATOR
#else
#define XX_CLICKTEAM_INSTALL_CREATOR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CIC_TAG_SIZE 6U
#define CIC_CHUNK_HEADER 8U
#define CIC_DATA_ID 0x7F7FU
#define CIC_LIST_ID_2 0x143AU
#define CIC_MAX_CHUNKS 256U
#define CIC_MAX_META (8U << 20)        /* one decoded metadata chunk */
#define CIC_MAX_META_TOTAL (32U << 20) /* read + decoded, one list search */
#define CIC_MAX_FILES 65536U
#define CIC_MAX_NAME 1024U
#define CIC_MAX_COMPONENTS 64U
#define CIC_MAX_SECTIONS 96U
#define CIC_WINDOW 32768U
#define CIC_WINDOW_MASK (CIC_WINDOW - 1U)
#define CIC_LIST1_MAX_ENTRY 0x4000U
#define CIC_MAX_PATCH_BASE (512U << 20) /* a delta's base file, held in memory */

static const uint8_t cic_tag[CIC_TAG_SIZE] = {0x77, 0x77, 0x67, 0x54, 0x29, 0x48};

static bool cic_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    uint8_t *out = (uint8_t *)buffer;
    size_t got = 0U;
    if (!device || offset < 0 || (!buffer && size) || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (got < size) {
        ssize_t r = xx_io_read(device, out + got, size - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static bool cic_stopped(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

/* ---------------------------------------------------------------------- */
/* Output sink: a write-only device that caps the output at the size the   */
/* container declares, keeps an Adler-32, and forwards to a device, to a   */
/* memory buffer, or nowhere (verification only).                          */

typedef struct cic_sink {
    xx_io_device device;
    xx_io_device *target;
    uint8_t *memory;
    uint64_t limit;
    uint64_t written;
    uint32_t adler_a;
    uint32_t adler_b;
    uint32_t sum;      /* 1.x member checksum: rotate-add over LE dwords */
    uint32_t sum_word; /* bytes of the dword being collected */
    uint32_t sum_fill;
} cic_sink;

/* The 1.x member checksum: for every little-endian dword h = rol(h, 1) + d;
 * the 1..3 bytes left at the end are added one by one the same way. */
static void cic_sum_update(cic_sink *sink, const uint8_t *data, size_t n)
{
    uint32_t h = sink->sum;
    while (n > 0U && sink->sum_fill != 0U) {
        sink->sum_word |= (uint32_t)*data++ << (8U * sink->sum_fill);
        --n;
        if (++sink->sum_fill == 4U) {
            h = ((h << 1) | (h >> 31)) + sink->sum_word;
            sink->sum_word = 0U;
            sink->sum_fill = 0U;
        }
    }
    while (n >= 4U) {
        h = ((h << 1) | (h >> 31)) + ((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
        data += 4;
        n -= 4U;
    }
    while (n > 0U) {
        sink->sum_word |= (uint32_t)*data++ << (8U * sink->sum_fill);
        ++sink->sum_fill;
        --n;
    }
    sink->sum = h;
}

static uint32_t cic_sink_sum(const cic_sink *sink)
{
    uint32_t h = sink->sum, i;
    for (i = 0U; i < sink->sum_fill; ++i) h = ((h << 1) | (h >> 31)) + ((sink->sum_word >> (8U * i)) & 0xFFU);
    return h;
}

/* The same sum continued over a byte string (no dword grouping state). */
static uint32_t cic_sum_bytes(uint32_t h, const uint8_t *data, size_t n)
{
    cic_sink sink;
    xx_mem_zero(&sink, sizeof(sink));
    sink.sum = h;
    cic_sum_update(&sink, data, n);
    return cic_sink_sum(&sink);
}

static void cic_adler_update(cic_sink *sink, const uint8_t *data, size_t n)
{
    uint32_t a = sink->adler_a, b = sink->adler_b;
    while (n > 0U) {
        size_t k = n < 5552U ? n : 5552U;
        n -= k;
        while (k--) {
            a += *data++;
            b += a;
        }
        a %= 65521U;
        b %= 65521U;
    }
    sink->adler_a = a;
    sink->adler_b = b;
}

static ssize_t cic_sink_write(xx_io_device *self, const void *buffer, size_t n)
{
    cic_sink *sink = self ? (cic_sink *)self->priv : NULL;
    if (!sink || (!buffer && n)) return -1;
    if ((uint64_t)n > sink->limit - sink->written) return -1;
    if (n == 0U) return 0;
    cic_adler_update(sink, (const uint8_t *)buffer, n);
    cic_sum_update(sink, (const uint8_t *)buffer, n);
    if (sink->memory) {
        xx_rt_memcpy(sink->memory + sink->written, buffer, n);
    } else if (sink->target) {
        if (xx_io_write(sink->target, buffer, n) != (ssize_t)n) return -1;
    }
    sink->written += (uint64_t)n;
    return (ssize_t)n;
}

static void cic_sink_init(cic_sink *sink, xx_io_device *target, uint8_t *memory, uint64_t limit)
{
    xx_mem_zero(sink, sizeof(*sink));
    sink->device.write = cic_sink_write;
    sink->device.priv = sink;
    sink->target = target;
    sink->memory = memory;
    sink->limit = limit;
    sink->adler_a = 1U;
}

static uint32_t cic_sink_adler(const cic_sink *sink)
{
    return (sink->adler_b << 16) | sink->adler_a;
}

/* ---------------------------------------------------------------------- */
/* Clickteam-Deflate                                                       */

typedef struct cic_huff {
    uint16_t count[16];
    uint16_t symbol[288];
} cic_huff;

typedef struct cic_inflate {
    xx_io_device *device; /* NULL when the input is in memory */
    int64_t position;     /* next device offset to fetch */
    uint64_t left;        /* input bytes not fetched yet */
    const uint8_t *buffer;
    uint8_t *owned_buffer;
    size_t buffer_size;
    size_t buffer_pos;
    size_t io_capacity;
    uint64_t consumed;
    uint32_t bits;
    uint32_t bit_count;
    bool error;
    cic_sink *sink;
    uint64_t total;
    uint64_t limit;
    uint32_t tick;
    xx_pd_struct *pd;
    cic_huff lit;
    cic_huff dist;
    cic_huff code;
    uint8_t lengths[320];
    uint8_t window[CIC_WINDOW];
} cic_inflate;

static const uint16_t cic_len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t cic_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t cic_dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t cic_dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
/* The code-length alphabet order differs from RFC 1951. */
static const uint8_t cic_code_order[19] = {18, 17, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static int cic_next_byte(cic_inflate *z)
{
    if (z->buffer_pos >= z->buffer_size) {
        size_t want;
        if (!z->device || z->left == 0U || !z->owned_buffer) return -1;
        want = z->left < z->io_capacity ? (size_t)z->left : z->io_capacity;
        if (!cic_read_at(z->device, z->position, z->owned_buffer, want)) return -1;
        z->position += (int64_t)want;
        z->left -= (uint64_t)want;
        z->buffer_size = want;
        z->buffer_pos = 0U;
    }
    ++z->consumed;
    return z->buffer[z->buffer_pos++];
}

/* n <= 16 */
static uint32_t cic_bits(cic_inflate *z, uint32_t n)
{
    uint32_t value;
    while (z->bit_count < n) {
        int c = cic_next_byte(z);
        if (c < 0) {
            z->error = true;
            return 0U;
        }
        z->bits |= (uint32_t)c << z->bit_count;
        z->bit_count += 8U;
    }
    value = z->bits & ((1U << n) - 1U);
    z->bits >>= n;
    z->bit_count -= n;
    return value;
}

/* Canonical code from lengths.  Negative: over-subscribed (rejected).
 * An incomplete code is accepted; a hole in it fails when decoded. */
static int cic_build(cic_huff *h, const uint8_t *lengths, uint32_t n)
{
    uint16_t offs[16];
    int left = 1;
    uint32_t len, sym;
    for (len = 0U; len < 16U; ++len) h->count[len] = 0U;
    for (sym = 0U; sym < n; ++sym) {
        if (lengths[sym] > 15U) return -1;
        h->count[lengths[sym]]++;
    }
    for (len = 1U; len < 16U; ++len) {
        left <<= 1;
        left -= (int)h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0U;
    for (len = 1U; len < 15U; ++len) offs[len + 1U] = (uint16_t)(offs[len] + h->count[len]);
    for (sym = 0U; sym < n; ++sym)
        if (lengths[sym]) h->symbol[offs[lengths[sym]]++] = (uint16_t)sym;
    return left;
}

static int cic_decode(cic_inflate *z, const cic_huff *h)
{
    int code = 0, first = 0, index = 0;
    uint32_t len;
    for (len = 1U; len < 16U; ++len) {
        int count;
        code |= (int)cic_bits(z, 1U);
        if (z->error) return -1;
        count = (int)h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static bool cic_flush(cic_inflate *z, size_t n)
{
    return n == 0U || cic_sink_write(&z->sink->device, z->window, n) == (ssize_t)n;
}

static bool cic_put(cic_inflate *z, uint8_t value)
{
    if (z->total >= z->limit) return false;
    z->window[z->total & CIC_WINDOW_MASK] = value;
    ++z->total;
    if ((z->total & CIC_WINDOW_MASK) == 0U) return cic_flush(z, CIC_WINDOW);
    return true;
}

static bool cic_codes(cic_inflate *z)
{
    for (;;) {
        int symbol = cic_decode(z, &z->lit);
        if (symbol < 0) return false;
        if ((++z->tick & 0xFFFFU) == 0U && cic_stopped(z->pd)) return false;
        if (symbol < 256) {
            if (!cic_put(z, (uint8_t)symbol)) return false;
        } else if (symbol == 256) {
            return true;
        } else {
            uint32_t length, distance;
            int dsym;
            symbol -= 257;
            if (symbol >= 29) return false;
            length = cic_len_base[symbol] + cic_bits(z, cic_len_extra[symbol]);
            dsym = cic_decode(z, &z->dist);
            if (dsym < 0 || dsym >= 30) return false;
            distance = cic_dist_base[dsym] + cic_bits(z, cic_dist_extra[dsym]);
            if (z->error || distance > CIC_WINDOW || (uint64_t)distance > z->total || (uint64_t)length > z->limit - z->total) return false;
            while (length--) {
                if (!cic_put(z, z->window[(z->total - distance) & CIC_WINDOW_MASK])) return false;
            }
        }
    }
}

static bool cic_fixed(cic_inflate *z)
{
    uint32_t i;
    for (i = 0U; i < 144U; ++i) z->lengths[i] = 8U;
    for (; i < 256U; ++i) z->lengths[i] = 9U;
    for (; i < 280U; ++i) z->lengths[i] = 7U;
    for (; i < 288U; ++i) z->lengths[i] = 8U;
    if (cic_build(&z->lit, z->lengths, 288U) < 0) return false;
    for (i = 0U; i < 30U; ++i) z->lengths[i] = 5U;
    if (cic_build(&z->dist, z->lengths, 30U) < 0) return false;
    return cic_codes(z);
}

static bool cic_dynamic(cic_inflate *z)
{
    uint32_t nlen = cic_bits(z, 5U) + 257U;
    uint32_t ndist = cic_bits(z, 5U) + 1U;
    uint32_t ncode = cic_bits(z, 4U) + 4U;
    uint32_t index;
    if (z->error || nlen > 288U || ndist > 32U || ncode > 19U) return false;
    for (index = 0U; index < 19U; ++index) z->lengths[index] = 0U;
    for (index = 0U; index < ncode; ++index) z->lengths[cic_code_order[index]] = (uint8_t)cic_bits(z, 3U);
    if (z->error || cic_build(&z->code, z->lengths, 19U) < 0) return false;
    index = 0U;
    while (index < nlen + ndist) {
        int symbol = cic_decode(z, &z->code);
        uint32_t repeat;
        uint8_t value = 0U;
        if (symbol < 0) return false;
        if (symbol < 16) {
            z->lengths[index++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16) {
            if (index == 0U) return false;
            value = z->lengths[index - 1U];
            repeat = 3U + cic_bits(z, 2U);
        } else if (symbol == 17) {
            repeat = 3U + cic_bits(z, 3U);
        } else {
            repeat = 11U + cic_bits(z, 7U);
        }
        if (z->error || repeat > nlen + ndist - index) return false;
        while (repeat--) z->lengths[index++] = value;
    }
    /* A block without an end-of-block code can never terminate. */
    if (z->lengths[256] == 0U) return false;
    if (cic_build(&z->lit, z->lengths, nlen) < 0 || cic_build(&z->dist, z->lengths + nlen, ndist) < 0) return false;
    return cic_codes(z);
}

static bool cic_inflate_run(cic_inflate *z)
{
    uint32_t final_block = 0U;
    do {
        uint32_t type = cic_bits(z, 3U);
        final_block = cic_bits(z, 1U);
        if (z->error) return false;
        if (type == 7U) {
            uint32_t length;
            z->bits >>= (z->bit_count & 7U);
            z->bit_count -= (z->bit_count & 7U);
            length = cic_bits(z, 16U);
            if (z->error || (uint64_t)length > z->limit - z->total) return false;
            while (length--) {
                uint32_t value = cic_bits(z, 8U);
                if (z->error || !cic_put(z, (uint8_t)value)) return false;
            }
        } else if (type == 5U) {
            if (!cic_fixed(z)) return false;
        } else if (type == 6U) {
            if (!cic_dynamic(z)) return false;
        } else {
            return false;
        }
        if (cic_stopped(z->pd)) return false;
    } while (!final_block);
    return !z->error && cic_flush(z, (size_t)(z->total & CIC_WINDOW_MASK)) && z->total == z->limit;
}

/* Decode one stream from a device range or a memory buffer into @p sink,
 * which must accept exactly @p limit bytes. */
static bool cic_ctdeflate(xx_io_device *device, int64_t offset, uint64_t size, const uint8_t *memory, cic_sink *sink, uint64_t limit, uint64_t *consumed,
                          xx_pd_struct *pd)
{
    cic_inflate *z;
    bool result;
    if (consumed) *consumed = 0U;
    if (!sink || size == 0U || (!device && !memory)) return false;
    z = (cic_inflate *)xx_mem_calloc(1U, sizeof(*z));
    if (!z) return false;
    if (memory) {
        z->buffer = memory;
        z->buffer_size = (size_t)size;
    } else {
        z->io_capacity = xx_get_file_buffer_size();
        if (size < z->io_capacity) z->io_capacity = (size_t)size;
        z->owned_buffer = (uint8_t *)xx_mem_alloc(z->io_capacity);
        if (!z->owned_buffer) {
            xx_mem_free(z);
            return false;
        }
        z->buffer = z->owned_buffer;
        z->device = device;
        z->position = offset;
        z->left = size;
    }
    z->sink = sink;
    z->limit = limit;
    z->pd = pd;
    result = cic_inflate_run(z);
    if (result && consumed) *consumed = z->consumed;
    if (z->owned_buffer) xx_mem_free(z->owned_buffer);
    xx_mem_free(z);
    return result;
}

bool xx_clickteam_install_creator_inflate_memory(const uint8_t *stream, size_t stream_size, uint8_t *output, size_t output_size, size_t *consumed)
{
    cic_sink sink;
    uint64_t used = 0U;
    bool result;
    if (consumed) *consumed = 0U;
    if (!stream || stream_size == 0U || (!output && output_size)) return false;
    cic_sink_init(&sink, NULL, output, (uint64_t)output_size);
    result = cic_ctdeflate(NULL, 0, (uint64_t)stream_size, stream, &sink, (uint64_t)output_size, &used, NULL) && sink.written == (uint64_t)output_size;
    if (result && consumed) *consumed = (size_t)used;
    return result;
}

/* ---------------------------------------------------------------------- */
/* Carrier: the PE overlay                                                 */

typedef struct cic_chunk {
    uint16_t id;
    uint16_t flags;
    uint32_t size;
    uint32_t unpacked; /* 1.x: decoded size of the body (its size when stored) */
    int64_t body;
} cic_chunk;

typedef struct cic_scan {
    uint32_t generation;
    int64_t overlay;
    int64_t end;
    int64_t region;
    int64_t region_size;
    uint32_t chunk_count;
    cic_chunk chunks[CIC_MAX_CHUNKS];
} cic_scan;

/* The overlay starts at the end of the furthest section's raw data.  An
 * Authenticode signature (security directory, a file offset) appended after
 * the payload is cut off. */
static bool cic_pe_overlay(xx_io_device *device, int64_t base, int64_t total, int64_t *overlay, int64_t *end)
{
    uint8_t mz[64], nt[24], magic[2], dir[8];
    uint8_t sections[CIC_MAX_SECTIONS * 40U];
    uint32_t lfanew, index, sec_off = 0U, sec_size = 0U;
    uint16_t nsec, optsz, optmagic;
    uint64_t size, max_end = 0U, table;
    uint32_t dd, nrva_at;
    if (base < 0 || total <= base) return false;
    size = (uint64_t)(total - base);
    if (size < 0x200U || !cic_read_at(device, base, mz, sizeof(mz)) || mz[0] != 'M' || mz[1] != 'Z') return false;
    lfanew = xx_data_get_u32(mz + 0x3C, 4, 0, false);
    if (lfanew < 0x40U || lfanew > 0x10000U || (uint64_t)lfanew + 24U + 2U > size || !cic_read_at(device, base + (int64_t)lfanew, nt, sizeof(nt)) || nt[0] != 'P' ||
        nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    nsec = xx_data_get_u16(nt + 6, 2, 0, false);
    optsz = xx_data_get_u16(nt + 20, 2, 0, false);
    if (nsec == 0U || nsec > CIC_MAX_SECTIONS || optsz < 2U || !cic_read_at(device, base + (int64_t)lfanew + 24, magic, 2U)) return false;
    optmagic = xx_data_get_u16(magic, 2, 0, false);
    if (optmagic == 0x10BU) {
        dd = 96U;
        nrva_at = 92U;
    } else if (optmagic == 0x20BU) {
        dd = 112U;
        nrva_at = 108U;
    } else {
        return false;
    }
    table = (uint64_t)lfanew + 24U + optsz;
    if (table + (uint64_t)nsec * 40U > size || !cic_read_at(device, base + (int64_t)table, sections, (size_t)nsec * 40U)) return false;
    for (index = 0U; index < nsec; ++index) {
        const uint8_t *s = sections + (size_t)index * 40U;
        uint32_t raw_size = xx_data_get_u32(s + 16, 4, 0, false), raw_ptr = xx_data_get_u32(s + 20, 4, 0, false);
        if (raw_size && (uint64_t)raw_ptr + raw_size > max_end) max_end = (uint64_t)raw_ptr + raw_size;
    }
    if (max_end == 0U || max_end >= size) return false;
    if ((uint32_t)optsz >= dd + 5U * 8U && cic_read_at(device, base + (int64_t)lfanew + 24 + nrva_at, dir, 4U) && xx_data_get_u32(dir, 4, 0, false) > 4U &&
        cic_read_at(device, base + (int64_t)lfanew + 24 + dd + 32, dir, 8U)) {
        sec_off = xx_data_get_u32(dir, 4, 0, false);
        sec_size = xx_data_get_u32(dir + 4, 4, 0, false);
    }
    *overlay = base + (int64_t)max_end;
    *end = total;
    if (sec_size && (uint64_t)sec_off >= max_end && (uint64_t)sec_off + sec_size <= size) *end = base + (int64_t)sec_off;
    return *end > *overlay;
}

/* Walk the chunk chain from the overlay to the 0x7F7F data chunk.  Bounded
 * reads only: this is the whole detection probe. */
static bool cic_walk(xx_io_device *device, int64_t base, cic_scan *scan)
{
    int64_t total, p;
    uint8_t head[13];
    uint32_t index;
    if (!device || !scan) return false;
    xx_mem_zero(scan, sizeof(*scan));
    total = xx_io_total_size(device);
    if (!cic_pe_overlay(device, base, total, &scan->overlay, &scan->end)) return false;
    if (scan->end - scan->overlay < 32 || !cic_read_at(device, scan->overlay, head, CIC_TAG_SIZE)) return false;
    p = scan->overlay;
    if (xx_rt_memcmp(head, cic_tag, CIC_TAG_SIZE) == 0) {
        scan->generation = 2U;
        p += CIC_TAG_SIZE;
    } else {
        scan->generation = 1U;
    }
    for (index = 0U; index < CIC_MAX_CHUNKS; ++index) {
        int64_t room = scan->end - p;
        size_t got = room >= 13 ? 13U : (size_t)(room > 0 ? room : 0);
        uint16_t id, flags;
        uint32_t size;
        if (got < CIC_CHUNK_HEADER || !cic_read_at(device, p, head, got)) return false;
        id = xx_data_get_u16(head, 2, 0, false);
        flags = xx_data_get_u16(head + 2, 2, 0, false);
        size = xx_data_get_u32(head + 4, 4, 0, false);
        if (id == CIC_DATA_ID) {
            /* {7F7F, 0, size} then the size again, then the members. */
            if (index == 0U || flags != 0U || got < 12U || xx_data_get_u32(head + 8, 4, 0, false) != size || (uint64_t)size > (uint64_t)(room - 12)) return false;
            scan->region = p + 12;
            scan->region_size = (int64_t)size;
            scan->chunk_count = index;
            return true;
        }
        if ((uint64_t)size > (uint64_t)(room - (int64_t)CIC_CHUNK_HEADER)) return false;
        if (scan->generation == 1U) {
            if ((id & 0xFF00U) != 0x1200U || flags > 1U) return false;
            if (index == 0U && (flags != 1U || (id != 0x1239U && id != 0x1241U && id != 0x1242U))) return false;
            if (flags == 1U) {
                /* {u32 unpacked, stream}; a Clickteam-Deflate stream opens
                 * on block type 5, 6 or 7. */
                if (size < 5U || got < 13U || xx_data_get_u32(head + 8, 4, 0, false) == 0U || (head[12] & 7U) < 5U) return false;
            }
        } else if (flags != 0U) {
            /* {u32 unpacked, u8 method, stream} */
            if (size < 6U || got < 13U || xx_data_get_u32(head + 8, 4, 0, false) == 0U || head[12] > 2U) return false;
        }
        scan->chunks[index].id = id;
        scan->chunks[index].flags = flags;
        scan->chunks[index].size = size;
        scan->chunks[index].unpacked = flags != 0U ? xx_data_get_u32(head + 8, 4, 0, false) : size;
        scan->chunks[index].body = p + (int64_t)CIC_CHUNK_HEADER;
        p += (int64_t)CIC_CHUNK_HEADER + (int64_t)size;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Metadata chunks                                                          */

/* Returns the decoded body of @p chunk (caller frees), or NULL.  The bytes
 * read and the bytes decoded are both charged to @p budget, which bounds
 * the work of one file-list search. */
static uint8_t *cic_load_chunk(xx_io_device *device, const cic_scan *scan, const cic_chunk *chunk, size_t *out_size, uint64_t *budget, xx_pd_struct *pd)
{
    uint8_t *body = NULL, *out = NULL;
    uint32_t unpacked = 0U;
    size_t written = 0U;
    bool ok = false;
    *out_size = 0U;
    if (chunk->size > CIC_MAX_META + 5U || (uint64_t)chunk->size > *budget || cic_stopped(pd)) return NULL;
    *budget -= chunk->size;
    body = (uint8_t *)xx_mem_alloc(chunk->size ? chunk->size : 1U);
    if (!body || !cic_read_at(device, chunk->body, body, chunk->size)) goto done;
    if (scan->generation == 1U && chunk->flags == 0U) {
        *out_size = chunk->size;
        return body;
    }
    if (chunk->size < 5U) goto done;
    unpacked = xx_data_get_u32(body, 4, 0, false);
    if (unpacked == 0U || unpacked > CIC_MAX_META || (uint64_t)unpacked > *budget) goto done;
    *budget -= unpacked;
    out = (uint8_t *)xx_mem_alloc(unpacked);
    if (!out) goto done;
    if (scan->generation == 1U) {
        size_t used = 0U;
        ok = xx_clickteam_install_creator_inflate_memory(body + 4, chunk->size - 4U, out, unpacked, &used) && used <= chunk->size - 4U;
    } else {
        const uint8_t *stream = body + 5;
        size_t stream_size = chunk->size - 5U;
        switch (body[4]) {
            case 0:
                ok = stream_size == unpacked;
                if (ok) xx_rt_memcpy(out, stream, unpacked);
                break;
            case 1:
                ok = xx_zlib_stream_decode_memory(stream, stream_size, out, unpacked, &written) && written == unpacked &&
                     xx_zlib_stream_trailer_matches(stream, stream_size, out, unpacked);
                break;
            case 2: ok = xx_bzip2_decompress_memory(stream, stream_size, out, unpacked, &written) && written == unpacked; break;
            default: break;
        }
    }
done:
    if (body) xx_mem_free(body);
    if (!ok) {
        if (out) xx_mem_free(out);
        return NULL;
    }
    *out_size = unpacked;
    return out;
}

/* ---------------------------------------------------------------------- */
/* File list                                                                */

typedef struct cic_entry {
    uint64_t offset; /* inside the member region */
    uint32_t packed;
    uint32_t unpacked;
    char *name; /* UTF-8, '/' separated */
    bool safe;
    bool shared; /* its record overlaps another member's */
    /* 1.x only */
    bool is_chunk;     /* a decoded metadata chunk ("File_<n>.bin") */
    bool patch;        /* a delta against another member */
    uint32_t chunk;    /* is_chunk: index into the scan's chunks */
    uint32_t checksum; /* 0: not checked */
    uint32_t group;    /* list a patch's base lives in / mask key slot */
    uint32_t id;       /* file id, pairs a patch with its base */
    uint32_t list;     /* index of the file list the entry came from */
} cic_entry;

#define CIC_SUM_KEYS 38U
#define CIC_KEY_SLOTS 32U

typedef struct cic_list {
    cic_entry *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t version;
    /* 1.x only */
    uint32_t list_count;              /* file lists seen (one per component set) */
    uint32_t mask_bits;               /* lists whose patches are XOR-masked */
    int8_t key[CIC_KEY_SLOTS];        /* 0 unknown, k + 1 = key k, -1 none fits */
    uint32_t sum_keys[CIC_SUM_KEYS];  /* checksum whitening candidates */
    int32_t sum_key;                  /* the candidate in use, -1 until one fits */
} cic_list;

static void cic_list_free(cic_list *list)
{
    uint32_t i;
    if (!list) return;
    if (list->entries) {
        for (i = 0U; i < list->count; ++i)
            if (list->entries[i].name) xx_mem_free(list->entries[i].name);
        xx_mem_free(list->entries);
    }
    xx_mem_zero(list, sizeof(*list));
}

/* Windows-1252 bytes 0x80..0x9F; 0 marks an unassigned byte. */


/* Converts the raw name (at most CIC_MAX_NAME bytes, no NUL) to UTF-8 with
 * '\' turned into '/'.  An unassigned byte becomes U+FFFD and makes the
 * name unsafe to write. */
static char *cic_name_utf8(const uint8_t *raw, size_t n, bool *clean)
{
    char *out = (char *)xx_mem_alloc(n * 3U + 1U);
    size_t i, o = 0U;
    *clean = true;
    if (!out) return NULL;
    for (i = 0U; i < n; ++i) {
        uint32_t c = raw[i];
        if (c == '\\') c = '/';
        if (c >= 0x80U && c < 0xA0U) {
            c = xx_codepage_decode_byte(xx_codepage_by_number(1252), (unsigned char)(c), 0x0);
            if (c == 0U) {
                c = 0xFFFDU;
                *clean = false;
            }
        }
        if (c < 0x80U) {
            out[o++] = (char)c;
        } else if (c < 0x800U) {
            out[o++] = (char)(0xC0U | (c >> 6));
            out[o++] = (char)(0x80U | (c & 0x3FU));
        } else {
            out[o++] = (char)(0xE0U | (c >> 12));
            out[o++] = (char)(0x80U | ((c >> 6) & 0x3FU));
            out[o++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[o] = 0;
    return out;
}

static char cic_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the component's stem (before the first '.', trailing spaces
 * dropped) is a Windows device name. */
static bool cic_is_device(const char *s, size_t n)
{
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, i, k;
    while (stem < n && s[stem] != '.') ++stem;
    while (stem > 0U && s[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const char *w = names[i];
        for (k = 0U; k < stem && w[k] && cic_upper(s[k]) == w[k]; ++k) {
        }
        if (k == stem && w[k] == 0) return true;
    }
    if (stem >= 4U &&
        ((cic_upper(s[0]) == 'C' && cic_upper(s[1]) == 'O' && cic_upper(s[2]) == 'M') || (cic_upper(s[0]) == 'L' && cic_upper(s[1]) == 'P' && cic_upper(s[2]) == 'T'))) {
        if (stem == 4U && s[3] >= '0' && s[3] <= '9') return true;
        /* COM1..3 / LPT1..3 spelled with superscript digits (U+00B9,
         * U+00B2, U+00B3), which Windows also maps to the devices. */
        if (stem == 5U && (uint8_t)s[3] == 0xC2U && ((uint8_t)s[4] == 0xB9U || (uint8_t)s[4] == 0xB2U || (uint8_t)s[4] == 0xB3U)) return true;
    }
    return false;
}

/* A relative path whose every component is an ordinary file name. */
static bool cic_safe_path(const char *name)
{
    size_t start = 0U, i, components = 0U;
    size_t length;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length > CIC_MAX_NAME * 3U) return false;
    for (i = 0U; i <= length; ++i) {
        if (i == length || name[i] == '/') {
            size_t n = i - start, k;
            const char *c = name + start;
            bool meaningful = false;
            if (n == 0U || ++components > CIC_MAX_COMPONENTS) return false;
            if (c[n - 1U] == '.' || c[n - 1U] == ' ') return false;
            for (k = 0U; k < n; ++k) {
                uint8_t b = (uint8_t)c[k];
                if (b < 0x20U || b == 0x7FU || b == ':' || b == '<' || b == '>' || b == '"' || b == '|' || b == '?' || b == '*' || b == '\\') return false;
                /* UTF-8 of a C1 control (U+0080..U+009F) or U+FFFD */
                if (b == 0xC2U && k + 1U < n && (uint8_t)c[k + 1U] < 0xA0U) return false;
                if (b == 0xEFU && k + 2U < n && (uint8_t)c[k + 1U] == 0xBFU && (uint8_t)c[k + 2U] == 0xBDU) return false;
                if (b != '.' && b != ' ') meaningful = true;
            }
            if (!meaningful || cic_is_device(c, n)) return false;
            start = i + 1U;
        }
    }
    return true;
}

/* Next code point of a name built by cic_name_utf8 (1..3-byte UTF-8), case
 * folded the way Windows folds the letters a Windows-1252 name can hold:
 * a-z, U+00E0..U+00FE (not U+00F7), and y/s/oe/z with diacritics. */
static uint32_t cic_fold_next(const char **cursor)
{
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t c = s[0];
    if (c >= 0xE0U && s[1] && s[2]) {
        c = ((c & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) | (uint32_t)(s[2] & 0x3FU);
        *cursor += 3;
    } else if (c >= 0xC0U && s[1]) {
        c = ((c & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
        *cursor += 2;
    } else {
        *cursor += 1;
    }
    if (c >= 'a' && c <= 'z') c -= 0x20U;
    else if (c >= 0xE0U && c <= 0xFEU && c != 0xF7U) c -= 0x20U;
    else if (c == 0xFFU) c = 0x178U;
    else if (c == 0x161U || c == 0x153U || c == 0x17EU) c -= 1U;
    return c;
}

/* Case-folded copy of a name built by cic_name_utf8.  Folding keeps every
 * code point's UTF-8 length, so the copy is exactly as long as the name. */
static void cic_fold_copy(const char *name, char *out)
{
    const char *s = name;
    while (*s) {
        uint32_t c = cic_fold_next(&s);
        if (c < 0x80U) {
            *out++ = (char)c;
        } else if (c < 0x800U) {
            *out++ = (char)(0xC0U | (c >> 6));
            *out++ = (char)(0x80U | (c & 0x3FU));
        } else {
            *out++ = (char)(0xE0U | (c >> 12));
            *out++ = (char)(0x80U | ((c >> 6) & 0x3FU));
            *out++ = (char)(0x80U | (c & 0x3FU));
        }
    }
    *out = 0;
}

#include "xx_sort_clickteam_install_creator_clickteam_install_creator_01.inc"

/* True for a component in the form of an NTFS 8.3 short-name alias
 * ("LONGFI~1.TXT"): stem of at most 8 bytes ending in '~' and digits,
 * extension of at most 3 bytes.  Such a name can open another member's
 * file through its short name. */
static bool cic_is_alias83(const char *c, size_t n)
{
    size_t dot = n, i, stem;
    for (i = 0U; i < n; ++i)
        if (c[i] == '.') {
            if (dot != n) return false;
            dot = i;
        }
    stem = dot;
    if (stem < 3U || stem > 8U || (dot < n && n - dot - 1U > 3U)) return false;
    i = stem;
    while (i > 0U && c[i - 1U] >= '0' && c[i - 1U] <= '9') --i;
    return i < stem && i >= 2U && c[i - 1U] == '~';
}

/* Appends "_<index>" (first rename) or "_<index>_<attempt>". */
static bool cic_rename(cic_entry *e, uint32_t index, uint32_t attempt)
{
    char suffix[32];
    char *renamed, *copy;
    size_t len;
    if (attempt <= 1U) (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u", index);
    else (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%u_%u", index, attempt);
    renamed = xx_str_concat(e->name, suffix);
    if (!renamed) return false;
    len = xx_str_len(renamed);
    copy = (char *)xx_mem_alloc(len + 1U);
    if (copy) xx_rt_memcpy(copy, renamed, len + 1U);
    xx_str_free(renamed);
    if (!copy) return false;
    xx_mem_free(e->name);
    e->name = copy;
    return true;
}

/* Gives every entry a name no other entry has (ignoring case the way
 * Windows does), so no member overwrites another on extraction.  A clash
 * gets "_<index>"; the entry with the lowest index keeps its name.  A last
 * component in 8.3-alias form is renamed the same way, and an entry with
 * such a directory component is not written.  Sorting keeps this
 * O(n log n) for any set of names (a hash table can be forced into long
 * probe chains with chosen names). */
static bool cic_dedupe(cic_list *list)
{
    uint32_t n = list->count, i, round;
    uint32_t *index = NULL;
    uint8_t *attempts = NULL;
    char **keys = NULL;
    char *arena = NULL;
    bool ok = false;
    if (n == 0U) return true;
    index = (uint32_t *)xx_mem_alloc((size_t)n * sizeof(uint32_t));
    attempts = (uint8_t *)xx_mem_calloc(n, 1U);
    keys = (char **)xx_mem_alloc((size_t)n * sizeof(char *));
    if (!index || !attempts || !keys) goto done;
    for (i = 0U; i < n; ++i) {
        cic_entry *e = &list->entries[i];
        const char *name = e->name;
        size_t len = xx_str_len(name), start = 0U, k;
        for (k = 0U; k <= len; ++k) {
            if (k < len && name[k] != '/') continue;
            if (cic_is_alias83(name + start, k - start)) {
                if (k < len) {
                    e->safe = false;
                } else {
                    if (!cic_rename(e, i, 1U)) goto done;
                    attempts[i] = 1U;
                }
            }
            start = k + 1U;
        }
    }
    for (round = 0U; round < 8U; ++round) {
        size_t total = 0U, at = 0U;
        bool clash = false;
        for (i = 0U; i < n; ++i) total += xx_str_len(list->entries[i].name) + 1U;
        arena = (char *)xx_mem_alloc(total);
        if (!arena) goto done;
        for (i = 0U; i < n; ++i) {
            keys[i] = arena + at;
            cic_fold_copy(list->entries[i].name, keys[i]);
            at += xx_str_len(keys[i]) + 1U;
            index[i] = i;
        }
        xx_sort_stable_context(index, n, sizeof(*index), cic_key_cmp, keys);
        for (i = 1U; i < n; ++i) {
            uint32_t cur = index[i];
            if (xx_str_cmp(keys[index[i - 1U]], keys[cur]) != 0) continue;
            /* same folded name as its predecessor, which has a lower index */
            clash = true;
            if (attempts[cur] >= 8U || !cic_rename(&list->entries[cur], cur, ++attempts[cur])) goto done;
        }
        xx_mem_free(arena);
        arena = NULL;
        if (!clash) {
            ok = true;
            break;
        }
    }
done:
    if (arena) xx_mem_free(arena);
    if (keys) xx_mem_free(keys);
    if (attempts) xx_mem_free(attempts);
    if (index) xx_mem_free(index);
    return ok;
}

static bool cic_add_entry(cic_list *list, uint32_t capacity, uint64_t offset, uint32_t packed, uint32_t unpacked, const uint8_t *raw_name, size_t name_size)
{
    cic_entry *e;
    bool clean;
    if (list->count >= capacity || name_size == 0U || name_size > CIC_MAX_NAME) return false;
    e = &list->entries[list->count];
    e->name = cic_name_utf8(raw_name, name_size, &clean);
    if (!e->name) return false;
    e->offset = offset;
    e->packed = packed;
    e->unpacked = unpacked;
    e->safe = clean && cic_safe_path(e->name);
    ++list->count;
    return true;
}

/* Length of the NUL-terminated name at @p p, bounded by @p n; 0 if the
 * name is empty or runs past the bound without a terminator and
 * @p need_nul is set. */
static size_t cic_name_length(const uint8_t *p, size_t n, bool need_nul)
{
    size_t i = 0U;
    while (i < n && p[i]) ++i;
    if (i == n && need_nul) return 0U;
    return i;
}

/* ---------------------------------------------------------------------- */
/* Install Creator 1.x file lists                                           */
/*                                                                          */
/* A list is {u32 count, count records}.  A record opens with its own size  */
/* (u16 or u32) and a fixed part whose layout changed from build to build;  */
/* fourteen layouts are known.  After the fixed part: 0x14 bytes when       */
/* flags & 6, three FILETIMEs when flags & 8, then the NUL-terminated       */
/* relative path; the record size skips whatever follows.  The layout is    */
/* not stored anywhere: it is the one that reads the whole first list       */
/* cleanly (sizes, reserved fields, the FILETIME range, printable names).   */
/* Members are addressed by offset inside the data region (the two oldest   */
/* layouts store them back to back in list order).                          */

typedef struct cic_layout1 {
    uint8_t version;
    uint8_t fixed;      /* bytes before the optional blocks */
    uint8_t size_bytes; /* width of the record-size field at +0 */
    uint8_t mode_at, mode_bytes;
    uint8_t offset_at; /* 0: members follow each other */
    uint8_t packed_at, unpacked_at, sum_at;
    uint8_t flags_at, flags_bytes;
    uint8_t group_at; /* u16; 0: none */
    uint8_t id_at, id_bytes;
} cic_layout1;

static const cic_layout1 cic_layouts1[] = {
    {1U, 0x18U, 2U, 0U, 0U, 0U, 0x06U, 0x0AU, 0x0EU, 0x12U, 4U, 0U, 0U, 0U},
    {2U, 0x1AU, 4U, 0U, 0U, 0U, 0x16U, 0x12U, 0x0EU, 0x0DU, 1U, 0U, 0U, 0U},
    {3U, 0x1EU, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x0EU, 0x12U, 0x16U, 4U, 0U, 0x1DU, 1U},
    {6U, 0x20U, 4U, 0x04U, 4U, 0x18U, 0x1CU, 0x14U, 0x10U, 0x08U, 4U, 0U, 0U, 0U},
    {7U, 0x22U, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x0EU, 0x12U, 0x16U, 4U, 0x1CU, 0x1EU, 4U},
    {8U, 0x24U, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x0EU, 0x12U, 0x16U, 4U, 0U, 0U, 0U},
    {9U, 0x24U, 4U, 0x04U, 4U, 0x16U, 0x1AU, 0x12U, 0x0EU, 0x0DU, 1U, 0x22U, 0x1EU, 4U},
    {10U, 0x26U, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x12U, 0x16U, 0x1AU, 4U, 0U, 0U, 0U},
    {11U, 0x26U, 4U, 0x0CU, 2U, 0x18U, 0x1CU, 0x14U, 0x10U, 0x0EU, 2U, 0x24U, 0x20U, 4U},
    {12U, 0x28U, 4U, 0x04U, 4U, 0x1CU, 0x20U, 0x18U, 0x14U, 0x08U, 4U, 0U, 0U, 0U},
    {13U, 0x2AU, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x12U, 0x16U, 0x1AU, 4U, 0U, 0U, 0U},
    {14U, 0x2CU, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x12U, 0x16U, 0x1AU, 4U, 0x26U, 0x28U, 4U},
    {15U, 0x2EU, 4U, 0x04U, 4U, 0x1CU, 0x20U, 0x18U, 0x14U, 0x08U, 4U, 0x2CU, 0x28U, 4U},
    {16U, 0x2EU, 2U, 0x02U, 4U, 0x06U, 0x0AU, 0x12U, 0x16U, 0x1AU, 4U, 0x26U, 0x2AU, 4U},
};

#define CIC_FLAG1_PATCH 0x10U
#define CIC_FLAG1_NO_DATA 0x200000U

static uint32_t cic_u16(const uint8_t *p)
{
    return xx_data_get_u16(p, 2, 0, false);
}

static uint32_t cic_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}

static uint32_t cic_field(const uint8_t *p, uint32_t bytes)
{
    return bytes == 1U ? p[0] : (bytes == 2U ? cic_u16(p) : cic_u32(p));
}

/* A 1.x FILETIME: 0x01A0000000000001..0x01FFFFFFFFFFFFFF (1972..2057), or
 * the same value with its top byte cleared. */
static bool cic_time1_ok(const uint8_t *p)
{
    uint64_t t = (uint64_t)cic_u32(p) | ((uint64_t)cic_u32(p + 4) << 32);
    if ((t >> 56) == 0U) t |= UINT64_C(0x0100000000000000);
    return t >= UINT64_C(0x01A0000000000001) && t <= UINT64_C(0x01FFFFFFFFFFFFFF);
}

static bool cic_list_reserve(cic_list *list, uint32_t extra)
{
    uint32_t need, capacity;
    cic_entry *grown;
    if (list->count > CIC_MAX_FILES + CIC_MAX_CHUNKS || extra > CIC_MAX_FILES + CIC_MAX_CHUNKS - list->count) return false;
    need = list->count + extra;
    if (need <= list->capacity) return true;
    capacity = list->capacity ? list->capacity : 64U;
    while (capacity < need) capacity *= 2U;
    grown = (cic_entry *)xx_mem_calloc(capacity, sizeof(cic_entry));
    if (!grown) return false;
    if (list->count) xx_rt_memcpy(grown, list->entries, (size_t)list->count * sizeof(cic_entry));
    if (list->entries) xx_mem_free(list->entries);
    list->entries = grown;
    list->capacity = capacity;
    return true;
}

typedef struct cic_record1 {
    uint32_t size;
    int64_t offset; /* -1: no data, -2: follows the previous member */
    uint32_t packed, unpacked, checksum, flags, group, id;
    size_t name_at, name_size;
} cic_record1;

/* Reads the record at @p pos; false when it cannot belong to the layout. */
static bool cic_record1_read(const cic_layout1 *l, const uint8_t *b, size_t n, size_t pos, uint64_t total, cic_record1 *r)
{
    const uint8_t *p = b + pos;
    size_t left = n - pos, at, length = 0U;
    uint32_t mode = 0U, value;
    if (left < l->fixed) return false;
    r->size = l->size_bytes == 2U ? cic_u16(p) : cic_u32(p);
    if (r->size < l->fixed || r->size > CIC_LIST1_MAX_ENTRY || r->size > left) return false;
    if (l->mode_bytes) mode = cic_field(p + l->mode_at, l->mode_bytes);
    r->packed = cic_u32(p + l->packed_at);
    r->unpacked = cic_u32(p + l->unpacked_at);
    r->checksum = cic_u32(p + l->sum_at);
    r->flags = cic_field(p + l->flags_at, l->flags_bytes);
    r->group = l->group_at ? cic_u16(p + l->group_at) : 0U;
    r->id = l->id_bytes ? cic_field(p + l->id_at, l->id_bytes) : 0U;
    r->offset = l->offset_at ? (int64_t)(int32_t)cic_u32(p + l->offset_at) : -2;
    if (r->packed > 0x7FFFFFFFU || r->unpacked > 0x7FFFFFFFU || r->id > 0x10000U || (l->offset_at && r->offset < -1)) return false;
    switch (l->version) {
        case 1U:
            if (cic_u32(p + 2) != 0U || cic_u16(p + 0x16) > 1U) return false;
            break;
        case 3U:
            if ((mode != 0U && mode != 2U) || cic_u16(p + 0x1A) > 1U || p[0x1C] != 0U || r->offset < 0 || (uint64_t)r->offset > total || (uint64_t)r->packed > total)
                return false;
            break;
        case 6U:
            value = cic_u32(p + 0x0C);
            if (value != 0U && value != 2U && value != 8U) return false;
            break;
        case 10U:
            value = cic_u32(p + 0x1E);
            if (cic_u32(p + 0x0E) != 0U || (value != 0U && value != 0x2000U)) return false;
            value = cic_u32(p + 0x22);
            if (value >= 16U || !((1U << value) & 0x50DU)) return false;
            break;
        case 11U:
            if (cic_u32(p + 4) != 0U || (mode != 0U && mode != 2U)) return false;
            break;
        case 12U:
            if (cic_u32(p + 0x24) != 0U) return false;
            break;
        case 13U:
            if (cic_u32(p + 0x0E) != 0U || cic_u32(p + 0x22) != 0U || cic_u32(p + 0x26) != 0U) return false;
            break;
        default: break;
    }
    at = l->fixed;
    if (r->flags & 6U) at += 0x14U;
    if (r->flags & 8U) {
        if (left < at + 0x18U || !cic_time1_ok(p + at) || !cic_time1_ok(p + at + 8U) || !cic_time1_ok(p + at + 16U)) return false;
        at += 0x18U;
    }
    if (at >= left) return false;
    /* The path runs to its NUL (or the end of the list). */
    while (at + length < left && p[at + length]) {
        if (p[at + length] < 0x20U || ++length >= CIC_MAX_NAME) return false;
    }
    if (length == 0U) return false;
    r->name_at = pos + at;
    r->name_size = length;
    return true;
}

/* Walks one list with one layout.  With @p list NULL it only tests. */
static bool cic_walk_list1(const cic_layout1 *l, const uint8_t *b, size_t n, uint64_t total, cic_list *list, uint32_t list_index)
{
    uint32_t count, i;
    size_t pos = 4U;
    uint64_t running = 0U;
    if (n < 4U) return false;
    count = cic_u32(b);
    if (count > CIC_MAX_FILES || (uint64_t)count * l->fixed > n - 4U) return false;
    for (i = 0U; i < count; ++i) {
        cic_record1 r;
        if (!cic_record1_read(l, b, n, pos, total, &r)) return false;
        if (r.offset == -2) {
            r.offset = (int64_t)running;
            running += r.packed;
        }
        if (list && r.offset >= 0 && !(r.flags & CIC_FLAG1_NO_DATA)) {
            cic_entry *e;
            if (!cic_list_reserve(list, 1U) || !cic_add_entry(list, list->capacity, (uint64_t)r.offset, r.packed, r.unpacked, b + r.name_at, r.name_size)) return false;
            e = &list->entries[list->count - 1U];
            e->patch = (r.flags & CIC_FLAG1_PATCH) != 0U;
            e->checksum = r.checksum;
            e->group = r.group;
            e->id = r.id;
            e->list = list_index;
        }
        pos += r.size;
    }
    return true;
}

static const cic_layout1 *cic_layout1_for(uint32_t version)
{
    uint32_t k;
    for (k = 0U; k < sizeof(cic_layouts1) / sizeof(cic_layouts1[0]); ++k)
        if (cic_layouts1[k].version == version) return &cic_layouts1[k];
    return NULL;
}

/* The one layout that reads the list; 0 when none or several do (an empty
 * list reads with every layout and decides nothing). */
static uint32_t cic_detect_list1(const uint8_t *b, size_t n, uint64_t total)
{
    uint32_t k, found = 0U;
    for (k = 0U; k < sizeof(cic_layouts1) / sizeof(cic_layouts1[0]); ++k) {
        if (!cic_walk_list1(&cic_layouts1[k], b, n, total, NULL, 0U)) continue;
        if (found) return 0U;
        found = cic_layouts1[k].version;
    }
    return found;
}

/* Install Creator 2.x node layouts per builder version.  Field offsets are
 * from the node start; 0xFF means "not present". */
typedef struct cic_layout2 {
    uint32_t version;
    uint8_t size_bytes;
    uint8_t type_at;
    uint8_t marker_at; /* 0: no 0xE2 marker byte */
    uint8_t unpacked_at, offset_at, packed_at, time_at, name_at;
    uint8_t e2_unpacked_at, e2_offset_at, e2_packed_at, e2_time_at, e2_name_at;
} cic_layout2;

#define CIC_NA 0xFFU

/* time_at: three FILETIMEs (created, accessed, written) */
static const cic_layout2 cic_layouts2[] = {
    {40U, 4U, 4U, 9U, 24U, 28U, 32U, 40U, 64U, CIC_NA, CIC_NA, CIC_NA, CIC_NA, 40U},
    {35U, 4U, 4U, 9U, 24U, 28U, 32U, 46U, 70U, 40U, 44U, 48U, 62U, 86U},
    {30U, 2U, 2U, 0U, 18U, 6U, 10U, 44U, 68U, 0U, 0U, 0U, 0U, 0U},
    {24U, 2U, 2U, 0U, 18U, 6U, 10U, 38U, 62U, 0U, 0U, 0U, 0U, 0U},
    {20U, 4U, 4U, 0U, 20U, 24U, 28U, 32U, 56U, 0U, 0U, 0U, 0U, 0U},
};

/* A FILETIME is either unset or a date between 1970 and 2200.  Reading a
 * node with the wrong layout lands these fields on name or size bytes. */
static bool cic_times_ok(const uint8_t *p)
{
    uint32_t i;
    for (i = 0U; i < 3U; ++i) {
        uint64_t t = (uint64_t)xx_data_get_u32(p + i * 8U, 4, 0, false) | ((uint64_t)xx_data_get_u32(p + i * 8U + 4U, 4, 0, false) << 32);
        if (t != 0U && (t < UINT64_C(0x019DB1DED53E8000) || t >= UINT64_C(0x029F8E129EF10000))) return false;
    }
    return true;
}

typedef struct cic_node {
    bool has_member;
    uint64_t offset;
    uint32_t packed, unpacked;
    size_t name_at, name_size;
} cic_node;

/* A 2.x member record opens with its method byte and, for zlib and bzip2,
 * the stream's own header.  This is what tells the node layouts apart: a
 * wrong layout reads its offsets from unrelated bytes. */
static bool cic_record_ok(xx_io_device *device, const cic_scan *scan, uint64_t offset, uint32_t packed)
{
    uint8_t head[3];
    if (packed == 0U || offset + packed > (uint64_t)scan->region_size) return false;
    if (!cic_read_at(device, scan->region + (int64_t)offset, head, packed < 3U ? packed : 3U)) return false;
    switch (head[0]) {
        case 0: return true;
        case 1: return packed >= 9U && xx_zlib_stream_header_is_valid(head + 1, 2U);
        case 2: return packed >= 15U && head[1] == 'B' && head[2] == 'Z';
        default: return false;
    }
}

/* Reads one node's member fields; false when the node cannot belong to the
 * layout at all. */
static bool cic_node2(const cic_layout2 *l, const uint8_t *node, size_t size, xx_io_device *device, const cic_scan *scan, cic_node *out)
{
    uint16_t type;
    uint8_t un_at = l->unpacked_at, off_at = l->offset_at, pk_at = l->packed_at, tm_at = l->time_at, nm_at = l->name_at;
    xx_mem_zero(out, sizeof(*out));
    if (size < (size_t)l->type_at + 2U) return false;
    type = xx_data_get_u16(node + l->type_at, 2, 0, false);
    if (type != 0U) {
        /* The v40 uninstaller node: sizes at +24/+28/+32, name at +40.  It
         * is kept when it reads cleanly and ignored otherwise; other
         * versions' non-file nodes are skipped. */
        if (l->version == 40U && type == 2U && size > 40U) {
            uint32_t unpacked = xx_data_get_u32(node + 24, 4, 0, false), offset = xx_data_get_u32(node + 28, 4, 0, false),
                     packed = xx_data_get_u32(node + 32, 4, 0, false);
            size_t name_size = cic_name_length(node + 40, size - 40U, false);
            if (name_size && name_size <= CIC_MAX_NAME && cic_record_ok(device, scan, offset, packed)) {
                out->has_member = true;
                out->offset = offset;
                out->packed = packed;
                out->unpacked = unpacked;
                out->name_at = 40U;
                out->name_size = name_size;
            }
        }
        return true;
    }
    if (l->marker_at) {
        if (size <= l->marker_at) return false;
        if (node[l->marker_at] == 0xE2U) {
            un_at = l->e2_unpacked_at;
            off_at = l->e2_offset_at;
            pk_at = l->e2_packed_at;
            tm_at = l->e2_time_at;
            nm_at = l->e2_name_at;
        }
    }
    if (nm_at >= size) return false;
    if (tm_at != CIC_NA && ((size_t)tm_at + 24U > size || !cic_times_ok(node + tm_at))) return false;
    out->name_at = nm_at;
    out->name_size = cic_name_length(node + nm_at, size - nm_at, false);
    if (out->name_size == 0U || out->name_size > CIC_MAX_NAME) return false;
    out->has_member = true;
    if (un_at == CIC_NA) return true; /* v40 empty dummy file: no data */
    if ((size_t)un_at + 4U > size || (size_t)off_at + 4U > size || (size_t)pk_at + 4U > size) return false;
    out->unpacked = xx_data_get_u32(node + un_at, 4, 0, false);
    out->offset = xx_data_get_u32(node + off_at, 4, 0, false);
    out->packed = xx_data_get_u32(node + pk_at, 4, 0, false);
    /* Even an empty file is stored as a record ({01} + an empty zlib
     * stream), so a node with data fields always points at one. */
    return cic_record_ok(device, scan, out->offset, out->packed);
}

/* Walks all nodes with one layout.  With @p list NULL it only tests. */
static bool cic_walk_list2(const cic_layout2 *l, const uint8_t *b, size_t n, uint32_t count, xx_io_device *device, const cic_scan *scan, bool exact, cic_list *list)
{
    size_t pos = 4U;
    uint32_t i;
    for (i = 0U; i < count; ++i) {
        size_t size;
        cic_node node;
        if (n - pos < l->size_bytes) return false;
        size = l->size_bytes == 2U ? xx_data_get_u16(b + pos, 2, 0, false) : xx_data_get_u32(b + pos, 4, 0, false);
        if (size < (size_t)l->size_bytes + 2U || size > n - pos) return false;
        if (!cic_node2(l, b + pos, size, device, scan, &node)) return false;
        if (list && node.has_member && !cic_add_entry(list, count, node.offset, node.packed, node.unpacked, b + pos + node.name_at, node.name_size)) return false;
        pos += size;
    }
    return !exact || pos == n;
}

#include "xx_sort_clickteam_install_creator_clickteam_install_creator_02.inc"

/* 2.x nodes point at records by offset, so several nodes could share one
 * record and expand it once each.  In offset order, a record that starts
 * before the end of the last claimed one is refused (never extracted). */
static bool cic_mark_overlaps(cic_list *list)
{
    uint32_t n = list->count, i;
    uint32_t *index;
    uint64_t claimed_end = 0U;
    if (n < 2U) return true;
    index = (uint32_t *)xx_mem_alloc((size_t)n * sizeof(uint32_t));
    if (!index) return false;
    for (i = 0U; i < n; ++i) index[i] = i;
    xx_sort_stable_context(index, n, sizeof(*index), cic_offset_cmp, list->entries);
    for (i = 0U; i < n; ++i) {
        cic_entry *e = &list->entries[index[i]];
        if (e->packed == 0U) continue;
        if (e->offset < claimed_end) {
            e->shared = true;
            continue;
        }
        claimed_end = e->offset + e->packed;
    }
    xx_mem_free(index);
    return true;
}

static bool cic_parse_list2(const uint8_t *b, size_t n, xx_io_device *device, const cic_scan *scan, cic_list *list)
{
    uint32_t count, pass, k;
    xx_mem_zero(list, sizeof(*list));
    if (n < 4U) return false;
    count = xx_data_get_u16(b, 2, 0, false); /* followed by two unused bytes */
    /* Every node is at least a size field and a type field. */
    if (count == 0U || (uint64_t)count * 4U > n - 4U) return false;
    /* First pass: the nodes must fill the list exactly.  Second pass: a
     * list with trailing bytes. */
    for (pass = 0U; pass < 2U; ++pass) {
        for (k = 0U; k < sizeof(cic_layouts2) / sizeof(cic_layouts2[0]); ++k) {
            const cic_layout2 *l = &cic_layouts2[k];
            if (!cic_walk_list2(l, b, n, count, device, scan, pass == 0U, NULL)) continue;
            list->entries = (cic_entry *)xx_mem_calloc(count, sizeof(cic_entry));
            if (!list->entries) return false;
            if (!cic_walk_list2(l, b, n, count, device, scan, pass == 0U, list)) {
                cic_list_free(list);
                return false;
            }
            list->version = l->version;
            if (!cic_mark_overlaps(list)) {
                cic_list_free(list);
                return false;
            }
            return true;
        }
    }
    return false;
}

/* Checksum whitening: a member's stored checksum is its sum XOR one of
 * these words, each first run through the sum of the product title (chunk
 * 0x1237; for the two oldest list layouts the strings of chunks 0x1235 and
 * 0x1239).  Which word applies is not stored: it is the one that fits the
 * first checked member, and it then holds for the whole setup. */
static const uint32_t cic_sum_key_table[CIC_SUM_KEYS] = {
    0x0C537EFFU, 0x13FC10E5U, 0x18B28974U, 0x1E99D544U, 0x21CA4D1AU, 0x304070C5U, 0x35CA5B09U, 0x35E4519CU, 0x375B3E52U, 0x38F7E80FU,
    0x3D99891AU, 0x3DC9BEFAU, 0x41AA608EU, 0x41E6C286U, 0x503BE7B6U, 0x5211F061U, 0x59B49970U, 0x6A87DC67U, 0x6C3CF255U, 0x6DF052EDU,
    0x75D98DF0U, 0x7AFCDDC2U, 0x8AE81D1FU, 0x8B500C75U, 0xA79BCF27U, 0xA903E273U, 0xAE2B6B08U, 0xB0F5C845U, 0xB47B783DU, 0xB673563DU,
    0xCC1896D2U, 0xD341F726U, 0xD526AEE6U, 0xD74AD264U, 0xD92E2893U, 0xDDE2B8D2U, 0xEC144819U, 0xFD0021C7U,
};

static bool cic_set_name(cic_entry *e, const char *text)
{
    size_t length = xx_str_len(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return false;
    xx_rt_memcpy(copy, text, length + 1U);
    if (e->name) xx_mem_free(e->name);
    e->name = copy;
    return true;
}

/* 1.x members are addressed by offset.  A later list repeats the members it
 * shares with an earlier one (same offset): the first occurrence is kept.
 * In offset order, a member that starts inside the last claimed one is
 * refused (never extracted). */
static bool cic1_resolve_offsets(cic_list *list)
{
    uint32_t n = list->count, i, members = 0U, kept = 0U;
    uint32_t *index;
    uint8_t *drop;
    uint64_t claimed_end = 0U, previous = 0U;
    bool have_previous = false;
    if (n == 0U) return true;
    index = (uint32_t *)xx_mem_alloc((size_t)n * sizeof(uint32_t));
    drop = (uint8_t *)xx_mem_calloc(n, 1U);
    if (!index || !drop) {
        if (index) xx_mem_free(index);
        if (drop) xx_mem_free(drop);
        return false;
    }
    for (i = 0U; i < n; ++i)
        if (!list->entries[i].is_chunk) index[members++] = i;
    xx_sort_stable_context(index, members, sizeof(*index), cic_offset_cmp, list->entries);
    for (i = 0U; i < members; ++i) {
        cic_entry *e = &list->entries[index[i]];
        if (have_previous && e->offset == previous) {
            drop[index[i]] = 1U;
            continue;
        }
        previous = e->offset;
        have_previous = true;
        if (e->packed == 0U) continue;
        if (e->offset < claimed_end) {
            e->shared = true;
            continue;
        }
        claimed_end = e->offset + e->packed;
    }
    for (i = 0U; i < n; ++i) {
        if (drop[i]) {
            if (list->entries[i].name) xx_mem_free(list->entries[i].name);
            continue;
        }
        if (kept != i) list->entries[kept] = list->entries[i];
        ++kept;
    }
    for (i = kept; i < n; ++i) xx_mem_zero(&list->entries[i], sizeof(cic_entry));
    list->count = kept;
    xx_mem_free(index);
    xx_mem_free(drop);
    return true;
}

/* Install Creator 1.x: every chunk before the data chunk becomes a member
 * "File_<n>.bin" (its decoded body); the chunks 0x123A/0x1242/0x1243/0x143A
 * that read as file lists add the installed files.  A component set opens
 * with chunk 0x123E/0x143E; with more than one set the files of set n go
 * under "License.<n>/". */
static bool cic1_read_list(xx_io_device *device, const cic_scan *scan, cic_list *list, xx_pd_struct *pd)
{
    uint64_t budget = CIC_MAX_META_TOTAL, total;
    uint32_t alternative[CIC_SUM_KEYS];
    uint32_t i, k;
    int64_t device_size = xx_io_total_size(device);
    char text[40];
    xx_mem_zero(list, sizeof(*list));
    list->sum_key = -1;
    total = device_size > 0 ? (uint64_t)device_size : 0U;
    for (k = 0U; k < CIC_SUM_KEYS; ++k) list->sum_keys[k] = alternative[k] = cic_sum_key_table[k];
    if (!cic_list_reserve(list, scan->chunk_count)) goto fail;
    for (i = 0U; i < scan->chunk_count; ++i) {
        const cic_chunk *c = &scan->chunks[i];
        cic_entry *e = &list->entries[list->count];
        (void)xx_rt_snprintf(text, sizeof(text), "File_%u.bin", i + 1U);
        if (!cic_set_name(e, text)) goto fail;
        e->is_chunk = true;
        e->chunk = i;
        e->packed = c->flags ? c->size - 4U : c->size;
        e->unpacked = c->unpacked;
        e->safe = true;
        ++list->count;
    }
    for (i = 0U; i < scan->chunk_count; ++i) {
        const cic_chunk *c = &scan->chunks[i];
        bool is_title = c->id == 0x1237U, is_alternative = c->id == 0x1235U || c->id == 0x1239U;
        bool is_set = c->id == 0x123EU || c->id == 0x143EU;
        bool is_list = c->id == 0x123AU || c->id == 0x1242U || c->id == 0x1243U || c->id == 0x143AU;
        uint8_t *body;
        size_t size;
        if (!is_title && !is_alternative && !is_set && !is_list) continue;
        if (cic_stopped(pd)) goto fail;
        body = cic_load_chunk(device, scan, c, &size, &budget, pd);
        if (!body) {
            /* The title strings feed every checksum. */
            if (is_title || is_alternative) goto fail;
            continue;
        }
        if (is_title || is_alternative) {
            const uint8_t *nul = size ? (const uint8_t *)xx_rt_memchr(body, 0, size) : NULL;
            uint32_t *keys = is_title ? list->sum_keys : alternative;
            if (nul)
                for (k = 0U; k < CIC_SUM_KEYS; ++k) keys[k] = cic_sum_bytes(keys[k], body, (size_t)(nul - body));
        } else if (is_set) {
            if (size >= 16U) {
                if (cic_u16(body + 2) != 0U && cic_u32(body + 4) != 2U && list->list_count < CIC_KEY_SLOTS) list->mask_bits |= 1U << list->list_count;
                ++list->list_count;
            }
        } else {
            if (list->version == 0U) list->version = cic_detect_list1(body, size, total);
            if (list->version != 0U) {
                const cic_layout1 *l = cic_layout1_for(list->version);
                if (list->list_count == 0U) list->list_count = 1U;
                if (l && cic_walk_list1(l, body, size, total, NULL, 0U) && !cic_walk_list1(l, body, size, total, list, list->list_count - 1U)) {
                    xx_mem_free(body);
                    goto fail;
                }
            }
        }
        xx_mem_free(body);
    }
    if (list->version == 1U || list->version == 2U) xx_rt_memcpy(list->sum_keys, alternative, sizeof(alternative));
    if (!cic1_resolve_offsets(list)) goto fail;
    if (list->list_count > 1U) {
        for (i = 0U; i < list->count; ++i) {
            cic_entry *e = &list->entries[i];
            char *joined;
            bool ok;
            if (e->is_chunk) continue;
            (void)xx_rt_snprintf(text, sizeof(text), "License.%u/", e->list + 1U);
            joined = xx_str_concat(text, e->name);
            ok = joined && cic_set_name(e, joined);
            if (joined) xx_str_free(joined);
            if (!ok) goto fail;
        }
    }
    if (!cic_dedupe(list)) goto fail;
    return true;
fail:
    cic_list_free(list);
    return false;
}

/* Finds and parses the file list; the entries get unique names. */
static bool cic_read_list(xx_io_device *device, const cic_scan *scan, cic_list *list, xx_pd_struct *pd)
{
    uint32_t i;
    uint64_t budget = CIC_MAX_META_TOTAL;
    if (scan->generation == 1U) return cic1_read_list(device, scan, list, pd);
    xx_mem_zero(list, sizeof(*list));
    for (i = 0U; i < scan->chunk_count; ++i) {
        const cic_chunk *c = &scan->chunks[i];
        uint8_t *body;
        size_t size;
        bool ok;
        if (c->id != CIC_LIST_ID_2) continue;
        body = cic_load_chunk(device, scan, c, &size, &budget, pd);
        if (!body) continue;
        ok = cic_parse_list2(body, size, device, scan, list);
        xx_mem_free(body);
        /* 2.x has one list chunk; trying every chunk that carries the id
         * would multiply the node walk (each node reads the device). */
        if (!ok) return false;
        if (!cic_dedupe(list)) {
            cic_list_free(list);
            return false;
        }
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Members                                                                  */

/* Copies a device range to the sink. */
static bool cic_copy_range(xx_io_device *device, int64_t position, uint64_t left, cic_sink *sink, xx_pd_struct *pd)
{
    uint8_t *buffer;
    size_t io_capacity = xx_get_file_buffer_size();
    bool ok = true;
    if (left == 0U) return true;
    if (left < io_capacity) io_capacity = (size_t)left;
    buffer = (uint8_t *)xx_mem_alloc(io_capacity);
    if (!buffer) return false;
    while (left > 0U && ok) {
        size_t k = left < io_capacity ? (size_t)left : io_capacity;
        ok = !cic_stopped(pd) && cic_read_at(device, position, buffer, k) && cic_sink_write(&sink->device, buffer, k) == (ssize_t)k;
        left -= k;
        position += (int64_t)k;
    }
    xx_mem_free(buffer);
    return ok;
}

static uint8_t *cic_load_range(xx_io_device *device, int64_t position, uint32_t size)
{
    uint8_t *data = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (data && !cic_read_at(device, position, data, size)) {
        xx_mem_free(data);
        data = NULL;
    }
    return data;
}

/* ---------------------------------------------------------------------- */
/* Install Creator 1.x members                                              */
/*                                                                          */
/* A member is a Clickteam-Deflate stream, or (list flag 0x10) a delta      */
/* against another member: {section commands, section literals}, each       */
/* section {u8 kind, u16|u32 size, [u16|u32 packed size, Clickteam-Deflate] */
/* or raw bytes}.  Commands copy literal runs and ranges of the base file,  */
/* the range position being relative to the end of the previous range.     */
/* The members of a protected component set are XOR-masked with a running   */
/* key derived from one of fourteen fixed strings; the string in use is     */
/* the one whose output passes the member checksum.                         */

#define CIC_KEY_COUNT 14U

static const char *const cic_mask_keys[CIC_KEY_COUNT] = {
    "IYKSU",           "JZ3E2",           "MAFH3",           "IAOK4MB1UMAOWCH", "IAX2A31PZLTLDV1", "IAYCPRBGLYSFLMB", "IA2MQYOUFMDQFYI",
    "IAP2HQAD5SZKM6D", "IA2C6OQT5DYPZQ1", "IAXR3E6LOMQN66B", "IAAHIN4ENSDZTKC", "IAX1Q3BN6NS1G32", "IAIFPP2CSAQCHP2", "IASLGD432WJX4TI",
};

static void cic_unmask(uint8_t *data, size_t n, uint32_t key_index)
{
    uint8_t key[16];
    const char *text = cic_mask_keys[key_index];
    size_t length = xx_str_len(text), at = 0U, i;
    uint8_t step = 0x39U;
    if (length == 0U || length > sizeof(key)) return;
    xx_rt_memcpy(key, text, length);
    for (i = 0U; i < n; ++i) {
        uint8_t x = (uint8_t)(step ^ key[at]);
        data[i] ^= x;
        key[at] = (uint8_t)((x << 1) | (x >> 7));
        step = (uint8_t)(step + 5U);
        if (++at >= length) at = 0U;
    }
}

/* One delta section.  @p data points into @p patch or at @p owned. */
static bool cic_patch_section(const uint8_t *patch, size_t n, size_t *at, uint32_t unpacked, uint8_t **owned, const uint8_t **data, size_t *size)
{
    uint8_t kind;
    uint32_t width, length, packed;
    size_t used = 0U;
    *owned = NULL;
    *data = NULL;
    *size = 0U;
    if (*at >= n) return false;
    kind = patch[(*at)++];
    if (kind >= 8U || !((1U << kind) & 0xAFU)) return false;
    width = (kind & 2U) ? 4U : 2U;
    if (n - *at < width) return false;
    length = width == 4U ? cic_u32(patch + *at) : cic_u16(patch + *at);
    *at += width;
    if (length > 0x7FFFFFFFU) return false;
    if (length == 0U) return true;
    if (!(kind & 1U)) {
        if (n - *at < length) return false;
        *data = patch + *at;
        *size = length;
        *at += length;
        return true;
    }
    width = (kind & 4U) ? 4U : 2U;
    if (n - *at < width) return false;
    packed = width == 4U ? cic_u32(patch + *at) : cic_u16(patch + *at);
    *at += width;
    if (packed == 0U || packed > 0x7FFFFFFFU || n - *at < packed || length > unpacked || length > CIC_MAX_PATCH_BASE) return false;
    *owned = (uint8_t *)xx_mem_alloc(length);
    if (!*owned || !xx_clickteam_install_creator_inflate_memory(patch + *at, packed, *owned, length, &used) || used > packed) {
        if (*owned) xx_mem_free(*owned);
        *owned = NULL;
        return false;
    }
    *data = *owned;
    *size = length;
    *at += packed;
    return true;
}

static bool cic_patch_apply(const uint8_t *base, size_t base_size, const uint8_t *patch, size_t n, uint32_t unpacked, cic_sink *sink, xx_pd_struct *pd)
{
    uint8_t *owned_commands = NULL, *owned_literals = NULL;
    const uint8_t *commands = NULL, *literals = NULL;
    size_t command_count = 0U, literal_count = 0U, at = 0U, c = 0U, literal_at = 0U;
    int64_t position = 0;
    uint32_t tick = 0U;
    bool ok = false;
    if (!cic_patch_section(patch, n, &at, unpacked, &owned_commands, &commands, &command_count) ||
        !cic_patch_section(patch, n, &at, unpacked, &owned_literals, &literals, &literal_count) || at != n)
        goto done;
    while (c < command_count) {
        uint32_t b = commands[c++], copy = 0U, x, v;
        int64_t delta, source;
        if ((++tick & 0xFFFU) == 0U && cic_stopped(pd)) goto done;
        if (b & 0x80U) {
            /* 1..32 literals, or 1..3 literals and then a range of 2..33 */
            uint32_t run = (b >> 5) & 3U;
            if (run == 0U) run = (b & 0x1FU) + 1U;
            else copy = (b & 0x1FU) + 2U;
            if (run > literal_count - literal_at || cic_sink_write(&sink->device, literals + literal_at, run) != (ssize_t)run) goto done;
            literal_at += run;
            if (copy == 0U) continue;
        } else if (b == 0U) {
            if (command_count - c < 2U) goto done;
            copy = cic_u16(commands + c) + 0x81U;
            c += 2U;
        } else {
            copy = b + 1U;
        }
        /* The range position: a signed step from the end of the last range
         * in 6, 15 or 22 bits (biased past the shorter forms), or 32 bits. */
        if (c >= command_count) goto done;
        x = commands[c++];
        if (!(x & 0x80U)) {
            if (c >= command_count) goto done;
            v = (x << 8) | commands[c++];
            if (x & 0x40U) v |= 0xFFFFC000U;
            delta = (int32_t)v;
            delta += delta < 0 ? -0x20 : 0x20;
        } else if (!(x & 0x40U)) {
            v = x & 0x3FU;
            if (x & 0x20U) v |= 0xFFFFFFE0U;
            delta = (int32_t)v;
        } else {
            if (command_count - c < 2U) goto done;
            v = ((x << 16) | ((uint32_t)commands[c] << 8) | commands[c + 1U]) & 0x3FFFFFU;
            c += 2U;
            if (v == 0x100000U) {
                if (command_count - c < 4U) goto done;
                v = ((uint32_t)commands[c] << 24) | ((uint32_t)commands[c + 1U] << 16) | ((uint32_t)commands[c + 2U] << 8) | commands[c + 3U];
                c += 4U;
            } else if (x & 0x20U) {
                v |= 0xFFE00000U;
            }
            delta = (int32_t)v;
            delta += delta < 0 ? -0x4020 : 0x4020;
        }
        source = position + delta;
        if (source < 0 || (uint64_t)source > (uint64_t)base_size || (uint64_t)copy > (uint64_t)base_size - (uint64_t)source ||
            cic_sink_write(&sink->device, base + source, copy) != (ssize_t)copy)
            goto done;
        position = source + (int64_t)copy;
    }
    ok = true;
done:
    if (owned_commands) xx_mem_free(owned_commands);
    if (owned_literals) xx_mem_free(owned_literals);
    return ok;
}

static bool cic1_member(xx_io_device *device, const cic_scan *scan, cic_list *list, const cic_entry *e, cic_sink *sink, uint32_t depth, xx_pd_struct *pd);

/* The base of a delta: the one plain member with the same file id in the
 * list the delta names (else in the first list). */
static const cic_entry *cic1_patch_base(const cic_list *list, const cic_entry *e)
{
    uint32_t pass, i;
    for (pass = 0U; pass < 2U; ++pass) {
        const cic_entry *found = NULL;
        uint32_t wanted = pass == 0U ? e->group : 0U, hits = 0U;
        if (pass == 1U && e->group == 0U) break;
        for (i = 0U; i < list->count; ++i) {
            const cic_entry *x = &list->entries[i];
            if (x->is_chunk || x->patch || x->id != e->id || x->list != wanted) continue;
            found = x;
            ++hits;
        }
        if (hits == 1U) return found;
    }
    return NULL;
}

/* Decodes one member with the given mask keys (-1: unmasked) and verifies
 * its size and checksum. */
static bool cic1_member_body(xx_io_device *device, const cic_scan *scan, cic_list *list, const cic_entry *e, cic_sink *sink, int member_key, int patch_key, uint32_t depth,
                             xx_pd_struct *pd)
{
    int64_t at = scan->region + (int64_t)e->offset;
    uint64_t used = 0U;
    bool ok = false;
    if (e->offset > (uint64_t)scan->region_size || (uint64_t)e->packed > (uint64_t)scan->region_size - e->offset) return false;
    if (!e->patch) {
        if (member_key < 0) {
            ok = cic_ctdeflate(device, at, e->packed, NULL, sink, e->unpacked, &used, pd);
        } else {
            uint8_t *packed = cic_load_range(device, at, e->packed);
            if (packed) {
                cic_unmask(packed, e->packed, (uint32_t)member_key);
                ok = cic_ctdeflate(NULL, 0, e->packed, packed, sink, e->unpacked, &used, pd);
                xx_mem_free(packed);
            }
        }
    } else {
        const cic_entry *base = depth == 0U ? cic1_patch_base(list, e) : NULL;
        bool masked = e->list < CIC_KEY_SLOTS && ((list->mask_bits >> e->list) & 1U) != 0U;
        uint8_t *base_data = NULL, *patch = NULL;
        cic_sink base_sink;
        if (!base || base->unpacked > CIC_MAX_PATCH_BASE || (masked && patch_key < 0)) return false;
        base_data = (uint8_t *)xx_mem_alloc(base->unpacked ? base->unpacked : 1U);
        if (base_data) {
            cic_sink_init(&base_sink, NULL, base_data, base->unpacked);
            if (cic1_member(device, scan, list, base, &base_sink, depth + 1U, pd) && (patch = cic_load_range(device, at, e->packed)) != NULL) {
                if (masked) cic_unmask(patch, e->packed, (uint32_t)patch_key);
                ok = cic_patch_apply(base_data, base->unpacked, patch, e->packed, e->unpacked, sink, pd);
            }
        }
        if (patch) xx_mem_free(patch);
        if (base_data) xx_mem_free(base_data);
    }
    if (!ok || sink->written != e->unpacked) return false;
    if (e->checksum != 0U) {
        uint32_t sum = cic_sink_sum(sink);
        if (list->version == 4U || list->version == 5U) return sum == e->checksum;
        if (list->sum_key < 0) {
            uint32_t k;
            for (k = 0U; k < CIC_SUM_KEYS; ++k) {
                if ((sum ^ list->sum_keys[k]) == e->checksum) {
                    list->sum_key = (int32_t)k;
                    break;
                }
            }
        }
        return list->sum_key >= 0 && (sum ^ list->sum_keys[list->sum_key]) == e->checksum;
    }
    return true;
}

/* Settles the mask key of slot @p slot by trial decodes of @p e into
 * nothing; the key is remembered for the slot. */
static bool cic1_find_key(xx_io_device *device, const cic_scan *scan, cic_list *list, const cic_entry *e, uint32_t slot, bool member_masked, uint32_t depth, xx_pd_struct *pd)
{
    uint32_t k;
    if (list->key[slot] != 0) return list->key[slot] > 0;
    for (k = 0U; k < CIC_KEY_COUNT; ++k) {
        cic_sink probe;
        int other = e->list < CIC_KEY_SLOTS && e->list != slot ? (int)list->key[e->list] - 1 : (int)k;
        if (cic_stopped(pd)) return false;
        cic_sink_init(&probe, NULL, NULL, e->unpacked);
        list->key[slot] = (int8_t)(k + 1U);
        if (cic1_member_body(device, scan, list, e, &probe, member_masked ? (int)k : -1, member_masked ? other : (int)k, depth, pd)) return true;
    }
    list->key[slot] = -1;
    return false;
}

static bool cic1_member(xx_io_device *device, const cic_scan *scan, cic_list *list, const cic_entry *e, cic_sink *sink, uint32_t depth, xx_pd_struct *pd)
{
    if (e->is_chunk || e->shared) return false;
    if (e->packed == 0U && !e->patch) return e->unpacked == 0U;
    if (e->group < CIC_KEY_SLOTS && e->checksum != 0U && ((list->mask_bits >> e->group) & 1U) != 0U) {
        int patch_key = e->list < CIC_KEY_SLOTS && list->key[e->list] > 0 ? (int)list->key[e->list] - 1 : -1;
        if (!cic1_find_key(device, scan, list, e, e->group, true, depth, pd)) return false;
        if (e->list == e->group) patch_key = (int)list->key[e->group] - 1;
        return cic1_member_body(device, scan, list, e, sink, (int)list->key[e->group] - 1, patch_key, depth, pd);
    }
    if (e->patch && e->list < CIC_KEY_SLOTS && ((list->mask_bits >> e->list) & 1U) != 0U) {
        if (!cic1_find_key(device, scan, list, e, e->list, false, depth, pd)) return false;
        return cic1_member_body(device, scan, list, e, sink, -1, (int)list->key[e->list] - 1, depth, pd);
    }
    return cic1_member_body(device, scan, list, e, sink, -1, -1, depth, pd);
}

/* A metadata chunk published as a member: its decoded body. */
static bool cic1_unpack_chunk(xx_io_device *device, const cic_scan *scan, const cic_entry *e, cic_sink *sink, xx_pd_struct *pd)
{
    const cic_chunk *c;
    uint64_t used = 0U;
    if (e->chunk >= scan->chunk_count) return false;
    c = &scan->chunks[e->chunk];
    if (c->flags == 0U) return cic_copy_range(device, c->body, c->size, sink, pd) && sink->written == e->unpacked;
    if (c->size < 5U) return false;
    return cic_ctdeflate(device, c->body + 4, (uint64_t)c->size - 4U, NULL, sink, e->unpacked, &used, pd) && sink->written == e->unpacked;
}

static uint32_t cic_entry_method(xx_io_device *device, const cic_scan *scan, const cic_entry *e)
{
    uint8_t method = 0U;
    if (scan->generation == 1U)
        return e->is_chunk && e->chunk < scan->chunk_count && scan->chunks[e->chunk].flags == 0U ? XX_CLICKTEAM_METHOD_STORED : XX_CLICKTEAM_METHOD_CTDEFLATE;
    if (e->packed == 0U || !cic_read_at(device, scan->region + (int64_t)e->offset, &method, 1U)) return XX_CLICKTEAM_METHOD_STORED;
    return method;
}

static bool cic_unpack_entry(xx_io_device *device, const cic_scan *scan, cic_list *list, const cic_entry *e, xx_io_device *destination, xx_pd_struct *pd)
{
    cic_sink sink;
    int64_t at = scan->region + (int64_t)e->offset;
    uint8_t method = 0U;
    cic_sink_init(&sink, destination, NULL, e->unpacked);
    if (scan->generation == 1U) return e->is_chunk ? cic1_unpack_chunk(device, scan, e, &sink, pd) : cic1_member(device, scan, list, e, &sink, 0U, pd);
    if (e->packed == 0U) return e->unpacked == 0U;
    if (e->shared) return false;
    if (e->offset + e->packed > (uint64_t)scan->region_size) return false;
    if (!cic_read_at(device, at, &method, 1U)) return false;
    if (method == 0U) {
        if ((uint64_t)e->packed - 1U != e->unpacked) return false;
        return cic_copy_range(device, at + 1, (uint64_t)e->packed - 1U, &sink, pd) && sink.written == e->unpacked;
    }
    if (method == 1U) {
        uint8_t header[2], trailer[4];
        uint32_t stored;
        /* {01}{zlib: 2-byte header, raw Deflate, big-endian Adler-32} */
        if (e->packed < 1U + 2U + 2U + 4U || !cic_read_at(device, at + 1, header, 2U) || !xx_zlib_stream_header_is_valid(header, 2U) ||
            !cic_read_at(device, at + (int64_t)e->packed - 4, trailer, 4U))
            return false;
        stored = ((uint32_t)trailer[0] << 24) | ((uint32_t)trailer[1] << 16) | ((uint32_t)trailer[2] << 8) | trailer[3];
        return xx_deflate_unpack_device(device, at + 3, (int64_t)e->packed - 7, &sink.device, false, pd) && sink.written == e->unpacked &&
               cic_sink_adler(&sink) == stored;
    }
    if (method == 2U) {
        if (e->packed < 1U + 14U) return false;
        return xx_bzip2_unpack_device(device, at + 1, (int64_t)e->packed - 1, &sink.device, pd) && sink.written == e->unpacked;
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                               */

typedef struct cic_stream {
    cic_scan scan;
    cic_list list;
    uint32_t index;
} cic_stream;

static void cic_stream_free(void *opaque)
{
    cic_stream *stream = (cic_stream *)opaque;
    if (!stream) return;
    cic_list_free(&stream->list);
    xx_mem_free(stream);
}

static bool cic_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *cic_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cic_set_record(xx_archive_record *record, xx_io_device *device, const cic_stream *stream)
{
    const cic_entry *e = &stream->list.entries[stream->index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = e->is_chunk && e->chunk < stream->scan.chunk_count ? stream->scan.chunks[e->chunk].body : stream->scan.region + (int64_t)e->offset;
    record->compressed_size = (int64_t)e->packed;
    return xx_archive_record_set_original_name(record, e->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, e->packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, e->unpacked) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, cic_entry_method(device, &stream->scan, e)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_clickteam_install_creator_init(xx_clickteam_install_creator *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CLICKTEAM_INSTALL_CREATOR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdownload");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_clickteam_install_creator_check_is_valid;
    archive->format.handle_base_info = xx_clickteam_install_creator_handle_base_info;
    archive->format.get_format_size = xx_clickteam_install_creator_get_format_size;
    archive->format.get_number_of_archive_records = xx_clickteam_install_creator_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_clickteam_install_creator_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_clickteam_install_creator_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_clickteam_install_creator_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_clickteam_install_creator_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_clickteam_install_creator_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->data_offset = -1;
}

xx_clickteam_install_creator *xx_clickteam_install_creator_create(xx_io_device *device, int64_t base_address)
{
    xx_clickteam_install_creator *archive = (xx_clickteam_install_creator *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_clickteam_install_creator_init(archive, device, base_address);
    return archive;
}

void xx_clickteam_install_creator_destroy(xx_clickteam_install_creator *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_clickteam_install_creator_free(xx_clickteam_install_creator *archive)
{
    if (!archive) return;
    xx_clickteam_install_creator_destroy(archive);
    xx_mem_free(archive);
}

bool xx_clickteam_install_creator_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    cic_scan *scan;
    bool result;
    (void)pd;
    if (!format || !format->device || format->base_address < 0) return false;
    scan = (cic_scan *)xx_mem_alloc(sizeof(*scan));
    if (!scan) return false;
    result = cic_walk(format->device, format->base_address, scan);
    xx_mem_free(scan);
    return result;
}

bool xx_clickteam_install_creator_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_clickteam_install_creator *archive;
    cic_stream *stream;
    uint64_t sum = 0U;
    uint32_t i;
    if (!format || !format->device || format->base_address < 0) return false;
    stream = (cic_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!cic_walk(format->device, format->base_address, &stream->scan) || !cic_read_list(format->device, &stream->scan, &stream->list, pd)) {
        cic_stream_free(stream);
        return false;
    }
    for (i = 0U; i < stream->list.count; ++i) sum += stream->list.entries[i].unpacked;
    archive = (xx_clickteam_install_creator *)format;
    archive->generation = stream->scan.generation;
    archive->list_version = stream->list.version;
    archive->overlay_offset = stream->scan.overlay;
    archive->data_offset = stream->scan.region;
    archive->data_size = stream->scan.region_size;
    archive->number_of_records = stream->list.count;
    archive->unpacked_size = sum;
    format->number_of_archive_records = stream->list.count;
    format->format_size = stream->scan.region + stream->scan.region_size - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    cic_stream_free(stream);
    return true;
}

int64_t xx_clickteam_install_creator_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_clickteam_install_creator_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_clickteam_install_creator_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_clickteam_install_creator_handle_base_info(format, pd))
               ? ((xx_clickteam_install_creator *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_clickteam_install_creator_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    cic_stream *stream;
    xx_archive_record_state *state;
    if (!format || !format->device || format->base_address < 0) return NULL;
    stream = (cic_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!cic_walk(format->device, format->base_address, &stream->scan) || !cic_read_list(format->device, &stream->scan, &stream->list, pd) || stream->list.count == 0U) {
        cic_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cic_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = cic_stream_free;
    state->total_records = (int64_t)stream->list.count;
    if (!cic_copy_options(&state->options, options) || !cic_set_record(&state->current_record, format->device, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_clickteam_install_creator_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_clickteam_install_creator_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    cic_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (cic_stream *)state->internal_state) || stream->index + 1U >= stream->list.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!cic_set_record(&state->current_record, format->device, stream)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_clickteam_install_creator_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    cic_stream *stream;
    const cic_entry *e;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (cic_stream *)state->internal_state) || stream->index >= stream->list.count ||
        cic_stopped(pd))
        return false;
    e = &stream->list.entries[stream->index];
    path_option = cic_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return cic_unpack_entry(format->device, &stream->scan, &stream->list, e, NULL, pd);
    if (!e->safe || e->shared) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", e->name) : xx_str_concat(base, e->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = cic_unpack_entry(format->device, &stream->scan, &stream->list, e, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_clickteam_install_creator_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
