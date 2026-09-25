/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * cloop V2 compressed loop images (Linux cloop 2.x, the KNOPPIX format) and
 * FreeBSD geom_uzip images written by mkuzip, which use the same layout.
 * xx_cloop.h carries the field table. Written from the format description;
 * no reference implementation's code is used.
 *
 * The flavour is the letter at 0x0B of the preamble "#!/bin/sh\n#V2.0 ...":
 *   V  zlib, each block one zlib stream        (cloop 2.x, mkuzip)
 *   L  xz, each block one .xz stream            (mkuzip -A lzma, "#L3.0")
 *   Z  zstd, each block one Zstandard frame     (mkuzip -A zstd, "#Z4.0")
 * mkuzip writes the letter in lower case when it de-duplicated blocks.
 *
 * Table of contents. Entry i is the absolute offset of block i and entry n
 * is the end of the data, so a block normally runs to the next entry. mkuzip
 * adds two things on top of that, and both are read here the way geom_uzip
 * reads them:
 *   - a block of nothing but zeros is not stored at all: its entry equals
 *     the next one, and it reads back as zeros;
 *   - with de-duplication a repeated block is not stored again: its entry
 *     points BACK at the first copy, below the end of the data laid out so
 *     far, and it takes that copy's length.
 * So the table is walked with a frontier, the end of the data laid out so
 * far. An entry AT the frontier starts a new block, whose length runs to the
 * first later entry at or above its start (a zero length is a zero block);
 * an entry BELOW the frontier is a back-reference to an earlier block that
 * starts exactly there; an entry ABOVE the frontier leaves a hole in the
 * data and is refused. A plain cloop 2.x image is the special case where
 * every entry starts a new block.
 *
 * Every block has to decode to exactly one block size, and the image is
 * n * block size bytes, which is what qemu's cloop driver produces as well.
 * zlib blocks are checked against their Adler-32, xz blocks against their
 * own check field and zstd frames against their checksum when they carry
 * one.
 *
 * Linux cloop 3.x/4.x moved the index to the end of the file and extended
 * it; those images are not this layout and are refused.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cloop/xx_cloop.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro defined next to the enumerator is tested instead. */
#ifdef CLOOP
#define XX_CLOOP_FILE_TYPE XX_FILE_TYPE_CLOOP
#else
#define XX_CLOOP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CLOOP_PREAMBLE_SIZE 0x80
#define CLOOP_TOC_OFFSET 0x88
#define CLOOP_TOC_ENTRY 8
/* Both tools require a multiple of 512; qemu refuses anything above 64 MiB
 * and mkuzip and create_compressed_fs stay far below this cap, which is also
 * what bounds the two per-block buffers. */
#define CLOOP_BLOCK_UNIT 512U
#define CLOOP_MAX_BLOCK_SIZE (16U * 1024U * 1024U)
/* A 128 MiB table of contents. */
#define CLOOP_MAX_BLOCKS (UINT32_C(1) << 24)
/* Zero and duplicate blocks cost no input, so the image size is capped on
 * its own, like the sparse image reader's expansion limit. */
#define CLOOP_MAX_IMAGE (UINT64_C(64) << 30)
/* The back-reference table holds one entry per stored block: 48 MiB. */
#define CLOOP_MAX_TABLE (UINT32_C(1) << 22)
/* Table of contents entries per window read: 32 KiB. */
#define CLOOP_TOC_WINDOW 4096U

#define CLOOP_METHOD_ZLIB 1U
#define CLOOP_METHOD_XZ 2U
#define CLOOP_METHOD_ZSTD 3U

#define CLOOP_KIND_DATA 0
#define CLOOP_KIND_ZERO 1
#define CLOOP_KIND_BACKREF 2

typedef struct cloop_info_s {
    int64_t span;          /**< Bytes from the base address to the end. */
    int64_t data_end;      /**< Last table entry: the format size. */
    uint64_t unpacked_size;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t method;
    uint32_t max_packed;   /**< Largest stored block accepted. */
    uint32_t data_blocks;
    uint32_t zero_blocks;
    uint32_t backref_blocks;
    bool iso;
} cloop_info;

typedef struct cloop_stream_s {
    cloop_info info;
    size_t index;
    size_t count;
} cloop_stream;

typedef struct cloop_toc_s {
    xx_io_device *device;
    int64_t base;      /**< Device offset of entry 0. */
    uint32_t entries;  /**< n + 1. */
    uint32_t start;    /**< Index of the first cached entry. */
    uint32_t count;    /**< Cached entries. */
    uint8_t buffer[CLOOP_TOC_WINDOW * CLOOP_TOC_ENTRY];
} cloop_toc;

typedef bool (*cloop_visit_fn)(void *context, uint32_t index, int kind,
                               uint64_t offset, uint64_t length);

/* ------------------------------------------------------------- helpers -- */

static uint32_t cloop_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t cloop_be64(const uint8_t *b) {
    return ((uint64_t)cloop_be32(b) << 32U) | (uint64_t)cloop_be32(b + 4U);
}

static bool cloop_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool cloop_write_all(xx_io_device *device, const uint8_t *data,
                            size_t size) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool cloop_toc_get(cloop_toc *toc, uint32_t index, uint64_t *value) {
    if (index >= toc->entries) return false;
    if (index < toc->start || index - toc->start >= toc->count) {
        uint32_t want = toc->entries - index;
        if (want > CLOOP_TOC_WINDOW) want = CLOOP_TOC_WINDOW;
        toc->count = 0U;
        if (!cloop_read_at(toc->device,
                           toc->base + (int64_t)index * CLOOP_TOC_ENTRY,
                           toc->buffer, (size_t)want * CLOOP_TOC_ENTRY))
            return false;
        toc->start = index;
        toc->count = want;
    }
    *value = cloop_be64(toc->buffer +
                        (size_t)(index - toc->start) * CLOOP_TOC_ENTRY);
    return true;
}

static bool cloop_contains(const uint8_t *data, size_t size,
                           const char *needle) {
    size_t length = xx_str_len(needle);
    size_t at;
    if (length == 0U || length > size) return false;
    for (at = 0U; at + length <= size; ++at) {
        if (xx_rt_memcmp(data + at, needle, length) == 0) return true;
    }
    return false;
}

/* ---------------------------------------------------------------- walk -- */

/* Walk the table of contents in block order under the rules in the file
 * comment, handing every block to `visit`. A data block's length is capped
 * at max_packed, except that the block ending at the end of the data may be
 * followed by padding (geom_uzip trims it the same way); its read is then
 * limited to max_packed. The walk fills the block counters in `info`. */
static bool cloop_walk(Abstractformat *format, cloop_info *info,
                       cloop_visit_fn visit, void *context,
                       xx_pd_struct *pd) {
    cloop_toc *toc;
    uint64_t first = 0U, last = 0U, frontier;
    uint64_t toc_end;
    uint32_t index;
    bool result = false;

    toc = (cloop_toc *)xx_mem_alloc(sizeof(*toc));
    if (!toc) return false;
    toc->device = format->device;
    toc->base = format->base_address + CLOOP_TOC_OFFSET;
    toc->entries = info->block_count + 1U;
    toc->start = 0U;
    toc->count = 0U;
    info->data_blocks = 0U;
    info->zero_blocks = 0U;
    info->backref_blocks = 0U;

    toc_end = (uint64_t)CLOOP_TOC_OFFSET +
              (uint64_t)toc->entries * CLOOP_TOC_ENTRY;
    if (!cloop_toc_get(toc, 0U, &first) ||
        !cloop_toc_get(toc, info->block_count, &last))
        goto done;
    /* Data after the table, and the end of the data inside the file. */
    if (first < toc_end || last < first || last > (uint64_t)info->span)
        goto done;
    frontier = first;
    for (index = 0U; index < info->block_count; ++index) {
        uint64_t offset = 0U, next = 0U, length;
        uint32_t ahead;
        if ((index & 0x3FFU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!cloop_toc_get(toc, index, &offset)) goto done;
        if (offset > frontier) goto done; /* a hole in the data */
        if (offset < frontier) {
            /* A de-duplicated block: it has to point at data already laid
             * out, and the decoder resolves which block starts there. */
            if (offset < first) goto done;
            ++info->backref_blocks;
            if (visit && !visit(context, index, CLOOP_KIND_BACKREF, offset, 0U))
                goto done;
            continue;
        }
        /* A new block: it runs to the first later entry at or above its
         * start. Entry n is at or above every start, so the scan ends. */
        for (ahead = index + 1U;; ++ahead) {
            if (!cloop_toc_get(toc, ahead, &next)) goto done;
            if (next >= offset) break;
        }
        if (next > last) goto done;
        length = next - offset;
        if (length == 0U) {
            ++info->zero_blocks;
            if (visit && !visit(context, index, CLOOP_KIND_ZERO, offset, 0U))
                goto done;
            continue;
        }
        if (length > info->max_packed) {
            if (next != last) goto done;
            length = info->max_packed; /* trailing padding */
        }
        ++info->data_blocks;
        frontier = next;
        if (visit && !visit(context, index, CLOOP_KIND_DATA, offset, length))
            goto done;
    }
    /* Only stored blocks can be the target of a back-reference. */
    if (info->backref_blocks != 0U && info->data_blocks == 0U) goto done;
    info->data_end = (int64_t)last;
    result = true;
done:
    xx_mem_free(toc);
    return result;
}

/* --------------------------------------------------------------- parse -- */

static uint32_t cloop_flavour(const uint8_t *preamble) {
    static const char prefix[] = "#!/bin/sh\n#";
    uint8_t letter;
    if (xx_rt_memcmp(preamble, prefix, sizeof(prefix) - 1U) != 0) return 0U;
    if (preamble[13] != '.' || preamble[14] != '0') return 0U;
    letter = preamble[11];
    if ((letter == 'V' || letter == 'v') && preamble[12] == '2')
        return CLOOP_METHOD_ZLIB;
    if ((letter == 'L' || letter == 'l') && preamble[12] == '3')
        return CLOOP_METHOD_XZ;
    if ((letter == 'Z' || letter == 'z') && preamble[12] == '4')
        return CLOOP_METHOD_ZSTD;
    return 0U;
}

/* The first stored block has to open like its codec's stream. */
static bool cloop_first_block_matches(void *context, uint32_t index, int kind,
                                      uint64_t offset, uint64_t length) {
    cloop_info *info = (cloop_info *)((void **)context)[0];
    Abstractformat *format = (Abstractformat *)((void **)context)[1];
    bool *checked = (bool *)((void **)context)[2];
    uint8_t head[6];
    (void)index;
    if (kind != CLOOP_KIND_DATA || *checked) return true;
    *checked = true;
    switch (info->method) {
        case CLOOP_METHOD_ZLIB:
            return length >= 2U &&
                   cloop_read_at(format->device,
                                 format->base_address + (int64_t)offset, head,
                                 2U) &&
                   xx_zlib_stream_header_is_valid(head, 2U);
        case CLOOP_METHOD_XZ:
            return length >= 6U &&
                   cloop_read_at(format->device,
                                 format->base_address + (int64_t)offset, head,
                                 6U) &&
                   head[0] == 0xFDU && head[1] == '7' && head[2] == 'z' &&
                   head[3] == 'X' && head[4] == 'Z' && head[5] == 0U;
        case CLOOP_METHOD_ZSTD:
            return length >= 4U &&
                   cloop_read_at(format->device,
                                 format->base_address + (int64_t)offset, head,
                                 4U) &&
                   head[0] == 0x28U && head[1] == 0xB5U && head[2] == 0x2FU &&
                   head[3] == 0xFDU;
        default:
            return false;
    }
}

static bool cloop_parse(Abstractformat *format, cloop_info *out,
                        xx_pd_struct *pd) {
    uint8_t header[CLOOP_TOC_OFFSET];
    cloop_info info;
    int64_t total;
    uint64_t toc_bytes;
    bool checked = false;
    void *context[3];

    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&info, sizeof(info));
    info.span = total - format->base_address;
    /* The header and a table of at least two entries. */
    if (info.span < CLOOP_TOC_OFFSET + 2 * CLOOP_TOC_ENTRY ||
        !cloop_read_at(format->device, format->base_address, header,
                       sizeof(header)))
        return false;
    info.method = cloop_flavour(header);
    if (info.method == 0U) return false;

    info.block_size = cloop_be32(header + CLOOP_PREAMBLE_SIZE);
    info.block_count = cloop_be32(header + CLOOP_PREAMBLE_SIZE + 4);
    if (info.block_size == 0U || (info.block_size % CLOOP_BLOCK_UNIT) != 0U ||
        info.block_size > CLOOP_MAX_BLOCK_SIZE)
        return false;
    if (info.block_count == 0U || info.block_count > CLOOP_MAX_BLOCKS)
        return false;
    toc_bytes = ((uint64_t)info.block_count + 1U) * CLOOP_TOC_ENTRY;
    if (toc_bytes > (uint64_t)(info.span - CLOOP_TOC_OFFSET)) return false;
    info.unpacked_size = (uint64_t)info.block_count * info.block_size;
    if (info.unpacked_size > CLOOP_MAX_IMAGE) return false;
    /* Well above the worst case of all three codecs (zlib's compressBound is
     * about 0.03% over, xz's and zstd's under 1%). */
    info.max_packed = info.block_size + info.block_size / 4U + 1024U;

    context[0] = &info;
    context[1] = format;
    context[2] = &checked;
    if (!cloop_walk(format, &info, cloop_first_block_matches, context, pd))
        return false;
    /* An image of nothing but zero blocks never proved its codec. */
    if (!checked) return false;
    if (info.backref_blocks != 0U && info.data_blocks > CLOOP_MAX_TABLE)
        return false;
    info.iso = cloop_contains(header, CLOOP_PREAMBLE_SIZE, "iso9660") ||
               cloop_contains(header, CLOOP_PREAMBLE_SIZE, "cd9660");
    *out = info;
    return true;
}

/* -------------------------------------------------------------- decode -- */

typedef struct cloop_decoder_s {
    Abstractformat *format;
    const cloop_info *info;
    xx_io_device *destination; /**< NULL: decode and discard. */
    xx_pd_struct *pd;
    uint8_t *packed;
    uint8_t *plain;
    uint64_t *table_offset;    /**< Stored blocks, ascending offsets. */
    uint32_t *table_length;
    uint32_t table_count;
    uint32_t table_capacity;
} cloop_decoder;

static bool cloop_decode_zlib(const uint8_t *packed, size_t size,
                              uint8_t *plain, size_t block_size) {
    xx_io_device *output;
    size_t consumed = 0U;
    int64_t produced;
    const uint8_t *trailer;
    bool decoded;
    if (size < 2U + 4U || !xx_zlib_stream_header_is_valid(packed, size))
        return false;
    output = xx_io_mem_open(plain, block_size);
    if (!output) return false;
    decoded = xx_deflate_unpack_memory_to_device_ex(packed + 2U, size - 2U,
                                                    output, &consumed, false,
                                                    NULL);
    produced = xx_io_tell(output);
    xx_io_close(output);
    if (!decoded || produced != (int64_t)block_size) return false;
    /* The Adler-32 follows the Deflate data; bytes after it are padding. */
    if (consumed > size - 2U - 4U) return false;
    trailer = packed + 2U + consumed;
    return cloop_be32(trailer) == xx_zlib_stream_adler32(plain, block_size);
}

/* An xz block is a complete .xz stream, which the xz format reader decodes
 * over a pair of memory devices; the library has no memory-to-memory entry
 * point for it. The output device holds exactly one block, so a stream that
 * decodes to more fails its write. */
static bool cloop_decode_xz(const uint8_t *packed, size_t size, uint8_t *plain,
                            size_t block_size) {
    xx_io_device *source = xx_io_mem_open_ro(packed, size);
    xx_io_device *output = xx_io_mem_open(plain, block_size);
    bool result = false;
    if (source && output) {
        xx_xz xz;
        xx_xz_init(&xz, source, 0);
        result = xx_xz_unpack_to_device(&xz, output, NULL) &&
                 xx_io_tell(output) == (int64_t)block_size;
        xx_xz_destroy(&xz);
    }
    if (source) xx_io_close(source);
    if (output) xx_io_close(output);
    return result;
}

static bool cloop_decode_block(const cloop_decoder *decoder, uint64_t offset,
                               uint64_t length) {
    const cloop_info *info = decoder->info;
    size_t written = 0U;
    if (length == 0U || length > info->max_packed) return false;
    if (!cloop_read_at(decoder->format->device,
                       decoder->format->base_address + (int64_t)offset,
                       decoder->packed, (size_t)length))
        return false;
    switch (info->method) {
        case CLOOP_METHOD_ZLIB:
            return cloop_decode_zlib(decoder->packed, (size_t)length,
                                     decoder->plain, info->block_size);
        case CLOOP_METHOD_XZ:
            return cloop_decode_xz(decoder->packed, (size_t)length,
                                   decoder->plain, info->block_size);
        case CLOOP_METHOD_ZSTD:
            /* Complete frames filling the buffer exactly. */
            return xx_zstd_decompress_memory(decoder->packed, (size_t)length,
                                             decoder->plain, info->block_size,
                                             &written) &&
                   written == info->block_size;
        default:
            return false;
    }
}

/* The stored block that starts at `offset`, if any. */
static bool cloop_table_find(const cloop_decoder *decoder, uint64_t offset,
                             uint64_t *length) {
    uint32_t low = 0U, high = decoder->table_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2U;
        uint64_t value = decoder->table_offset[middle];
        if (value == offset) {
            *length = decoder->table_length[middle];
            return true;
        }
        if (value < offset)
            low = middle + 1U;
        else
            high = middle;
    }
    return false;
}

static bool cloop_decode_visit(void *context, uint32_t index, int kind,
                               uint64_t offset, uint64_t length) {
    cloop_decoder *decoder = (cloop_decoder *)context;
    const cloop_info *info = decoder->info;
    (void)index;
    if (decoder->pd && xx_pd_is_stopped(decoder->pd)) return false;
    if (kind == CLOOP_KIND_ZERO) {
        xx_mem_zero(decoder->plain, info->block_size);
        return cloop_write_all(decoder->destination, decoder->plain,
                               info->block_size);
    }
    if (kind == CLOOP_KIND_BACKREF) {
        if (!decoder->table_offset ||
            !cloop_table_find(decoder, offset, &length))
            return false;
    } else if (decoder->table_offset) {
        /* Stored blocks are laid out in ascending order, so appending keeps
         * the table sorted for the binary search. */
        if (decoder->table_count >= decoder->table_capacity ||
            (decoder->table_count != 0U &&
             decoder->table_offset[decoder->table_count - 1U] >= offset))
            return false;
        decoder->table_offset[decoder->table_count] = offset;
        decoder->table_length[decoder->table_count] = (uint32_t)length;
        ++decoder->table_count;
    }
    return cloop_decode_block(decoder, offset, length) &&
           cloop_write_all(decoder->destination, decoder->plain,
                           info->block_size);
}

/* Decode the whole image to `destination` (or nowhere, to verify it). The
 * two block buffers are bounded by the block size cap; the back-reference
 * table exists only for de-duplicated images and by parse is capped at
 * CLOOP_MAX_TABLE stored blocks. */
static bool cloop_unpack_to_device(Abstractformat *format,
                                   const cloop_info *parsed,
                                   xx_io_device *destination,
                                   xx_pd_struct *pd) {
    cloop_info info = *parsed;
    cloop_decoder decoder;
    bool result = false;

    xx_mem_zero(&decoder, sizeof(decoder));
    decoder.format = format;
    decoder.info = &info;
    decoder.destination = destination;
    decoder.pd = pd;
    decoder.packed = (uint8_t *)xx_mem_alloc(info.max_packed);
    decoder.plain = (uint8_t *)xx_mem_alloc(info.block_size);
    if (!decoder.packed || !decoder.plain) goto done;
    if (info.backref_blocks != 0U) {
        if (info.data_blocks == 0U || info.data_blocks > CLOOP_MAX_TABLE)
            goto done;
        decoder.table_capacity = info.data_blocks;
        decoder.table_offset = (uint64_t *)xx_mem_alloc(
            (size_t)info.data_blocks * sizeof(uint64_t));
        decoder.table_length = (uint32_t *)xx_mem_alloc(
            (size_t)info.data_blocks * sizeof(uint32_t));
        if (!decoder.table_offset || !decoder.table_length) goto done;
    }
    result = cloop_walk(format, &info, cloop_decode_visit, &decoder, pd);
done:
    if (decoder.table_offset) xx_mem_free(decoder.table_offset);
    if (decoder.table_length) xx_mem_free(decoder.table_length);
    if (decoder.packed) xx_mem_free(decoder.packed);
    if (decoder.plain) xx_mem_free(decoder.plain);
    return result;
}

/* ------------------------------------------------------------- records -- */

static void cloop_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static const char *cloop_member_name(const cloop_info *info) {
    return info->iso ? "disk.iso" : "disk.img";
}

static bool cloop_copy_options(xx_list_s *destination,
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

static const xx_var *cloop_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cloop_set_record(Abstractformat *format,
                             xx_archive_record *record,
                             const cloop_info *info) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = CLOOP_TOC_OFFSET +
                          ((int64_t)info->block_count + 1) * CLOOP_TOC_ENTRY;
    /* The decoder reads the table of contents again for itself, so the
     * member's extent is the whole container. */
    record->data_offset = format->base_address;
    record->compressed_size = info->data_end;
    return xx_archive_record_set_original_name(record,
                                               cloop_member_name(info)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)info->data_end) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          info->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          info->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_cloop_init(xx_cloop *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_CLOOP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cloop");
    xx_format_set_extension(&archive->format, "cloop");
    archive->format.check_is_valid = xx_cloop_check_is_valid;
    archive->format.handle_base_info = xx_cloop_handle_base_info;
    archive->format.get_format_size = xx_cloop_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cloop_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cloop_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cloop_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cloop_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cloop_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cloop_free_archive_records_reading;
}

xx_cloop *xx_cloop_create(xx_io_device *device, int64_t base_address) {
    xx_cloop *archive = (xx_cloop *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_cloop_init(archive, device, base_address);
    return archive;
}

void xx_cloop_destroy(xx_cloop *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_cloop_free(xx_cloop *archive) {
    if (!archive) return;
    xx_cloop_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_cloop_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    cloop_info info;
    return cloop_parse(format, &info, pd);
}

bool xx_cloop_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    cloop_info info;
    xx_cloop *archive;
    if (!format) return false;
    if (!cloop_parse(format, &info, pd)) {
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        format->is_valid = false;
        format->base_info_handled = false;
        return false;
    }
    archive = (xx_cloop *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = info.unpacked_size;
    archive->block_size = info.block_size;
    archive->block_count = info.block_count;
    archive->method = info.method;
    format->number_of_archive_records = 1U;
    format->format_size = info.data_end;
    format->file_type = XX_CLOOP_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_cloop_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cloop_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_cloop_get_number_of_archive_records(Abstractformat *format,
                                                xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cloop_handle_base_info(format, pd))
               ? ((xx_cloop *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_cloop_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    cloop_stream *stream;
    xx_archive_record_state *state;
    cloop_info info;
    if (!cloop_parse(format, &info, pd)) return NULL;
    stream = (cloop_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->info = info;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = cloop_stream_free;
    state->total_records = 1;
    if (!cloop_copy_options(&state->options, options) ||
        !cloop_set_record(format, &state->current_record, &stream->info)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_cloop_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cloop_archive_record_move_to_next(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    cloop_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (cloop_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_cloop_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    cloop_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    const char *name;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (cloop_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    /* The name is one of two constants chosen here, never file data. */
    name = cloop_member_name(&stream->info);
    path_option = cloop_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return cloop_unpack_to_device(format, &stream->info, NULL, pd);
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
               ? xx_str_concat3(base, "/", name)
               : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = cloop_unpack_to_device(format, &stream->info, destination,
                                        pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_cloop_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
