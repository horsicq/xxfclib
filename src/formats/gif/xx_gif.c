/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GIF 87a/89a images.  Detection, validation and size follow binwalk's
 * src/signatures/gif.rs, src/structures/gif.rs and src/extractors/gif.rs:
 * the header, then every block walked to the 0x3B trailer.  The size this
 * reader reports is binwalk's carve length for a GIF at the base address,
 * byte for byte; anything after the trailer is overlay.  The layout and the
 * one quirk that matters for the size (Application / Plain Text extensions)
 * are described in xx_gif.h.
 *
 * NOT an archive.  binwalk's extractor carves the GIF itself and declines
 * even that at offset 0, so there is nothing inside to publish as a record.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gif/xx_gif.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_GIF exists in the enum. */
#ifdef GIF
#define XX_GIF_FILE_TYPE XX_FILE_TYPE_GIF
#else
#define XX_GIF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Header / descriptor flag bits (same layout in both places). */
#define XX_GIF_FLAG_COLOR_TABLE 0x80U
#define XX_GIF_FLAG_TABLE_SIZE_MASK 0x07U

/* Read-ahead window for the block walk.  Sub-blocks are at most 256 bytes,
 * so one refill covers a run of them; the window lives on the stack of the
 * parse and is never larger than this. */
#define XX_GIF_WINDOW_SIZE 4096U

/* How often (in walked sub-blocks / blocks) the stop flag is polled. */
#define XX_GIF_STOP_POLL_MASK 0x3FFU

typedef struct xx_gif_parsed_s {
    int64_t input_size;
    int64_t end;            /* absolute offset one past the trailer */
    int64_t trailer_offset; /* absolute offset of the trailer byte */
    uint64_t images;
    uint64_t extensions;
    uint32_t global_color_table_size;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint8_t flags;
    uint8_t bg_color_index;
    uint8_t aspect_ratio;
} xx_gif_parsed;

typedef struct xx_gif_cursor_s {
    xx_io_device *device;
    int64_t end;          /* device size: nothing at or past this is read */
    int64_t window_start; /* absolute offset of window[0] */
    size_t window_size;   /* valid bytes in window, 0 = empty */
    uint8_t window[XX_GIF_WINDOW_SIZE];
} xx_gif_cursor;

static void xx_gif_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: long is 32-bit on Win64. */
static bool xx_gif_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* One byte at an absolute offset, through the read-ahead window.  Refuses
 * anything outside [0, end), so no caller can read past the device. */
static bool xx_gif_byte(xx_gif_cursor *cursor, int64_t offset,
                        uint8_t *value) {
    if (!cursor || !value || offset < 0 || offset >= cursor->end) {
        return false;
    }
    if (cursor->window_size == 0U || offset < cursor->window_start ||
        offset - cursor->window_start >= (int64_t)cursor->window_size) {
        int64_t remaining = cursor->end - offset;
        size_t want = remaining < (int64_t)XX_GIF_WINDOW_SIZE
                          ? (size_t)remaining
                          : (size_t)XX_GIF_WINDOW_SIZE;
        cursor->window_size = 0U;
        if (!xx_gif_read_at(cursor->device, offset, cursor->window, want)) {
            return false;
        }
        cursor->window_start = offset;
        cursor->window_size = want;
    }
    *value = cursor->window[offset - cursor->window_start];
    return true;
}

/* Colour table size in bytes for a header / image-descriptor flags byte:
 * 3 * 2^(n+1), at most 768.  Zero when bit 7 is clear. */
static uint32_t xx_gif_color_table_size(uint8_t flags) {
    if ((flags & XX_GIF_FLAG_COLOR_TABLE) == 0U) return 0U;
    return 3U * (UINT32_C(1) << ((flags & XX_GIF_FLAG_TABLE_SIZE_MASK) + 1U));
}

/* Walks data sub-blocks starting at `offset` up to and including the zero
 * terminator; `*next` receives the offset just past it.  Mirrors binwalk's
 * parse_gif_sub_blocks: a length byte that would carry the walk to or past
 * the end of the device, before a terminator is seen, is a failure.  Every
 * step advances by at least two bytes and stays below `end`, so the loop is
 * bounded by the device size; the stop flag is polled along the way. */
static bool xx_gif_skip_sub_blocks(xx_gif_cursor *cursor, int64_t offset,
                                   int64_t *next, xx_pd_struct *pd) {
    uint32_t steps = 0U;

    while (offset >= 0 && offset < cursor->end) {
        uint8_t length;
        if (!xx_gif_byte(cursor, offset, &length)) return false;
        if (length == 0U) {
            *next = offset + 1;
            return true;
        }
        /* offset + length + 1 >= end: binwalk's loop ends without having
         * found a terminator.  Written as a subtraction so it cannot
         * overflow. */
        if ((int64_t)length + 1 >= cursor->end - offset) return false;
        offset += (int64_t)length + 1;
        if ((++steps & XX_GIF_STOP_POLL_MASK) == 0U && pd &&
            xx_pd_is_stopped(pd)) {
            return false;
        }
    }
    return false;
}

/* --------------------------------------------------------------- parse -- */

static void xx_gif_parsed_reset(xx_gif_parsed *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->end = -1;
    parsed->trailer_offset = -1;
}

static bool xx_gif_parse(Abstractformat *self, xx_gif_parsed *parsed,
                         xx_pd_struct *pd) {
    uint8_t header[XX_GIF_HEADER_SIZE];
    xx_gif_cursor cursor;
    int64_t available;
    int64_t position;
    uint32_t header_size;
    uint32_t blocks = 0U;

    xx_gif_parsed_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) return false;
    available = parsed->input_size - self->base_address;
    if (available < (int64_t)XX_GIF_HEADER_SIZE ||
        !xx_gif_read_at(self->device, self->base_address, header,
                        sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, XX_GIF_SIGNATURE_89A, XX_GIF_SIGNATURE_SIZE) ==
        0) {
        parsed->version = 89U;
    } else if (xx_rt_memcmp(header, XX_GIF_SIGNATURE_87A,
                            XX_GIF_SIGNATURE_SIZE) == 0) {
        parsed->version = 87U;
    } else {
        return false;
    }
    parsed->width = xx_data_get_u16(header, sizeof(header), 6U, false);
    parsed->height = xx_data_get_u16(header, sizeof(header), 8U, false);
    parsed->flags = header[10];
    parsed->bg_color_index = header[11];
    parsed->aspect_ratio = header[12];
    parsed->global_color_table_size = xx_gif_color_table_size(parsed->flags);
    header_size = XX_GIF_HEADER_SIZE + parsed->global_color_table_size;

    /* The first block must start strictly inside the device: binwalk hands
     * an empty remainder to its block walk, which then fails. */
    if ((int64_t)header_size >= available) return false;

    xx_mem_zero(&cursor, sizeof(cursor));
    cursor.device = self->device;
    cursor.end = parsed->input_size;
    position = self->base_address + (int64_t)header_size;

    while (position < cursor.end) {
        uint8_t type;
        int64_t remaining = cursor.end - position;

        if ((++blocks & XX_GIF_STOP_POLL_MASK) == 0U && pd &&
            xx_pd_is_stopped(pd)) {
            return false;
        }
        if (!xx_gif_byte(&cursor, position, &type)) return false;

        if (type == XX_GIF_BLOCK_TRAILER) {
            parsed->trailer_offset = position;
            parsed->end = position + 1;
            return true;
        }

        if (type == XX_GIF_BLOCK_IMAGE) {
            uint8_t flags;
            int64_t data_offset;
            /* The fixed 10-byte descriptor must be present in full. */
            if (remaining < (int64_t)XX_GIF_IMAGE_DESCRIPTOR_SIZE ||
                !xx_gif_byte(&cursor, position + 9, &flags)) {
                return false;
            }
            /* descriptor + local colour table + LZW minimum code size byte;
             * at most 10 + 768 + 1, so no overflow.  The sub-blocks must
             * start strictly inside the device. */
            data_offset = (int64_t)XX_GIF_IMAGE_DESCRIPTOR_SIZE +
                          (int64_t)xx_gif_color_table_size(flags) + 1;
            if (data_offset >= remaining) return false;
            if (!xx_gif_skip_sub_blocks(&cursor, position + data_offset,
                                        &position, pd)) {
                return false;
            }
            ++parsed->images;
            continue;
        }

        if (type == XX_GIF_BLOCK_EXTENSION) {
            uint8_t label;
            uint8_t first;
            int64_t data_offset = 2;
            /* binwalk parses a 3-byte extension header for every label. */
            if (remaining < 3 || !xx_gif_byte(&cursor, position + 1, &label) ||
                !xx_gif_byte(&cursor, position + 2, &first)) {
                return false;
            }
            /* Application and Plain Text extensions open with a fixed-size
             * field whose length byte is skipped unconditionally, even when
             * it is zero; every other label goes straight to sub-blocks. */
            if (label == XX_GIF_EXTENSION_APPLICATION ||
                label == XX_GIF_EXTENSION_PLAIN_TEXT) {
                data_offset += (int64_t)first + 1;
            }
            if (data_offset >= remaining) return false;
            if (!xx_gif_skip_sub_blocks(&cursor, position + data_offset,
                                        &position, pd)) {
                return false;
            }
            ++parsed->extensions;
            continue;
        }

        /* Anything else where a block is expected ends the walk: no GIF. */
        return false;
    }
    /* Ran off the end without a trailer: no provable end, no GIF. */
    return false;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_gif_init(xx_gif *gif, xx_io_device *dev, int64_t base_address) {
    if (!gif) return;
    xx_mem_zero(gif, sizeof(*gif));
    xx_format_init(&gif->format, dev, base_address);
    gif->format.endian = XX_ENDIAN_LITTLE;
    gif->format.file_type = XX_GIF_FILE_TYPE;
    /* The format-type enum has no image kind; a GIF is not an archive,
     * executable, firmware or package, so it stays UNKNOWN. */
    gif->format.format_type = XX_TYPE_UNKNOWN;
    gif->format.is_archive = false;
    xx_format_set_mime_type(&gif->format, "image/gif");
    xx_format_set_extension(&gif->format, "gif");
    gif->format.check_is_valid = xx_gif_check_is_valid;
    gif->format.handle_base_info = xx_gif_handle_base_info;
    gif->format.get_format_size = xx_gif_get_format_size;
    gif->format.destroy = xx_gif_vtable_destroy;
    gif->trailer_offset = -1;
}

xx_gif *xx_gif_create(xx_io_device *dev, int64_t base_address) {
    xx_gif *gif = (xx_gif *)xx_mem_alloc(sizeof(*gif));

    if (gif) xx_gif_init(gif, dev, base_address);
    return gif;
}

void xx_gif_destroy(xx_gif *gif) {
    if (!gif) return;
    xx_format_cleanup_extra_parameters(&gif->format);
}

static void xx_gif_vtable_destroy(Abstractformat *self) {
    xx_gif_destroy((xx_gif *)self);
}

void xx_gif_free(xx_gif *gif) {
    if (!gif) return;
    xx_gif_destroy(gif);
    xx_mem_free(gif);
}

/* -------------------------------------------------------------- format -- */

bool xx_gif_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gif_parsed parsed;

    return xx_gif_parse(self, &parsed, pd);
}

bool xx_gif_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gif *gif = (xx_gif *)self;
    xx_gif_parsed parsed;

    if (!self || !gif) return false;
    if (!xx_gif_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    gif->version = parsed.version;
    gif->width = parsed.width;
    gif->height = parsed.height;
    gif->flags = parsed.flags;
    gif->bg_color_index = parsed.bg_color_index;
    gif->aspect_ratio = parsed.aspect_ratio;
    gif->global_color_table_size = parsed.global_color_table_size;
    gif->number_of_images = parsed.images;
    gif->number_of_extensions = parsed.extensions;
    gif->trailer_offset = parsed.trailer_offset;
    self->format_size = parsed.end - self->base_address;
    if (parsed.input_size > parsed.end) {
        self->overlay_offset = parsed.end;
        self->overlay_size = parsed.input_size - parsed.end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_gif_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

uint16_t xx_gif_get_version(const xx_gif *gif) {
    return gif ? gif->version : 0U;
}

uint16_t xx_gif_get_width(const xx_gif *gif) { return gif ? gif->width : 0U; }

uint16_t xx_gif_get_height(const xx_gif *gif) {
    return gif ? gif->height : 0U;
}

uint32_t xx_gif_get_global_color_table_size(const xx_gif *gif) {
    return gif ? gif->global_color_table_size : 0U;
}

uint64_t xx_gif_get_number_of_images(const xx_gif *gif) {
    return gif ? gif->number_of_images : 0U;
}

uint64_t xx_gif_get_number_of_extensions(const xx_gif *gif) {
    return gif ? gif->number_of_extensions : 0U;
}

int64_t xx_gif_get_trailer_offset(const xx_gif *gif) {
    return gif ? gif->trailer_offset : -1;
}
