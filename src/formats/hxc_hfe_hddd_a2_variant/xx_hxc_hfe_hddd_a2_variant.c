/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for HFE images written by the HxC tools in "HDDD A2" mode
 * (hxcfe -conv:HXC_HDDD_A2_HFE).  The container is an ordinary HFE v1 layout
 * ("HXCPICFE", revision 0, 512-byte header, track lookup table, cylinders
 * with the two sides interleaved in 256-byte chunks), but the bit cells run
 * at twice the rate: every pair of standard-rate cells (c, x) is stored as
 * the four cells c,0,1,0.  Only the first cell of each pair survives, which
 * is lossless for Apple II GCR (whose second cell is always empty) and keeps
 * only the clock cells of an MFM source.
 *
 * Structure recognised: every stored track byte has the form 0100x10x in
 * LSB-first cell order, i.e. (byte & 0xEE) == 0x44.
 *
 * Members:
 *   image.hfe  the same tracks folded back to a standard-rate HFE v1 file
 *              (bit rate halved, HDDD GCR encodings 8/9 mapped to 6/7);
 *   image.do   when Apple II 6-and-2 GCR sectors decode, a DOS 3.3 ordered
 *              sector image (tracks x 16 x 256, missing sectors zero).
 *
 * Written from the observed structure of hxcfe output; no code taken from
 * HxC (GPL).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hxc_hfe_hddd_a2_variant/xx_hxc_hfe_hddd_a2_variant.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef HXC_HFE_HDDD_A2_VARIANT
#define XX_HXC_HFE_HDDD_A2_VARIANT_FILE_TYPE XX_FILE_TYPE_HXC_HFE_HDDD_A2_VARIANT
#else
#define XX_HXC_HFE_HDDD_A2_VARIANT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define A2_HEADER_SIZE 512
#define A2_BLOCK 512
#define A2_CHUNK 256
#define A2_MAX_FILE (24U * 1024U * 1024U)
#define A2_SECTORS 16
#define A2_SECTOR_SIZE 256
#define A2_MAX_MEMBERS 2U

typedef struct a2_member_s {
    const char *name;
    uint32_t kind; /* 0 = standard HFE, 1 = DOS-order sector image */
    uint64_t unpacked_size;
} a2_member;

typedef struct a2_stream_s {
    a2_member items[A2_MAX_MEMBERS];
    size_t count;
    size_t index;
    int64_t archive_size;
} a2_stream;

typedef struct a2_geometry_s {
    int32_t tracks;
    int32_t sides;
    int64_t lut_offset;
    uint8_t encoding;
    uint16_t bitrate;
    uint32_t lut_blocks;
    uint32_t data_blocks;
    uint64_t plain_size;
} a2_geometry;

static uint16_t a2_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static void a2_put16(uint8_t *b, uint32_t value) {
    b[0] = (uint8_t)(value & 0xffU);
    b[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static bool a2_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Fixed header fields only; cheap enough for a detector. */
static bool a2_header(const uint8_t *file, size_t size, a2_geometry *out) {
    static const uint8_t magic[8] = {'H', 'X', 'C', 'P', 'I', 'C', 'F', 'E'};
    if (size < A2_HEADER_SIZE || xx_rt_memcmp(file, magic, 8U) != 0 ||
        file[8] != 0U || file[9] == 0U || (file[10] != 1U && file[10] != 2U))
        return false;
    xx_mem_zero(out, sizeof(*out));
    out->tracks = (int32_t)file[9];
    out->sides = (int32_t)file[10];
    out->encoding = file[11];
    out->bitrate = a2_le16(file + 0x0c);
    out->lut_offset = (int64_t)a2_le16(file + 0x12) * A2_BLOCK;
    if (out->lut_offset < A2_HEADER_SIZE ||
        out->lut_offset > (int64_t)size ||
        (int64_t)out->tracks * 4 > (int64_t)size - out->lut_offset)
        return false;
    return true;
}

/* One LUT entry: the cylinder's byte offset and its stored length.  The
 * stored data is padded to whole 512-byte blocks, and that padded extent must
 * lie inside the file because side 1 of the last chunk sits past `length`. */
static bool a2_track(const uint8_t *file, size_t size, const a2_geometry *g,
                     int32_t track, int64_t *offset, uint32_t *side_length) {
    const uint8_t *entry = file + g->lut_offset + (int64_t)track * 4;
    const int64_t start = (int64_t)a2_le16(entry) * A2_BLOCK;
    const uint32_t length = a2_le16(entry + 2);
    const int64_t padded =
        (((int64_t)length + A2_BLOCK - 1) / A2_BLOCK) * A2_BLOCK;
    if (length < 8U || start < A2_HEADER_SIZE || start > (int64_t)size ||
        padded > (int64_t)size - start)
        return false;
    *offset = start;
    *side_length = length / 2U;
    return true;
}

static uint8_t a2_side_byte(const uint8_t *file, int64_t offset, int32_t side,
                            uint32_t index) {
    return file[offset + (int64_t)(index / A2_CHUNK) * A2_BLOCK +
                (int64_t)side * A2_CHUNK + (int64_t)(index % A2_CHUNK)];
}

/* Every track, every present side: each byte must be two c010 groups.  A
 * track made only of 0x55 is also what a blank standard HFE looks like, so
 * the encoding byte must be an HDDD GCR one unless some other value shows. */
static bool a2_validate(const uint8_t *file, size_t size, a2_geometry *g) {
    int32_t track;
    bool varied = false;
    uint64_t blocks = 0U;
    g->lut_blocks = (uint32_t)(((uint32_t)g->tracks * 4U + A2_BLOCK - 1U) /
                               A2_BLOCK);
    for (track = 0; track < g->tracks; ++track) {
        int64_t offset;
        uint32_t side_length, index, plain_length;
        int32_t side;
        if (!a2_track(file, size, g, track, &offset, &side_length))
            return false;
        for (side = 0; side < g->sides; ++side) {
            for (index = 0U; index < side_length; ++index) {
                const uint8_t value = a2_side_byte(file, offset, side, index);
                if ((value & 0xeeU) != 0x44U) return false;
                if (value != 0x55U) varied = true;
            }
        }
        plain_length = (side_length / 2U) * 2U;
        blocks += (plain_length + A2_BLOCK - 1U) / A2_BLOCK;
    }
    if (!varied && g->encoding != 8U && g->encoding != 9U) return false;
    if (1U + (uint64_t)g->lut_blocks + blocks > 0xffffU) return false;
    g->data_blocks = (uint32_t)blocks;
    g->plain_size = (1U + (uint64_t)g->lut_blocks + blocks) * A2_BLOCK;
    return true;
}

/* Fold the double-rate cells back: HDDD byte bits 0 and 4 carry the kept
 * cells, which land on the even cells of the standard-rate byte. */
static void a2_build_plain(const uint8_t *file, size_t size,
                           const a2_geometry *g, uint8_t *out) {
    uint32_t block = 1U + g->lut_blocks;
    int32_t track;
    xx_mem_zero(out, (size_t)g->plain_size);
    xx_mem_copy(out, file, A2_HEADER_SIZE);
    a2_put16(out + 0x0c, (uint32_t)g->bitrate / 2U);
    if (out[0x0b] == 8U || out[0x0b] == 9U) out[0x0b] = (uint8_t)(out[0x0b] - 2U);
    if (out[0x17] == 8U || out[0x17] == 9U) out[0x17] = (uint8_t)(out[0x17] - 2U);
    if (out[0x19] == 8U || out[0x19] == 9U) out[0x19] = (uint8_t)(out[0x19] - 2U);
    a2_put16(out + 0x12, 1U);
    xx_rt_memset(out + A2_BLOCK, 0xff, (size_t)g->lut_blocks * A2_BLOCK);
    for (track = 0; track < g->tracks; ++track) {
        int64_t offset;
        uint32_t side_length, plain_side, j;
        int32_t side;
        uint8_t *target = out + (size_t)block * A2_BLOCK;
        int64_t padded;
        if (!a2_track(file, size, g, track, &offset, &side_length)) return;
        /* a2_track guaranteed this extent lies inside the file */
        padded = (((int64_t)a2_le16(file + g->lut_offset + (int64_t)track * 4 +
                                    2) +
                   A2_BLOCK - 1) / A2_BLOCK) * A2_BLOCK;
        plain_side = side_length / 2U;
        a2_put16(out + A2_BLOCK + (size_t)track * 4U, block);
        a2_put16(out + A2_BLOCK + (size_t)track * 4U + 2U, plain_side * 2U);
        for (side = 0; side < g->sides; ++side) {
            for (j = 0U; j < plain_side; ++j) {
                const uint8_t h0 = a2_side_byte(file, offset, side, 2U * j);
                const uint8_t h1 = a2_side_byte(file, offset, side, 2U * j + 1U);
                const uint8_t value =
                    (uint8_t)((h0 & 1U) | (((h0 >> 4U) & 1U) << 2U) |
                              ((h1 & 1U) << 4U) | (((h1 >> 4U) & 1U) << 6U));
                target[(size_t)(j / A2_CHUNK) * A2_BLOCK +
                       (size_t)side * A2_CHUNK + (size_t)(j % A2_CHUNK)] = value;
            }
            /* The tail of a side's last chunk is outside the stored length;
             * hxcfe still fills it, so fold whatever the padded source holds
             * there and use 0x55 (hxcfe's filler) past the source. */
            for (; j % A2_CHUNK != 0U; ++j) {
                uint8_t value = 0x55U;
                const uint32_t i0 = 2U * j;
                const int64_t s0 = (int64_t)(i0 / A2_CHUNK) * A2_BLOCK +
                                   (int64_t)side * A2_CHUNK +
                                   (int64_t)(i0 % A2_CHUNK);
                if (s0 + 1 < padded && (i0 % A2_CHUNK) + 1U < A2_CHUNK) {
                    const uint8_t h0 = file[offset + s0];
                    const uint8_t h1 = file[offset + s0 + 1];
                    value = (uint8_t)((h0 & 1U) | (((h0 >> 4U) & 1U) << 2U) |
                                      ((h1 & 1U) << 4U) |
                                      (((h1 >> 4U) & 1U) << 6U));
                }
                target[(size_t)(j / A2_CHUNK) * A2_BLOCK +
                       (size_t)side * A2_CHUNK + (size_t)(j % A2_CHUNK)] = value;
            }
        }
        block += (plain_side * 2U + A2_BLOCK - 1U) / A2_BLOCK;
    }
}

/* Apple II 6-and-2 disk nibbles, index = 6-bit value. */
static const uint8_t a2_nibbles[64] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac, 0xad,
    0xae, 0xaf, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3, 0xd6, 0xd7, 0xd9, 0xda,
    0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf9, 0xfa, 0xfb,
    0xfc, 0xfd, 0xfe, 0xff};

/* DOS 3.3 file order: physical sector -> logical sector. */
static const uint8_t a2_phys_to_dos[16] = {0, 7, 14, 6, 13, 5, 12, 4,
                                           11, 3, 10, 2, 9, 1, 8, 15};

static bool a2_data_field(const uint8_t *nib, const uint8_t *table,
                          uint8_t *sector) {
    uint8_t buffer[342];
    uint8_t previous = 0U;
    size_t i;
    for (i = 0U; i < 342U; ++i) {
        const uint8_t value = table[nib[i]];
        if (value == 0xffU) return false;
        previous = (uint8_t)(value ^ previous);
        buffer[i] = previous;
    }
    if (table[nib[342]] == 0xffU || (uint8_t)(table[nib[342]] ^ previous) != 0U)
        return false;
    for (i = 0U; i < 256U; ++i) {
        const uint8_t aux = (uint8_t)((buffer[i % 86U] >> (2U * (i / 86U))) & 3U);
        sector[i] = (uint8_t)((buffer[86U + i] << 2U) | ((aux & 1U) << 1U) |
                              ((aux >> 1U) & 1U));
    }
    return true;
}

/* Walk side 0 of every cylinder as an Apple bit stream (bit k = kept cell k),
 * reading two revolutions so a sector crossing the index is still whole.  A
 * sector is accepted only when both address and data checksums hold.
 * With image == NULL it only counts. */
static uint32_t a2_decode_apple(const uint8_t *file, size_t size,
                                const a2_geometry *g, uint8_t *image) {
    uint8_t table[256];
    uint8_t *nib = NULL;
    uint32_t found = 0U;
    uint32_t capacity = 0U;
    int32_t track;
    size_t i;
    xx_rt_memset(table, 0xff, sizeof(table));
    for (i = 0U; i < 64U; ++i) table[a2_nibbles[i]] = (uint8_t)i;
    for (track = 0; track < g->tracks; ++track) {
        int64_t offset;
        uint32_t side_length, bits, k, count = 0U, first = 0U, n;
        uint8_t shift = 0U;
        if (!a2_track(file, size, g, track, &offset, &side_length)) break;
        bits = side_length * 2U;
        if (capacity < bits / 4U + 4U) {
            if (nib) xx_mem_free(nib);
            capacity = bits / 4U + 4U;
            nib = (uint8_t *)xx_mem_alloc(capacity);
            if (!nib) return found;
        }
        for (k = 0U; k < 2U * bits; ++k) {
            const uint32_t cell = k % bits;
            const uint8_t byte = a2_side_byte(file, offset, 0, cell / 2U);
            const uint8_t bit = (uint8_t)((byte >> ((cell & 1U) ? 4U : 0U)) & 1U);
            if (k == bits) first = count;
            if (shift == 0U && bit == 0U) continue;
            shift = (uint8_t)((shift << 1U) | bit);
            if (shift & 0x80U) {
                if (count < capacity) nib[count++] = shift;
                shift = 0U;
            }
        }
        for (n = 0U; n < first && n + 11U <= count; ++n) {
            uint8_t volume, trk, sec, sum;
            uint32_t d, limit;
            if (nib[n] != 0xd5U || nib[n + 1U] != 0xaaU || nib[n + 2U] != 0x96U)
                continue;
            volume = (uint8_t)(((nib[n + 3U] << 1U) | 1U) & nib[n + 4U]);
            trk = (uint8_t)(((nib[n + 5U] << 1U) | 1U) & nib[n + 6U]);
            sec = (uint8_t)(((nib[n + 7U] << 1U) | 1U) & nib[n + 8U]);
            sum = (uint8_t)(((nib[n + 9U] << 1U) | 1U) & nib[n + 10U]);
            if ((uint8_t)(volume ^ trk ^ sec) != sum || sec >= A2_SECTORS ||
                (int32_t)trk >= g->tracks)
                continue;
            limit = n + 11U + 64U;
            for (d = n + 11U; d < limit && d + 3U + 343U <= count; ++d) {
                uint8_t sector[A2_SECTOR_SIZE];
                if (nib[d] == 0xd5U && nib[d + 1U] == 0xaaU &&
                    nib[d + 2U] == 0x96U)
                    break;
                if (nib[d] != 0xd5U || nib[d + 1U] != 0xaaU ||
                    nib[d + 2U] != 0xadU)
                    continue;
                if (a2_data_field(nib + d + 3U, table, sector)) {
                    ++found;
                    if (image)
                        xx_mem_copy(image + ((size_t)trk * A2_SECTORS +
                                             a2_phys_to_dos[sec]) *
                                                A2_SECTOR_SIZE,
                                    sector, A2_SECTOR_SIZE);
                }
                break;
            }
        }
    }
    if (nib) xx_mem_free(nib);
    return found;
}

static bool a2_load(Abstractformat *format, uint8_t **file, size_t *size) {
    int64_t total;
    uint8_t *data;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < A2_HEADER_SIZE || (uint64_t)total > A2_MAX_FILE) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)total);
    if (!data) return false;
    if (!a2_read_at(format->device, format->base_address, data, (size_t)total)) {
        xx_mem_free(data);
        return false;
    }
    *file = data;
    *size = (size_t)total;
    return true;
}

/* Before loading the whole file: the fixed header fields, then the first
 * 256 bytes of track 0 side 0 must already show the HDDD cell groups, so an
 * ordinary HFE (or garbage behind the magic) is refused after two reads. */
static bool a2_quick(Abstractformat *format) {
    uint8_t header[A2_HEADER_SIZE];
    uint8_t entry[4];
    uint8_t chunk[A2_CHUNK];
    int64_t total, lut, start;
    uint32_t length, index;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < A2_HEADER_SIZE || (uint64_t)total > A2_MAX_FILE ||
        !a2_read_at(format->device, format->base_address, header,
                    sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, "HXCPICFE", 8U) != 0 || header[8] != 0U ||
        header[9] == 0U || (header[10] != 1U && header[10] != 2U))
        return false;
    lut = (int64_t)a2_le16(header + 0x12) * A2_BLOCK;
    if (lut < A2_HEADER_SIZE || lut > total - 4 ||
        !a2_read_at(format->device, format->base_address + lut, entry, 4U))
        return false;
    start = (int64_t)a2_le16(entry) * A2_BLOCK;
    length = a2_le16(entry + 2);
    if (length < 8U || start < A2_HEADER_SIZE || start > total - A2_CHUNK ||
        !a2_read_at(format->device, format->base_address + start, chunk,
                    A2_CHUNK))
        return false;
    if (length / 2U < A2_CHUNK) length = (length / 2U) * 2U;
    else length = 2U * A2_CHUNK;
    for (index = 0U; index < length / 2U; ++index)
        if ((chunk[index] & 0xeeU) != 0x44U) return false;
    return true;
}

static bool a2_parse(Abstractformat *format, bool with_members,
                     a2_stream **result) {
    uint8_t *file = NULL;
    size_t size = 0U;
    a2_geometry g;
    a2_stream *stream;
    uint32_t sectors = 0U;
    if (!result || !a2_quick(format) || !a2_load(format, &file, &size))
        return false;
    if (!a2_header(file, size, &g) || !a2_validate(file, size, &g)) {
        xx_mem_free(file);
        return false;
    }
    if (with_members) sectors = a2_decode_apple(file, size, &g, NULL);
    xx_mem_free(file);
    stream = (a2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items[0].name = "image.hfe";
    stream->items[0].kind = 0U;
    stream->items[0].unpacked_size = g.plain_size;
    stream->count = 1U;
    if (sectors != 0U) {
        stream->items[1].name = "image.do";
        stream->items[1].kind = 1U;
        stream->items[1].unpacked_size =
            (uint64_t)g.tracks * A2_SECTORS * A2_SECTOR_SIZE;
        stream->count = 2U;
    }
    stream->archive_size = (int64_t)size;
    *result = stream;
    return true;
}

static void a2_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool a2_extract(Abstractformat *format, const a2_member *member,
                       uint8_t **plain, size_t *plain_size) {
    uint8_t *file = NULL;
    uint8_t *output;
    size_t size = 0U;
    a2_geometry g;
    if (!a2_load(format, &file, &size)) return false;
    if (!a2_header(file, size, &g) || !a2_validate(file, size, &g)) {
        xx_mem_free(file);
        return false;
    }
    if (member->kind == 0U) {
        if (g.plain_size != member->unpacked_size) {
            xx_mem_free(file);
            return false;
        }
        output = (uint8_t *)xx_mem_alloc((size_t)g.plain_size);
        if (!output) {
            xx_mem_free(file);
            return false;
        }
        a2_build_plain(file, size, &g, output);
    } else {
        const uint64_t image_size =
            (uint64_t)g.tracks * A2_SECTORS * A2_SECTOR_SIZE;
        if (image_size != member->unpacked_size) {
            xx_mem_free(file);
            return false;
        }
        output = (uint8_t *)xx_mem_alloc((size_t)image_size);
        if (!output) {
            xx_mem_free(file);
            return false;
        }
        xx_mem_zero(output, (size_t)image_size);
        if (a2_decode_apple(file, size, &g, output) == 0U) {
            xx_mem_free(output);
            xx_mem_free(file);
            return false;
        }
    }
    xx_mem_free(file);
    *plain = output;
    *plain_size = (size_t)member->unpacked_size;
    return true;
}

static bool a2_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *a2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool a2_set_record(Abstractformat *format, xx_archive_record *record,
                          const a2_stream *stream, const a2_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = A2_HEADER_SIZE;
    /* The LUT scatters the cells over the whole container. */
    record->data_offset = format->base_address;
    record->compressed_size = stream->archive_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->archive_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_hxc_hfe_hddd_a2_variant_init(xx_hxc_hfe_hddd_a2_variant *archive,
                                     xx_io_device *device,
                                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HXC_HFE_HDDD_A2_VARIANT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hfe");
    xx_format_set_extension(&archive->format, "hfe");
    archive->format.check_is_valid = xx_hxc_hfe_hddd_a2_variant_check_is_valid;
    archive->format.handle_base_info =
        xx_hxc_hfe_hddd_a2_variant_handle_base_info;
    archive->format.get_format_size = xx_hxc_hfe_hddd_a2_variant_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hxc_hfe_hddd_a2_variant_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hxc_hfe_hddd_a2_variant_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hxc_hfe_hddd_a2_variant_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hxc_hfe_hddd_a2_variant_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hxc_hfe_hddd_a2_variant_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hxc_hfe_hddd_a2_variant_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_hxc_hfe_hddd_a2_variant *xx_hxc_hfe_hddd_a2_variant_create(
    xx_io_device *device, int64_t base_address) {
    xx_hxc_hfe_hddd_a2_variant *archive =
        (xx_hxc_hfe_hddd_a2_variant *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hxc_hfe_hddd_a2_variant_init(archive, device, base_address);
    return archive;
}

void xx_hxc_hfe_hddd_a2_variant_destroy(xx_hxc_hfe_hddd_a2_variant *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_hxc_hfe_hddd_a2_variant_free(xx_hxc_hfe_hddd_a2_variant *archive) {
    if (!archive) return;
    xx_hxc_hfe_hddd_a2_variant_destroy(archive);
    xx_mem_free(archive);
}

bool xx_hxc_hfe_hddd_a2_variant_check_is_valid(Abstractformat *format,
                                               xx_pd_struct *pd) {
    a2_stream *stream;
    (void)pd;
    if (!a2_parse(format, false, &stream)) return false;
    a2_stream_free(stream);
    return true;
}

bool xx_hxc_hfe_hddd_a2_variant_handle_base_info(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    a2_stream *stream;
    xx_hxc_hfe_hddd_a2_variant *archive;
    (void)pd;
    if (!format || !a2_parse(format, true, &stream)) return false;
    archive = (xx_hxc_hfe_hddd_a2_variant *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    a2_stream_free(stream);
    return true;
}

int64_t xx_hxc_hfe_hddd_a2_variant_get_format_size(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxc_hfe_hddd_a2_variant_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_hxc_hfe_hddd_a2_variant_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxc_hfe_hddd_a2_variant_handle_base_info(format, pd))
               ? ((xx_hxc_hfe_hddd_a2_variant *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_hxc_hfe_hddd_a2_variant_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    a2_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!a2_parse(format, true, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        a2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = a2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!a2_copy_options(&state->options, options) ||
        !a2_set_record(format, &state->current_record, stream,
                       &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_hxc_hfe_hddd_a2_variant_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_hxc_hfe_hddd_a2_variant_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    a2_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (a2_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = a2_set_record(format, &state->current_record, stream,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hxc_hfe_hddd_a2_variant_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    a2_stream *stream;
    a2_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (a2_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!a2_extract(format, member, &plain, &plain_size)) goto done;
    path_option = a2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
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
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
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

void xx_hxc_hfe_hddd_a2_variant_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
