/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JPEG image reader, ported from binwalk: src/signatures/jpeg.rs (the three
 * lead patterns) and src/extractors/jpeg.rs (get_jpeg_data_size).  binwalk
 * validates a JPEG by doing an extraction dry run, and the dry run is nothing
 * but a marker walk to the End Of Image marker; the offset just past FF D9 is
 * the carve length (result.size).  xx_jpeg_walk() below is that walk,
 * decision for decision, so format_size is the same number binwalk reports.
 *
 * The walk, as binwalk does it:
 *
 *   loop:
 *     byte at pos must be 0xFF, else FAIL;          pos += 1
 *     id = next byte (missing -> FAIL);              pos += 1
 *     unless id is 00 01 D0..D7 D8 D9:
 *         2-byte big-endian length must be present, else FAIL
 *         pos += length          (the length counts its own two bytes)
 *     if id == DA (Start Of Scan):
 *         advance pos one byte at a time until data[pos] == 0xFF and
 *         data[pos+1] is not 00 / D0..D7, needing two bytes each step;
 *         running out of data means FAIL
 *     if id == D9 (EOI): SUCCESS, size = pos
 *
 * Nothing else is checked: a length of 0 or 1 lands pos back on the length
 * bytes, which then fail the 0xFF test, so every iteration moves forward by
 * at least two bytes and the loop is bounded by the device size.
 *
 * One deliberate, stricter-than-binwalk rule: an id of 0x00 or 0xFF at a
 * marker position is refused.  binwalk lists 0x00 among the length-less
 * markers and treats 0xFF as a marker with a length, but FF 00 is a stuffed
 * data byte that can never start a segment, and FF FF is a fill byte, whose
 * "length" binwalk would read from the next marker.  Neither changes the
 * size of any stream this reader accepts; both only turn a walk through
 * garbage into a rejection.
 *
 * Not an archive.  binwalk's extractor carves the image itself to image.jpg
 * and there is nothing inside to enumerate (an Exif thumbnail is a segment
 * payload, which binwalk does not extract either).  The reader validates,
 * reports the size, the frame header of the first SOFn and a few counters,
 * and publishes everything past the EOI as overlay.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jpeg/xx_jpeg.h"

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as JPEG is registered there.
 * Delete this block once XX_FILE_TYPE_JPEG exists in the enum. */
#ifdef JPEG
#define XX_JPEG_FILE_TYPE XX_FILE_TYPE_JPEG
#else
#define XX_JPEG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_JPEG_MARKER_MAGIC 0xFFU
#define XX_JPEG_MARKER_SOS 0xDAU
#define XX_JPEG_MARKER_EOI 0xD9U

/* Read-ahead window for the walk.  Scan data is searched byte by byte, so
 * the device is read in blocks of this size instead of per byte. */
#define XX_JPEG_WINDOW_SIZE ((size_t)65536U)

/* How many markers between two stop-flag polls, and how many bytes of
 * entropy-coded data a scan search covers between two polls. */
#define XX_JPEG_POLL_INTERVAL 4096U
#define XX_JPEG_SCAN_POLL_BYTES ((int64_t)1 << 20)

static const uint8_t xx_jpeg_magic_jfif[XX_JPEG_MAX_MAGIC_SIZE] = {
    0xFFU, 0xD8U, 0xFFU, 0xE0U, 0x00U, 0x10U, 'J', 'F', 'I', 'F', 0x00U};
static const uint8_t xx_jpeg_magic_exif[4] = {0xFFU, 0xD8U, 0xFFU, 0xE1U};
static const uint8_t xx_jpeg_magic_dqt[4] = {0xFFU, 0xD8U, 0xFFU, 0xDBU};

typedef struct xx_jpeg_parsed_s {
    int64_t input_size;
    int64_t image_size; /* relative to base_address */
    xx_jpeg_variant variant;
    uint32_t marker_count;
    uint32_t scan_count;
    uint8_t frame_marker;
    uint8_t precision;
    uint16_t height;
    uint16_t width;
    uint8_t components;
} xx_jpeg_parsed;

typedef struct xx_jpeg_cursor_s {
    xx_io_device *device;
    int64_t base;         /* absolute offset of the SOI */
    int64_t span;         /* bytes from base to the end of the device */
    uint8_t *window;      /* window_capacity bytes */
    size_t window_capacity;
    int64_t window_start; /* offset of window[0], relative to base */
    size_t window_size;   /* valid bytes in window */
} xx_jpeg_cursor;

static void xx_jpeg_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: the base address inside a larger
 * image is not bounded by any 32-bit field, and long is 32-bit on Win64. */
static bool xx_jpeg_read_at(xx_io_device *device, int64_t offset, void *data,
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

/* Make the window cover @p offset (relative to base).  False past the end of
 * the device or on a read error. */
static bool xx_jpeg_cursor_cover(xx_jpeg_cursor *cursor, int64_t offset) {
    int64_t remain;
    size_t want;

    if (offset < 0 || offset >= cursor->span) return false;
    if (offset >= cursor->window_start &&
        offset - cursor->window_start < (int64_t)cursor->window_size) {
        return true;
    }
    remain = cursor->span - offset;
    want = remain < (int64_t)cursor->window_capacity
               ? (size_t)remain
               : cursor->window_capacity;
    cursor->window_size = 0U;
    /* base + offset < base + span == device size: no overflow. */
    if (!xx_jpeg_read_at(cursor->device, cursor->base + offset,
                         cursor->window, want)) {
        return false;
    }
    cursor->window_start = offset;
    cursor->window_size = want;
    return true;
}

static bool xx_jpeg_cursor_byte(xx_jpeg_cursor *cursor, int64_t offset,
                                uint8_t *value) {
    if (!xx_jpeg_cursor_cover(cursor, offset)) return false;
    *value = cursor->window[(size_t)(offset - cursor->window_start)];
    return true;
}

/* binwalk's no_length_markers: 00, 01 (TEM), D0..D7 (RSTn), D8 (SOI),
 * D9 (EOI). */
static bool xx_jpeg_marker_has_no_length(uint8_t id) {
    return id == 0x00U || id == 0x01U || (id >= 0xD0U && id <= 0xD9U);
}

/* binwalk's sos_skip_markers: a 0xFF inside scan data followed by one of
 * these is stuffing (00) or a restart marker (D0..D7), not the next
 * segment. */
static bool xx_jpeg_is_scan_escape(uint8_t id) {
    return id == 0x00U || (id >= 0xD0U && id <= 0xD7U);
}

/* SOF0..SOF15 minus DHT (C4), JPG (C8) and DAC (CC). */
static bool xx_jpeg_is_frame_marker(uint8_t id) {
    return id >= 0xC0U && id <= 0xCFU && id != 0xC4U && id != 0xC8U &&
           id != 0xCCU;
}

/* Scan entropy-coded data from @p start for the first 0xFF that is followed
 * by anything but 00 / D0..D7, exactly as binwalk's inner SOS loop does:
 * a candidate at p needs p + 2 <= span.  When the data runs out binwalk's
 * outer loop then always fails (it is left on the last byte, or past it), so
 * this returns false directly in that case. */
static bool xx_jpeg_scan(xx_jpeg_cursor *cursor, int64_t start,
                         int64_t *marker_offset, xx_pd_struct *pd) {
    int64_t pos = start;
    int64_t next_poll = start;

    if (start < 0) return false;
    while (pos <= cursor->span - 2) {
        const uint8_t *window;
        size_t index;
        size_t limit;
        uint8_t next;

        /* pos only grows, so this polls once per XX_JPEG_SCAN_POLL_BYTES of
         * progress whatever the mix of plain bytes and FF 00 / FF Dn pairs.
         * next_poll <= span + 1 MiB: no overflow. */
        if (pos >= next_poll) {
            if (xx_pd_is_stopped(pd)) return false;
            next_poll = pos + XX_JPEG_SCAN_POLL_BYTES;
        }
        if (!xx_jpeg_cursor_cover(cursor, pos)) return false;
        window = cursor->window;
        index = (size_t)(pos - cursor->window_start);
        limit = cursor->window_size;
        while (index < limit && window[index] != XX_JPEG_MARKER_MAGIC) {
            ++index;
        }
        pos = cursor->window_start + (int64_t)index;
        if (index == limit) continue; /* no 0xFF here: next window */
        /* 0xFF at pos.  binwalk needs both bytes to exist. */
        if (pos > cursor->span - 2) return false;
        if (!xx_jpeg_cursor_byte(cursor, pos + 1, &next)) return false;
        if (xx_jpeg_is_scan_escape(next)) {
            ++pos;
            continue;
        }
        *marker_offset = pos;
        return true;
    }
    return false;
}

static xx_jpeg_variant xx_jpeg_match_magic(const uint8_t *data, size_t size) {
    if (!data) return XX_JPEG_VARIANT_UNKNOWN;
    if (size >= sizeof(xx_jpeg_magic_jfif) &&
        xx_rt_memcmp(data, xx_jpeg_magic_jfif, sizeof(xx_jpeg_magic_jfif)) ==
            0) {
        return XX_JPEG_VARIANT_JFIF;
    }
    if (size >= sizeof(xx_jpeg_magic_exif) &&
        xx_rt_memcmp(data, xx_jpeg_magic_exif, sizeof(xx_jpeg_magic_exif)) ==
            0) {
        return XX_JPEG_VARIANT_EXIF;
    }
    if (size >= sizeof(xx_jpeg_magic_dqt) &&
        xx_rt_memcmp(data, xx_jpeg_magic_dqt, sizeof(xx_jpeg_magic_dqt)) ==
            0) {
        return XX_JPEG_VARIANT_DQT;
    }
    return XX_JPEG_VARIANT_UNKNOWN;
}

/* The marker walk of binwalk's get_jpeg_data_size, plus the 00 / FF refusal
 * described at the top of the file.  Fills the counters and the first frame
 * header on the way; those never influence the verdict. */
static bool xx_jpeg_walk(xx_jpeg_cursor *cursor, xx_jpeg_parsed *parsed,
                         xx_pd_struct *pd) {
    int64_t pos = 0;
    uint32_t iterations = 0U;

    for (;;) {
        uint8_t magic;
        uint8_t id;

        if (++iterations % XX_JPEG_POLL_INTERVAL == 0U &&
            xx_pd_is_stopped(pd)) {
            return false;
        }
        if (!xx_jpeg_cursor_byte(cursor, pos, &magic) ||
            magic != XX_JPEG_MARKER_MAGIC) {
            return false;
        }
        ++pos;
        if (!xx_jpeg_cursor_byte(cursor, pos, &id)) return false;
        ++pos;
        if (id == 0x00U || id == XX_JPEG_MARKER_MAGIC) return false;
        if (parsed->marker_count < UINT32_MAX) ++parsed->marker_count;

        if (!xx_jpeg_marker_has_no_length(id)) {
            uint8_t high;
            uint8_t low;
            uint16_t length;

            /* get(pos..pos + 2) must succeed. */
            if (pos > cursor->span - 2) return false;
            if (!xx_jpeg_cursor_byte(cursor, pos, &high) ||
                !xx_jpeg_cursor_byte(cursor, pos + 1, &low)) {
                return false;
            }
            length = (uint16_t)(((uint16_t)high << 8) | (uint16_t)low);

            /* Frame header: Lf(2) P(1) Y(2) X(2) Nf(1) ... -- informational
             * only, read when the segment claims to hold it and the bytes
             * exist. */
            if (parsed->frame_marker == 0U && xx_jpeg_is_frame_marker(id) &&
                length >= 8U && pos <= cursor->span - 8) {
                uint8_t header[6];
                size_t index;
                bool ok = true;
                for (index = 0U; index < sizeof(header) && ok; ++index) {
                    ok = xx_jpeg_cursor_byte(cursor, pos + 2 + (int64_t)index,
                                             &header[index]);
                }
                if (!ok) return false;
                parsed->frame_marker = id;
                parsed->precision = header[0];
                parsed->height =
                    (uint16_t)(((uint16_t)header[1] << 8) | header[2]);
                parsed->width =
                    (uint16_t)(((uint16_t)header[3] << 8) | header[4]);
                parsed->components = header[5];
            }
            /* pos <= span - 2 and length <= 0xFFFF: cannot overflow. */
            pos += (int64_t)length;
        }

        if (id == XX_JPEG_MARKER_SOS) {
            if (parsed->scan_count < UINT32_MAX) ++parsed->scan_count;
            if (!xx_jpeg_scan(cursor, pos, &pos, pd)) return false;
        }

        if (id == XX_JPEG_MARKER_EOI) {
            parsed->image_size = pos;
            return true;
        }
    }
}

/* --------------------------------------------------------------- parse -- */

static bool xx_jpeg_parse(Abstractformat *self, xx_jpeg_parsed *parsed,
                          xx_pd_struct *pd) {
    uint8_t lead[XX_JPEG_MAX_MAGIC_SIZE];
    xx_jpeg_cursor cursor;
    size_t lead_size;
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
    if (span < XX_JPEG_MIN_SIZE) return false;

    lead_size = span < (int64_t)sizeof(lead) ? (size_t)span : sizeof(lead);
    if (!xx_jpeg_read_at(self->device, self->base_address, lead, lead_size)) {
        return false;
    }
    parsed->variant = xx_jpeg_match_magic(lead, lead_size);
    if (parsed->variant == XX_JPEG_VARIANT_UNKNOWN) return false;

    xx_mem_zero(&cursor, sizeof(cursor));
    cursor.device = self->device;
    cursor.base = self->base_address;
    cursor.span = span;
    cursor.window_capacity = span < (int64_t)XX_JPEG_WINDOW_SIZE
                                 ? (size_t)span
                                 : XX_JPEG_WINDOW_SIZE;
    cursor.window = (uint8_t *)xx_mem_alloc(cursor.window_capacity);
    if (!cursor.window) return false;
    cursor.window_start = 0;
    cursor.window_size = 0U;

    result = xx_jpeg_walk(&cursor, parsed, pd);
    xx_mem_free(cursor.window);

    if (!result || parsed->image_size < XX_JPEG_MIN_SIZE ||
        parsed->image_size > span) {
        parsed->image_size = -1;
        return false;
    }
    return true;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_jpeg_init(xx_jpeg *jpeg, xx_io_device *dev, int64_t base_address) {
    if (!jpeg) return;
    xx_mem_zero(jpeg, sizeof(*jpeg));
    xx_format_init(&jpeg->format, dev, base_address);
    /* Marker lengths and frame fields are big-endian. */
    jpeg->format.endian = XX_ENDIAN_BIG;
    jpeg->format.file_type = XX_JPEG_FILE_TYPE;
    /* There is no image format type in the enum; a picture is neither code
     * nor a container. */
    jpeg->format.format_type = XX_TYPE_UNKNOWN;
    jpeg->format.is_archive = false;
    xx_format_set_mime_type(&jpeg->format, "image/jpeg");
    xx_format_set_extension(&jpeg->format, "jpg");
    jpeg->format.check_is_valid = xx_jpeg_check_is_valid;
    jpeg->format.handle_base_info = xx_jpeg_handle_base_info;
    jpeg->format.get_format_size = xx_jpeg_get_format_size;
    jpeg->format.destroy = xx_jpeg_vtable_destroy;
    jpeg->variant = XX_JPEG_VARIANT_UNKNOWN;
    jpeg->image_end = -1;
}

xx_jpeg *xx_jpeg_create(xx_io_device *dev, int64_t base_address) {
    xx_jpeg *jpeg = (xx_jpeg *)xx_mem_alloc(sizeof(*jpeg));

    if (jpeg) xx_jpeg_init(jpeg, dev, base_address);
    return jpeg;
}

void xx_jpeg_destroy(xx_jpeg *jpeg) {
    if (!jpeg) return;
    xx_format_cleanup_extra_parameters(&jpeg->format);
}

static void xx_jpeg_vtable_destroy(Abstractformat *self) {
    xx_jpeg_destroy((xx_jpeg *)self);
}

void xx_jpeg_free(xx_jpeg *jpeg) {
    if (!jpeg) return;
    xx_jpeg_destroy(jpeg);
    xx_mem_free(jpeg);
}

/* -------------------------------------------------------------- format -- */

bool xx_jpeg_check_magic(const uint8_t *data, size_t size) {
    return xx_jpeg_match_magic(data, size) != XX_JPEG_VARIANT_UNKNOWN;
}

bool xx_jpeg_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jpeg_parsed parsed;

    return xx_jpeg_parse(self, &parsed, pd);
}

bool xx_jpeg_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jpeg *jpeg = (xx_jpeg *)self;
    xx_jpeg_parsed parsed;
    int64_t image_end;

    if (!self || !jpeg) return false;
    if (!xx_jpeg_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    image_end = self->base_address + parsed.image_size;

    jpeg->variant = parsed.variant;
    jpeg->image_end = image_end;
    jpeg->marker_count = parsed.marker_count;
    jpeg->scan_count = parsed.scan_count;
    jpeg->frame_marker = parsed.frame_marker;
    jpeg->precision = parsed.precision;
    jpeg->height = parsed.height;
    jpeg->width = parsed.width;
    jpeg->components = parsed.components;

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

int64_t xx_jpeg_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

/* ------------------------------------------------------------ accessors -- */

xx_jpeg_variant xx_jpeg_get_variant(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->variant : XX_JPEG_VARIANT_UNKNOWN;
}

int64_t xx_jpeg_get_image_end(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->image_end : -1;
}

uint32_t xx_jpeg_get_marker_count(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->marker_count : 0U;
}

uint32_t xx_jpeg_get_scan_count(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->scan_count : 0U;
}

uint8_t xx_jpeg_get_frame_marker(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->frame_marker : 0U;
}

bool xx_jpeg_is_progressive(const xx_jpeg *jpeg) {
    /* SOF2, SOF6, SOF10, SOF14: progressive DCT, Huffman or arithmetic,
     * non-differential or differential. */
    return jpeg && (jpeg->frame_marker == 0xC2U ||
                    jpeg->frame_marker == 0xC6U ||
                    jpeg->frame_marker == 0xCAU ||
                    jpeg->frame_marker == 0xCEU);
}

uint8_t xx_jpeg_get_precision(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->precision : 0U;
}

uint16_t xx_jpeg_get_width(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->width : 0U;
}

uint16_t xx_jpeg_get_height(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->height : 0U;
}

uint8_t xx_jpeg_get_components(const xx_jpeg *jpeg) {
    return jpeg ? jpeg->components : 0U;
}
