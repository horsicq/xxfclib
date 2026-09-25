/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PNG image reader, ported from binwalk: src/signatures/png.rs (the 16-byte
 * magic), src/structures/png.rs (parse_png_chunk_header) and
 * src/extractors/png.rs (get_png_data_size).  binwalk validates a PNG by an
 * extraction dry run, and the dry run is nothing but a chunk walk to IEND;
 * the offset just past the IEND chunk is the carve length (result.size).
 * xx_png_walk() below is that walk, decision for decision, so format_size is
 * the same number binwalk reports.
 *
 * The walk, as binwalk does it (offsets relative to the image start):
 *
 *   magic: 89 50 4E 47 0D 0A 1A 0A 00 00 00 0D 49 48 44 52 at 0
 *   pos = 8
 *   loop:
 *     pos must be < size of the data              (is_offset_safe)
 *     8 bytes at pos must exist, else FAIL        (common::parse)
 *     length = u32 BE at pos, type = u32 BE at pos + 4
 *     pos += 8 + length + 4                       (header + data + CRC)
 *     if type == "IEND": SUCCESS, size = pos
 *   then binwalk.rs drops any result whose end lies past the end of the
 *   file, so size <= data size is part of the verdict.
 *
 * Every chunk is at least 12 bytes, so pos strictly increases (binwalk's
 * previous-offset test can never fire) and the walk is bounded by the data
 * size divided by 12.
 *
 * Deliberate, stricter-than-binwalk rules.  Each is a MUST of the PNG
 * specification (ISO/IEC 15948, W3C PNG 2nd/3rd edition), none changes the
 * size of any image this reader accepts, and together they turn a walk
 * through garbage or a damaged header into a rejection:
 *
 *   - IHDR (always the first chunk, guaranteed by the magic): width and
 *     height in 1..2^31-1, a legal colour type / bit depth pair, compression
 *     method 0, filter method 0, interlace method 0 or 1, and the chunk's
 *     CRC-32 must match.  libpng treats a CRC error in a critical chunk as
 *     fatal, so an image failing it does not decode anywhere.
 *   - every chunk length is at most 2^31-1;
 *   - every chunk type is four ASCII letters (A-Z, a-z);
 *   - IEND carries no data (length 0);
 *   - at least one IDAT precedes IEND (the image data is mandatory).
 *
 * Only the IHDR CRC is checked.  Checking every chunk's CRC would read the
 * whole image, which a size walk otherwise never does; it is left to a
 * decoder.
 *
 * Not an archive.  binwalk's extractor carves the image itself to image.png
 * and nothing inside is extracted, so the reader validates, reports the size,
 * the IHDR fields and a few counters, and publishes everything past the IEND
 * chunk as overlay.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/png/xx_png.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as PNG is registered there.
 * Delete this block once XX_FILE_TYPE_PNG exists in the enum. */
#ifdef PNG
#define XX_PNG_FILE_TYPE XX_FILE_TYPE_PNG
#else
#define XX_PNG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Chunk type codes, as the big-endian u32 binwalk compares them. */
#define XX_PNG_TYPE_IEND UINT32_C(0x49454E44) /* "IEND" */
#define XX_PNG_TYPE_IDAT UINT32_C(0x49444154) /* "IDAT" */
#define XX_PNG_TYPE_ACTL UINT32_C(0x6163544C) /* "acTL" */

/* PNG spec: a chunk length "must not exceed 2^31-1". */
#define XX_PNG_MAX_CHUNK_LENGTH UINT32_C(0x7FFFFFFF)
/* Same bound for IHDR width and height. */
#define XX_PNG_MAX_DIMENSION UINT32_C(0x7FFFFFFF)

/* The lead read in one go: magic (16) + IHDR data (13) + IHDR CRC (4). */
#define XX_PNG_LEAD_SIZE \
    (XX_PNG_MAGIC_SIZE + XX_PNG_IHDR_DATA_SIZE + XX_PNG_CHUNK_CRC_SIZE)

/* Read-ahead window for chunk headers.  An image with many small chunks
 * (an APNG with hundreds of fcTL / fdAT pairs) is then read in blocks
 * rather than eight bytes at a time; a chunk larger than the window costs
 * one seek. */
#define XX_PNG_WINDOW_SIZE ((size_t)16384U)

/* How many chunks between two stop-flag polls. */
#define XX_PNG_POLL_INTERVAL 4096U

static const uint8_t xx_png_magic[XX_PNG_MAGIC_SIZE] = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU,
    0x00U, 0x00U, 0x00U, 0x0DU, 0x49U, 0x48U, 0x44U, 0x52U};

typedef struct xx_png_parsed_s {
    int64_t input_size;
    int64_t image_size; /* relative to base_address */
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t colour_type;
    uint8_t interlace;
    uint32_t chunk_count;
    uint32_t idat_count;
    int64_t idat_size;
    bool is_animated;
    uint32_t frame_count;
    uint32_t play_count;
} xx_png_parsed;

typedef struct xx_png_cursor_s {
    xx_io_device *device;
    int64_t base;         /* absolute offset of the signature */
    int64_t span;         /* bytes from base to the end of the device */
    uint8_t *window;      /* window_capacity bytes */
    size_t window_capacity;
    int64_t window_start; /* offset of window[0], relative to base */
    size_t window_size;   /* valid bytes in window */
} xx_png_cursor;

static void xx_png_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: the base address inside a larger
 * image is not bounded by any 32-bit field, and long is 32-bit on Win64. */
static bool xx_png_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static uint32_t xx_png_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Copy @p size bytes at @p offset (relative to base) out of the window,
 * refilling it when the range is not fully inside.  False when the range
 * runs past the end of the device or on a read error. */
static bool xx_png_cursor_get(xx_png_cursor *cursor, int64_t offset,
                              uint8_t *out, size_t size) {
    int64_t remain;
    size_t want;

    if (offset < 0 || size > cursor->window_capacity ||
        offset > cursor->span - (int64_t)size) {
        return false;
    }
    if (!(offset >= cursor->window_start &&
          offset - cursor->window_start <=
              (int64_t)cursor->window_size - (int64_t)size)) {
        remain = cursor->span - offset;
        want = remain < (int64_t)cursor->window_capacity
                   ? (size_t)remain
                   : cursor->window_capacity;
        cursor->window_size = 0U;
        /* base + offset < base + span == device size: no overflow. */
        if (!xx_png_read_at(cursor->device, cursor->base + offset,
                            cursor->window, want)) {
            return false;
        }
        cursor->window_start = offset;
        cursor->window_size = want;
    }
    xx_rt_memcpy(out,
                 cursor->window + (size_t)(offset - cursor->window_start),
                 size);
    return true;
}

static bool xx_png_is_letter(uint8_t c) {
    return (c >= 0x41U && c <= 0x5AU) || (c >= 0x61U && c <= 0x7AU);
}

/* PNG spec table 11.1: allowed bit depths per colour type. */
static bool xx_png_depth_is_legal(uint8_t colour_type, uint8_t bit_depth) {
    switch (colour_type) {
        case XX_PNG_COLOUR_GRAYSCALE:
            return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U ||
                   bit_depth == 8U || bit_depth == 16U;
        case XX_PNG_COLOUR_PALETTE:
            return bit_depth == 1U || bit_depth == 2U || bit_depth == 4U ||
                   bit_depth == 8U;
        case XX_PNG_COLOUR_RGB:
        case XX_PNG_COLOUR_GRAYSCALE_ALPHA:
        case XX_PNG_COLOUR_RGBA:
            return bit_depth == 8U || bit_depth == 16U;
        default:
            return false;
    }
}

/* The 16-byte magic, the IHDR fields and the IHDR CRC.  @p lead holds
 * XX_PNG_LEAD_SIZE bytes from the image start. */
static bool xx_png_check_lead(const uint8_t *lead, xx_png_parsed *parsed) {
    const uint8_t *ihdr = lead + XX_PNG_MAGIC_SIZE;
    uint32_t stored_crc;
    uint32_t crc;

    if (xx_rt_memcmp(lead, xx_png_magic, XX_PNG_MAGIC_SIZE) != 0) {
        return false;
    }
    parsed->width = xx_png_be32(ihdr + 0);
    parsed->height = xx_png_be32(ihdr + 4);
    parsed->bit_depth = ihdr[8];
    parsed->colour_type = ihdr[9];
    parsed->interlace = ihdr[12];
    if (parsed->width == 0U || parsed->width > XX_PNG_MAX_DIMENSION ||
        parsed->height == 0U || parsed->height > XX_PNG_MAX_DIMENSION) {
        return false;
    }
    if (!xx_png_depth_is_legal(parsed->colour_type, parsed->bit_depth)) {
        return false;
    }
    /* compression method 0 (deflate), filter method 0 (adaptive),
     * interlace method 0 (none) or 1 (Adam7) are the only ones defined. */
    if (ihdr[10] != 0U || ihdr[11] != 0U || ihdr[12] > 1U) return false;

    /* CRC-32 over the chunk type and data: bytes 12..28 of the lead. */
    stored_crc = xx_png_be32(ihdr + XX_PNG_IHDR_DATA_SIZE);
    crc = xx_crc32_calc(0U, lead + XX_PNG_SIGNATURE_SIZE + 4U,
                        4U + XX_PNG_IHDR_DATA_SIZE);
    return crc == stored_crc;
}

/* The chunk walk of binwalk's get_png_data_size, plus the length / type /
 * IEND / IDAT rules described at the top of the file. */
static bool xx_png_walk(xx_png_cursor *cursor, xx_png_parsed *parsed,
                        xx_pd_struct *pd) {
    int64_t pos = (int64_t)XX_PNG_SIGNATURE_SIZE;
    uint32_t iterations = 0U;

    for (;;) {
        uint8_t header[XX_PNG_CHUNK_HEADER_SIZE];
        uint32_t length;
        uint32_t type;
        int64_t chunk_end;
        size_t index;

        if (++iterations % XX_PNG_POLL_INTERVAL == 0U &&
            xx_pd_is_stopped(pd)) {
            return false;
        }
        /* is_offset_safe + common::parse: the whole 8-byte header must be
         * inside the data. */
        if (pos >= cursor->span ||
            !xx_png_cursor_get(cursor, pos, header, sizeof(header))) {
            return false;
        }
        length = xx_png_be32(header);
        type = xx_png_be32(header + 4);
        if (length > XX_PNG_MAX_CHUNK_LENGTH) return false;
        for (index = 4U; index < 8U; ++index) {
            if (!xx_png_is_letter(header[index])) return false;
        }
        if (parsed->chunk_count < UINT32_MAX) ++parsed->chunk_count;

        /* pos < span and length < 2^31: no overflow in int64. */
        chunk_end = pos + (int64_t)XX_PNG_CHUNK_HEADER_SIZE +
                    (int64_t)length + (int64_t)XX_PNG_CHUNK_CRC_SIZE;

        if (type == XX_PNG_TYPE_IEND) {
            if (length != 0U || parsed->idat_count == 0U) return false;
            /* binwalk.rs: a result ending past EOF is ignored. */
            if (chunk_end > cursor->span) return false;
            parsed->image_size = chunk_end;
            return true;
        }
        if (type == XX_PNG_TYPE_IDAT) {
            if (parsed->idat_count < UINT32_MAX) ++parsed->idat_count;
            parsed->idat_size += (int64_t)length;
        } else if (type == XX_PNG_TYPE_ACTL && !parsed->is_animated &&
                   length >= 8U && chunk_end <= cursor->span) {
            /* acTL: num_frames u32, num_plays u32 -- informational. */
            uint8_t actl[8];
            if (!xx_png_cursor_get(cursor,
                                   pos + (int64_t)XX_PNG_CHUNK_HEADER_SIZE,
                                   actl, sizeof(actl))) {
                return false;
            }
            parsed->is_animated = true;
            parsed->frame_count = xx_png_be32(actl);
            parsed->play_count = xx_png_be32(actl + 4);
        }
        pos = chunk_end;
    }
}

/* --------------------------------------------------------------- parse -- */

static bool xx_png_parse(Abstractformat *self, xx_png_parsed *parsed,
                         xx_pd_struct *pd) {
    uint8_t lead[XX_PNG_LEAD_SIZE];
    xx_png_cursor cursor;
    int64_t span;
    bool result;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->image_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) return false;
    span = parsed->input_size - self->base_address;
    if (span < XX_PNG_MIN_SIZE) return false;

    if (!xx_png_read_at(self->device, self->base_address, lead,
                        sizeof(lead))) {
        return false;
    }
    if (!xx_png_check_lead(lead, parsed)) return false;

    xx_mem_zero(&cursor, sizeof(cursor));
    cursor.device = self->device;
    cursor.base = self->base_address;
    cursor.span = span;
    cursor.window_capacity = span < (int64_t)XX_PNG_WINDOW_SIZE
                                 ? (size_t)span
                                 : XX_PNG_WINDOW_SIZE;
    cursor.window = (uint8_t *)xx_mem_alloc(cursor.window_capacity);
    if (!cursor.window) return false;
    cursor.window_start = 0;
    cursor.window_size = 0U;

    result = xx_png_walk(&cursor, parsed, pd);
    xx_mem_free(cursor.window);

    if (!result || parsed->image_size < XX_PNG_MIN_SIZE ||
        parsed->image_size > span) {
        parsed->image_size = -1;
        return false;
    }
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_png_init(xx_png *png, xx_io_device *dev, int64_t base_address) {
    if (!png) return;
    xx_mem_zero(png, sizeof(*png));
    xx_format_init(&png->format, dev, base_address);
    /* Chunk lengths, CRCs and IHDR fields are big-endian. */
    png->format.endian = XX_ENDIAN_BIG;
    png->format.file_type = XX_PNG_FILE_TYPE;
    /* There is no image format type in the enum; a picture is neither code
     * nor a container. */
    png->format.format_type = XX_TYPE_UNKNOWN;
    png->format.is_archive = false;
    xx_format_set_mime_type(&png->format, "image/png");
    xx_format_set_extension(&png->format, "png");
    png->format.check_is_valid = xx_png_check_is_valid;
    png->format.handle_base_info = xx_png_handle_base_info;
    png->format.get_format_size = xx_png_get_format_size;
    png->format.destroy = xx_png_vtable_destroy;
    png->image_end = -1;
}

xx_png *xx_png_create(xx_io_device *dev, int64_t base_address) {
    xx_png *png = (xx_png *)xx_mem_alloc(sizeof(*png));

    if (png) xx_png_init(png, dev, base_address);
    return png;
}

void xx_png_destroy(xx_png *png) {
    if (!png) return;
    xx_format_cleanup_extra_parameters(&png->format);
}

static void xx_png_vtable_destroy(Abstractformat *self) {
    xx_png_destroy((xx_png *)self);
}

void xx_png_free(xx_png *png) {
    if (!png) return;
    xx_png_destroy(png);
    xx_mem_free(png);
}

/* -------------------------------------------------------------- format -- */

bool xx_png_check_magic(const uint8_t *data, size_t size) {
    return data && size >= XX_PNG_MAGIC_SIZE &&
           xx_rt_memcmp(data, xx_png_magic, XX_PNG_MAGIC_SIZE) == 0;
}

bool xx_png_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_png_parsed parsed;

    return xx_png_parse(self, &parsed, pd);
}

bool xx_png_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_png *png = (xx_png *)self;
    xx_png_parsed parsed;
    int64_t image_end;

    if (!self || !png) return false;
    if (!xx_png_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    image_end = self->base_address + parsed.image_size;

    png->image_end = image_end;
    png->width = parsed.width;
    png->height = parsed.height;
    png->bit_depth = parsed.bit_depth;
    png->colour_type = parsed.colour_type;
    png->interlace = parsed.interlace;
    png->chunk_count = parsed.chunk_count;
    png->idat_count = parsed.idat_count;
    png->idat_size = parsed.idat_size;
    png->is_animated = parsed.is_animated;
    png->frame_count = parsed.frame_count;
    png->play_count = parsed.play_count;

    self->format_size = parsed.image_size;
    if (parsed.input_size > image_end) {
        self->overlay_offset = image_end;
        self->overlay_size = parsed.input_size - image_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_png_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

int64_t xx_png_get_image_end(const xx_png *png) {
    return png ? png->image_end : -1;
}

uint32_t xx_png_get_width(const xx_png *png) { return png ? png->width : 0U; }

uint32_t xx_png_get_height(const xx_png *png) {
    return png ? png->height : 0U;
}

uint8_t xx_png_get_bit_depth(const xx_png *png) {
    return png ? png->bit_depth : 0U;
}

uint8_t xx_png_get_colour_type(const xx_png *png) {
    return png ? png->colour_type : 0U;
}

uint8_t xx_png_get_interlace(const xx_png *png) {
    return png ? png->interlace : 0U;
}

uint32_t xx_png_get_chunk_count(const xx_png *png) {
    return png ? png->chunk_count : 0U;
}

uint32_t xx_png_get_idat_count(const xx_png *png) {
    return png ? png->idat_count : 0U;
}

bool xx_png_is_animated(const xx_png *png) {
    return png ? png->is_animated : false;
}

uint32_t xx_png_get_frame_count(const xx_png *png) {
    return png ? png->frame_count : 0U;
}
