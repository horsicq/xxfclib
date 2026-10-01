/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BinSCII: Apple II text transport encoding.  xx_binscii.h carries the
 * layout.  The segment state machine follows Deark's modules/binscii.c
 * (Copyright (C) 2023 Jason Summers, MIT licence): the same line handling
 * (leading bytes <= 0x20 dropped, '@'-based name length, CRC-16/XMODEM over
 * header bytes 0..23 and over every decoded data byte including padding).
 * It is stricter than Deark where that costs nothing: alphabet characters
 * must be distinct printables, data lines must be 64 alphabet characters,
 * and a member is only reported good when every segment and CRC checks out.
 *
 * Every read goes through one bounded line buffer; lines longer than
 * BSC_LINE_MAX are truncated for parsing (they are never valid BinSCII
 * lines past that length anyway), so no allocation depends on the input
 * except the record table, which is capped.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/binscii/xx_binscii.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef BINSCII
#define XX_BINSCII_FILE_TYPE XX_FILE_TYPE_BINSCII
#else
#define XX_BINSCII_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BSC_LINE_MAX 128U
#define BSC_BUFFER 0x10000U
#define BSC_UNITS_PER_LINE 16U
#define BSC_DATA_CHARS (BSC_UNITS_PER_LINE * 4U)   /* 64 */
#define BSC_DATA_BYTES (BSC_UNITS_PER_LINE * 3U)   /* 48 */
#define BSC_HEADER_CHARS 52U
#define BSC_NAME_FIELD 15U
#define BSC_MAX_RECORDS 4096U
/* check_is_valid looks for the first member header this far into the text. */
#define BSC_CHECK_WINDOW 0x10000
#define BSC_OUT_BUFFER 0x1000U
/* Room for "<15-char name>_<n>". */
#define BSC_NAME_MAX 32U

/* ---------------------------------------------------------------------- */
/* Line reader                                                             */

typedef struct bsc_lines_s {
    xx_io_device *device;
    int64_t pos;        /**< Absolute offset of the next unread byte. */
    int64_t end;        /**< Absolute end of the device. */
    int64_t buf_start;
    size_t buf_len;
    uint8_t *buf;
    bool error;
    uint8_t line[BSC_LINE_MAX];
    size_t line_len;
    int64_t line_start; /**< Offset of the line's first byte. */
} bsc_lines;

static bool bsc_lines_open(bsc_lines *r, xx_io_device *device, int64_t start,
                           int64_t end) {
    xx_mem_zero(r, sizeof(*r));
    r->device = device;
    r->pos = start;
    r->end = end;
    r->buf = (uint8_t *)xx_mem_alloc(BSC_BUFFER);
    return r->buf != NULL;
}

static void bsc_lines_close(bsc_lines *r) {
    if (r->buf) xx_mem_free(r->buf);
    r->buf = NULL;
}

/* Byte at @p at, or -1 at end of data / on a read error. */
static int bsc_byte(bsc_lines *r, int64_t at) {
    if (at < 0 || at >= r->end || r->error) return -1;
    if (at < r->buf_start || at >= r->buf_start + (int64_t)r->buf_len) {
        size_t want = BSC_BUFFER, done = 0U;
        if ((int64_t)want > r->end - at) want = (size_t)(r->end - at);
        if (xx_io_seek64(r->device, at, SEEK_SET) != 0) {
            r->error = true;
            return -1;
        }
        while (done < want) {
            ssize_t got = xx_io_read(r->device, r->buf + done, want - done);
            if (got <= 0 || (size_t)got > want - done) break;
            done += (size_t)got;
        }
        if (done == 0U) {
            r->error = true;
            return -1;
        }
        r->buf_start = at;
        r->buf_len = done;
    }
    return r->buf[at - r->buf_start];
}

/* Next line: content up to CR, LF or CR LF, leading bytes <= 0x20 dropped,
 * at most BSC_LINE_MAX bytes kept.  False at end of data. */
static bool bsc_next_line(bsc_lines *r) {
    int c;
    bool leading = true;
    if (r->pos >= r->end || r->error) return false;
    r->line_start = r->pos;
    r->line_len = 0U;
    for (;;) {
        c = bsc_byte(r, r->pos);
        if (c < 0) break;
        ++r->pos;
        if (c == '\r') {
            if (bsc_byte(r, r->pos) == '\n') ++r->pos;
            break;
        }
        if (c == '\n') break;
        if (leading && c <= 0x20) continue;
        leading = false;
        if (r->line_len < BSC_LINE_MAX) r->line[r->line_len++] = (uint8_t)c;
    }
    return !r->error;
}

static bool bsc_is_signature(const bsc_lines *r) {
    return r->line_len >= XX_BINSCII_SIGNATURE_SIZE &&
           xx_rt_memcmp(r->line, XX_BINSCII_SIGNATURE,
                        XX_BINSCII_SIGNATURE_SIZE) == 0;
}

/* Advance to the next signature line. */
static bool bsc_find_signature(bsc_lines *r, xx_pd_struct *pd) {
    uint32_t count = 0U;
    for (;;) {
        if ((++count & 0x3FFU) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (!bsc_next_line(r)) return false;
        if (bsc_is_signature(r)) return true;
    }
}

/* ---------------------------------------------------------------------- */
/* Segments                                                                */

static uint16_t bsc_crc16(uint16_t crc, const uint8_t *data, size_t size) {
    return xx_crc16_xmodem_calc(crc, data, size);
}

typedef struct bsc_segment_s {
    uint8_t map[256];      /**< Character value, 0xFF when not in the alphabet. */
    char name[BSC_NAME_FIELD + 1U];
    uint32_t file_len;
    uint32_t offset;
    uint32_t seg_len;
    int64_t start;         /**< Offset of the signature line. */
    int64_t end;           /**< Offset just past the CRC line. */
} bsc_segment;

/* Decode @p units 4-character units; false on a character outside the
 * alphabet. */
static bool bsc_decode(const bsc_segment *s, const uint8_t *src, size_t units,
                       uint8_t *dst) {
    size_t i;
    for (i = 0U; i < units; ++i) {
        uint8_t v0 = s->map[src[i * 4U]], v1 = s->map[src[i * 4U + 1U]],
                v2 = s->map[src[i * 4U + 2U]], v3 = s->map[src[i * 4U + 3U]];
        if ((v0 | v1 | v2 | v3) & 0xC0U) return false;
        dst[i * 3U] = (uint8_t)((v3 << 2U) | (v2 >> 4U));
        dst[i * 3U + 1U] = (uint8_t)(((v2 & 0x0FU) << 4U) | (v1 >> 2U));
        dst[i * 3U + 2U] = (uint8_t)(((v1 & 0x03U) << 6U) | v0);
    }
    return true;
}

static uint32_t bsc_le24(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U);
}

/* The signature line has just been read: parse the alphabet and header
 * lines.  On success the reader stands at the first data line. */
static bool bsc_read_header(bsc_lines *r, bsc_segment *s) {
    uint8_t hdr[27];
    size_t i, name_len;
    xx_mem_zero(s, sizeof(*s));
    s->start = r->line_start;
    /* Alphabet: 64 distinct printable characters. */
    if (!bsc_next_line(r) || r->line_len < 64U) return false;
    xx_rt_memset(s->map, 0xFF, sizeof(s->map));
    for (i = 0U; i < 64U; ++i) {
        uint8_t c = r->line[i];
        if (c <= 0x20U || c >= 0x7FU || s->map[c] != 0xFFU) return false;
        s->map[c] = (uint8_t)i;
    }
    /* Header. */
    if (!bsc_next_line(r) || r->line_len < BSC_HEADER_CHARS) return false;
    if (r->line[0] < 0x41U || r->line[0] > 0x4FU) return false;
    name_len = (size_t)(r->line[0] - 0x40U);
    for (i = 0U; i < name_len; ++i) {
        uint8_t c = r->line[1U + i];
        if (c <= 0x20U || c >= 0x7FU) return false;
        s->name[i] = (char)c;
    }
    s->name[name_len] = 0;
    if (!bsc_decode(s, r->line + 16U, 9U, hdr)) return false;
    if (bsc_crc16(0U, hdr, 24U) !=
        (uint16_t)((uint16_t)hdr[24] | ((uint16_t)hdr[25] << 8U)))
        return false;
    s->file_len = bsc_le24(hdr);
    s->offset = bsc_le24(hdr + 3U);
    s->seg_len = bsc_le24(hdr + 21U);
    if (s->offset > s->file_len || s->seg_len > s->file_len - s->offset)
        return false;
    return true;
}

typedef struct bsc_sink_s {
    xx_io_device *device;
    uint8_t buf[BSC_OUT_BUFFER];
    size_t used;
    uint64_t written;
} bsc_sink;

static bool bsc_sink_flush(bsc_sink *k) {
    size_t done = 0U;
    if (k->device) {
        while (done < k->used) {
            ssize_t w = xx_io_write(k->device, k->buf + done, k->used - done);
            if (w <= 0 || (size_t)w > k->used - done) return false;
            done += (size_t)w;
        }
    }
    k->used = 0U;
    return true;
}

static bool bsc_sink_put(bsc_sink *k, const uint8_t *data, size_t size) {
    if (size > BSC_OUT_BUFFER - k->used && !bsc_sink_flush(k)) return false;
    xx_rt_memcpy(k->buf + k->used, data, size);
    k->used += size;
    k->written += size;
    return true;
}

/* Data lines and CRC line of a parsed header; the segment's payload goes to
 * @p sink (may be NULL).  On success s->end is set. */
static bool bsc_read_data(bsc_lines *r, bsc_segment *s, bsc_sink *sink,
                          xx_pd_struct *pd) {
    uint8_t out[BSC_DATA_BYTES];
    uint16_t crc = 0U;
    uint32_t left = s->seg_len, lines;
    uint32_t n;
    lines = (s->seg_len + BSC_DATA_BYTES - 1U) / BSC_DATA_BYTES;
    if (lines == 0U) {
        /* An empty segment may or may not carry one all-padding data line
         * (Deark expects one); take it when the next line is that long. */
        if (!bsc_next_line(r)) return false;
        if (r->line_len >= BSC_DATA_CHARS) {
            if (!bsc_decode(s, r->line, BSC_UNITS_PER_LINE, out)) return false;
            crc = bsc_crc16(crc, out, BSC_DATA_BYTES);
            if (!bsc_next_line(r)) return false;
        }
        goto crc_line;
    }
    for (n = 0U; n < lines; ++n) {
        size_t take;
        if ((n & 0xFFU) == 0xFFU && pd && xx_pd_is_stopped(pd)) return false;
        if (!bsc_next_line(r) || r->line_len < BSC_DATA_CHARS ||
            !bsc_decode(s, r->line, BSC_UNITS_PER_LINE, out))
            return false;
        crc = bsc_crc16(crc, out, BSC_DATA_BYTES);
        take = left < BSC_DATA_BYTES ? left : BSC_DATA_BYTES;
        if (sink && !bsc_sink_put(sink, out, take)) return false;
        left -= (uint32_t)take;
    }
    /* CRC line: one unit, shorter than a data line. */
    if (!bsc_next_line(r)) return false;
crc_line:
    if (r->line_len < 4U ||
        r->line_len >= BSC_DATA_CHARS || !bsc_decode(s, r->line, 1U, out))
        return false;
    if (crc != (uint16_t)((uint16_t)out[0] | ((uint16_t)out[1] << 8U)))
        return false;
    s->end = r->pos;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Members                                                                 */

typedef struct bsc_member_s {
    char name[BSC_NAME_MAX];
    char base[BSC_NAME_FIELD + 1U];
    int64_t start;         /**< Signature line of the first segment. */
    int64_t end;           /**< Past the last good segment. */
    uint32_t file_len;
    uint32_t segments;
    bool good;
} bsc_member;

/* The reader stands after the header of a first segment @p first.  Decode
 * the member to @p out (NULL only verifies), following its continuation
 * segments.  m->end is the end of the last good segment, or the line after
 * the first header when even the first segment is bad. */
static bool bsc_assemble(bsc_lines *r, bsc_segment *first, bsc_member *m,
                         xx_io_device *out, xx_pd_struct *pd) {
    bsc_sink *sink;
    bsc_segment *s = first;
    bsc_segment *next = NULL;
    bool result = false;
    m->start = first->start;
    m->end = r->pos;
    m->file_len = first->file_len;
    m->segments = 0U;
    m->good = false;
    sink = (bsc_sink *)xx_mem_calloc(1U, sizeof(*sink));
    if (!sink) return false;
    sink->device = out;
    for (;;) {
        if (s->offset != sink->written || s->file_len != m->file_len) break;
        if (!bsc_read_data(r, s, sink, pd)) break;
        ++m->segments;
        m->end = s->end;
        if (sink->written >= m->file_len) {
            result = bsc_sink_flush(sink) && sink->written == m->file_len;
            break;
        }
        if (s->seg_len == 0U) break; /* no progress: malformed */
        if (!next) {
            next = (bsc_segment *)xx_mem_alloc(sizeof(*next));
            if (!next) break;
        }
        if (!bsc_find_signature(r, pd) || !bsc_read_header(r, next)) break;
        s = next;
    }
    m->good = result;
    if (next) xx_mem_free(next);
    xx_mem_free(sink);
    return result;
}

typedef struct bsc_table_s {
    bsc_member *items;
    uint32_t count;
    uint64_t segments;
    int64_t end;
} bsc_table;

static char bsc_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool bsc_same_name(const char *a, const char *b) {
    while (*a && *b) {
        if (bsc_upper(*a) != bsc_upper(*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool bsc_name_taken(const bsc_table *t, const char *name) {
    uint32_t i;
    for (i = 0U; i < t->count; ++i)
        if (bsc_same_name(t->items[i].name, name)) return true;
    return false;
}

/* Keep duplicate names apart: "NAME", "NAME_2", "NAME_3", ...  A member
 * that still finds no free name is not extracted rather than overwrite. */
static void bsc_unique_name(bsc_table *t, bsc_member *m) {
    uint32_t same = 0U, i, attempt;
    for (i = 0U; i < t->count; ++i)
        if (bsc_same_name(t->items[i].base, m->base)) ++same;
    for (attempt = 0U; attempt < 64U; ++attempt) {
        if (same + attempt == 0U)
            xx_rt_snprintf(m->name, sizeof(m->name), "%s", m->base);
        else
            xx_rt_snprintf(m->name, sizeof(m->name), "%s_%u", m->base,
                           (unsigned)(same + attempt + 1U));
        if (!bsc_name_taken(t, m->name)) return;
    }
    m->good = false;
}

/* Walk the whole input: one table entry per member whose first segment is
 * present.  Stray continuation segments and broken segments are skipped. */
static bool bsc_scan(Abstractformat *format, bsc_table *t, bool first_only,
                     xx_pd_struct *pd) {
    bsc_lines r;
    bsc_segment *s = NULL;
    int64_t total;
    bool ok = false;
    xx_mem_zero(t, sizeof(*t));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total <= format->base_address) return false;
    /* The quick check never looks (or scans a line) past its window. */
    if (first_only && total - format->base_address > BSC_CHECK_WINDOW)
        total = format->base_address + BSC_CHECK_WINDOW;
    if (!bsc_lines_open(&r, format->device, format->base_address, total))
        return false;
    s = (bsc_segment *)xx_mem_alloc(sizeof(*s));
    if (!s) goto done;
    if (!first_only) {
        t->items = (bsc_member *)xx_mem_calloc(16U, sizeof(bsc_member));
        if (!t->items) goto done;
    }
    {
        uint32_t capacity = 16U;
        while (bsc_find_signature(&r, pd)) {
            int64_t after_sig = r.pos;
            if (!bsc_read_header(&r, s)) {
                r.pos = after_sig;
                continue;
            }
            if (first_only) {
                if (s->offset == 0U) {
                    ok = true;
                    goto done;
                }
                continue;
            }
            if (s->offset != 0U) {
                /* Continuation of a member that started elsewhere. */
                int64_t after_hdr = r.pos;
                if (bsc_read_data(&r, s, NULL, pd)) {
                    ++t->segments;
                    if (s->end > t->end) t->end = s->end;
                } else {
                    r.pos = after_hdr;
                }
                continue;
            }
            if (t->count >= BSC_MAX_RECORDS) break;
            if (t->count == capacity) {
                bsc_member *grown = (bsc_member *)xx_mem_calloc(
                    (size_t)capacity * 2U, sizeof(bsc_member));
                if (!grown) goto done;
                xx_rt_memcpy(grown, t->items,
                             (size_t)capacity * sizeof(bsc_member));
                xx_mem_free(t->items);
                t->items = grown;
                capacity *= 2U;
            }
            {
                bsc_member *m = &t->items[t->count];
                xx_rt_memcpy(m->base, s->name, sizeof(m->base));
                (void)bsc_assemble(&r, s, m, NULL, pd);
                bsc_unique_name(t, m);
                t->segments += m->segments;
                if (m->segments && m->end > t->end) t->end = m->end;
                ++t->count;
                r.pos = m->end;
            }
            if (pd && xx_pd_is_stopped(pd)) goto done;
        }
    }
    ok = !first_only && t->count > 0U && !r.error;
done:
    if (s) xx_mem_free(s);
    bsc_lines_close(&r);
    if (!ok && t->items) {
        xx_mem_free(t->items);
        t->items = NULL;
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static bool bsc_stem_is(const char *name, size_t stem, const char *word) {
    size_t i;
    for (i = 0U; i < stem; ++i)
        if (!word[i] || bsc_upper(name[i]) != word[i]) return false;
    return word[stem] == 0;
}

static bool bsc_safe_output_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, i;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (i = 0U; i < length; ++i) {
        char c = name[i];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (bsc_stem_is(name, stem, devices[i])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((bsc_upper(name[0]) == 'C' && bsc_upper(name[1]) == 'O' &&
          bsc_upper(name[2]) == 'M') ||
         (bsc_upper(name[0]) == 'L' && bsc_upper(name[1]) == 'P' &&
          bsc_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

typedef struct bsc_stream_s {
    bsc_table table;
    uint32_t index;
} bsc_stream;

static void bsc_stream_free(void *opaque) {
    bsc_stream *stream = (bsc_stream *)opaque;
    if (!stream) return;
    if (stream->table.items) xx_mem_free(stream->table.items);
    xx_mem_free(stream);
}

void xx_binscii_init(xx_binscii *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BINSCII_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/x-binscii");
    xx_format_set_extension(&archive->format, "bsc");
    archive->format.check_is_valid = xx_binscii_check_is_valid;
    archive->format.handle_base_info = xx_binscii_handle_base_info;
    archive->format.get_format_size = xx_binscii_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_binscii_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_binscii_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_binscii_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_binscii_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_binscii_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_binscii_free_archive_records_reading;
}

xx_binscii *xx_binscii_create(xx_io_device *device, int64_t base_address) {
    xx_binscii *archive = (xx_binscii *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_binscii_init(archive, device, base_address);
    return archive;
}

void xx_binscii_destroy(xx_binscii *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_binscii_free(xx_binscii *archive) {
    if (!archive) return;
    xx_binscii_destroy(archive);
    xx_mem_free(archive);
}

/* A signature line, a valid alphabet and a first-segment header whose CRC
 * checks, within the first BSC_CHECK_WINDOW bytes. */
bool xx_binscii_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    bsc_table t;
    if (!format || format->base_address < 0) return false;
    return bsc_scan(format, &t, true, pd);
}

bool xx_binscii_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    bsc_table t;
    xx_binscii *archive;
    if (!format || !bsc_scan(format, &t, false, pd)) return false;
    archive = (xx_binscii *)format;
    archive->number_of_records = t.count;
    archive->number_of_segments = t.segments;
    format->number_of_archive_records = t.count;
    format->format_size = t.end > format->base_address
                              ? t.end - format->base_address : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_mem_free(t.items);
    return true;
}

int64_t xx_binscii_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binscii_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_binscii_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binscii_handle_base_info(format, pd))
               ? ((xx_binscii *)format)->number_of_records : 0U;
}

static bool bsc_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
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

static const xx_var *bsc_option(const xx_list_s *options, uint32_t id) {
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bsc_set_record(xx_archive_record *record, const bsc_member *m) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = m->start;
    record->header_size = 0;
    record->data_offset = m->start;
    record->compressed_size = m->end > m->start ? m->end - m->start : 0;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)m->file_len) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

xx_archive_record_state *xx_binscii_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    bsc_stream *stream;
    xx_archive_record_state *state;
    stream = (bsc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!bsc_scan(format, &stream->table, false, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bsc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bsc_stream_free;
    state->total_records = stream->table.count;
    if (!bsc_copy_options(&state->options, options) ||
        !bsc_set_record(&state->current_record, &stream->table.items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_binscii_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_binscii_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    bsc_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (bsc_stream *)state->internal_state) ||
        stream->index + 1U >= stream->table.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!bsc_set_record(&state->current_record,
                        &stream->table.items[stream->index])) {
        state->has_record = false;
        return false;
    }
    return true;
}

/* Re-decode one member from its first segment. */
static bool bsc_unpack_member(Abstractformat *format, const bsc_member *m,
                              xx_io_device *out, xx_pd_struct *pd) {
    bsc_lines r;
    bsc_segment *s;
    bsc_member check;
    bool result = false;
    int64_t total;
    if (!m->good) return false;
    total = xx_io_total_size(format->device);
    if (m->start < 0 || m->start >= total) return false;
    if (!bsc_lines_open(&r, format->device, m->start, total)) return false;
    s = (bsc_segment *)xx_mem_alloc(sizeof(*s));
    if (s && bsc_next_line(&r) && bsc_is_signature(&r) &&
        bsc_read_header(&r, s) && s->offset == 0U &&
        s->file_len == m->file_len) {
        xx_mem_zero(&check, sizeof(check));
        result = bsc_assemble(&r, s, &check, out, pd) &&
                 check.end == m->end;
    }
    if (s) xx_mem_free(s);
    bsc_lines_close(&r);
    return result;
}

bool xx_binscii_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    bsc_stream *stream;
    const bsc_member *m;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (bsc_stream *)state->internal_state) ||
        stream->index >= stream->table.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    m = &stream->table.items[stream->index];
    path_option = bsc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return bsc_unpack_member(format, m, NULL, pd);
    if (!m->good || !bsc_safe_output_name(m->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", m->name)
               : xx_str_concat(base, m->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = bsc_unpack_member(format, m, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_binscii_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
