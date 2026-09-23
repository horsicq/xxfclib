/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BMP images.  The validation and the carve size follow binwalk's "BMP image"
 * signature: src/signatures/bmp.rs runs the extractor as a dry run,
 * src/extractors/bmp.rs parses the file header, reads the DIB header size and
 * requires the pixel offset to lie past it, and src/structures/bmp.rs holds
 * the field checks.  binwalk's result.size is bfSize, and so is ours.
 *
 * Because the magic is two bytes, the reader is deliberately STRICTER than
 * binwalk: every binwalk check is applied first, in binwalk's order, and then
 * the header fields binwalk never looks at (planes, dimensions, the
 * depth/compression pair, the pixel array's extent) have to be what an
 * encoder actually writes.  The full list, and why each one is safe for real
 * files, is in xx_bmp.h.
 *
 * Not an archive: binwalk's extractor carves the image itself.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bmp/xx_bmp.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as BMP is registered there. */
#ifdef BMP
#define XX_BMP_FILE_TYPE XX_FILE_TYPE_BMP
#else
#define XX_BMP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* binwalk reads the file header and the u32 DIB header size before anything
 * else, so nothing shorter than this is ever examined. */
#define XX_BMP_PREFIX_SIZE (XX_BMP_FILE_HEADER_SIZE + 4U)

/* Of the DIB header only the BITMAPINFOHEADER part is ever needed; the
 * V4/V5 colour-space tail carries nothing this reader validates. */
#define XX_BMP_INFO_FIELDS_SIZE XX_BMP_INFO_HEADER_SIZE

typedef struct xx_bmp_parsed_s {
    int64_t input_size;
    uint32_t file_size;
    uint32_t data_offset;
    uint32_t dib_header_size;
    uint32_t width;
    uint32_t height;
    bool top_down;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    uint32_t colors_used;
} xx_bmp_parsed;

static void xx_bmp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: a BMP carved out of a flash dump can
 * sit past 2 GiB, and long is 32-bit on Win64. */
static bool xx_bmp_read_at(xx_io_device *device, int64_t offset, void *data,
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

static uint16_t xx_bmp_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t xx_bmp_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

/* The DIB header sizes binwalk's get_dib_header_size() accepts. */
static bool xx_bmp_dib_size_is_known(uint32_t size) {
    return size == XX_BMP_CORE_HEADER_SIZE || size == XX_BMP_INFO_HEADER_SIZE ||
           size == XX_BMP_V4_HEADER_SIZE || size == XX_BMP_V5_HEADER_SIZE;
}

/* The documented (depth, compression) pairs.  BITMAPCOREHEADER has no
 * compression field and only the four OS/2 1.x depths.  The 24-bit
 * BI_BITFIELDS form is not in Microsoft's table but is read by common
 * decoders and costs nothing to allow.  BI_JPEG / BI_PNG are meant to carry
 * depth 0; a nonzero standard depth is tolerated because the embedded
 * stream's own signature is checked instead. */
static bool xx_bmp_depth_is_valid(uint32_t dib_header_size, uint16_t bpp,
                                  uint32_t compression) {
    if (dib_header_size == XX_BMP_CORE_HEADER_SIZE) {
        return compression == XX_BMP_BI_RGB &&
               (bpp == 1U || bpp == 4U || bpp == 8U || bpp == 24U);
    }
    switch (compression) {
    case XX_BMP_BI_RGB:
        return bpp == 1U || bpp == 2U || bpp == 4U || bpp == 8U ||
               bpp == 16U || bpp == 24U || bpp == 32U || bpp == 64U;
    case XX_BMP_BI_RLE8:
        return bpp == 8U;
    case XX_BMP_BI_RLE4:
        return bpp == 4U;
    case XX_BMP_BI_BITFIELDS:
        return bpp == 16U || bpp == 24U || bpp == 32U;
    case XX_BMP_BI_ALPHABITFIELDS:
        return bpp == 16U || bpp == 32U;
    case XX_BMP_BI_JPEG:
    case XX_BMP_BI_PNG:
        return bpp == 0U || bpp == 1U || bpp == 2U || bpp == 4U ||
               bpp == 8U || bpp == 16U || bpp == 24U || bpp == 32U;
    default:
        /* BI_CMYK* (11..13) are metafile-only; anything else is unknown. */
        return false;
    }
}

/* --------------------------------------------------------------- parse -- */

static bool xx_bmp_parse(Abstractformat *self, xx_bmp_parsed *parsed,
                         xx_pd_struct *pd) {
    uint8_t prefix[XX_BMP_PREFIX_SIZE];
    uint8_t dib[XX_BMP_INFO_FIELDS_SIZE];
    int64_t available;
    int64_t start;
    uint64_t pixel_bytes;
    uint64_t row_bytes;
    uint32_t dib_read;
    uint32_t payload;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) return false;
    available = parsed->input_size - self->base_address;
    start = self->base_address;

    /* --- binwalk: parse_bmp_file_header + get_dib_header_size ---------- */
    if (available < (int64_t)XX_BMP_PREFIX_SIZE) return false;
    if (!xx_bmp_read_at(self->device, start, prefix, sizeof(prefix))) {
        return false;
    }
    if (prefix[0] != 0x42U || prefix[1] != 0x4DU) return false; /* "BM" */
    parsed->file_size = xx_bmp_u32(prefix + 2);
    parsed->data_offset = xx_bmp_u32(prefix + 10);
    parsed->dib_header_size = xx_bmp_u32(prefix + 14);

    /* bfSize: nonzero and no larger than what is there. */
    if (parsed->file_size == 0U ||
        (int64_t)parsed->file_size > available) {
        return false;
    }
    /* bfOffBits: nonzero and inside the available data. */
    if (parsed->data_offset == 0U ||
        (int64_t)parsed->data_offset > available) {
        return false;
    }
    if (!xx_bmp_dib_size_is_known(parsed->dib_header_size)) return false;
    /* The pixel array cannot start inside the DIB header. */
    if (parsed->data_offset <
        XX_BMP_FILE_HEADER_SIZE + parsed->dib_header_size) {
        return false;
    }

    /* --- beyond binwalk ------------------------------------------------- */
    /* The pixel array starts inside the file.  With the check above this
     * also puts the whole DIB header inside bfSize, so it can be read. */
    if (parsed->data_offset > parsed->file_size) return false;

    dib_read = parsed->dib_header_size < XX_BMP_INFO_FIELDS_SIZE
                   ? parsed->dib_header_size
                   : XX_BMP_INFO_FIELDS_SIZE;
    if (!xx_bmp_read_at(self->device, start + (int64_t)XX_BMP_FILE_HEADER_SIZE,
                        dib, dib_read)) {
        return false;
    }

    if (parsed->dib_header_size == XX_BMP_CORE_HEADER_SIZE) {
        /* BITMAPCOREHEADER: u16 width, u16 height (always bottom-up),
         * u16 planes, u16 depth. */
        parsed->width = xx_bmp_u16(dib + 4);
        parsed->height = xx_bmp_u16(dib + 6);
        parsed->top_down = false;
        parsed->planes = xx_bmp_u16(dib + 8);
        parsed->bits_per_pixel = xx_bmp_u16(dib + 10);
        parsed->compression = XX_BMP_BI_RGB;
    } else {
        int32_t width = (int32_t)xx_bmp_u32(dib + 4);
        int32_t height = (int32_t)xx_bmp_u32(dib + 8);

        if (width <= 0 || height == 0 || height == INT32_MIN) return false;
        parsed->width = (uint32_t)width;
        parsed->top_down = height < 0;
        parsed->height = height < 0 ? (uint32_t)(-(int64_t)height)
                                    : (uint32_t)height;
        parsed->planes = xx_bmp_u16(dib + 12);
        parsed->bits_per_pixel = xx_bmp_u16(dib + 14);
        parsed->compression = xx_bmp_u32(dib + 16);
        parsed->image_size = xx_bmp_u32(dib + 20);
        parsed->colors_used = xx_bmp_u32(dib + 32);
    }

    if (parsed->planes != 1U) return false;
    if (parsed->width == 0U || parsed->height == 0U ||
        parsed->width > XX_BMP_MAX_DIMENSION ||
        parsed->height > XX_BMP_MAX_DIMENSION) {
        return false;
    }
    if (!xx_bmp_depth_is_valid(parsed->dib_header_size,
                               parsed->bits_per_pixel, parsed->compression)) {
        return false;
    }

    /* What lies between bfOffBits and bfSize has to be able to hold the
     * image.  data_offset <= file_size was checked above. */
    payload = parsed->file_size - parsed->data_offset;
    switch (parsed->compression) {
    case XX_BMP_BI_RGB:
    case XX_BMP_BI_BITFIELDS:
    case XX_BMP_BI_ALPHABITFIELDS:
        /* Unpadded rows: the minimum any encoder writes.  width and height
         * are <= 2^24 and depth <= 64, so this stays below 2^51. */
        row_bytes = ((uint64_t)parsed->width * parsed->bits_per_pixel + 7U) /
                    8U;
        pixel_bytes = row_bytes * (uint64_t)parsed->height;
        if (pixel_bytes > (uint64_t)payload) return false;
        break;
    case XX_BMP_BI_RLE8:
    case XX_BMP_BI_RLE4:
        /* The shortest RLE bitmap is the end-of-bitmap escape 00 01. */
        if (payload < 2U) return false;
        break;
    case XX_BMP_BI_JPEG: {
        uint8_t soi[3];
        if (payload < sizeof(soi) ||
            !xx_bmp_read_at(self->device, start + parsed->data_offset, soi,
                            sizeof(soi))) {
            return false;
        }
        if (soi[0] != 0xFFU || soi[1] != 0xD8U || soi[2] != 0xFFU) {
            return false;
        }
        break;
    }
    case XX_BMP_BI_PNG: {
        static const uint8_t png_signature[8] = {0x89U, 0x50U, 0x4EU, 0x47U,
                                                 0x0DU, 0x0AU, 0x1AU, 0x0AU};
        uint8_t sig[8];
        if (payload < sizeof(sig) ||
            !xx_bmp_read_at(self->device, start + parsed->data_offset, sig,
                            sizeof(sig))) {
            return false;
        }
        if (xx_rt_memcmp(sig, png_signature, sizeof(sig)) != 0) return false;
        break;
    }
    default:
        return false;
    }
    return !(pd && xx_pd_is_stopped(pd));
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_bmp_init(xx_bmp *bmp, xx_io_device *dev, int64_t base_address) {
    if (!bmp) return;
    xx_mem_zero(bmp, sizeof(*bmp));
    xx_format_init(&bmp->format, dev, base_address);
    bmp->format.endian = XX_ENDIAN_LITTLE;
    bmp->format.file_type = XX_BMP_FILE_TYPE;
    /* The format-type enum has no image kind; a bitmap is not an archive,
     * executable, firmware or package, so it stays UNKNOWN. */
    bmp->format.format_type = XX_TYPE_UNKNOWN;
    bmp->format.is_archive = false;
    xx_format_set_mime_type(&bmp->format, "image/bmp");
    xx_format_set_extension(&bmp->format, "bmp");
    bmp->format.check_is_valid = xx_bmp_check_is_valid;
    bmp->format.handle_base_info = xx_bmp_handle_base_info;
    bmp->format.get_format_size = xx_bmp_get_format_size;
    bmp->format.destroy = xx_bmp_vtable_destroy;
}

xx_bmp *xx_bmp_create(xx_io_device *dev, int64_t base_address) {
    xx_bmp *bmp = (xx_bmp *)xx_mem_alloc(sizeof(*bmp));

    if (bmp) xx_bmp_init(bmp, dev, base_address);
    return bmp;
}

void xx_bmp_destroy(xx_bmp *bmp) {
    if (!bmp) return;
    xx_format_cleanup_extra_parameters(&bmp->format);
}

static void xx_bmp_vtable_destroy(Abstractformat *self) {
    xx_bmp_destroy((xx_bmp *)self);
}

void xx_bmp_free(xx_bmp *bmp) {
    if (!bmp) return;
    xx_bmp_destroy(bmp);
    xx_mem_free(bmp);
}

/* -------------------------------------------------------------- format -- */

bool xx_bmp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_bmp_parsed parsed;

    return xx_bmp_parse(self, &parsed, pd);
}

bool xx_bmp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bmp *bmp = (xx_bmp *)self;
    xx_bmp_parsed parsed;
    int64_t end;

    if (!self || !bmp) return false;
    if (!xx_bmp_parse(self, &parsed, pd)) {
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    bmp->file_size = parsed.file_size;
    bmp->data_offset = parsed.data_offset;
    bmp->dib_header_size = parsed.dib_header_size;
    bmp->width = parsed.width;
    bmp->height = parsed.height;
    bmp->top_down = parsed.top_down;
    bmp->bits_per_pixel = parsed.bits_per_pixel;
    bmp->compression = parsed.compression;
    bmp->image_size = parsed.image_size;
    bmp->colors_used = parsed.colors_used;

    /* binwalk's carve length.  parse() guaranteed base + bfSize <= total. */
    end = self->base_address + (int64_t)parsed.file_size;
    self->format_size = (int64_t)parsed.file_size;
    if (end < parsed.input_size) {
        self->overlay_offset = end;
        self->overlay_size = parsed.input_size - end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_BMP_FILE_TYPE;
    self->number_of_archive_records = 0U;
    self->is_archive = false;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_bmp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

uint32_t xx_bmp_get_width(const xx_bmp *bmp) {
    return bmp ? bmp->width : 0U;
}

uint32_t xx_bmp_get_height(const xx_bmp *bmp) {
    return bmp ? bmp->height : 0U;
}

bool xx_bmp_is_top_down(const xx_bmp *bmp) {
    return bmp ? bmp->top_down : false;
}

uint16_t xx_bmp_get_bits_per_pixel(const xx_bmp *bmp) {
    return bmp ? bmp->bits_per_pixel : 0U;
}

uint32_t xx_bmp_get_compression(const xx_bmp *bmp) {
    return bmp ? bmp->compression : 0U;
}

uint32_t xx_bmp_get_dib_header_size(const xx_bmp *bmp) {
    return bmp ? bmp->dib_header_size : 0U;
}

uint32_t xx_bmp_get_data_offset(const xx_bmp *bmp) {
    return bmp ? bmp->data_offset : 0U;
}
