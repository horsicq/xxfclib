/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * gBurner GBI disc images (the PowerISO DAA container with a "GBI"
 * signature).  xx_gbi.h carries the field table.
 *
 * Written from the container layout.  The GPL references (libmirage
 * filter-daa, daa2iso) were used to understand the layout only; every
 * behaviour below was confirmed against daa2iso 0.1.7e on generated images:
 *   - the GBI chunk-table scramble is always applied; when header bit 27 is
 *     also set the DAA scramble (keyed by the image size in 2048-byte
 *     sectors) is undone after it;
 *   - with header bit 17 the table's bit fields are xored with a per-entry
 *     key built from the entry index (libmirage never advances that index;
 *     daa2iso does, and only the advancing form reproduces its output);
 *   - header bits 23-24 (xor 1 for GBI) select a permutation of the deflate
 *     block-type codes.  The value written for a real type t is
 *     table[t] with tables {0,1,2} {1,2,0} {0,2,1} {1,0,2}.  The library's
 *     inflater cannot take such a permutation, so this file carries a small
 *     inflater of its own for those chunks.
 *
 * Detection is the 16-byte signature plus the header CRC-32; the chunk
 * table is only read by handle_base_info / unpack.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gbi/xx_gbi.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: xxfc_defs.h is shared and not edited from here;
 * the alias macro next to the enumerator switches this over once GBI is
 * registered. */
#ifdef GBI
#define XX_GBI_FILE_TYPE XX_FILE_TYPE_GBI
#else
#define XX_GBI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GBI_HEADER_SIZE 76U
#define GBI_CRC_SPAN 72U
#define GBI_VERSION_1 0x100U
#define GBI_VERSION_2 0x110U
/* Version 0x110 can express at most 0xFFF * 16 KiB; version 0x100 is held
 * to the same ceiling. */
#define GBI_MAX_CHUNK ((uint32_t)0xFFFU << 14U)
/* The table sits below the 24-bit data offset in version 0x110. */
#define GBI_MAX_TABLE ((uint32_t)16U * 1024U * 1024U)
#define GBI_MAX_DESCRIPTORS 4096U
#define GBI_IMAGE_NAME "image.iso"

#define GBI_FLAG_TABLE_PACKED 0x4000U
#define GBI_FLAG_BITS_SCRAMBLED 0x20000U
#define GBI_FLAG_TABLE_DAA 0x8000000U

#define GBI_DESC_SPLIT 2U
#define GBI_DESC_ENCRYPTION 3U

enum { GBI_CHUNK_LZMA = 0, GBI_CHUNK_DEFLATE = 1, GBI_CHUNK_STORED = 2 };

typedef struct gbi_layout_s {
    int64_t available;  /**< bytes from base_address to end of device */
    uint32_t version;
    uint32_t size_field; /**< raw u32 at 0x24 */
    uint32_t chunk_size;
    uint64_t image_size;
    uint32_t table_offset;
    uint32_t data_offset;
    uint32_t crc;
    uint8_t lzma_filter;
    uint8_t lzma_props[5];
    unsigned bits_type;
    unsigned bits_length;
    uint8_t btype_real[4]; /**< code read from the stream -> real type */
    bool bits_scrambled;
    bool table_daa;
    bool encrypted;
    bool multi_volume;
    bool unsupported;
    uint64_t chunk_count;
    int64_t format_size;
    uint32_t max_packed;
    bool table_checked;
} gbi_layout;

typedef struct gbi_stream_s {
    gbi_layout layout;
    size_t index;
    size_t count;
} gbi_stream;

static bool gbi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool gbi_write_all(xx_io_device *output, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(output, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ------------------------------------------------------------ header */

static bool gbi_read_header(Abstractformat *format, gbi_layout *layout) {
    uint8_t h[GBI_HEADER_SIZE];
    int64_t total;
    size_t i;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address ||
        total - format->base_address < (int64_t)GBI_HEADER_SIZE)
        return false;
    if (!gbi_read_at(format->device, format->base_address, h, sizeof(h)))
        return false;
    if (h[0] != 'G' || h[1] != 'B' || h[2] != 'I') return false;
    for (i = 3U; i < 16U; ++i)
        if (h[i] != 0U) return false;
    xx_mem_zero(layout, sizeof(*layout));
    layout->available = total - format->base_address;
    layout->version = xx_data_get_u32(h + 0x14, 4, 0, false);
    if (layout->version != GBI_VERSION_1 && layout->version != GBI_VERSION_2)
        return false;
    layout->crc = xx_data_get_u32(h + 0x48, 4, 0, false);
    if (xx_crc32_calc(0U, h, GBI_CRC_SPAN) != layout->crc) return false;
    layout->table_offset = xx_data_get_u32(h + 0x10, 4, 0, false);
    layout->data_offset = xx_data_get_u32(h + 0x18, 4, 0, false);
    layout->size_field = xx_data_get_u32(h + 0x24, 4, 0, false);
    layout->image_size = xx_data_get_u64(h + 0x28, 8, 0, false);
    if (layout->version == GBI_VERSION_2) {
        unsigned swap, real;
        static const uint8_t tables[4][3] = {
            {0, 1, 2}, {1, 2, 0}, {0, 2, 1}, {1, 0, 2}};
        layout->data_offset &= 0x00FFFFFFU;
        layout->chunk_size = (layout->size_field & 0xFFFU) << 14U;
        layout->bits_scrambled =
            (layout->size_field & GBI_FLAG_BITS_SCRAMBLED) != 0U;
        layout->table_daa = (layout->size_field & GBI_FLAG_TABLE_DAA) != 0U;
        if (layout->size_field & GBI_FLAG_TABLE_PACKED)
            layout->unsupported = true;
        swap = ((layout->size_field >> 23U) & 3U) ^ 1U;
        layout->btype_real[0] = layout->btype_real[1] =
            layout->btype_real[2] = layout->btype_real[3] = 0xFFU;
        for (real = 0U; real < 3U; ++real)
            layout->btype_real[tables[swap][real]] = (uint8_t)real;
        layout->bits_type = h[0x3D] & 7U;
        layout->bits_length = h[0x3D] >> 3U;
        if (layout->bits_length != 0U) {
            layout->bits_length += 10U;
        } else {
            uint32_t v = layout->chunk_size;
            while (v > layout->bits_type) {
                ++layout->bits_length;
                v >>= 1U;
            }
        }
        if (layout->bits_length > 32U || layout->bits_type > 2U)
            layout->unsupported = true;
        layout->lzma_filter = h[0x3E];
        xx_rt_memcpy(layout->lzma_props, h + 0x3F, 5U);
        if (layout->lzma_filter > 1U) layout->unsupported = true;
    } else {
        layout->chunk_size = layout->size_field;
        layout->btype_real[0] = 0U;
        layout->btype_real[1] = 1U;
        layout->btype_real[2] = 2U;
        layout->btype_real[3] = 0xFFU;
    }
    if (layout->chunk_size == 0U || layout->chunk_size > GBI_MAX_CHUNK ||
        layout->image_size == 0U || layout->image_size > (UINT64_C(1) << 52U))
        return false;
    if (layout->table_offset < GBI_HEADER_SIZE ||
        layout->data_offset <= layout->table_offset ||
        (int64_t)layout->data_offset > layout->available ||
        layout->data_offset - layout->table_offset > GBI_MAX_TABLE)
        return false;
    layout->chunk_count =
        (layout->image_size - 1U) / layout->chunk_size + 1U;
    return true;
}

/* Walk the descriptors between the header and the chunk table. */
static bool gbi_read_descriptors(Abstractformat *format, gbi_layout *layout) {
    uint32_t position = GBI_HEADER_SIZE;
    unsigned seen = 0U;
    while (position < layout->table_offset) {
        uint8_t d[16];
        uint32_t type, length;
        if (++seen > GBI_MAX_DESCRIPTORS ||
            layout->table_offset - position < 8U ||
            !gbi_read_at(format->device, format->base_address + position, d,
                         8U))
            return false;
        type = xx_data_get_u32(d, 4, 0, false);
        length = xx_data_get_u32(d + 4, 4, 0, false);
        if (length < 8U || length > layout->table_offset - position)
            return false;
        if (type == GBI_DESC_SPLIT && length >= 16U) {
            if (!gbi_read_at(format->device,
                             format->base_address + position + 8U, d + 8, 8U))
                return false;
            if (xx_data_get_u32(d + 8, 4, 0, false) > 1U) layout->multi_volume = true;
        } else if (type == GBI_DESC_ENCRYPTION) {
            layout->encrypted = true;
        }
        position += length;
    }
    return true;
}

/* ------------------------------------------------------------ chunk table */

typedef struct gbi_table_s {
    uint8_t *data;
    uint32_t size;
    uint64_t capacity; /**< entries the table can hold */
    uint64_t bit_position;
    uint64_t index;
} gbi_table;

static void gbi_table_free(gbi_table *table) {
    if (table->data) xx_mem_free(table->data);
    table->data = NULL;
}

static bool gbi_table_load(Abstractformat *format, const gbi_layout *layout,
                           gbi_table *table) {
    uint32_t i;
    uint8_t key, crc8;
    xx_mem_zero(table, sizeof(*table));
    table->size = layout->data_offset - layout->table_offset;
    if (layout->version == GBI_VERSION_1) {
        table->capacity = table->size / 3U;
    } else {
        unsigned width = layout->bits_type + layout->bits_length;
        if (width == 0U) return false;
        table->capacity = ((uint64_t)table->size * 8U) / width;
    }
    if (table->capacity < layout->chunk_count) return false;
    table->data = (uint8_t *)xx_mem_alloc(table->size + 8U);
    if (!table->data) return false;
    xx_mem_zero(table->data + table->size, 8U);
    if (!gbi_read_at(format->device,
                     format->base_address + layout->table_offset, table->data,
                     table->size)) {
        gbi_table_free(table);
        return false;
    }
    key = (uint8_t)(table->size / 4U);
    crc8 = (uint8_t)layout->crc;
    for (i = 0U; i < table->size; ++i)
        table->data[i] = (uint8_t)((uint8_t)(table->data[i] - crc8) ^ key);
    if (layout->version == GBI_VERSION_2 && layout->table_daa) {
        uint64_t sectors = layout->image_size / 2048U;
        uint8_t step = (uint8_t)(sectors >> 8U);
        uint8_t value = (uint8_t)sectors;
        for (i = 0U; i < table->size; ++i) {
            table->data[i] = (uint8_t)(table->data[i] - value);
            value = (uint8_t)(value + step);
        }
    }
    return true;
}

static uint32_t gbi_table_bits(const gbi_table *table, uint64_t position,
                               unsigned count) {
    uint64_t value = 0U;
    unsigned got = 0U;
    while (got < count) {
        uint64_t byte = position >> 3U;
        unsigned shift = (unsigned)(position & 7U);
        unsigned take = 8U - shift;
        if (take > count - got) take = count - got;
        value |= (uint64_t)((table->data[byte] >> shift) &
                            ((1U << take) - 1U))
                 << got;
        got += take;
        position += take;
    }
    return (uint32_t)value;
}

/* Next entry: packed length and kind.  The caller has checked the index
 * against the table capacity. */
static bool gbi_table_next(const gbi_layout *layout, gbi_table *table,
                           uint32_t *packed, int *kind) {
    if (table->index >= table->capacity) return false;
    if (layout->version == GBI_VERSION_1) {
        const uint8_t *e = table->data + table->index * 3U;
        *packed = ((uint32_t)e[0] << 16U) | ((uint32_t)e[2] << 8U) |
                  (uint32_t)e[1];
        *kind = GBI_CHUNK_DEFLATE;
    } else {
        static const uint8_t salt[8] = {0x0A, 0x35, 0x2D, 0x3F,
                                        0x08, 0x33, 0x09, 0x15};
        uint32_t length, type, key = 0U;
        uint64_t length_value;
        if (layout->bits_scrambled)
            key = (uint32_t)((table->index ^ salt[table->index & 7U]) &
                             0xFFU) *
                  UINT32_C(0x01010101);
        length = gbi_table_bits(table, table->bit_position,
                                layout->bits_length) ^
                 key;
        if (layout->bits_length < 32U)
            length &= (UINT32_C(1) << layout->bits_length) - 1U;
        table->bit_position += layout->bits_length;
        type = (gbi_table_bits(table, table->bit_position, layout->bits_type) ^
                key) &
               ((1U << layout->bits_type) - 1U);
        table->bit_position += layout->bits_type;
        length_value = (uint64_t)length + 5U;
        if (length_value > UINT32_MAX) return false;
        *packed = (uint32_t)length_value;
        if (*packed >= layout->chunk_size)
            *kind = GBI_CHUNK_STORED;
        else if (type == 0U)
            *kind = GBI_CHUNK_LZMA;
        else if (type == 1U)
            *kind = GBI_CHUNK_DEFLATE;
        else
            return false;
    }
    ++table->index;
    return true;
}

static uint32_t gbi_expected(const gbi_layout *layout, uint64_t index) {
    uint64_t start = index * layout->chunk_size;
    uint64_t left = layout->image_size - start;
    return left < layout->chunk_size ? (uint32_t)left : layout->chunk_size;
}

/* Validate every needed entry and measure the container. */
static bool gbi_check_table(Abstractformat *format, gbi_layout *layout,
                            xx_pd_struct *pd) {
    gbi_table table;
    uint64_t index, offset = 0U;
    int64_t room = layout->available - (int64_t)layout->data_offset;
    if (!gbi_table_load(format, layout, &table)) return false;
    layout->max_packed = 0U;
    for (index = 0U; index < layout->chunk_count; ++index) {
        uint32_t packed = 0U, expected = gbi_expected(layout, index);
        int kind = 0;
        if ((index & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd)) break;
        if (!gbi_table_next(layout, &table, &packed, &kind) || packed == 0U ||
            packed > layout->chunk_size + 4U ||
            (kind == GBI_CHUNK_STORED && packed - 4U != expected)) {
            gbi_table_free(&table);
            return false;
        }
        if (packed > layout->max_packed) layout->max_packed = packed;
        offset += packed;
        if (!layout->multi_volume && offset > (uint64_t)room) {
            gbi_table_free(&table);
            return false;
        }
    }
    gbi_table_free(&table);
    if (index != layout->chunk_count) return false;
    layout->format_size = layout->multi_volume
                              ? layout->available
                              : (int64_t)layout->data_offset + (int64_t)offset;
    layout->table_checked = true;
    return true;
}

static bool gbi_parse(Abstractformat *format, gbi_layout *layout, bool deep,
                      xx_pd_struct *pd) {
    if (!gbi_read_header(format, layout)) return false;
    if (!deep) return true;
    if (!gbi_read_descriptors(format, layout)) return false;
    if (layout->unsupported) {
        layout->format_size = layout->available;
        return true;
    }
    return gbi_check_table(format, layout, pd);
}

/* ------------------------------------------------------------ inflate
 *
 * A plain RFC 1951 decoder whose only difference from the standard is the
 * block-type lookup.  One chunk in, one chunk out, all in memory. */

typedef struct gbi_bits_s {
    const uint8_t *in;
    size_t size;
    size_t position;
    uint32_t buffer;
    unsigned count;
} gbi_bits;

static bool gbi_need(gbi_bits *s, unsigned n, uint32_t *value) {
    while (s->count < n) {
        if (s->position >= s->size) return false;
        s->buffer |= (uint32_t)s->in[s->position++] << s->count;
        s->count += 8U;
    }
    *value = s->buffer & ((UINT32_C(1) << n) - 1U);
    s->buffer >>= n;
    s->count -= n;
    return true;
}

#define GBI_MAX_BITS 15U

typedef struct gbi_huff_s {
    uint16_t count[GBI_MAX_BITS + 1U];
    uint16_t symbol[320];
} gbi_huff;

static bool gbi_huff_build(gbi_huff *h, const uint8_t *lengths, unsigned n) {
    uint16_t offsets[GBI_MAX_BITS + 1U];
    int left = 1;
    unsigned i;
    xx_mem_zero(h->count, sizeof(h->count));
    for (i = 0U; i < n; ++i) {
        if (lengths[i] > GBI_MAX_BITS) return false;
        h->count[lengths[i]]++;
    }
    for (i = 1U; i <= GBI_MAX_BITS; ++i) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0) return false;
    }
    offsets[1] = 0U;
    for (i = 1U; i < GBI_MAX_BITS; ++i)
        offsets[i + 1U] = (uint16_t)(offsets[i] + h->count[i]);
    for (i = 0U; i < n; ++i)
        if (lengths[i] != 0U) h->symbol[offsets[lengths[i]]++] = (uint16_t)i;
    return true;
}

static int gbi_huff_decode(gbi_bits *s, const gbi_huff *h) {
    int code = 0, first = 0, index = 0;
    unsigned length;
    for (length = 1U; length <= GBI_MAX_BITS; ++length) {
        uint32_t bit;
        int count;
        if (!gbi_need(s, 1U, &bit)) return -1;
        code |= (int)bit;
        count = h->count[length];
        if (code - first < count) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t gbi_len_base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t gbi_len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                          1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                          4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t gbi_dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,   97,   129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t gbi_dist_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                           4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                           9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static bool gbi_inflate_codes(gbi_bits *s, const gbi_huff *lit,
                              const gbi_huff *dist, uint8_t *out,
                              size_t capacity, size_t *written) {
    size_t at = *written;
    for (;;) {
        int symbol = gbi_huff_decode(s, lit);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (at >= capacity) return false;
            out[at++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            break;
        } else {
            uint32_t extra, length, distance;
            symbol -= 257;
            if (symbol >= 29) return false;
            if (!gbi_need(s, gbi_len_extra[symbol], &extra)) return false;
            length = gbi_len_base[symbol] + extra;
            symbol = gbi_huff_decode(s, dist);
            if (symbol < 0 || symbol >= 30) return false;
            if (!gbi_need(s, gbi_dist_extra[symbol], &extra)) return false;
            distance = gbi_dist_base[symbol] + extra;
            if (distance > at || length > capacity - at) return false;
            while (length--) {
                out[at] = out[at - distance];
                ++at;
            }
        }
    }
    *written = at;
    return true;
}

static bool gbi_inflate(const uint8_t *in, size_t size, const uint8_t *btype,
                        uint8_t *out, size_t capacity, size_t *written) {
    gbi_bits s;
    gbi_huff *lit, *dist;
    uint8_t lengths[320];
    uint32_t last = 0U;
    bool ok = false;
    lit = (gbi_huff *)xx_mem_alloc(2U * sizeof(gbi_huff));
    if (!lit) return false;
    dist = lit + 1;
    xx_mem_zero(&s, sizeof(s));
    s.in = in;
    s.size = size;
    *written = 0U;
    do {
        uint32_t code, real;
        if (!gbi_need(&s, 1U, &last) || !gbi_need(&s, 2U, &code)) goto done;
        real = btype[code];
        if (real == 0U) {
            size_t length;
            s.buffer = 0U;
            s.count = 0U;
            if (size - s.position < 4U) goto done;
            length = (size_t)in[s.position] | ((size_t)in[s.position + 1U] << 8U);
            if ((uint16_t)~length !=
                (uint16_t)((size_t)in[s.position + 2U] |
                           ((size_t)in[s.position + 3U] << 8U)))
                goto done;
            s.position += 4U;
            if (length > size - s.position || length > capacity - *written)
                goto done;
            xx_rt_memcpy(out + *written, in + s.position, length);
            s.position += length;
            *written += length;
        } else if (real == 1U) {
            unsigned i;
            for (i = 0U; i < 144U; ++i) lengths[i] = 8U;
            for (; i < 256U; ++i) lengths[i] = 9U;
            for (; i < 280U; ++i) lengths[i] = 7U;
            for (; i < 288U; ++i) lengths[i] = 8U;
            if (!gbi_huff_build(lit, lengths, 288U)) goto done;
            for (i = 0U; i < 30U; ++i) lengths[i] = 5U;
            if (!gbi_huff_build(dist, lengths, 30U) ||
                !gbi_inflate_codes(&s, lit, dist, out, capacity, written))
                goto done;
        } else if (real == 2U) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,
                                              6,  10, 5,  11, 4, 12, 3,
                                              13, 2,  14, 1,  15};
            uint32_t hlit, hdist, hclen, value;
            unsigned i, n;
            if (!gbi_need(&s, 5U, &hlit) || !gbi_need(&s, 5U, &hdist) ||
                !gbi_need(&s, 4U, &hclen))
                goto done;
            hlit += 257U;
            hdist += 1U;
            hclen += 4U;
            if (hlit > 286U || hdist > 30U) goto done;
            xx_mem_zero(lengths, sizeof(lengths));
            for (i = 0U; i < hclen; ++i) {
                if (!gbi_need(&s, 3U, &value)) goto done;
                lengths[order[i]] = (uint8_t)value;
            }
            if (!gbi_huff_build(lit, lengths, 19U)) goto done;
            n = 0U;
            while (n < hlit + hdist) {
                int symbol = gbi_huff_decode(&s, lit);
                uint32_t repeat;
                uint8_t fill = 0U;
                if (symbol < 0) goto done;
                if (symbol < 16) {
                    lengths[n++] = (uint8_t)symbol;
                    continue;
                }
                if (symbol == 16) {
                    if (n == 0U || !gbi_need(&s, 2U, &repeat)) goto done;
                    fill = lengths[n - 1U];
                    repeat += 3U;
                } else if (symbol == 17) {
                    if (!gbi_need(&s, 3U, &repeat)) goto done;
                    repeat += 3U;
                } else {
                    if (!gbi_need(&s, 7U, &repeat)) goto done;
                    repeat += 11U;
                }
                if (repeat > hlit + hdist - n) goto done;
                while (repeat--) lengths[n++] = fill;
            }
            if (lengths[256] == 0U) goto done;
            {
                uint8_t distance_lengths[30];
                xx_rt_memcpy(distance_lengths, lengths + hlit, hdist);
                if (!gbi_huff_build(lit, lengths, hlit) ||
                    !gbi_huff_build(dist, distance_lengths, hdist) ||
                    !gbi_inflate_codes(&s, lit, dist, out, capacity, written))
                    goto done;
            }
        } else {
            goto done;
        }
    } while (!last);
    ok = true;
done:
    xx_mem_free(lit);
    return ok;
}

/* ------------------------------------------------------------ x86 BCJ
 *
 * Ported from src/formats/lzma86/xx_lzma86.c (xxfclib, MIT), which follows
 * the public-domain LZMA SDK filter.  One call converts a whole chunk:
 * start address 0, fresh state, the last four bytes left as they are. */

static bool gbi_x86_ms_byte(uint8_t value) {
    return ((uint8_t)(value + 1U) & 0xFEU) == 0U;
}

static void gbi_bcj_decode(uint8_t *data, size_t size) {
    size_t pos = 0U, limit;
    uint32_t mask = 0U, ip = 5U;
    if (size < 5U) return;
    limit = size - 4U;
    while (pos < limit) {
        size_t candidate = pos, distance;
        uint32_t value, current;
        while (candidate < limit && (data[candidate] & 0xFEU) != 0xE8U)
            ++candidate;
        distance = candidate - pos;
        if (candidate >= limit) break;
        pos = candidate;
        if (distance > 2U) {
            mask = 0U;
        } else {
            mask >>= (unsigned)distance;
            if (mask != 0U &&
                (mask > 4U || mask == 3U ||
                 gbi_x86_ms_byte(data[candidate + (mask >> 1U) + 1U]))) {
                mask = (mask >> 1U) | 4U;
                ++pos;
                continue;
            }
        }
        if (!gbi_x86_ms_byte(data[candidate + 4U])) {
            mask = (mask >> 1U) | 4U;
            ++pos;
            continue;
        }
        value = xx_data_get_u32(data + candidate + 1U, 4, 0, false);
        current = ip + (uint32_t)candidate;
        value -= current;
        if (mask != 0U) {
            unsigned shift = (mask & 6U) << 2U;
            if (gbi_x86_ms_byte((uint8_t)(value >> shift))) {
                value ^= ((UINT32_C(0x100) << shift) - 1U);
                value -= current;
            }
            mask = 0U;
        }
        data[candidate + 1U] = (uint8_t)value;
        data[candidate + 2U] = (uint8_t)(value >> 8U);
        data[candidate + 3U] = (uint8_t)(value >> 16U);
        data[candidate + 4U] = (uint8_t)(0U - ((value >> 24U) & 1U));
        pos = candidate + 5U;
    }
}

/* ------------------------------------------------------------ decode */

static bool gbi_decode_chunk(const gbi_layout *layout, int kind,
                             const uint8_t *packed, uint32_t packed_size,
                             uint8_t *out, uint32_t expected) {
    size_t written = 0U;
    if (kind == GBI_CHUNK_STORED) {
        if (packed_size < 4U || packed_size - 4U != expected) return false;
        xx_rt_memcpy(out, packed, expected);
        return true;
    }
    if (kind == GBI_CHUNK_DEFLATE) {
        bool identity = layout->btype_real[0] == 0U &&
                        layout->btype_real[1] == 1U &&
                        layout->btype_real[2] == 2U;
        if (identity) {
            if (!xx_deflate_decompress_memory(packed, packed_size, out,
                                              expected, &written, false))
                return false;
        } else if (!gbi_inflate(packed, packed_size, layout->btype_real, out,
                                expected, &written)) {
            return false;
        }
        return written == expected;
    }
    if (kind == GBI_CHUNK_LZMA) {
        /* A chunk never reaches back past its own start, so a dictionary
         * of one chunk is exact and spares the decoder a large one. */
        uint8_t props[5];
        uint32_t dictionary = xx_data_get_u32(layout->lzma_props + 1, 4, 0, false);
        props[0] = layout->lzma_props[0];
        if (dictionary > layout->chunk_size) dictionary = layout->chunk_size;
        props[1] = (uint8_t)dictionary;
        props[2] = (uint8_t)(dictionary >> 8U);
        props[3] = (uint8_t)(dictionary >> 16U);
        props[4] = (uint8_t)(dictionary >> 24U);
        if (!xx_lzma_decompress_memory(packed, packed_size, props, 5U,
                                       (int64_t)expected, out, expected,
                                       &written) ||
            written != expected)
            return false;
        if (layout->lzma_filter == 1U) gbi_bcj_decode(out, expected);
        return true;
    }
    return false;
}

static bool gbi_unpack_to_device(Abstractformat *format,
                                 const gbi_layout *layout,
                                 xx_io_device *destination, xx_pd_struct *pd) {
    gbi_table table;
    uint8_t *packed = NULL, *plain = NULL;
    uint64_t index;
    int64_t offset = format->base_address + (int64_t)layout->data_offset;
    bool result = false;
    if (layout->encrypted || layout->multi_volume || layout->unsupported ||
        !layout->table_checked)
        return false;
    if (!gbi_table_load(format, layout, &table)) return false;
    packed = (uint8_t *)xx_mem_alloc(layout->max_packed ? layout->max_packed
                                                        : 1U);
    plain = (uint8_t *)xx_mem_alloc(layout->chunk_size);
    if (!packed || !plain) goto done;
    for (index = 0U; index < layout->chunk_count; ++index) {
        uint32_t size = 0U, expected = gbi_expected(layout, index);
        int kind = 0;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!gbi_table_next(layout, &table, &size, &kind) ||
            size > layout->max_packed ||
            !gbi_read_at(format->device, offset, packed, size) ||
            !gbi_decode_chunk(layout, kind, packed, size, plain, expected) ||
            !gbi_write_all(destination, plain, expected))
            goto done;
        offset += size;
    }
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    gbi_table_free(&table);
    return result;
}

/* ------------------------------------------------------------ API */

static void gbi_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool gbi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *gbi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool gbi_set_record(Abstractformat *format, xx_archive_record *record,
                           const gbi_layout *layout) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = (int64_t)layout->data_offset;
    record->data_offset = format->base_address + (int64_t)layout->data_offset;
    record->compressed_size =
        layout->format_size - (int64_t)layout->data_offset;
    return xx_archive_record_set_original_name(record, GBI_IMAGE_NAME) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          layout->image_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           layout->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_gbi_init(xx_gbi *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GBI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gbi");
    xx_format_set_extension(&archive->format, "gbi");
    archive->format.check_is_valid = xx_gbi_check_is_valid;
    archive->format.handle_base_info = xx_gbi_handle_base_info;
    archive->format.get_format_size = xx_gbi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gbi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gbi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gbi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gbi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gbi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gbi_free_archive_records_reading;
}

xx_gbi *xx_gbi_create(xx_io_device *device, int64_t base_address) {
    xx_gbi *archive = (xx_gbi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_gbi_init(archive, device, base_address);
    return archive;
}

void xx_gbi_destroy(xx_gbi *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_gbi_free(xx_gbi *archive) {
    if (!archive) return;
    xx_gbi_destroy(archive);
    xx_mem_free(archive);
}

bool xx_gbi_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    gbi_layout layout;
    return gbi_parse(format, &layout, false, pd);
}

bool xx_gbi_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    gbi_layout layout;
    xx_gbi *archive;
    if (!format || !gbi_parse(format, &layout, true, pd)) return false;
    archive = (xx_gbi *)format;
    archive->number_of_records = 1U;
    archive->image_size = layout.image_size;
    archive->version = layout.version;
    archive->chunk_size = layout.chunk_size;
    archive->chunk_count = layout.chunk_count;
    archive->encrypted = layout.encrypted;
    archive->multi_volume = layout.multi_volume;
    archive->unsupported = layout.unsupported;
    format->number_of_archive_records = 1U;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_gbi_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gbi_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_gbi_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gbi_handle_base_info(format, pd))
               ? ((xx_gbi *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_gbi_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    gbi_stream *stream;
    xx_archive_record_state *state;
    gbi_layout layout;
    if (!gbi_parse(format, &layout, true, pd)) return NULL;
    stream = (gbi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->layout = layout;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = gbi_stream_free;
    state->total_records = 1U;
    if (!gbi_copy_options(&state->options, options) ||
        !gbi_set_record(format, &state->current_record, &stream->layout)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_gbi_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gbi_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    gbi_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (gbi_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_gbi_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    gbi_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (gbi_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->layout.encrypted || stream->layout.multi_volume ||
        stream->layout.unsupported)
        return false;
    path_option = gbi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return stream->layout.table_checked;
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
               ? xx_str_concat3(base, "/", GBI_IMAGE_NAME)
               : xx_str_concat(base, GBI_IMAGE_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = gbi_unpack_to_device(format, &stream->layout, destination,
                                      pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_gbi_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
