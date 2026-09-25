/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QSetup Installation Suite (Pantaray Research) self-extracting installer.
 *
 * Ported from the XArchive reference module installers/xqsetup.cpp (MIT,
 * Copyright (c) 2026 hors), restructured to stream every record through the
 * library's Deflate decoder instead of holding it in memory.  Nothing in the
 * stub is executed or emulated: only the PE headers are read, to find where
 * the image ends.
 *
 * Carrier: a PE32 stub ("QSetup will extract files to your Temp Drive").
 * The container sits exactly at the PE overlay (the end of the section with
 * the highest raw end):
 *
 *   u32 n1 (1 or 2), n1 bytes of '|'
 *   u32 n2 (>= 7), n2 bytes: the '|'-joined product list, which opens with
 *       "|http:" ("|http://|.info|.exe|<key>|" in every known build)
 *   records, each
 *       u32 size, then size bytes of ONE complete zlib (RFC 1950) stream
 *   trailer, 0x4A bytes:
 *       +0x04 u32 container offset (the overlay offset again)
 *       +0x08 u32 number of records
 *       +0x0C u32 4A3B2C1D
 *       +0x46 u32 0x4A
 *
 * A record decodes to a NUL-terminated header line "|<name>|<seconds>|",
 * seconds counted from 1980-01-01, and the file body is everything behind the
 * NUL.  A trailing '*' on the name marks "run after install" and is not part
 * of the name.  Payload files carry an index key "NNNNN#" in front of the
 * installed name; the key is dropped, and only brought back when two records
 * would otherwise extract to the same name (a PocketPC carrier ships
 * Setup.exe three times).
 *
 * Walking the chain only needs the size words.  A record's name, though, sits
 * inside its compressed stream, so listing decodes the first few KB of every
 * record (the header line, never the body); extraction decodes the whole
 * stream and checks its Adler-32.  Nothing declares a decoded size, so the
 * records carry no uncompressed size (XArchive inflates every record at
 * listing time to get it; with the header-only listing a large installer
 * lists in milliseconds and a decompression bomb costs nothing to list).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qsetup_installation_suite/xx_qsetup_installation_suite.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef QSETUP_INSTALLATION_SUITE
#define XX_QSETUP_INSTALLATION_SUITE_FILE_TYPE \
    XX_FILE_TYPE_QSETUP_INSTALLATION_SUITE
#else
#define XX_QSETUP_INSTALLATION_SUITE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define QS_MZ_HEADER 0x40U
#define QS_NT_HEADER 24U
#define QS_MIN_LFANEW 0x40U
#define QS_MAX_LFANEW 0x10000U
#define QS_MAX_SECTIONS 96U
#define QS_SECTION_SIZE 40U
/* The two declared strings plus the first record's size word and zlib
 * header: everything the cheap check reads from the container. */
#define QS_MIN_CONTAINER 0x20
#define QS_HEAD 16U
#define QS_MIN_STRING2 7U
/* A zlib stream is at least a 2-byte header, one block and the Adler-32. */
#define QS_MIN_RECORD 8U
#define QS_TRAILER_SIZE 0x4AU
#define QS_TRAILER_MAGIC 0x4A3B2C1DU
#define QS_TRAILER_OFFSET 0x04U
#define QS_TRAILER_COUNT 0x08U
#define QS_TRAILER_MAGIC_AT 0x0CU
#define QS_TRAILER_SIZE_AT 0x46U
/* A desktop setup program; the caps only keep a crafted chain from growing
 * the member table, a header line or one decoded file without bound. */
#define QS_MAX_MEMBERS 65536U
#define QS_MAX_HEADER_LINE 4096U
#define QS_MAX_BODY 0x20000000U
#define QS_MAX_NAME 255U
#define QS_MAX_SECONDS_DIGITS 10U
/* Seconds from 1970-01-01 to 1980-01-01. */
#define QS_EPOCH_1980 315532800ULL
#define QS_METHOD_DEFLATE 8U
#define QS_ADLER_MOD 65521U
#define QS_ADLER_BLOCK 5552U

static const char g_qs_http[6] = {'|', 'h', 't', 't', 'p', ':'};

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint16_t qs_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t qs_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static bool qs_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool qs_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }

/* RFC 1950 header without a preset dictionary. */
static bool qs_zlib_header_ok(const uint8_t *h) {
    uint32_t cmf = h[0], flg = h[1];
    return (cmf & 0x0FU) == 8U && (cmf >> 4U) <= 7U &&
           ((cmf << 8U) | flg) % 31U == 0U && (flg & 0x20U) == 0U;
}

/* ---------------------------------------------------------------------- */
/* Carrier                                                                 */

typedef struct qs_location_s {
    int64_t total;     /**< Bytes from base to the end of the device. */
    int64_t container; /**< Base-relative overlay start. */
    int64_t records;   /**< Base-relative first record. */
} qs_location;

/* Reads the PE headers far enough to know where the image ends, then checks
 * the two declared strings at that exact offset and the first record's size
 * word and zlib header.  Five small reads; cheap enough for every MZ file. */
static bool qs_locate(Abstractformat *format, qs_location *out) {
    uint8_t mz[QS_MZ_HEADER];
    uint8_t nt[QS_NT_HEADER];
    uint8_t sections[QS_MAX_SECTIONS * QS_SECTION_SIZE];
    uint8_t head[QS_HEAD];
    uint8_t record[6];
    int64_t base, total, table, overlay = 0, container_size, records;
    uint32_t lfanew, nsec, optsz, index, length1, length2;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    total -= base;
    if (total < (int64_t)QS_MZ_HEADER) return false;
    if (!qs_read_at(format->device, base, mz, sizeof(mz)) || mz[0] != 'M' ||
        mz[1] != 'Z')
        return false;
    lfanew = qs_le32(mz + 0x3C);
    if (lfanew < QS_MIN_LFANEW || lfanew > QS_MAX_LFANEW ||
        (int64_t)lfanew + (int64_t)sizeof(nt) > total ||
        !qs_read_at(format->device, base + lfanew, nt, sizeof(nt)) ||
        nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0 || nt[3] != 0)
        return false;
    nsec = qs_le16(nt + 6);
    optsz = qs_le16(nt + 20);
    if (nsec == 0U || nsec > QS_MAX_SECTIONS) return false;
    table = (int64_t)lfanew + (int64_t)QS_NT_HEADER + (int64_t)optsz;
    if (table + (int64_t)nsec * (int64_t)QS_SECTION_SIZE > total ||
        !qs_read_at(format->device, base + table, sections,
                    nsec * QS_SECTION_SIZE))
        return false;
    for (index = 0U; index < nsec; ++index) {
        const uint8_t *s = sections + index * QS_SECTION_SIZE;
        int64_t raw_size = (int64_t)qs_le32(s + 16);
        int64_t raw_ptr = (int64_t)qs_le32(s + 20);
        if (raw_size != 0 && raw_ptr + raw_size > overlay)
            overlay = raw_ptr + raw_size;
    }
    if (overlay <= 0 || overlay >= total) return false;
    container_size = total - overlay;
    if (container_size < QS_MIN_CONTAINER) return false;

    if (!qs_read_at(format->device, base + overlay, head, sizeof(head)))
        return false;
    length1 = qs_le32(head);
    if (length1 != 1U && length1 != 2U) return false;
    for (index = 0U; index < length1; ++index)
        if (head[4U + index] != '|') return false;
    length2 = qs_le32(head + 4U + length1);
    /* The product list must leave room for one record behind it. */
    if (length2 < QS_MIN_STRING2 ||
        (int64_t)length2 > container_size - (int64_t)(8U + length1) -
                               (int64_t)(4U + QS_MIN_RECORD) ||
        xx_rt_memcmp(head + 8U + length1, g_qs_http, sizeof(g_qs_http)) != 0)
        return false;
    records = overlay + (int64_t)(8U + length1) + (int64_t)length2;

    if (!qs_read_at(format->device, base + records, record, sizeof(record)))
        return false;
    {
        int64_t size = (int64_t)qs_le32(record);
        if (size < (int64_t)QS_MIN_RECORD || size > total - records - 4 ||
            !qs_zlib_header_ok(record + 4))
            return false;
    }
    out->total = total;
    out->container = overlay;
    out->records = records;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Decoded-stream sink                                                     */

/* Extraction: the decoder writes the whole decoded stream here.  The header
 * line is skipped, only the body reaches the target (none when merely
 * verifying), and the Adler-32 runs over everything.  The body cap turns a
 * decompression bomb into a write error. */
typedef struct qs_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t body;
    uint32_t adler_a;
    uint32_t adler_b;
    size_t line_length;
    bool line_done;
    bool failed;
} qs_sink;

static void qs_adler(qs_sink *sink, const uint8_t *data, size_t size) {
    uint32_t a = sink->adler_a, b = sink->adler_b;
    while (size > 0U) {
        size_t chunk = size < QS_ADLER_BLOCK ? size : QS_ADLER_BLOCK;
        size -= chunk;
        while (chunk-- > 0U) {
            a += *data++;
            b += a;
        }
        a %= QS_ADLER_MOD;
        b %= QS_ADLER_MOD;
    }
    sink->adler_a = a;
    sink->adler_b = b;
}

static ssize_t qs_sink_write(xx_io_device *self, const void *buffer,
                             size_t size) {
    qs_sink *sink = self ? (qs_sink *)self->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t used = 0U, rest, done = 0U;
    if (!sink || sink->failed || (!bytes && size != 0U) ||
        size > (size_t)0x7FFFFFFF)
        return -1;
    while (!sink->line_done && used < size) {
        if (bytes[used++] == 0U) {
            sink->line_done = true;
            break;
        }
        if (++sink->line_length > QS_MAX_HEADER_LINE) {
            sink->failed = true;
            return -1;
        }
    }
    qs_adler(sink, bytes, used);
    rest = size - used;
    if (rest == 0U) return (ssize_t)size;
    if ((uint64_t)rest > (uint64_t)QS_MAX_BODY - sink->body) {
        sink->failed = true;
        return -1;
    }
    qs_adler(sink, bytes + used, rest);
    while (sink->target && done < rest) {
        ssize_t wrote =
            xx_io_write(sink->target, bytes + used + done, rest - done);
        if (wrote <= 0 || (size_t)wrote > rest - done) {
            sink->failed = true;
            return -1;
        }
        done += (size_t)wrote;
    }
    sink->body += (uint64_t)rest;
    return (ssize_t)size;
}

/* Inflates one whole record (absolute stream offset and size) into target.
 * The stream must decode completely, carry a header line of line_length
 * bytes and match its Adler-32.  *body receives the body size. */
static bool qs_inflate(xx_io_device *device, int64_t offset, int64_t size,
                       size_t line_length, xx_io_device *target,
                       uint64_t *body, xx_pd_struct *pd) {
    qs_sink sink;
    uint8_t trailer[4];
    uint32_t stored;
    if (size < (int64_t)QS_MIN_RECORD) return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = qs_sink_write;
    sink.device.priv = &sink;
    sink.target = target;
    sink.adler_a = 1U;
    if (!xx_deflate_unpack_device(device, offset + 2, size - 2, &sink.device,
                                  false, pd) ||
        sink.failed || !sink.line_done || sink.line_length != line_length ||
        !qs_read_at(device, offset + size - 4, trailer, sizeof(trailer)))
        return false;
    stored = ((uint32_t)trailer[0] << 24U) | ((uint32_t)trailer[1] << 16U) |
             ((uint32_t)trailer[2] << 8U) | (uint32_t)trailer[3];
    if (stored != ((sink.adler_b << 16U) | sink.adler_a)) return false;
    if (body) *body = sink.body;
    return true;
}

/* Listing: decodes only the start of a record, into a buffer one byte
 * larger than the longest header line, and returns the line's length (the
 * bytes before the NUL).  The decoder stops as soon as the buffer is full,
 * so a record costs at most one input buffer and QS_MAX_HEADER_LINE + 1
 * decoded bytes however large its body is. */
static bool qs_read_line(xx_io_device *device, int64_t offset, int64_t size,
                         uint8_t *line, size_t *length, xx_pd_struct *pd) {
    size_t written = 0U, index;
    if (size < (int64_t)QS_MIN_RECORD) return false;
    (void)xx_deflate_unpack_device_to_memory(device, offset + 2, size - 2,
                                             line, QS_MAX_HEADER_LINE + 1U,
                                             &written, false, pd);
    if (written > QS_MAX_HEADER_LINE + 1U) written = QS_MAX_HEADER_LINE + 1U;
    for (index = 0U; index < written; ++index) {
        if (line[index] == 0U) {
            *length = index;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------- */
/* Header line and names                                                   */

typedef struct qs_line_s {
    const uint8_t *name; /**< Inside the sink's line; '*' already dropped. */
    size_t name_length;
    uint32_t seconds;
    bool run_after;
} qs_line;

/* "|<name>|<seconds>|": exactly three '|', the last one closing the line,
 * and a decimal seconds field. */
static bool qs_parse_line(const uint8_t *line, size_t length, qs_line *out) {
    size_t first = 0U, second, index;
    uint64_t seconds = 0U;
    if (length < 4U || line[0] != '|' || line[length - 1U] != '|') return false;
    second = 1U;
    while (second < length - 1U && line[second] != '|') ++second;
    if (second >= length - 1U) return false;
    if (second + 1U >= length - 1U ||
        length - 1U - (second + 1U) > QS_MAX_SECONDS_DIGITS)
        return false;
    for (index = second + 1U; index < length - 1U; ++index) {
        if (line[index] < '0' || line[index] > '9') return false;
        seconds = seconds * 10U + (uint64_t)(line[index] - '0');
    }
    if (seconds > 0xFFFFFFFFULL) return false;
    out->name = line + first + 1U;
    out->name_length = second - 1U;
    out->run_after = false;
    if (out->name_length > 0U && out->name[out->name_length - 1U] == '*') {
        --out->name_length;
        out->run_after = true;
    }
    out->seconds = (uint32_t)seconds;
    return true;
}

static char qs_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the name's stem (before its first dot, trailing spaces ignored)
 * is a Windows device name. */
static bool qs_is_device(const uint8_t *name, size_t length) {
    static const char *const names[] = {"CON",    "PRN",     "AUX",   "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index, k;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *n = names[index];
        for (k = 0U; k < stem && n[k]; ++k)
            if (qs_upper((char)name[k]) != n[k]) break;
        if (k == stem && n[k] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = qs_upper((char)name[0]), b = qs_upper((char)name[1]),
             c = qs_upper((char)name[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

/* Windows-1252 for 0x80..0x9F; 0 marks the five unassigned bytes.  The
 * remaining high bytes are the same code points as Latin-1. */
static const uint16_t g_qs_cp1252[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

static uint32_t qs_code_point(uint8_t c) {
    if (c < 0x80U || c >= 0xA0U) return c;
    return g_qs_cp1252[c - 0x80U];
}

/* A member name is one bare file name.  Refused: empty or over-long names,
 * "." and "..", a trailing dot or space (Windows drops those, so "a.txt."
 * would land on "a.txt"), path separators and the other characters Windows
 * reserves, control bytes, unassigned code-page bytes and device names. */
static bool qs_name_safe(const uint8_t *raw, size_t length) {
    size_t index;
    if (length == 0U || length > QS_MAX_NAME) return false;
    if (raw[length - 1U] == '.' || raw[length - 1U] == ' ') return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c == 0x7FU || c == '<' || c == '>' || c == ':' ||
            c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' ||
            c == '*' || qs_code_point(c) == 0U)
            return false;
    }
    return !qs_is_device(raw, length);
}

/* Converts a safe code-page name to UTF-8. */
static char *qs_make_name(const uint8_t *raw, size_t length) {
    size_t out_length = 0U, index, o = 0U;
    char *name;
    for (index = 0U; index < length; ++index) {
        uint32_t cp = qs_code_point(raw[index]);
        out_length += cp < 0x80U ? 1U : cp < 0x800U ? 2U : 3U;
    }
    name = (char *)xx_mem_alloc(out_length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        uint32_t cp = qs_code_point(raw[index]);
        if (cp < 0x80U) {
            name[o++] = (char)cp;
        } else if (cp < 0x800U) {
            name[o++] = (char)(0xC0U | (cp >> 6U));
            name[o++] = (char)(0x80U | (cp & 0x3FU));
        } else {
            name[o++] = (char)(0xE0U | (cp >> 12U));
            name[o++] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
            name[o++] = (char)(0x80U | (cp & 0x3FU));
        }
    }
    name[o] = 0;
    return name;
}

static char *qs_generated_name(size_t index, uint32_t suffix) {
    char text[48];
    if (suffix)
        (void)xx_rt_snprintf(text, sizeof(text), "file_%04lu_%lu",
                             (unsigned long)index, (unsigned long)suffix);
    else
        (void)xx_rt_snprintf(text, sizeof(text), "file_%04lu",
                             (unsigned long)index);
    return xx_str_dup(text);
}

/* Names compare without case (output lands on case-insensitive file
 * systems), and that includes the accented letters of Windows-1252: "É.txt"
 * and "é.txt" are one file on NTFS.  The names are UTF-8 this reader built
 * from code-page bytes, so every sequence is well formed and at most three
 * bytes long.  Returns the folded code point and advances *s; 0 at the end. */
static uint32_t qs_fold_next(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp;
    if (p[0] == 0U) return 0U;
    if (p[0] < 0x80U) {
        cp = p[0];
        *s += 1;
    } else if ((p[0] & 0xE0U) == 0xC0U && p[1]) {
        cp = ((uint32_t)(p[0] & 0x1FU) << 6U) | (uint32_t)(p[1] & 0x3FU);
        *s += 2;
    } else if ((p[0] & 0xF0U) == 0xE0U && p[1] && p[2]) {
        cp = ((uint32_t)(p[0] & 0x0FU) << 12U) |
             ((uint32_t)(p[1] & 0x3FU) << 6U) | (uint32_t)(p[2] & 0x3FU);
        *s += 3;
    } else {
        cp = p[0];
        *s += 1;
    }
    if (cp >= 'a' && cp <= 'z') return cp - 0x20U;
    if (cp >= 0xE0U && cp <= 0xFEU && cp != 0xF7U) return cp - 0x20U;
    switch (cp) {
    case 0xFFU: return 0x0178U;  /* y diaeresis */
    case 0x0161U: return 0x0160U; /* s caron */
    case 0x0153U: return 0x0152U; /* oe */
    case 0x017EU: return 0x017DU; /* z caron */
    default: return cp;
    }
}

/* An open-addressing set keeps the duplicate check linear. */
static uint32_t qs_hash(const char *s) {
    uint32_t h = 2166136261U, cp;
    while ((cp = qs_fold_next(&s)) != 0U) {
        h ^= cp;
        h *= 16777619U;
    }
    return h;
}

static bool qs_same(const char *a, const char *b) {
    for (;;) {
        uint32_t x = qs_fold_next(&a), y = qs_fold_next(&b);
        if (x != y) return false;
        if (x == 0U) return true;
    }
}

typedef struct qs_names_s {
    const char **slots;
    size_t mask;
} qs_names;

static bool qs_names_has(const qs_names *set, const char *name) {
    size_t at = qs_hash(name) & set->mask;
    while (set->slots[at]) {
        if (qs_same(set->slots[at], name)) return true;
        at = (at + 1U) & set->mask;
    }
    return false;
}

static void qs_names_insert(qs_names *set, const char *name) {
    size_t at = qs_hash(name) & set->mask;
    while (set->slots[at]) at = (at + 1U) & set->mask;
    set->slots[at] = name;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

typedef struct qs_member_s {
    int64_t stream_offset; /**< Absolute offset of the zlib stream. */
    int64_t stream_size;
    uint8_t *raw;          /**< Name bytes from the header line, key kept. */
    size_t raw_length;
    char *name;            /**< Unique UTF-8 output name. */
    size_t line_length;    /**< Header line bytes before its NUL. */
    uint32_t seconds;      /**< Since 1980-01-01. */
    bool header_ok;        /**< The header line parsed. */
    bool run_after;
} qs_member;

typedef struct qs_list_s {
    qs_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    qs_location location;
    int64_t end; /**< Base-relative end of what was walked. */
    bool complete;
} qs_list;

static void qs_list_free(void *opaque) {
    qs_list *list = (qs_list *)opaque;
    size_t index;
    if (!list) return;
    if (list->items) {
        for (index = 0U; index < list->count; ++index) {
            if (list->items[index].raw) xx_mem_free(list->items[index].raw);
            if (list->items[index].name) xx_mem_free(list->items[index].name);
        }
        xx_mem_free(list->items);
    }
    xx_mem_free(list);
}

static bool qs_list_add(qs_list *list, const qs_member *member) {
    if (list->count >= QS_MAX_MEMBERS) return false;
    if (list->count == list->capacity) {
        size_t grown = list->capacity ? list->capacity * 2U : 32U;
        qs_member *items;
        if (grown > QS_MAX_MEMBERS) grown = QS_MAX_MEMBERS;
        items = (qs_member *)xx_mem_realloc(list->items,
                                            grown * sizeof(*items));
        if (!items) return false;
        list->items = items;
        list->capacity = grown;
    }
    list->items[list->count++] = *member;
    return true;
}

/* Decodes a record's header line.  Returns false only when memory runs
 * out; a line that does not parse leaves header_ok false.  Without a member
 * the line only has to parse. */
static bool qs_read_header(Abstractformat *format, int64_t offset,
                           int64_t size, uint8_t *buffer, qs_member *m,
                           bool *parsed, xx_pd_struct *pd) {
    qs_line line;
    size_t length = 0U;
    xx_mem_zero(&line, sizeof(line));
    *parsed = qs_read_line(format->device, offset, size, buffer, &length,
                           pd) &&
              qs_parse_line(buffer, length, &line);
    if (!*parsed || !m) return true;
    m->header_ok = true;
    m->line_length = length;
    m->seconds = line.seconds;
    m->run_after = line.run_after;
    m->raw_length = line.name_length;
    m->raw = (uint8_t *)xx_mem_alloc(line.name_length + 1U);
    if (!m->raw) return false;
    if (line.name_length) xx_rt_memcpy(m->raw, line.name, line.name_length);
    m->raw[line.name_length] = 0U;
    return true;
}

/* Output names: the installed name (index key dropped), then the keyed name,
 * then "file_NNNN", then "file_NNNN_k", whichever no earlier member took. */
static bool qs_assign_names(qs_list *list) {
    qs_names set;
    size_t slots = 64U, index;
    bool ok = true;
    while (slots < list->count * 2U) slots *= 2U;
    set.slots = (const char **)xx_mem_calloc(slots, sizeof(char *));
    if (!set.slots) return false;
    set.mask = slots - 1U;
    for (index = 0U; index < list->count && ok; ++index) {
        qs_member *m = &list->items[index];
        char *candidate = NULL;
        uint32_t suffix;
        if (m->header_ok && qs_name_safe(m->raw, m->raw_length)) {
            const uint8_t *plain = m->raw;
            size_t plain_length = m->raw_length, k;
            bool keyed = m->raw_length > 6U && m->raw[5] == '#';
            for (k = 0U; keyed && k < 5U; ++k)
                if (m->raw[k] < '0' || m->raw[k] > '9') keyed = false;
            if (keyed && qs_name_safe(m->raw + 6, m->raw_length - 6U)) {
                plain = m->raw + 6;
                plain_length = m->raw_length - 6U;
            }
            candidate = qs_make_name(plain, plain_length);
            if (!candidate) {
                ok = false;
                break;
            }
            if (qs_names_has(&set, candidate) && plain != m->raw) {
                xx_mem_free(candidate);
                candidate = qs_make_name(m->raw, m->raw_length);
                if (!candidate) {
                    ok = false;
                    break;
                }
            }
        }
        /* At most count + 1 generated names are ever tried, and the set
         * holds at most count names. */
        for (suffix = 0U; !candidate || qs_names_has(&set, candidate);
             ++suffix) {
            if (candidate) xx_mem_free(candidate);
            candidate = NULL;
            if (suffix > list->count + 1U) break;
            candidate = qs_generated_name(index, suffix);
            if (!candidate) break;
        }
        if (!candidate) {
            ok = false;
            break;
        }
        m->name = candidate;
        qs_names_insert(&set, m->name);
    }
    xx_mem_free((void *)set.slots);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Walk                                                                    */

/* Follows the size words from the first record to the trailer.  The first
 * record's header line must parse, which is what separates a QSetup chain
 * from a lookalike overlay; with members every record's header line is
 * decoded for its name.  A chain that runs out of data before its trailer is
 * a truncated carrier: the records that fit whole are kept.  A trailer that
 * disagrees with the walk means the walk went astray, and nothing is
 * published. */
static bool qs_walk(Abstractformat *format, qs_list **result, bool members,
                    xx_pd_struct *pd) {
    qs_list *list;
    uint8_t *buffer = NULL;
    int64_t base, position;
    bool ok = false;
    if (!result) return false;
    *result = NULL;
    list = (qs_list *)xx_mem_calloc(1U, sizeof(*list));
    if (!list) return false;
    if (!qs_locate(format, &list->location)) goto done;
    buffer = (uint8_t *)xx_mem_alloc(QS_MAX_HEADER_LINE + 1U);
    if (!buffer) goto done;
    base = format->base_address;
    position = list->location.records;
    for (;;) {
        uint8_t header[QS_TRAILER_SIZE];
        int64_t left = list->location.total - position, size;
        size_t have;
        qs_member member;
        bool parsed;
        if (qs_stopped(pd)) goto done;
        if (left < 4) break;
        have = left < (int64_t)QS_TRAILER_SIZE ? (size_t)left
                                               : QS_TRAILER_SIZE;
        if (!qs_read_at(format->device, base + position, header, have))
            break;
        if (have == QS_TRAILER_SIZE &&
            qs_le32(header + QS_TRAILER_MAGIC_AT) == QS_TRAILER_MAGIC &&
            qs_le32(header + QS_TRAILER_SIZE_AT) == QS_TRAILER_SIZE) {
            if ((int64_t)qs_le32(header + QS_TRAILER_OFFSET) !=
                    list->location.container ||
                (size_t)qs_le32(header + QS_TRAILER_COUNT) != list->count)
                goto done;
            list->complete = true;
            position += QS_TRAILER_SIZE;
            break;
        }
        size = (int64_t)qs_le32(header);
        if (size < (int64_t)QS_MIN_RECORD || size > left - 4 ||
            !qs_zlib_header_ok(header + 4))
            break;
        if (list->count >= QS_MAX_MEMBERS) goto done;
        xx_mem_zero(&member, sizeof(member));
        member.stream_offset = base + position + 4;
        member.stream_size = size;
        if (members || list->count == 0U) {
            if (!qs_read_header(format, member.stream_offset, size, buffer,
                                members ? &member : NULL, &parsed, pd))
                goto done;
            if (list->count == 0U && !parsed) goto done;
        }
        if (members) {
            if (!qs_list_add(list, &member)) {
                if (member.raw) xx_mem_free(member.raw);
                goto done;
            }
        } else {
            ++list->count;
        }
        position += 4 + size;
    }
    if (list->count == 0U) goto done;
    list->end = position;
    if (members && !qs_assign_names(list)) goto done;
    ok = true;
done:
    if (buffer) xx_mem_free(buffer);
    if (!ok) {
        qs_list_free(list);
        return false;
    }
    *result = list;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool qs_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *qs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool qs_set_record(xx_archive_record *record, const qs_member *m) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = m->stream_offset - 4;
    record->header_size = 4;
    record->data_offset = m->stream_offset;
    record->compressed_size = m->stream_size;
    if (!xx_archive_record_set_original_name(record, m->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)m->stream_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        QS_METHOD_DEFLATE) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* Unix seconds; 0 in the container means no stamp. */
    if (m->header_ok && m->seconds != 0U &&
        !xx_archive_record_set_meta_u64(
            record, XX_META_ID_TIMESTAMP,
            (uint64_t)m->seconds + QS_EPOCH_1980))
        return false;
    return true;
}

/* The whole stream is decoded here; nothing declares the body size ahead. */
static bool qs_extract(Abstractformat *format, const qs_member *m,
                       xx_io_device *destination, xx_pd_struct *pd) {
    uint64_t body = 0U;
    if (!m->header_ok) return false;
    return qs_inflate(format->device, m->stream_offset, m->stream_size,
                      m->line_length, destination, &body, pd);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_qsetup_installation_suite_init(xx_qsetup_installation_suite *archive,
                                       xx_io_device *device,
                                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_QSETUP_INSTALLATION_SUITE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-qsetup-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_qsetup_installation_suite_check_is_valid;
    archive->format.handle_base_info =
        xx_qsetup_installation_suite_handle_base_info;
    archive->format.get_format_size =
        xx_qsetup_installation_suite_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qsetup_installation_suite_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qsetup_installation_suite_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qsetup_installation_suite_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qsetup_installation_suite_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qsetup_installation_suite_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qsetup_installation_suite_free_archive_records_reading;
    archive->container_offset = -1;
    archive->records_offset = -1;
}

xx_qsetup_installation_suite *xx_qsetup_installation_suite_create(
    xx_io_device *device, int64_t base_address) {
    xx_qsetup_installation_suite *archive =
        (xx_qsetup_installation_suite *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_qsetup_installation_suite_init(archive, device, base_address);
    return archive;
}

void xx_qsetup_installation_suite_destroy(
    xx_qsetup_installation_suite *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_qsetup_installation_suite_free(xx_qsetup_installation_suite *archive) {
    if (!archive) return;
    xx_qsetup_installation_suite_destroy(archive);
    xx_mem_free(archive);
}

/* Cheap: the PE headers, 16 bytes of the container and 6 bytes of the first
 * record. */
bool xx_qsetup_installation_suite_check_is_valid(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    qs_location location;
    (void)pd;
    return qs_locate(format, &location);
}

bool xx_qsetup_installation_suite_handle_base_info(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    qs_list *list;
    xx_qsetup_installation_suite *archive;
    if (!format) return false;
    if (!qs_walk(format, &list, false, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_qsetup_installation_suite *)format;
    archive->number_of_records = list->count;
    archive->container_offset =
        format->base_address + list->location.container;
    archive->records_offset = format->base_address + list->location.records;
    archive->complete = list->complete;
    format->number_of_archive_records = list->count;
    format->format_size = list->end;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_QSETUP_INSTALLATION_SUITE_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    qs_list_free(list);
    return true;
}

int64_t xx_qsetup_installation_suite_get_format_size(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qsetup_installation_suite_handle_base_info(format,
                                                                    pd))
               ? format->format_size
               : -1;
}

uint64_t xx_qsetup_installation_suite_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qsetup_installation_suite_handle_base_info(format,
                                                                    pd))
               ? ((xx_qsetup_installation_suite *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_qsetup_installation_suite_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    qs_list *list;
    xx_archive_record_state *state;
    if (!qs_walk(format, &list, true, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        qs_list_free(list);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = list;
    state->free_internal = qs_list_free;
    state->total_records = (int64_t)list->count;
    if (!qs_copy_options(&state->options, options) ||
        !qs_set_record(&state->current_record, &list->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_qsetup_installation_suite_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_qsetup_installation_suite_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    qs_list *list;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(list = (qs_list *)state->internal_state) ||
        list->index + 1U >= list->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++list->index;
    ++state->current_index;
    state->has_record =
        qs_set_record(&state->current_record, &list->items[list->index]);
    return state->has_record;
}

bool xx_qsetup_installation_suite_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    qs_list *list;
    qs_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(list = (qs_list *)state->internal_state) ||
        list->index >= list->count || qs_stopped(pd))
        return false;
    member = &list->items[list->index];
    if (!member->header_ok || !member->name) return false;
    path_option = qs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return qs_extract(format, member, NULL, pd);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = qs_extract(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_qsetup_installation_suite_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
