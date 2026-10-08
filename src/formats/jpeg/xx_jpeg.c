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
 * The component archive API publishes marker payloads and entropy-coded
 * scans separately. It does not decode the JPEG image.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jpeg/xx_jpeg.h"

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/hash/xx_hash.h"

#include "../bmp/xx_component_archive_impl.h"
#include "xxfclib/data/xx_data.h"

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
    bool failed;
} xx_jpeg_cursor;

static void xx_jpeg_vtable_destroy(Abstractformat *self);
static void xx_jpeg_analysis_destroy(xx_jpeg *jpeg);

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
        cursor->failed = true;
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

/* ---------------------------------------------------------- inspection -- */

typedef struct xx_jpeg_analysis_s {
    char version[32];
    char comment[101];
    char dqt_md5[33];
    char *camera;
    bool markers[256];
    int64_t exif_offset;
    int64_t exif_size;
} xx_jpeg_analysis;

static void xx_jpeg_analysis_destroy(xx_jpeg *jpeg) {
    xx_jpeg_analysis *info = (xx_jpeg_analysis *)jpeg->analysis;
    if (!info) return;
    xx_str_free(info->camera);
    xx_mem_free(info);
    jpeg->analysis = NULL;
}

static bool xx_jpeg_tiff_read(xx_jpeg *jpeg, int64_t offset,
                              void *data, size_t size, bool *failed) {
    if (xx_jpeg_read_at(jpeg->format.device, offset, data, size)) return true;
    *failed = true;
    return false;
}

static char *xx_jpeg_tiff_ascii(xx_jpeg *jpeg, const xx_jpeg_analysis *info,
                                uint16_t wanted, xx_pd_struct *pd,
                                bool *failed) {
    static const uint8_t widths[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};
    uint8_t header[8], entry[12], bytes[4];
    int64_t table;
    bool be;
    unsigned guard;
    if (info->exif_size < 8 ||
        !xx_jpeg_tiff_read(jpeg, info->exif_offset, header, sizeof(header), failed)) return NULL;
    be = header[0] == 'M' && header[1] == 'M' && header[2] == 0 && header[3] == 42;
    if (!be && !(header[0] == 'I' && header[1] == 'I' && header[2] == 42 && header[3] == 0)) return NULL;
    table = xx_data_get_u32(header + 4, 4, 0, be);
    for (guard = 0; table > 0 && table <= info->exif_size - 2 && guard < 64; ++guard) {
        uint16_t count, i;
        int64_t current, next;
        if (xx_pd_is_stopped(pd) || !xx_jpeg_tiff_read(jpeg,
                info->exif_offset + table, bytes, 2, failed)) return NULL;
        count = xx_data_get_u16(bytes, 2, 0, be); current = table + 2;
        /* An IFD entry and its next-table pointer must remain in this APP1. */
        if ((int64_t)count * 12 + 4 > info->exif_size - current) return NULL;
        for (i = 0; i < count; ++i, current += 12) {
            uint16_t type;
            int64_t length, offset;
            char *text;
            size_t at;
            if (xx_pd_is_stopped(pd) || !xx_jpeg_tiff_read(jpeg,
                    info->exif_offset + current, entry, sizeof(entry), failed)) return NULL;
            if (xx_data_get_u16(entry, 2, 0, be) != wanted) continue;
            type = xx_data_get_u16(entry + 2, 2, 0, be);
            length = type < sizeof(widths) ? (int64_t)widths[type] * xx_data_get_u32(entry + 4, 4, 0, be) : 0;
            offset = length > 4 ? xx_data_get_u32(entry + 8, 4, 0, be) : current + 8;
            if (length <= 0 || offset < 0 || offset >= info->exif_size) return NULL;
            if (length > info->exif_size - offset) length = info->exif_size - offset;
            text = xx_str_create_len((size_t)length);
            if (!text) { *failed = true; return NULL; }
            if (!xx_jpeg_tiff_read(jpeg, info->exif_offset + offset, text, (size_t)length, failed)) {
                xx_str_free(text); return NULL;
            }
            for (at = 0; at < (size_t)length && text[at]; ++at) {}
            text[at] = 0; return text;
        }
        if (!xx_jpeg_tiff_read(jpeg, info->exif_offset + current, bytes, 4, failed)) return NULL;
        next = xx_data_get_u32(bytes, 4, 0, be);
        if (next < current + 8) break;
        table = next;
    }
    return NULL;
}

bool xx_jpeg_analyze(xx_jpeg *jpeg, xx_pd_struct *pd) {
    xx_jpeg_cursor cursor;
    xx_jpeg_analysis *info = NULL;
    xx_hash_context hash;
    uint8_t digest[16], head[13];
    int64_t total, pos = 0, saved;
    size_t comment_size = 0;
    uint32_t count = 0;
    bool complete = false, first_app1 = false, ok = false;
    if (!jpeg || !jpeg->format.device || xx_pd_is_stopped(pd)) return false;
    if (jpeg->analysis) return true;
    xx_mem_zero(&cursor, sizeof(cursor));
    saved = xx_io_tell(jpeg->format.device);
    total = xx_io_total_size(jpeg->format.device);
    if (jpeg->format.base_address < 0 || total < jpeg->format.base_address ||
        total - jpeg->format.base_address < 20) goto done;
    cursor.device = jpeg->format.device; cursor.base = jpeg->format.base_address;
    cursor.span = total - cursor.base; cursor.window_start = -1;
    cursor.window_capacity = XX_JPEG_WINDOW_SIZE;
    cursor.window = (uint8_t *)xx_mem_alloc(cursor.window_capacity);
    info = (xx_jpeg_analysis *)xx_mem_calloc(1, sizeof(*info));
    if (!cursor.window || !info) goto done;
    if (!xx_jpeg_read_at(cursor.device, cursor.base, head, sizeof(head))) goto done;
    if (head[0] != 0xFF || head[1] != 0xD8 || head[2] != 0xFF) goto done;
    if (xx_rt_memcmp(head + 6, "JFIF\0", 5) == 0)
        (void)xx_rt_snprintf(info->version, sizeof(info->version), "%u.%u", (unsigned)head[11], (unsigned)head[12]);
    if (!xx_hash_init(&hash, XX_HASH_MD5)) goto done;
    while (pos <= cursor.span - 2 && count <= 100000) {
        uint8_t prefix, marker, a = 0, b;
        int64_t size = 2, payload, remaining;
        if (xx_pd_is_stopped(pd)) goto done;
        if (!xx_jpeg_cursor_byte(&cursor, pos, &prefix) ||
            !xx_jpeg_cursor_byte(&cursor, pos + 1, &marker)) goto done;
        if (prefix != 0xFF || marker == 0 || marker == 0xFF) break;
        if (marker == 0xDD) size = 6;
        else if (!(marker == 0xD8 || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7))) {
            if (pos > cursor.span - 4) break;
            if (!xx_jpeg_cursor_byte(&cursor, pos + 2, &a) ||
                !xx_jpeg_cursor_byte(&cursor, pos + 3, &b)) goto done;
            size = 2 + ((int64_t)a << 8 | b);
            if (size < 4) break;
        }
        if (size > cursor.span - pos) break;
        info->markers[marker] = true; ++count;
        payload = pos + 4; remaining = size - 4;
        if (marker == 0xFE && remaining > 0 && comment_size < 100) {
            int64_t i;
            for (i = 0; i < remaining && comment_size < 100; ++i) {
                if (!xx_jpeg_cursor_byte(&cursor, payload + i, &a)) goto done;
                if (!a) break;
                info->comment[comment_size++] = (char)a;
            }
        }
        if (marker == 0xDB && remaining > 0) {
            while (remaining > 0) {
                size_t offset, amount;
                if (xx_pd_is_stopped(pd) || !xx_jpeg_cursor_cover(&cursor, payload)) goto done;
                offset = (size_t)(payload - cursor.window_start);
                amount = cursor.window_size - offset;
                if ((int64_t)amount > remaining) amount = (size_t)remaining;
                xx_hash_update(&hash, cursor.window + offset, amount);
                payload += (int64_t)amount; remaining -= (int64_t)amount;
            }
        }
        if (marker == 0xE1 && !first_app1) {
            first_app1 = true;
            if (size > 10) {
                uint8_t ident[5];
                if (!xx_jpeg_read_at(cursor.device, cursor.base + pos + 4, ident, sizeof(ident))) goto done;
                if (xx_rt_memcmp(ident, "Exif\0", sizeof(ident)) == 0) {
                    info->exif_offset = cursor.base + pos + 10; info->exif_size = size - 10;
                }
            }
        }
        pos += size;
        if (marker == 0xD9) { complete = true; break; }
        if (marker == 0xDA) {
            /* Use the native buffered scan, then collapse FF fill runs before
             * deciding whether they lead to a restart or the next segment. */
            for (;;) {
                int64_t found, id_offset;
                if (!xx_jpeg_scan(&cursor, pos, &found, pd)) {
                    if (cursor.failed) goto done;
                    pos = cursor.span; break;
                }
                id_offset = found + 1;
                while (id_offset < cursor.span) {
                    if (!xx_jpeg_cursor_byte(&cursor, id_offset, &a)) goto done;
                    if (a != 0xFF) break;
                    ++id_offset;
                }
                if (id_offset >= cursor.span) { pos = cursor.span; break; }
                if (xx_jpeg_is_scan_escape(a)) { pos = id_offset + 1; continue; }
                pos = id_offset - 1; break;
            }
            if (xx_pd_is_stopped(pd)) goto done;
        }
    }
    if (!complete) {
        xx_mem_zero(info->markers, sizeof(info->markers));
        info->comment[0] = 0; comment_size = 0; info->exif_size = 0;
        (void)xx_hash_init(&hash, XX_HASH_MD5);
    }
    {
        size_t i, out = 0;
        for (i = 0; i < comment_size; ++i)
            if (info->comment[i] != '\r' && info->comment[i] != '\n') info->comment[out++] = info->comment[i];
        info->comment[out] = 0;
    }
    if (!xx_hash_final(&hash, digest, sizeof(digest)) ||
        !xx_hash_to_hex(digest, sizeof(digest), info->dqt_md5, sizeof(info->dqt_md5))) goto done;
    if (info->exif_size > 0) {
        bool failed = false;
        char *make = xx_jpeg_tiff_ascii(jpeg, info, 0x10F, pd, &failed);
        char *model = failed ? NULL : xx_jpeg_tiff_ascii(jpeg, info, 0x110, pd, &failed);
        if (failed) { xx_str_free(make); xx_str_free(model); goto done; }
        if ((make && *make) || (model && *model)) {
            char *prefix = xx_str_concat(make ? make : "", "(");
            char *middle = prefix ? xx_str_concat(prefix, model ? model : "") : NULL;
            info->camera = middle ? xx_str_concat(middle, ")") : NULL;
            xx_str_free(prefix); xx_str_free(middle);
            if (!info->camera) { xx_str_free(make); xx_str_free(model); goto done; }
        }
        xx_str_free(make); xx_str_free(model);
    }
    if (xx_pd_is_stopped(pd)) goto done;
    jpeg->analysis = info; info = NULL; ok = true;
done:
    if (info) { xx_str_free(info->camera); xx_mem_free(info); }
    xx_mem_free(cursor.window);
    if (saved >= 0) (void)xx_io_seek64(jpeg->format.device, saved, SEEK_SET);
    return ok;
}

const char *xx_jpeg_get_version(const xx_jpeg *jpeg) {
    const xx_jpeg_analysis *i = jpeg ? (const xx_jpeg_analysis *)jpeg->analysis : NULL;
    return i ? i->version : "";
}
const char *xx_jpeg_get_comment(const xx_jpeg *jpeg) {
    const xx_jpeg_analysis *i = jpeg ? (const xx_jpeg_analysis *)jpeg->analysis : NULL;
    return i ? i->comment : "";
}
const char *xx_jpeg_get_dqt_md5(const xx_jpeg *jpeg) {
    const xx_jpeg_analysis *i = jpeg ? (const xx_jpeg_analysis *)jpeg->analysis : NULL;
    return i ? i->dqt_md5 : "";
}
const char *xx_jpeg_get_exif_camera_name(const xx_jpeg *jpeg) {
    const xx_jpeg_analysis *i = jpeg ? (const xx_jpeg_analysis *)jpeg->analysis : NULL;
    return i && i->camera ? i->camera : "";
}
int64_t xx_jpeg_get_exif_size(const xx_jpeg *jpeg) {
    const xx_jpeg_analysis *i = jpeg ? (const xx_jpeg_analysis *)jpeg->analysis : NULL;
    return i ? i->exif_size : 0;
}
bool xx_jpeg_is_chunk_present(const xx_jpeg *jpeg, uint8_t marker) {
    const xx_jpeg_analysis *i = jpeg ? (const xx_jpeg_analysis *)jpeg->analysis : NULL;
    return i && i->markers[marker];
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
    xx_components_install(&jpeg->format);
}

xx_jpeg *xx_jpeg_create(xx_io_device *dev, int64_t base_address) {
    xx_jpeg *jpeg = (xx_jpeg *)xx_mem_alloc(sizeof(*jpeg));

    if (jpeg) xx_jpeg_init(jpeg, dev, base_address);
    return jpeg;
}

void xx_jpeg_destroy(xx_jpeg *jpeg) {
    if (!jpeg) return;
    xx_jpeg_analysis_destroy(jpeg);
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
    if (!xx_components_finish(self, pd)) return false;
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

/* Encoded/structural component members; this does not decode media. */
static bool xx_components_build(Abstractformat *f, xx_component_stream *s, xx_pd_struct *pd) {

    xx_jpeg_cursor cursor;
    int64_t pos=2;
    bool ok=false;
    xx_mem_zero(&cursor,sizeof(cursor)); cursor.device=f->device; cursor.base=f->base_address; cursor.span=f->format_size;
    cursor.window_capacity=65536; cursor.window=(uint8_t *)xx_mem_alloc(cursor.window_capacity);
    if(!cursor.window) return false;
    while(pos<f->format_size) {
        uint8_t a,id,hi,lo; uint32_t length; char kind[11]="segment-00";
        if(xx_pd_is_stopped(pd) || !xx_jpeg_cursor_byte(&cursor,pos,&a) || a!=0xff ||
            !xx_jpeg_cursor_byte(&cursor,pos+1,&id)) goto done;
        pos+=2;
        if(id==0xd9) { ok=pos==f->format_size; goto done; }
        if(xx_jpeg_marker_has_no_length(id)) continue;
        if(!xx_jpeg_cursor_byte(&cursor,pos,&hi) || !xx_jpeg_cursor_byte(&cursor,pos+1,&lo)) goto done;
        length=((uint32_t)hi<<8)|lo;
        kind[8]="0123456789ABCDEF"[id>>4]; kind[9]="0123456789ABCDEF"[id&15];
        if(length<2 || !xx_component_add(f,s,pos+2,length-2,kind)) goto done;
        pos+=length;
        if(id==0xda) {
            int64_t start=pos;
            while(pos<f->format_size-1) {
                if(xx_pd_is_stopped(pd) || !xx_jpeg_cursor_byte(&cursor,pos,&a) ||
                    !xx_jpeg_cursor_byte(&cursor,pos+1,&id)) goto done;
                if(a==0xff && !xx_jpeg_is_scan_escape(id)) break;
                ++pos;
            }
            if(!xx_component_add(f,s,start,pos-start,"entropy-coded-scan")) goto done;
        }
    }
done:
    xx_mem_free(cursor.window); return ok;
}
