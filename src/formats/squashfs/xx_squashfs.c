/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/*
 * SQUASHFS IS NOT ONE LAYOUT.  A v4-only reader covers only part of the
 * corpus; v3.0 and v2.1 images are common.  Three superblock shapes, all
 * packed:
 *
 *   v4      96 bytes, 64-bit tables from +0x20.
 *   v3     119 bytes (0x77) - 32-bit legacy pointers at 0x08..0x1B,
 *          mkfs_time UNALIGNED at 0x27 and the 64-bit tables only from 0x3F.
 *   v1/v2   63 bytes (0x3F), same prefix but 32-bit table pointers.
 *
 * The inode common prefix is 3 bytes on v1, 4 on v2/v3 and 8 on v4.  Directory
 * headers are 4, 9 and 12 bytes, directory entries 3, 5 and 8 - and BOTH the
 * header's entry count AND the entry's name length are stored BIASED BY ONE.
 *
 * Only inode types 1 DIR, 2 FILE, 8 LDIR and 9 LREG are walked.  Symlinks,
 * devices, fifos and sockets are skipped, which is why the emitted file count
 * sits far below the superblock's inode count.
 *
 * In a block list, bit 0x1000000 means the block is STORED raw and the low 24
 * bits are its on-disk length (v1 packs the same flag as 0x8000 in a u16 and
 * it is rescaled here).  A ZERO entry is a sparse block: it occupies nothing
 * on disk and expands to a block of zeros.
 *
 * COMPRESSION.  v1..v3 carry no compressor field at all, so the first bytes of
 * a block are sniffed instead (0x78 zlib, 0x5D 00 00 LZMA, FD 37 7A 58 5A 00
 * xz); v4 states its compressor and is taken at its word.  The OpenWrt
 * squashfs-lzma fork keeps the 'hsqs' magic while storing LZMA data, which is
 * exactly the case the sniff exists for: its blocks open 5D 00 00 08 00, five
 * props bytes and NO 8-byte size field.
 *
 * Unlike the C++ reference this reader streams through the xx_io device and
 * caches only decompressed metadata blocks, rather than mapping the whole
 * image into memory, and it keeps each member's chunk list as a native array
 * instead of serialising it into a 'SQFD' descriptor blob.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/squashfs/xx_squashfs.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/lzo/xx_lzo.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_SQUASHFS_SUPERBLOCK_SIZE 0x80
#define XX_SQUASHFS_META_MAX 8192U
#define XX_SQUASHFS_MAX_DEPTH 64U
#define XX_SQUASHFS_MAX_MEMBERS 500000U
#define XX_SQUASHFS_MAX_CHUNKS 200000U
#define XX_SQUASHFS_MAX_BLOCK_SIZE (8 * 1024 * 1024)
#define XX_SQUASHFS_MAX_NAME 256U
#define XX_SQUASHFS_MAX_PATH 4096U
#define XX_SQUASHFS_MAX_UNCOMPRESSED INT64_C(0x40000000)
#define XX_SQUASHFS_MAX_INPUT INT64_C(0x20000000)
#define XX_SQUASHFS_CACHE_BUDGET (16U * 1024U * 1024U)
#define XX_SQUASHFS_META_GUARD 0x10000U

/* A member's chunk kinds. */
#define XX_SQUASHFS_KIND_STORED 0U
#define XX_SQUASHFS_KIND_COMPRESSED 1U
#define XX_SQUASHFS_KIND_SPARSE 2U

/* Inode types walked by this reader. */
#define XX_SQUASHFS_INODE_DIR 1
#define XX_SQUASHFS_INODE_FILE 2
#define XX_SQUASHFS_INODE_LDIR 8
#define XX_SQUASHFS_INODE_LREG 9

typedef struct xx_squashfs_superblock_s {
    int32_t major;
    int32_t minor;
    uint32_t compressor; /* SquashFS id: 1 gzip .. 6 zstd; 0 when unstated */
    bool big_endian;
    int64_t inodes;
    int64_t block_size;
    int64_t bytes_used;
    int64_t root_inode;
    int64_t inode_table;
    int64_t directory_table;
    int64_t fragment_table;
    int64_t fragments;
} xx_squashfs_superblock;

/* One piece of a member's payload, addressed absolutely in the image. */
typedef struct xx_squashfs_chunk_s {
    int64_t offset;   /* absolute file offset; meaningless when sparse */
    int64_t on_disk;  /* bytes occupied on disk */
    int64_t out_size; /* bytes this chunk contributes to the member */
    int64_t skip;     /* bytes to drop off the front of the produced block */
    uint32_t kind;
} xx_squashfs_chunk;

typedef struct xx_squashfs_member_s {
    char *name;
    int64_t uncompressed_size;
    int64_t span_offset;
    int64_t span_size;
    xx_squashfs_chunk *chunks;
    size_t chunk_count;
} xx_squashfs_member;

/* One decompressed metadata block, keyed by (base, block). */
typedef struct xx_squashfs_meta_block_s {
    int64_t base;
    int64_t block;
    int64_t next;
    uint8_t *data;
    size_t size;
} xx_squashfs_meta_block;

typedef struct xx_squashfs_private_s {
    xx_squashfs_superblock super;
    xx_squashfs_member *members;
    size_t count;
    size_t capacity;
    xx_squashfs_meta_block *cache;
    size_t cache_count;
    size_t cache_capacity;
    size_t cache_bytes;
    int64_t image_size; /* bytes of the image addressable from base_address */
} xx_squashfs_private;

/* Everything the inode walk needs that is not part of the parsed result. */
typedef struct xx_squashfs_walk_s {
    Abstractformat *format;
    xx_squashfs_private *parsed;
    xx_pd_struct *pd;
} xx_squashfs_walk;

/* A cursor into one metadata stream; `base` is the owning table's offset. */
typedef struct xx_squashfs_meta_stream_s {
    xx_squashfs_walk *walk;
    int64_t base;
    int64_t block;
    int64_t offset;
} xx_squashfs_meta_stream;

typedef struct xx_squashfs_archive_stream_s {
    xx_squashfs_private parsed;
    size_t index;
} xx_squashfs_archive_stream;

static void xx_squashfs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------- byte readers --- */

static uint16_t xx_squashfs_read16(const uint8_t *data, bool big_endian) {
    return big_endian ? (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1])
                      : (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_squashfs_read32(const uint8_t *data, bool big_endian) {
    if (big_endian) {
        return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
               ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
    }
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t xx_squashfs_read64(const uint8_t *data, bool big_endian) {
    if (big_endian) {
        return ((uint64_t)xx_squashfs_read32(data, true) << 32U) |
               (uint64_t)xx_squashfs_read32(data + 4U, true);
    }
    return (uint64_t)xx_squashfs_read32(data, false) |
           ((uint64_t)xx_squashfs_read32(data + 4U, false) << 32U);
}

/* Absolute device read; every offset in this reader is relative to
 * format->base_address, so callers pass image-relative offsets. */
static bool xx_squashfs_read_at(Abstractformat *self, int64_t offset,
                                void *data, size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    int64_t absolute;
    if (!self || !self->device || (!data && size != 0U) || offset < 0 ||
        self->base_address < 0 || offset > INT64_MAX - self->base_address) {
        return false;
    }
    absolute = self->base_address + offset;
    if (xx_io_seek64(self->device, absolute, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(self->device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* -------------------------------------------------------- compression --- */

/* The squashfs-lzma variant: five props bytes and NO size field.  The plain
 * 13-byte "alone" header is tried second, exactly as the reference does. */
static bool xx_squashfs_lzma_block(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_cap,
                                   size_t *written) {
    unsigned variant;
    if (input_size <= 5U || output_cap == 0U) return false;
    for (variant = 0U; variant < 2U; ++variant) {
        size_t header_size = (variant == 0U) ? 5U : 13U;
        size_t produced = 0U;
        if (input_size <= header_size) continue;
        /* uncomp_size -1: the length is not in the stream, so decoding runs
         * until the output buffer fills or the input is exhausted. */
        if (xx_lzma_decompress_memory(input + header_size,
                                      input_size - header_size, input, 5U,
                                      (int64_t)-1, output, output_cap,
                                      &produced) &&
            produced != 0U) {
            *written = produced;
            return true;
        }
    }
    return false;
}

/* SquashFS XZ blocks are complete .xz streams, so the xz format reader decodes
 * them directly over a pair of memory devices.  There is no memory-to-memory
 * XZ entry point in the library to call instead. */
static bool xx_squashfs_xz_block(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_cap,
                                 size_t *written) {
    xx_io_device *source;
    xx_io_device *destination;
    xx_xz xz;
    int64_t produced;
    bool result = false;
    if (!input || input_size == 0U || !output || output_cap == 0U) return false;
    source = xx_io_mem_open_ro(input, input_size);
    destination = xx_io_mem_open(output, output_cap);
    if (source && destination) {
        xx_xz_init(&xz, source, 0);
        if (xx_xz_unpack_to_device(&xz, destination, NULL)) {
            produced = xx_io_tell(destination);
            if (produced > 0) {
                *written = (size_t)produced;
                result = true;
            }
        }
        xx_xz_destroy(&xz);
    }
    if (source) xx_io_close(source);
    if (destination) xx_io_close(destination);
    return result;
}

/* Decode one block with a single, known compressor id. */
static bool xx_squashfs_decompress_with(uint32_t compressor,
                                        const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_cap, size_t *written) {
    size_t produced = 0U;
    if (!input || input_size == 0U || !output || output_cap == 0U) return false;
    switch (compressor) {
        case XX_SQUASHFS_COMPRESSOR_GZIP:
            /* SquashFS "gzip" blocks are bare zlib streams, not gzip members. */
            if (!xx_zlib_stream_decode_memory(input, input_size, output,
                                              output_cap, &produced)) {
                return false;
            }
            break;
        case XX_SQUASHFS_COMPRESSOR_LZMA:
            return xx_squashfs_lzma_block(input, input_size, output, output_cap,
                                          written);
        case XX_SQUASHFS_COMPRESSOR_LZO:
            /* xx_lzo1x_decompress wants an exactly sized buffer, so a block
             * whose real output is shorter than output_cap is rejected. */
            if (!xx_lzo1x_decompress(input, input_size, output, output_cap,
                                     &produced)) {
                return false;
            }
            break;
        case XX_SQUASHFS_COMPRESSOR_XZ:
            return xx_squashfs_xz_block(input, input_size, output, output_cap,
                                        written);
        case XX_SQUASHFS_COMPRESSOR_LZ4:
            /* Only framed LZ4 decodes here; see the note in the header of
             * xx_squashfs_decompress(). */
            if (!xx_lz4_decompress_memory(input, input_size, output, output_cap,
                                          &produced)) {
                return false;
            }
            break;
        case XX_SQUASHFS_COMPRESSOR_ZSTD:
            if (!xx_zstd_decompress_memory(input, input_size, output,
                                           output_cap, &produced)) {
                return false;
            }
            break;
        default: return false;
    }
    if (produced == 0U) return false;
    *written = produced;
    return true;
}

/*
 * Decode one block.  A v4 image states its compressor and is taken at its
 * word; v1..v3 have no such field, so the opening bytes are sniffed and the
 * usual suspects are then tried in turn.
 *
 * LZ4 is a known gap: squashfs-tools emits raw LZ4 blocks (LZ4_compress_default
 * output with no frame header) and the only public LZ4 entry point in this
 * library, xx_lz4_decompress_memory(), decodes LZ4 *frames*.  Those blocks
 * therefore fail cleanly rather than being mis-decoded.
 */
static bool xx_squashfs_decompress(const xx_squashfs_superblock *super,
                                   const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_cap,
                                   size_t *written) {
    uint32_t order[3];
    uint32_t sniffed = 0U;
    unsigned count = 0U;
    unsigned index;
    if (!super || !input || input_size == 0U || !output || output_cap == 0U ||
        !written) {
        return false;
    }
    *written = 0U;

    if (super->major >= 4 && super->compressor != 0U) {
        return xx_squashfs_decompress_with(super->compressor, input, input_size,
                                           output, output_cap, written);
    }

    if (input[0] == 0x5DU && input_size >= 3U && input[1] == 0U &&
        input[2] == 0U) {
        sniffed = XX_SQUASHFS_COMPRESSOR_LZMA;
    } else if (input[0] == 0x78U) {
        sniffed = XX_SQUASHFS_COMPRESSOR_GZIP;
    } else if (input_size >= 6U && input[0] == 0xFDU && input[1] == 0x37U &&
               input[2] == 0x7AU && input[3] == 0x58U && input[4] == 0x5AU &&
               input[5] == 0x00U) {
        sniffed = XX_SQUASHFS_COMPRESSOR_XZ;
    }
    if (sniffed != 0U) order[count++] = sniffed;
    if (sniffed != XX_SQUASHFS_COMPRESSOR_GZIP) {
        order[count++] = XX_SQUASHFS_COMPRESSOR_GZIP;
    }
    if (sniffed != XX_SQUASHFS_COMPRESSOR_LZMA) {
        order[count++] = XX_SQUASHFS_COMPRESSOR_LZMA;
    }
    if (count < 3U && sniffed != XX_SQUASHFS_COMPRESSOR_XZ) {
        order[count++] = XX_SQUASHFS_COMPRESSOR_XZ;
    }
    for (index = 0U; index < count; ++index) {
        if (xx_squashfs_decompress_with(order[index], input, input_size, output,
                                        output_cap, written)) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------- metadata streams --- */

static bool xx_squashfs_cache_insert(xx_squashfs_private *parsed, int64_t base,
                                     int64_t block, int64_t next,
                                     const uint8_t *data, size_t size) {
    xx_squashfs_meta_block *grown;
    uint8_t *copy = NULL;
    size_t capacity;
    /* Over budget the block is simply not cached: correctness is unaffected,
     * only the cost of reaching it again. */
    if (!parsed || parsed->cache_bytes > XX_SQUASHFS_CACHE_BUDGET) return false;
    if (parsed->cache_count == parsed->cache_capacity) {
        capacity = parsed->cache_capacity ? parsed->cache_capacity * 2U : 64U;
        if (capacity < parsed->cache_count ||
            capacity > SIZE_MAX / sizeof(*parsed->cache)) {
            return false;
        }
        grown = (xx_squashfs_meta_block *)xx_mem_realloc(
            parsed->cache, capacity * sizeof(*parsed->cache));
        if (!grown) return false;
        parsed->cache = grown;
        parsed->cache_capacity = capacity;
    }
    if (size != 0U) {
        copy = (uint8_t *)xx_mem_alloc(size);
        if (!copy) return false;
        xx_mem_copy(copy, data, size);
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

static const xx_squashfs_meta_block *xx_squashfs_cache_find(
    const xx_squashfs_private *parsed, int64_t base, int64_t block) {
    size_t index;
    if (!parsed) return NULL;
    for (index = 0U; index < parsed->cache_count; ++index) {
        if (parsed->cache[index].base == base &&
            parsed->cache[index].block == block) {
            return &parsed->cache[index];
        }
    }
    return NULL;
}

/*
 * One metadata block: an 8 KiB payload behind a u16 length header whose 0x8000
 * bit means the payload is stored raw.  `block` is a byte offset relative to
 * the owning table, except fragment index targets which are absolute and so
 * use base 0.
 *
 * The returned pointer is owned by the cache and stays valid for the lifetime
 * of the parse; `plain`/`plain_size` are the fallback when the block could not
 * be cached, and the caller must consume them before the next call.
 */
static bool xx_squashfs_meta_get_block(xx_squashfs_walk *walk, int64_t base,
                                       int64_t block, uint8_t *plain,
                                       const uint8_t **data, size_t *size,
                                       int64_t *next) {
    const xx_squashfs_meta_block *cached;
    uint8_t header_bytes[2];
    uint8_t packed[XX_SQUASHFS_META_MAX];
    uint16_t header;
    size_t length;
    size_t produced = 0U;
    int64_t position;
    if (!walk || !walk->parsed || !plain || !data || !size || !next ||
        base < 0 || block < 0 || block > INT64_MAX - base) {
        return false;
    }
    cached = xx_squashfs_cache_find(walk->parsed, base, block);
    if (cached) {
        *data = cached->data;
        *size = cached->size;
        *next = cached->next;
        return true;
    }
    position = base + block;
    if (position > walk->parsed->image_size - 2) return false;
    if (!xx_squashfs_read_at(walk->format, position, header_bytes, 2U)) {
        return false;
    }
    header = xx_squashfs_read16(header_bytes, walk->parsed->super.big_endian);
    length = (size_t)(header & 0x7FFFU);
    if (length > XX_SQUASHFS_META_MAX) return false;
    if ((int64_t)length > walk->parsed->image_size - position - 2) return false;
    *next = block + 2 + (int64_t)length;
    if (length == 0U) {
        *data = plain;
        *size = 0U;
        (void)xx_squashfs_cache_insert(walk->parsed, base, block, *next, plain,
                                       0U);
        return true;
    }
    if (!xx_squashfs_read_at(walk->format, position + 2, packed, length)) {
        return false;
    }
    if ((header & 0x8000U) != 0U) {
        xx_mem_copy(plain, packed, length);
        produced = length;
    } else if (!xx_squashfs_decompress(&walk->parsed->super, packed, length,
                                       plain, XX_SQUASHFS_META_MAX,
                                       &produced)) {
        return false;
    }
    (void)xx_squashfs_cache_insert(walk->parsed, base, block, *next, plain,
                                   produced);
    cached = xx_squashfs_cache_find(walk->parsed, base, block);
    *data = cached ? cached->data : plain;
    *size = produced;
    return true;
}

static void xx_squashfs_meta_seek(xx_squashfs_meta_stream *stream,
                                  int64_t block, int64_t offset) {
    if (!stream) return;
    stream->block = block;
    stream->offset = offset;
}

/* Reads `size` bytes out of the stream into a caller buffer.  Every structure
 * this reader parses is at most XX_SQUASHFS_MAX_NAME bytes long, so no caller
 * needs a heap buffer here. */
static bool xx_squashfs_meta_read(xx_squashfs_meta_stream *stream, size_t size,
                                  uint8_t *out) {
    uint8_t plain[XX_SQUASHFS_META_MAX];
    size_t done = 0U;
    unsigned guard = 0U;
    if (!stream || !stream->walk || (!out && size != 0U)) return false;
    while (done < size) {
        const uint8_t *data = NULL;
        size_t block_size = 0U;
        int64_t next = 0;
        int64_t available;
        size_t take;
        if (++guard > XX_SQUASHFS_META_GUARD) return false;
        if (!xx_squashfs_meta_get_block(stream->walk, stream->base,
                                        stream->block, plain, &data,
                                        &block_size, &next)) {
            return false;
        }
        available = (int64_t)block_size - stream->offset;
        if (available <= 0) {
            /* An empty or fully consumed block advances the cursor; a block
             * that points at itself would otherwise spin forever. */
            if (next == stream->block) return false;
            stream->block = next;
            stream->offset = 0;
            continue;
        }
        take = (size_t)available;
        if (take > size - done) take = size - done;
        if (data) xx_mem_copy(out + done, data + stream->offset, take);
        done += take;
        stream->offset += (int64_t)take;
        if (stream->offset >= (int64_t)block_size) {
            stream->block = next;
            stream->offset = 0;
        }
    }
    return true;
}

/* ------------------------------------------------------------- inodes --- */

/*
 * v1..v3 pack several values into one word as C bitfields, and a big-endian
 * image was produced by a big-endian compiler -- which allocates bitfields
 * from the most significant bit downwards, not upwards from the least. So the
 * same logical field sits at opposite ends of the word depending on the
 * image's byte order, and reading it with a fixed mask works for exactly one
 * of the two. Getting this wrong on a big-endian v2 image yields inode type 15
 * instead of 1, the walk finds nothing, and the parse fails with no hint why.
 *
 * `total_bits` is the width of the containing word, `lsb_shift` the field's
 * offset from the LSB in the little-endian layout, `width` its bit count.
 * v4 is unaffected: it uses real u16/u32 fields rather than bitfields.
 */
static uint32_t xx_squashfs_bits(uint32_t word, unsigned total_bits,
                                 unsigned lsb_shift, unsigned width,
                                 bool big_endian) {
    unsigned shift = big_endian ? (total_bits - lsb_shift - width) : lsb_shift;
    uint32_t mask = (width >= 32U) ? 0xFFFFFFFFU
                                   : (uint32_t)((1UL << width) - 1UL);
    return (word >> shift) & mask;
}

static bool xx_squashfs_base_inode(xx_squashfs_walk *walk,
                                   xx_squashfs_meta_stream *stream,
                                   int32_t *type) {
    uint8_t data[8];
    int32_t major;
    bool big_endian;
    if (!walk || !stream || !type) return false;
    major = walk->parsed->super.major;
    big_endian = walk->parsed->super.big_endian;
    if (major == 1) {
        if (!xx_squashfs_meta_read(stream, 3U, data)) return false;
        *type = (int32_t)xx_squashfs_bits(
            xx_squashfs_read16(data, big_endian), 16U, 0U, 4U, big_endian);
        return true;
    }
    if (major == 4) {
        if (!xx_squashfs_meta_read(stream, 8U, data)) return false;
        *type = (int32_t)xx_squashfs_read16(data, big_endian);
        return true;
    }
    if (!xx_squashfs_meta_read(stream, 4U, data)) return false;
    *type = (int32_t)xx_squashfs_bits(
        xx_squashfs_read16(data, big_endian), 16U, 0U, 4U, big_endian);
    return true;
}

static bool xx_squashfs_dir_inode(xx_squashfs_walk *walk,
                                  xx_squashfs_meta_stream *stream,
                                  bool extended, int64_t *size,
                                  int64_t *offset, int64_t *start_block) {
    uint8_t data[32];
    uint32_t word;
    int32_t major;
    bool big_endian;
    if (!walk || !stream || !size || !offset || !start_block) return false;
    major = walk->parsed->super.major;
    big_endian = walk->parsed->super.big_endian;

    if (major == 1 || major == 2) {
        if (extended && major == 2) {
            if (!xx_squashfs_meta_read(stream, 4U, data)) return false;
            word = xx_squashfs_read32(data, big_endian);
            *size = (int64_t)xx_squashfs_bits(word, 32U, 0U, 27U, big_endian);
            if (!xx_squashfs_meta_read(stream, 1U, data)) return false;
            *offset = (int64_t)xx_squashfs_bits(word, 32U, 27U, 5U, big_endian) +
                      (int64_t)data[0] * 32;
        } else {
            if (!xx_squashfs_meta_read(stream, 4U, data)) return false;
            word = xx_squashfs_read32(data, big_endian);
            *size = (int64_t)xx_squashfs_bits(word, 32U, 0U, 19U, big_endian);
            *offset = (int64_t)xx_squashfs_bits(word, 32U, 19U, 13U, big_endian);
        }
        if (!xx_squashfs_meta_read(stream, 4U, data)) return false; /* mtime */
        if (!xx_squashfs_meta_read(stream, 3U, data)) return false;
        if (big_endian) {
            *start_block = ((int64_t)data[0] << 16) | ((int64_t)data[1] << 8) |
                           (int64_t)data[2];
        } else {
            *start_block = (int64_t)data[0] | ((int64_t)data[1] << 8) |
                           ((int64_t)data[2] << 16);
        }
        return true;
    }

    if (major == 3) {
        uint8_t skip[12];
        /* mtime, inode number, nlink */
        if (!xx_squashfs_meta_read(stream, 12U, skip)) return false;
        if (extended) {
            if (!xx_squashfs_meta_read(stream, 4U, data)) return false;
            word = xx_squashfs_read32(data, big_endian);
            *size = (int64_t)xx_squashfs_bits(word, 32U, 0U, 27U, big_endian);
            if (!xx_squashfs_meta_read(stream, 1U, data)) return false;
            *offset = (int64_t)xx_squashfs_bits(word, 32U, 27U, 5U, big_endian) +
                      (int64_t)data[0] * 32;
        } else {
            if (!xx_squashfs_meta_read(stream, 4U, data)) return false;
            word = xx_squashfs_read32(data, big_endian);
            *size = (int64_t)xx_squashfs_bits(word, 32U, 0U, 19U, big_endian);
            *offset = (int64_t)xx_squashfs_bits(word, 32U, 19U, 13U, big_endian);
        }
        if (!xx_squashfs_meta_read(stream, 4U, data)) return false;
        *start_block = (int64_t)xx_squashfs_read32(data, big_endian);
        *size -= 3;
        return true;
    }

    /* v4 */
    if (extended) {
        if (!xx_squashfs_meta_read(stream, 32U, data)) return false;
        *size = (int64_t)xx_squashfs_read32(data + 12U, big_endian);
        *start_block = (int64_t)xx_squashfs_read32(data + 16U, big_endian);
        *offset = (int64_t)xx_squashfs_read16(data + 26U, big_endian);
    } else {
        if (!xx_squashfs_meta_read(stream, 24U, data)) return false;
        *start_block = (int64_t)xx_squashfs_read32(data + 8U, big_endian);
        *size = (int64_t)xx_squashfs_read16(data + 16U, big_endian);
        *offset = (int64_t)xx_squashfs_read16(data + 18U, big_endian);
    }
    *size -= 3;
    return true;
}

static bool xx_squashfs_file_inode(xx_squashfs_walk *walk,
                                   xx_squashfs_meta_stream *stream,
                                   bool extended, int64_t *start,
                                   int64_t *fragment, int64_t *block_offset,
                                   int64_t *size) {
    uint8_t data[48];
    int32_t major;
    bool big_endian;
    if (!walk || !stream || !start || !fragment || !block_offset || !size) {
        return false;
    }
    major = walk->parsed->super.major;
    big_endian = walk->parsed->super.big_endian;

    if (major == 1) {
        if (!xx_squashfs_meta_read(stream, 12U, data)) return false;
        *start = (int64_t)xx_squashfs_read32(data + 4U, big_endian);
        *size = (int64_t)xx_squashfs_read32(data + 8U, big_endian);
        *fragment = INT64_C(0xFFFFFFFF);
        *block_offset = 0;
        return true;
    }
    if (major == 2) {
        if (!xx_squashfs_meta_read(stream, 20U, data)) return false;
        *start = (int64_t)xx_squashfs_read32(data + 4U, big_endian);
        *fragment = (int64_t)xx_squashfs_read32(data + 8U, big_endian);
        *block_offset = (int64_t)xx_squashfs_read32(data + 12U, big_endian);
        *size = (int64_t)xx_squashfs_read32(data + 16U, big_endian);
        return true;
    }
    if (major == 3) {
        if (extended) {
            if (!xx_squashfs_meta_read(stream, 36U, data)) return false;
            *start = (int64_t)xx_squashfs_read64(data + 12U, big_endian);
            *fragment = (int64_t)xx_squashfs_read32(data + 20U, big_endian);
            *block_offset = (int64_t)xx_squashfs_read32(data + 24U, big_endian);
            *size = (int64_t)xx_squashfs_read64(data + 28U, big_endian);
        } else {
            if (!xx_squashfs_meta_read(stream, 28U, data)) return false;
            *start = (int64_t)xx_squashfs_read64(data + 8U, big_endian);
            *fragment = (int64_t)xx_squashfs_read32(data + 16U, big_endian);
            *block_offset = (int64_t)xx_squashfs_read32(data + 20U, big_endian);
            *size = (int64_t)xx_squashfs_read32(data + 24U, big_endian);
        }
        return true;
    }

    if (extended) {
        if (!xx_squashfs_meta_read(stream, 48U, data)) return false;
        *start = (int64_t)xx_squashfs_read64(data + 8U, big_endian);
        *size = (int64_t)xx_squashfs_read64(data + 16U, big_endian);
        *fragment = (int64_t)xx_squashfs_read32(data + 36U, big_endian);
        *block_offset = (int64_t)xx_squashfs_read32(data + 40U, big_endian);
    } else {
        if (!xx_squashfs_meta_read(stream, 24U, data)) return false;
        *start = (int64_t)xx_squashfs_read32(data + 8U, big_endian);
        *fragment = (int64_t)xx_squashfs_read32(data + 12U, big_endian);
        *block_offset = (int64_t)xx_squashfs_read32(data + 16U, big_endian);
        *size = (int64_t)xx_squashfs_read32(data + 20U, big_endian);
    }
    return true;
}

/* The index value is an ABSOLUTE file offset of a metadata block, which is why
 * the stream is opened on base 0. */
static bool xx_squashfs_fragment(xx_squashfs_walk *walk, int64_t index,
                                 int64_t *start, int64_t *size) {
    xx_squashfs_meta_stream stream;
    uint8_t entry[16];
    uint8_t pointer[8];
    int64_t block;
    int64_t entry_offset;
    int64_t index_position;
    int32_t major;
    bool big_endian;
    if (!walk || !start || !size) return false;
    major = walk->parsed->super.major;
    big_endian = walk->parsed->super.big_endian;
    if (major == 1 || index < 0 || index >= walk->parsed->super.fragments) {
        return false;
    }

    if (major == 2) {
        index_position = walk->parsed->super.fragment_table + (index >> 10) * 4;
        if (index_position < 0 ||
            index_position > walk->parsed->image_size - 4) {
            return false;
        }
        if (!xx_squashfs_read_at(walk->format, index_position, pointer, 4U)) {
            return false;
        }
        block = (int64_t)xx_squashfs_read32(pointer, big_endian);
        entry_offset = (index & 0x3FF) * 8;
    } else {
        index_position = walk->parsed->super.fragment_table + (index >> 9) * 8;
        if (index_position < 0 ||
            index_position > walk->parsed->image_size - 8) {
            return false;
        }
        if (!xx_squashfs_read_at(walk->format, index_position, pointer, 8U)) {
            return false;
        }
        block = (int64_t)xx_squashfs_read64(pointer, big_endian);
        entry_offset = (index & 0x1FF) * 16;
    }

    stream.walk = walk;
    stream.base = 0;
    stream.block = 0;
    stream.offset = 0;
    xx_squashfs_meta_seek(&stream, block, entry_offset);
    if (major == 2) {
        if (!xx_squashfs_meta_read(&stream, 8U, entry)) return false;
        *start = (int64_t)xx_squashfs_read32(entry, big_endian);
        *size = (int64_t)xx_squashfs_read32(entry + 4U, big_endian);
        return true;
    }
    if (!xx_squashfs_meta_read(&stream, 16U, entry)) return false;
    *start = (int64_t)xx_squashfs_read64(entry, big_endian);
    *size = (int64_t)xx_squashfs_read32(entry + 8U, big_endian);
    return true;
}

/* -------------------------------------------------------- collections --- */

static void xx_squashfs_member_cleanup(xx_squashfs_member *member) {
    if (!member) return;
    if (member->name) xx_str_free(member->name);
    if (member->chunks) xx_mem_free(member->chunks);
    xx_mem_zero(member, sizeof(*member));
}

static void xx_squashfs_private_cleanup(xx_squashfs_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        xx_squashfs_member_cleanup(&parsed->members[index]);
    }
    if (parsed->members) xx_mem_free(parsed->members);
    for (index = 0U; index < parsed->cache_count; ++index) {
        if (parsed->cache[index].data) xx_mem_free(parsed->cache[index].data);
    }
    if (parsed->cache) xx_mem_free(parsed->cache);
    xx_mem_zero(parsed, sizeof(*parsed));
}

/* Takes ownership of *member on success and zeroes it. */
static bool xx_squashfs_append_member(xx_squashfs_private *parsed,
                                      xx_squashfs_member *member) {
    xx_squashfs_member *grown;
    size_t capacity;
    if (!parsed || !member || !member->name ||
        parsed->count >= XX_SQUASHFS_MAX_MEMBERS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->members)) {
            return false;
        }
        grown = (xx_squashfs_member *)xx_mem_realloc(
            parsed->members, capacity * sizeof(*parsed->members));
        if (!grown) return false;
        parsed->members = grown;
        parsed->capacity = capacity;
    }
    parsed->members[parsed->count++] = *member;
    xx_mem_zero(member, sizeof(*member));
    return true;
}

static bool xx_squashfs_append_chunk(xx_squashfs_chunk **chunks, size_t *count,
                                     size_t *capacity, int64_t offset,
                                     int64_t on_disk, int64_t out_size,
                                     int64_t skip, uint32_t kind) {
    xx_squashfs_chunk *grown;
    size_t grown_capacity;
    if (!chunks || !count || !capacity || *count >= XX_SQUASHFS_MAX_CHUNKS) {
        return false;
    }
    if (*count == *capacity) {
        grown_capacity = *capacity ? *capacity * 2U : 16U;
        if (grown_capacity < *count ||
            grown_capacity > SIZE_MAX / sizeof(**chunks)) {
            return false;
        }
        grown = (xx_squashfs_chunk *)xx_mem_realloc(
            *chunks, grown_capacity * sizeof(**chunks));
        if (!grown) return false;
        *chunks = grown;
        *capacity = grown_capacity;
    }
    (*chunks)[*count].offset = offset;
    (*chunks)[*count].on_disk = on_disk;
    (*chunks)[*count].out_size = out_size;
    (*chunks)[*count].skip = skip;
    (*chunks)[*count].kind = kind;
    ++(*count);
    return true;
}

/* ------------------------------------------------------------- names --- */

/* Same rules as the ISO 9660 reader: no absolute paths, no traversal, no
 * characters a Windows path cannot carry. */
static bool xx_squashfs_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* Unlike the reference, which prefixes every path with '/' because the root
 * walk starts from an empty QString, the root's children are emitted without a
 * leading separator so the names stay relative. */
static char *xx_squashfs_join_name(const char *prefix, const uint8_t *name,
                                   size_t name_size) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    char *combined;
    size_t index;
    if (!name || name_size == 0U || name_size > XX_SQUASHFS_MAX_NAME ||
        prefix_size >= XX_SQUASHFS_MAX_PATH ||
        name_size > XX_SQUASHFS_MAX_PATH - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    for (index = 0U; index < name_size; ++index) {
        if (name[index] == 0U || name[index] == '/' || name[index] == '\\') {
            return NULL;
        }
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_mem_copy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_mem_copy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_mem_copy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* --------------------------------------------------------- the walk --- */

static bool xx_squashfs_walk_node(xx_squashfs_walk *walk, int64_t block,
                                  int64_t offset, int32_t want,
                                  const char *name, unsigned depth);

/* Builds a member's chunk list from its block list plus optional fragment. */
static bool xx_squashfs_file_data(xx_squashfs_walk *walk,
                                  xx_squashfs_meta_stream *stream,
                                  bool extended, const char *name) {
    xx_squashfs_member member;
    xx_squashfs_chunk *chunks = NULL;
    size_t chunk_count = 0U;
    size_t chunk_capacity = 0U;
    int64_t start = 0;
    int64_t fragment = 0;
    int64_t block_offset = 0;
    int64_t size = 0;
    int64_t block_size;
    int64_t block_count;
    int64_t tail;
    int64_t position;
    int64_t min_offset = -1;
    int64_t max_end = -1;
    size_t index;
    bool big_endian;

    if (!walk || !stream || !name) return false;
    if (!xx_squashfs_file_inode(walk, stream, extended, &start, &fragment,
                                &block_offset, &size)) {
        return false;
    }
    if (size < 0 || size > XX_SQUASHFS_MAX_UNCOMPRESSED || block_offset < 0) {
        return false;
    }

    block_size = walk->parsed->super.block_size;
    big_endian = walk->parsed->super.big_endian;
    if (walk->parsed->super.major == 1 || fragment == INT64_C(0xFFFFFFFF)) {
        /* No fragment: the tail lives in a short final block. */
        block_count = (size + block_size - 1) / block_size;
        tail = 0;
    } else {
        block_count = size / block_size;
        tail = size % block_size;
    }
    if (block_count < 0 || block_count > (int64_t)XX_SQUASHFS_MAX_CHUNKS) {
        return false;
    }

    position = start;
    for (index = 0U; index < (size_t)block_count; ++index) {
        uint8_t entry_bytes[4];
        uint32_t entry;
        int64_t on_disk;
        if (walk->pd && xx_pd_is_stopped(walk->pd)) goto fail;
        if (walk->parsed->super.major == 1) {
            uint32_t word;
            if (!xx_squashfs_meta_read(stream, 2U, entry_bytes)) goto fail;
            word = xx_squashfs_read16(entry_bytes, big_endian);
            /* v1 packs the stored flag as 0x8000; rescale it to 0x1000000. */
            entry = ((word & 0x8000U) << 9U) | (word & 0x7FFFU);
        } else {
            if (!xx_squashfs_meta_read(stream, 4U, entry_bytes)) goto fail;
            entry = xx_squashfs_read32(entry_bytes, big_endian);
        }
        on_disk = (int64_t)(entry & 0xFFFFFFU);
        if (entry == 0U) {
            /* A sparse block occupies nothing on disk. */
            int64_t zero_size = block_size;
            int64_t remaining = size - (int64_t)index * block_size;
            if (index + 1U == (size_t)block_count && remaining < block_size) {
                zero_size = remaining;
            }
            if (zero_size < 0) zero_size = 0;
            if (!xx_squashfs_append_chunk(&chunks, &chunk_count,
                                          &chunk_capacity, 0, 0, zero_size, 0,
                                          XX_SQUASHFS_KIND_SPARSE)) {
                goto fail;
            }
        } else {
            if (position < 0 || on_disk > walk->parsed->image_size - position) {
                goto fail;
            }
            if (!xx_squashfs_append_chunk(
                    &chunks, &chunk_count, &chunk_capacity, position, on_disk,
                    block_size, 0,
                    (entry & 0x1000000U) ? XX_SQUASHFS_KIND_STORED
                                         : XX_SQUASHFS_KIND_COMPRESSED)) {
                goto fail;
            }
        }
        if (on_disk > INT64_MAX - position) goto fail;
        position += on_disk;
    }

    if (tail != 0) {
        int64_t fragment_start = 0;
        int64_t fragment_size = 0;
        int64_t on_disk;
        if (!xx_squashfs_fragment(walk, fragment, &fragment_start,
                                  &fragment_size)) {
            goto fail;
        }
        on_disk = fragment_size & 0xFFFFFF;
        if (fragment_start < 0 ||
            on_disk > walk->parsed->image_size - fragment_start) {
            goto fail;
        }
        if (!xx_squashfs_append_chunk(
                &chunks, &chunk_count, &chunk_capacity, fragment_start, on_disk,
                tail, block_offset,
                (fragment_size & 0x1000000) ? XX_SQUASHFS_KIND_STORED
                                            : XX_SQUASHFS_KIND_COMPRESSED)) {
            goto fail;
        }
    }

    /* The smallest byte range holding every chunk becomes the record's
     * stream; a fully sparse member has none and publishes an empty range. */
    for (index = 0U; index < chunk_count; ++index) {
        int64_t from;
        int64_t to;
        if (chunks[index].kind == XX_SQUASHFS_KIND_SPARSE) continue;
        from = chunks[index].offset;
        to = from + chunks[index].on_disk;
        if (min_offset < 0 || from < min_offset) min_offset = from;
        if (max_end < 0 || to > max_end) max_end = to;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_create(name);
    if (!member.name) goto fail;
    member.uncompressed_size = size;
    member.span_offset = (min_offset < 0) ? 0 : min_offset;
    member.span_size = (max_end < 0) ? 0 : (max_end - member.span_offset);
    member.chunks = chunks;
    member.chunk_count = chunk_count;
    if (!xx_squashfs_append_member(walk->parsed, &member)) {
        xx_squashfs_member_cleanup(&member);
        return false;
    }
    return true;

fail:
    if (chunks) xx_mem_free(chunks);
    return false;
}

static bool xx_squashfs_walk_dir(xx_squashfs_walk *walk, int64_t start_block,
                                 int64_t offset, int64_t size,
                                 const char *prefix, unsigned depth) {
    xx_squashfs_meta_stream stream;
    int64_t remaining = size;
    int32_t major;
    bool big_endian;
    if (!walk || depth > XX_SQUASHFS_MAX_DEPTH) return false;
    major = walk->parsed->super.major;
    big_endian = walk->parsed->super.big_endian;

    stream.walk = walk;
    stream.base = walk->parsed->super.directory_table;
    stream.block = 0;
    stream.offset = 0;
    xx_squashfs_meta_seek(&stream, start_block, offset);

    while (remaining > 0) {
        uint8_t header[12];
        int64_t count;
        int64_t entry_block;
        int64_t index;
        if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;

        if (major == 1 || major == 2) {
            uint32_t word;
            if (remaining < 4) return false;
            remaining -= 4;
            if (!xx_squashfs_meta_read(&stream, 4U, header)) return false;
            word = xx_squashfs_read32(header, big_endian);
            /* The entry count is stored biased by one, and like every other
             * v1/v2 bitfield it swaps ends with the image's byte order. */
            count = (int64_t)xx_squashfs_bits(word, 32U, 0U, 8U, big_endian) + 1;
            entry_block =
                (int64_t)xx_squashfs_bits(word, 32U, 8U, 24U, big_endian);
        } else if (major == 3) {
            if (remaining < 9) return false;
            remaining -= 9;
            if (!xx_squashfs_meta_read(&stream, 1U, header)) return false;
            count = (int64_t)header[0] + 1;
            if (!xx_squashfs_meta_read(&stream, 4U, header)) return false;
            entry_block = (int64_t)xx_squashfs_read32(header, big_endian);
            if (!xx_squashfs_meta_read(&stream, 4U, header)) return false;
        } else {
            if (remaining < 12) return false;
            remaining -= 12;
            if (!xx_squashfs_meta_read(&stream, 12U, header)) return false;
            count = (int64_t)xx_squashfs_read32(header, big_endian) + 1;
            entry_block = (int64_t)xx_squashfs_read32(header + 4U, big_endian);
        }

        for (index = 0; index < count; ++index) {
            uint8_t entry[8];
            uint8_t name[XX_SQUASHFS_MAX_NAME];
            int64_t entry_offset;
            int64_t entry_type;
            int64_t name_size;
            if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;

            if (major == 4) {
                if (remaining < 8) return false;
                remaining -= 8;
                if (!xx_squashfs_meta_read(&stream, 8U, entry)) return false;
                entry_offset = (int64_t)xx_squashfs_read16(entry, big_endian);
                entry_type = (int64_t)xx_squashfs_read16(entry + 4U, big_endian);
                /* The name length is stored biased by one too. */
                name_size =
                    (int64_t)xx_squashfs_read16(entry + 6U, big_endian) + 1;
            } else {
                uint32_t word;
                int64_t need = (major == 1 || major == 2) ? 3 : 5;
                if (remaining < need) return false;
                remaining -= need;
                if (!xx_squashfs_meta_read(&stream, 2U, entry)) return false;
                word = xx_squashfs_read16(entry, big_endian);
                entry_offset =
                    (int64_t)xx_squashfs_bits(word, 16U, 0U, 13U, big_endian);
                entry_type =
                    (int64_t)xx_squashfs_bits(word, 16U, 13U, 3U, big_endian);
                if (!xx_squashfs_meta_read(&stream, 1U, entry)) return false;
                name_size = (int64_t)entry[0] + 1;
                if (major == 3) {
                    /* v3 carries a two-byte inode-number delta here. */
                    if (!xx_squashfs_meta_read(&stream, 2U, entry)) return false;
                }
            }

            if (name_size > remaining) return false;
            remaining -= name_size;
            if (name_size > (int64_t)XX_SQUASHFS_MAX_NAME) return false;
            if (!xx_squashfs_meta_read(&stream, (size_t)name_size, name)) {
                return false;
            }

            if (entry_type == XX_SQUASHFS_INODE_DIR ||
                entry_type == XX_SQUASHFS_INODE_FILE) {
                char *child = xx_squashfs_join_name(prefix, name,
                                                     (size_t)name_size);
                bool ok;
                if (!child) return false;
                ok = xx_squashfs_walk_node(walk, entry_block, entry_offset,
                                           (int32_t)entry_type, child,
                                           depth + 1U);
                xx_str_free(child);
                if (!ok) return false;
            }
        }
    }

    return remaining == 0;
}

static bool xx_squashfs_walk_node(xx_squashfs_walk *walk, int64_t block,
                                  int64_t offset, int32_t want,
                                  const char *name, unsigned depth) {
    xx_squashfs_meta_stream stream;
    int32_t type = 0;
    if (!walk || depth > XX_SQUASHFS_MAX_DEPTH) return false;
    if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;

    stream.walk = walk;
    stream.base = walk->parsed->super.inode_table;
    stream.block = 0;
    stream.offset = 0;
    xx_squashfs_meta_seek(&stream, block, offset);
    if (!xx_squashfs_base_inode(walk, &stream, &type)) return false;
    /* Only directories and regular files, basic and extended; everything else
     * (symlinks, devices, fifos, sockets) is skipped. */
    if (type != want &&
        !(type == XX_SQUASHFS_INODE_LDIR && want == XX_SQUASHFS_INODE_DIR) &&
        !(type == XX_SQUASHFS_INODE_LREG && want == XX_SQUASHFS_INODE_FILE)) {
        return false;
    }

    if (type == XX_SQUASHFS_INODE_DIR || type == XX_SQUASHFS_INODE_LDIR) {
        int64_t dir_size = 0;
        int64_t dir_offset = 0;
        int64_t dir_start = 0;
        if (!xx_squashfs_dir_inode(walk, &stream,
                                   type == XX_SQUASHFS_INODE_LDIR, &dir_size,
                                   &dir_offset, &dir_start)) {
            return false;
        }
        return xx_squashfs_walk_dir(walk, dir_start, dir_offset, dir_size, name,
                                    depth);
    }
    if (type == XX_SQUASHFS_INODE_FILE || type == XX_SQUASHFS_INODE_LREG) {
        if (walk->parsed->count >= XX_SQUASHFS_MAX_MEMBERS) return false;
        return xx_squashfs_file_data(walk, &stream,
                                     type == XX_SQUASHFS_INODE_LREG, name);
    }

    return false;
}

/* --------------------------------------------------------- superblock --- */

/*
 * Magic plus a major version of 1..4 is the whole of the reference detector:
 * there is no length field and no checksum anywhere in the format.
 *
 * 'hsqs' is the little-endian spelling of magic 0x73717368 and is what
 * mksquashfs produces; 'sqsh' is its big-endian sibling.  'hsqt' and 'shsq'
 * are the LZO and LZ4 forks, which state their compressor through the magic
 * because their superblocks predate the compressor field.
 */
static bool xx_squashfs_parse_superblock(const uint8_t *header,
                                         int64_t image_size,
                                         xx_squashfs_superblock *super) {
    uint32_t magic;
    bool big_endian;
    if (!header || !super) return false;
    xx_mem_zero(super, sizeof(*super));

    magic = xx_squashfs_read32(header, false);
    if (magic == 0x73717368U) { /* 'hsqs' */
        super->big_endian = false;
        super->compressor = 0U;
    } else if (magic == 0x74717368U) { /* 'hsqt' - LZO fork */
        super->big_endian = false;
        super->compressor = XX_SQUASHFS_COMPRESSOR_LZO;
    } else if (magic == 0x71736873U) { /* 'shsq' - LZ4 fork */
        super->big_endian = false;
        super->compressor = XX_SQUASHFS_COMPRESSOR_LZ4;
    } else if (magic == 0x68737173U) { /* 'sqsh' - big endian */
        super->big_endian = true;
        super->compressor = 0U;
    } else if (magic == 0x68737174U) {
        super->big_endian = true;
        super->compressor = XX_SQUASHFS_COMPRESSOR_LZO;
    } else if (magic == 0x68191122U) {
        super->big_endian = false;
        super->compressor = 0U;
    } else {
        return false;
    }

    big_endian = super->big_endian;
    super->major = (int32_t)xx_squashfs_read16(header + 0x1CU, big_endian);
    super->minor = (int32_t)xx_squashfs_read16(header + 0x1EU, big_endian);
    if (super->major < 1 || super->major > 4) return false;

    if (super->major == 4) {
        uint32_t compressor_id;
        super->inodes = (int64_t)xx_squashfs_read32(header + 4U, big_endian);
        super->block_size = (int64_t)xx_squashfs_read32(header + 12U, big_endian);
        super->fragments = (int64_t)xx_squashfs_read32(header + 16U, big_endian);
        compressor_id = xx_squashfs_read16(header + 0x14U, big_endian);
        super->root_inode =
            (int64_t)xx_squashfs_read64(header + 0x20U, big_endian);
        super->bytes_used =
            (int64_t)xx_squashfs_read64(header + 0x28U, big_endian);
        super->inode_table =
            (int64_t)xx_squashfs_read64(header + 0x40U, big_endian);
        super->directory_table =
            (int64_t)xx_squashfs_read64(header + 0x48U, big_endian);
        super->fragment_table =
            (int64_t)xx_squashfs_read64(header + 0x50U, big_endian);
        if (compressor_id >= XX_SQUASHFS_COMPRESSOR_GZIP &&
            compressor_id <= XX_SQUASHFS_COMPRESSOR_ZSTD) {
            super->compressor = compressor_id;
        }
    } else if (super->major == 3) {
        /* v3 is PACKED: the 64-bit tables start at the unaligned 0x3F. */
        super->inodes = (int64_t)xx_squashfs_read32(header + 4U, big_endian);
        super->root_inode =
            (int64_t)xx_squashfs_read64(header + 0x2BU, big_endian);
        super->block_size =
            (int64_t)xx_squashfs_read32(header + 0x33U, big_endian);
        super->fragments =
            (int64_t)xx_squashfs_read32(header + 0x37U, big_endian);
        super->bytes_used =
            (int64_t)xx_squashfs_read64(header + 0x3FU, big_endian);
        super->inode_table =
            (int64_t)xx_squashfs_read64(header + 0x57U, big_endian);
        super->directory_table =
            (int64_t)xx_squashfs_read64(header + 0x5FU, big_endian);
        super->fragment_table =
            (int64_t)xx_squashfs_read64(header + 0x67U, big_endian);
    } else {
        super->inodes = (int64_t)xx_squashfs_read32(header + 4U, big_endian);
        if (super->major == 1) {
            super->block_size =
                (int64_t)xx_squashfs_read16(header + 0x20U, big_endian);
        } else {
            super->block_size =
                (int64_t)xx_squashfs_read32(header + 0x33U, big_endian);
        }
        super->root_inode =
            (int64_t)xx_squashfs_read64(header + 0x2BU, big_endian);
        super->bytes_used =
            (int64_t)xx_squashfs_read32(header + 8U, big_endian);
        super->inode_table =
            (int64_t)xx_squashfs_read32(header + 0x14U, big_endian);
        super->directory_table =
            (int64_t)xx_squashfs_read32(header + 0x18U, big_endian);
        super->fragments =
            (super->major == 2)
                ? (int64_t)xx_squashfs_read32(header + 0x37U, big_endian)
                : 0;
        super->fragment_table =
            (super->major == 2)
                ? (int64_t)xx_squashfs_read32(header + 0x3BU, big_endian)
                : 0;
    }

    if (super->block_size == 0) {
        int32_t block_log =
            (int32_t)xx_squashfs_read16(header + 0x22U, big_endian);
        if (block_log < 0 || block_log > 23) return false;
        super->block_size = INT64_C(1) << block_log;
    }
    if (super->block_size <= 0 ||
        super->block_size > XX_SQUASHFS_MAX_BLOCK_SIZE) {
        return false;
    }
    if (super->inode_table < 0 || super->directory_table < 0 ||
        super->root_inode < 0) {
        return false;
    }
    if (image_size > 0 && (super->inode_table > image_size ||
                           super->directory_table > image_size)) {
        return false;
    }
    return true;
}

/*
 * `full` false stops at the superblock, which is all detection and sizing
 * need.  `full` true walks the inode tree; like the reference, a walk that
 * dies half way keeps everything collected so far, and the parse succeeds as
 * long as at least one regular file was found.
 */
static bool xx_squashfs_parse(Abstractformat *self, xx_squashfs_private *parsed,
                              bool full, xx_pd_struct *pd) {
    uint8_t header[XX_SQUASHFS_SUPERBLOCK_SIZE];
    xx_squashfs_walk walk;
    int64_t total_size;
    /* Initialise before the guard clauses: callers run the cleanup on their
     * stack copy whatever this returns, and cleaning up an uninitialised one
     * would free indeterminate pointers. */
    if (parsed) xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    parsed->image_size = total_size - self->base_address;
    if (parsed->image_size < XX_SQUASHFS_SUPERBLOCK_SIZE ||
        parsed->image_size > XX_SQUASHFS_MAX_INPUT) {
        return false;
    }
    if (!xx_squashfs_read_at(self, 0, header, sizeof(header))) goto fail;
    if (!xx_squashfs_parse_superblock(header, parsed->image_size,
                                      &parsed->super)) {
        goto fail;
    }
    if (!full) return true;

    walk.format = self;
    walk.parsed = parsed;
    walk.pd = pd;
    /* The root inode reference packs the metadata block in the high 48 bits
     * and the offset within that block in the low 16. */
    (void)xx_squashfs_walk_node(&walk, parsed->super.root_inode >> 16,
                                parsed->super.root_inode & 0xFFFF,
                                XX_SQUASHFS_INODE_DIR, "", 0U);
    if (parsed->count == 0U) goto fail;
    return true;

fail:
    xx_squashfs_private_cleanup(parsed);
    return false;
}

/* --------------------------------------------------------- extraction --- */

/* Decodes one member's chunks straight into `destination`. */
static bool xx_squashfs_extract_member(Abstractformat *self,
                                       const xx_squashfs_private *parsed,
                                       const xx_squashfs_member *member,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_capacity = 0U;
    size_t plain_capacity = 0U;
    int64_t produced_total = 0;
    size_t index;
    bool result = false;

    if (!self || !parsed || !member || !destination) return false;

    for (index = 0U; index < member->chunk_count; ++index) {
        const xx_squashfs_chunk *chunk = &member->chunks[index];
        if (chunk->on_disk > (int64_t)packed_capacity) {
            packed_capacity = (size_t)chunk->on_disk;
        }
        if (chunk->out_size + chunk->skip > (int64_t)plain_capacity) {
            plain_capacity = (size_t)(chunk->out_size + chunk->skip);
        }
    }
    if (plain_capacity < (size_t)parsed->super.block_size) {
        plain_capacity = (size_t)parsed->super.block_size;
    }
    if (packed_capacity > (size_t)XX_SQUASHFS_MAX_BLOCK_SIZE ||
        plain_capacity > (size_t)XX_SQUASHFS_MAX_BLOCK_SIZE) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc(packed_capacity ? packed_capacity : 1U);
    plain = (uint8_t *)xx_mem_alloc(plain_capacity ? plain_capacity : 1U);
    if (!packed || !plain) goto cleanup;

    for (index = 0U; index < member->chunk_count; ++index) {
        const xx_squashfs_chunk *chunk = &member->chunks[index];
        const uint8_t *emit;
        size_t emit_size;
        size_t produced = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (chunk->out_size < 0 || chunk->skip < 0) goto cleanup;

        if (chunk->kind == XX_SQUASHFS_KIND_SPARSE) {
            /* A sparse block expands to zeros and reads nothing from disk. */
            size_t left = (size_t)chunk->out_size;
            xx_mem_zero(plain, plain_capacity);
            while (left != 0U) {
                size_t step = (left < plain_capacity) ? left : plain_capacity;
                if (xx_io_write(destination, plain, step) != (ssize_t)step) {
                    goto cleanup;
                }
                left -= step;
            }
            produced_total += chunk->out_size;
            continue;
        }

        if (chunk->on_disk <= 0 ||
            (size_t)chunk->on_disk > packed_capacity ||
            !xx_squashfs_read_at(self, chunk->offset, packed,
                                 (size_t)chunk->on_disk)) {
            goto cleanup;
        }
        if (chunk->kind == XX_SQUASHFS_KIND_STORED) {
            emit = packed;
            emit_size = (size_t)chunk->on_disk;
        } else {
            if (!xx_squashfs_decompress(&parsed->super, packed,
                                        (size_t)chunk->on_disk, plain,
                                        plain_capacity, &produced)) {
                goto cleanup;
            }
            emit = plain;
            emit_size = produced;
        }
        /* A fragment shares its block with other members, so the leading
         * `skip` bytes belong to somebody else. */
        if (chunk->skip > 0) {
            if ((size_t)chunk->skip >= emit_size) goto cleanup;
            emit += chunk->skip;
            emit_size -= (size_t)chunk->skip;
        }
        if (emit_size > (size_t)chunk->out_size) {
            emit_size = (size_t)chunk->out_size;
        }
        if (emit_size != 0U &&
            xx_io_write(destination, emit, emit_size) != (ssize_t)emit_size) {
            goto cleanup;
        }
        produced_total += (int64_t)emit_size;
    }

    /* Exactly the promised length or nothing: a partially decoded member
     * reported as success is the one failure a caller cannot detect. */
    result = produced_total == member->uncompressed_size;

cleanup:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

/* ------------------------------------------------------ record plumbing --- */

static bool xx_squashfs_copy_options(xx_list_s *destination,
                                     const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
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

static const xx_var *xx_squashfs_find_option(const xx_list_s *options,
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

static bool xx_squashfs_populate_record(xx_archive_record *record,
                                        const xx_squashfs_member *member,
                                        uint32_t compressor) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = -1;
    record->data_offset = member->span_offset;
    record->compressed_size = member->span_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->span_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          (uint64_t)compressor) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_squashfs_archive_stream_free(void *pointer) {
    xx_squashfs_archive_stream *stream = (xx_squashfs_archive_stream *)pointer;
    if (!stream) return;
    xx_squashfs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_squashfs_init(xx_squashfs *squashfs, xx_io_device *dev,
                      int64_t base_address) {
    if (!squashfs) return;
    xx_mem_zero(squashfs, sizeof(*squashfs));
    xx_format_init(&squashfs->format, dev, base_address);
    squashfs->format.endian = XX_ENDIAN_LITTLE;
    squashfs->format.file_type = XX_SQUASHFS_FILE_TYPE;
    squashfs->format.format_type = XX_TYPE_ARCHIVE;
    squashfs->format.is_archive = true;
    xx_format_set_mime_type(&squashfs->format, "application/x-squashfs");
    xx_format_set_extension(&squashfs->format, "squashfs");
    squashfs->format.check_is_valid = xx_squashfs_check_is_valid;
    squashfs->format.handle_base_info = xx_squashfs_handle_base_info;
    squashfs->format.get_format_size = xx_squashfs_get_format_size;
    squashfs->format.get_number_of_archive_records =
        xx_squashfs_get_number_of_archive_records;
    squashfs->format.create_archive_records_reading =
        xx_squashfs_create_archive_records_reading;
    squashfs->format.get_current_archive_record =
        xx_squashfs_get_current_archive_record;
    squashfs->format.unpack_current_archive_record =
        xx_squashfs_unpack_current_archive_record;
    squashfs->format.archive_record_move_to_next =
        xx_squashfs_archive_record_move_to_next;
    squashfs->format.free_archive_records_reading =
        xx_squashfs_free_archive_records_reading;
    squashfs->format.destroy = xx_squashfs_vtable_destroy;
}

xx_squashfs *xx_squashfs_create(xx_io_device *dev, int64_t base_address) {
    xx_squashfs *squashfs = (xx_squashfs *)xx_mem_alloc(sizeof(*squashfs));
    if (squashfs) xx_squashfs_init(squashfs, dev, base_address);
    return squashfs;
}

void xx_squashfs_destroy(xx_squashfs *squashfs) {
    if (!squashfs) return;
    if (squashfs->internal) {
        xx_squashfs_private_cleanup((xx_squashfs_private *)squashfs->internal);
        xx_mem_free(squashfs->internal);
        squashfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&squashfs->format);
}

static void xx_squashfs_vtable_destroy(Abstractformat *self) {
    xx_squashfs_destroy((xx_squashfs *)self);
}

void xx_squashfs_free(xx_squashfs *squashfs) {
    if (!squashfs) return;
    xx_squashfs_destroy(squashfs);
    xx_mem_free(squashfs);
}

/* -------------------------------------------------------------- vtable --- */

bool xx_squashfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_squashfs_private parsed;
    /* Detection stops at the superblock: the inode walk only happens on the
     * full parse, so validity stays cheap. */
    bool result = xx_squashfs_parse(self, &parsed, false, pd);
    xx_squashfs_private_cleanup(&parsed);
    return result;
}

bool xx_squashfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_squashfs_private *parsed;
    xx_squashfs *squashfs = (xx_squashfs *)self;
    int64_t total_size;
    int64_t archive_size;
    if (!self) return false;
    parsed = (xx_squashfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_squashfs_parse(self, parsed, true, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (squashfs->internal) {
        xx_squashfs_private_cleanup((xx_squashfs_private *)squashfs->internal);
        xx_mem_free(squashfs->internal);
    }
    squashfs->internal = parsed;
    squashfs->number_of_records = parsed->count;
    squashfs->number_of_members = parsed->count;
    squashfs->version_major = (uint32_t)parsed->super.major;
    squashfs->version_minor = (uint32_t)parsed->super.minor;
    squashfs->compressor = parsed->super.compressor;
    squashfs->block_size = (uint32_t)parsed->super.block_size;
    squashfs->inode_count = (uint64_t)parsed->super.inodes;
    squashfs->fragment_count = (uint64_t)parsed->super.fragments;
    squashfs->bytes_used = parsed->super.bytes_used;
    squashfs->big_endian = parsed->super.big_endian;
    self->endian = parsed->super.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;

    /* bytes_used is the authoritative archive size when it is sane; anything
     * past it is overlay. */
    archive_size = parsed->image_size;
    if (parsed->super.bytes_used > 0 &&
        parsed->super.bytes_used <= parsed->image_size) {
        archive_size = parsed->super.bytes_used;
    }
    self->format_size = archive_size;
    total_size = xx_io_total_size(self->device);
    if (total_size > self->base_address + archive_size) {
        self->overlay_offset = self->base_address + archive_size;
        self->overlay_size = total_size - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_squashfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_squashfs_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_squashfs *)self)->number_of_records;
}

xx_archive_record_state *xx_squashfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_squashfs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_squashfs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_squashfs_copy_options(&state->options, options) ||
        !xx_squashfs_parse(self, &stream->parsed, true, pd)) {
        xx_squashfs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_squashfs_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_squashfs_populate_record(&state->current_record,
                                    &stream->parsed.members[0],
                                    stream->parsed.super.compressor)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_squashfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_squashfs_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_squashfs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_squashfs_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_squashfs_populate_record(&state->current_record,
                                     &stream->parsed.members[stream->index],
                                     stream->parsed.super.compressor)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_squashfs_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_squashfs_archive_stream *stream;
    const xx_squashfs_member *member;
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
    stream = (xx_squashfs_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    member = &stream->parsed.members[stream->index];
    if (!xx_squashfs_safe_name(member->name)) return false;

    option =
        xx_squashfs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the member's span is addressable. */
        return member->span_offset >= 0 && member->span_size >= 0 &&
               member->span_offset <= stream->parsed.image_size &&
               member->span_size <=
                   stream->parsed.image_size - member->span_offset;
    }
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
    created = destination != NULL;
    if (!destination) goto cleanup;
    result = xx_squashfs_extract_member(self, &stream->parsed, member,
                                        destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_squashfs_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* --------------------------------------------------------- accessors --- */

uint64_t xx_squashfs_get_number_of_records(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->number_of_records : 0U;
}
uint64_t xx_squashfs_get_number_of_members(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->number_of_members : 0U;
}
uint32_t xx_squashfs_get_version_major(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->version_major : 0U;
}
uint32_t xx_squashfs_get_version_minor(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->version_minor : 0U;
}
uint32_t xx_squashfs_get_compressor(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->compressor : 0U;
}
uint32_t xx_squashfs_get_block_size(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->block_size : 0U;
}
int64_t xx_squashfs_get_bytes_used(const xx_squashfs *squashfs) {
    return squashfs ? squashfs->bytes_used : 0;
}

const char *xx_squashfs_compressor_to_string(uint32_t compressor) {
    switch (compressor) {
        case XX_SQUASHFS_COMPRESSOR_GZIP: return "GZIP";
        case XX_SQUASHFS_COMPRESSOR_LZMA: return "LZMA";
        case XX_SQUASHFS_COMPRESSOR_LZO: return "LZO";
        case XX_SQUASHFS_COMPRESSOR_XZ: return "XZ";
        case XX_SQUASHFS_COMPRESSOR_LZ4: return "LZ4";
        case XX_SQUASHFS_COMPRESSOR_ZSTD: return "ZSTD";
        default: return "Unknown";
    }
}
