/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ABBYY "Fine Objects" setup / patch executables (the Lingvo 5 updater).
 * xx_sfx_abbyy_fine_objects.h carries the payload layout.
 *
 * The layout was recovered from the corpus sample (Lingvo5a.exe, five
 * members); its name and size tables are the ones U3's "SFX Lingvo" handler
 * checks, and U3 lists the same five members.  U3 copies the member streams
 * out raw; here they are decoded, because every member is a complete FINEAR
 * stream (the format of xxfclib's finear reader) whose stored plaintext size
 * and CRC-16/ARC anchor the decode.  The code is written from the file
 * structure.
 *
 * The stub executable is parsed only as far as its section table; nothing in
 * it is executed.  The probe reads the section table, the payload header
 * through a 4 KiB window, the 19-byte trailer and each 17-byte FINEAR header,
 * and allocates nothing.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_abbyy_fine_objects/xx_sfx_abbyy_fine_objects.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef SFX_ABBYY_FINE_OBJECTS
#define XX_SFX_ABBYY_FINE_OBJECTS_FILE_TYPE XX_FILE_TYPE_SFX_ABBYY_FINE_OBJECTS
#else
#define XX_SFX_ABBYY_FINE_OBJECTS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- limits -------------------------------------------------------------- */

#define AFO_MIN_FILE 0x200
#define AFO_MAX_LFANEW INT64_C(0x10000000)
#define AFO_MAX_SECTIONS 96U
/* Sanity cap on the member and window-class counts (the known setup has 5
 * and 3); the counts are also bounded by the bytes left in the file. */
#define AFO_MAX_COUNT 0x10000U
#define AFO_EXTRA_STRINGS 3U
#define AFO_DATES_SIZE 12
#define AFO_FINEAR_SIZE 17U
#define AFO_TRAILER_SIZE 19U
#define AFO_TAG_SIZE 14U
/* Smallest member: a one-byte name, its u32 size and a bare FINEAR header. */
#define AFO_MIN_MEMBER (2 + 4 + (int64_t)AFO_FINEAR_SIZE)
/* Fixed payload bytes besides the members: three counts, three empty
 * strings, the 12 fixed bytes and the trailer. */
#define AFO_FIXED_BYTES (12 + 3 + AFO_DATES_SIZE + (int64_t)AFO_TRAILER_SIZE)
/* One FINEAR stream may not ask for more than this. */
#define AFO_MAX_PLAIN UINT64_C(0x10000000)
#define AFO_MAX_BODY UINT64_C(0x10000000)
/* LHA -lh1- cannot expand more than 48 times (a 60-byte match costs at
 * least a 1-bit symbol and a 9-bit position); a declared plaintext beyond
 * 64 times the body is a lie and must not size an allocation. */
#define AFO_LH1_RATIO UINT64_C(64)
#define AFO_METHOD_LH1 1U
#define AFO_WINDOW 4096U
#define AFO_RENAME_LIMIT 100000U

static const uint8_t afo_finear_magic[9] = {'F', 'I', 'N', 'E', 'A',
                                            'R', 0xddU, 0x88U, 0xddU};
static const char afo_tag[AFO_TAG_SIZE + 1U] = "ArcUpdateABBYY";

/* --- small helpers --------------------------------------------------------- */

static uint32_t afo_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t afo_le32(const uint8_t *bytes) {
    return afo_le16(bytes) | (afo_le16(bytes + 2) << 16U);
}

static bool afo_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool afo_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

/* A forward reader over the payload header with a 4 KiB window, so the
 * name and class tables cost one device read per window, not per string. */
typedef struct afo_cursor_s {
    xx_io_device *device;
    int64_t base;      /* device offset of relative position 0 */
    int64_t size;      /* bytes available from the base */
    int64_t position;  /* relative to the base */
    int64_t window_at; /* relative offset of window[0] */
    size_t window_size;
    uint8_t window[AFO_WINDOW];
} afo_cursor;

static void afo_cursor_init(afo_cursor *cursor, xx_io_device *device,
                            int64_t base, int64_t size, int64_t at) {
    cursor->device = device;
    cursor->base = base;
    cursor->size = size;
    cursor->position = at;
    cursor->window_at = 0;
    cursor->window_size = 0U;
}

/* Copies @p count bytes (or skips them when @p out is NULL). */
static bool afo_cursor_read(afo_cursor *cursor, uint8_t *out, size_t count) {
    while (count != 0U) {
        size_t index, chunk;
        if (cursor->position < cursor->window_at ||
            cursor->position - cursor->window_at >=
                (int64_t)cursor->window_size) {
            int64_t left = cursor->size - cursor->position;
            size_t want;
            if (cursor->position < 0 || left <= 0) return false;
            want = left < (int64_t)AFO_WINDOW ? (size_t)left : AFO_WINDOW;
            cursor->window_size = 0U;
            if (!afo_read_at(cursor->device, cursor->base + cursor->position,
                             cursor->window, want))
                return false;
            cursor->window_at = cursor->position;
            cursor->window_size = want;
        }
        index = (size_t)(cursor->position - cursor->window_at);
        chunk = cursor->window_size - index;
        if (chunk > count) chunk = count;
        if (out) {
            xx_rt_memcpy(out, cursor->window + index, chunk);
            out += chunk;
        }
        cursor->position += (int64_t)chunk;
        count -= chunk;
    }
    return true;
}

static bool afo_cursor_u32(afo_cursor *cursor, uint32_t *value) {
    uint8_t bytes[4];
    if (!afo_cursor_read(cursor, bytes, sizeof(bytes))) return false;
    *value = afo_le32(bytes);
    return true;
}

/* One Pascal string into @p text (256 bytes); *length receives its size. */
static bool afo_cursor_pstring(afo_cursor *cursor, uint8_t *text,
                               size_t *length) {
    uint8_t size;
    if (!afo_cursor_read(cursor, &size, 1U) ||
        !afo_cursor_read(cursor, text, size))
        return false;
    *length = size;
    return true;
}

/* --- stub ------------------------------------------------------------------ */

/* The overlay: where the last section's raw data ends.  A section that
 * claims bytes beyond the file cannot belong to a complete setup. */
static bool afo_overlay(xx_io_device *device, int64_t base, int64_t size,
                        int64_t *overlay) {
    uint8_t dos[0x40], nt[24], table[AFO_MAX_SECTIONS * 40U];
    uint32_t sections, optional, index;
    int64_t lfanew, table_at, end = 0;
    if (size < AFO_MIN_FILE || !afo_read_at(device, base, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    lfanew = (int64_t)afo_le32(dos + 0x3c);
    if (lfanew < 4 || lfanew > AFO_MAX_LFANEW || lfanew > size - 24 ||
        !afo_read_at(device, base + lfanew, nt, sizeof(nt)) || nt[0] != 'P' ||
        nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    sections = afo_le16(nt + 6);
    optional = afo_le16(nt + 20);
    table_at = lfanew + 24 + (int64_t)optional;
    if (sections == 0U || sections > AFO_MAX_SECTIONS ||
        table_at > size - (int64_t)sections * 40 ||
        !afo_read_at(device, base + table_at, table, (size_t)sections * 40U))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * 40U;
        int64_t raw_size = (int64_t)afo_le32(row + 16);
        int64_t raw_offset = (int64_t)afo_le32(row + 20);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    if (end <= table_at) return false;
    *overlay = end;
    return true;
}

/* --- payload --------------------------------------------------------------- */

typedef struct afo_info_s {
    int64_t overlay;    /* member count */
    int64_t names_at;   /* first name */
    int64_t sizes_at;   /* first stream size */
    int64_t streams_at; /* first FINEAR stream */
    int64_t trailer_at;
    uint32_t count;
    uint32_t classes;
} afo_info;

/* A FINEAR header that fits a stream of @p stream_size bytes. */
static bool afo_finear_ok(const uint8_t *header, uint32_t stream_size,
                          uint32_t *plain, uint32_t *crc) {
    uint64_t body, declared;
    uint32_t checksum;
    if (stream_size < AFO_FINEAR_SIZE ||
        xx_rt_memcmp(header, afo_finear_magic, sizeof(afo_finear_magic)) != 0)
        return false;
    body = (uint64_t)stream_size - AFO_FINEAR_SIZE;
    checksum = afo_le32(header + 9);
    declared = afo_le32(header + 13);
    /* A 16-bit CRC stored in a 32-bit slot. */
    if (checksum > 0xffffU || body > AFO_MAX_BODY ||
        declared > AFO_MAX_PLAIN || declared > body * AFO_LH1_RATIO)
        return false;
    if (plain) *plain = (uint32_t)declared;
    if (crc) *crc = checksum;
    return true;
}

/* Finds and checks the whole payload: both tables, the classes, the three
 * strings, the trailer (its stub size must name the overlay) and every
 * member's FINEAR header. */
static bool afo_scan(Abstractformat *format, afo_info *info,
                     xx_pd_struct *pd) {
    afo_cursor cursor;
    uint8_t text[256];
    uint8_t trailer[AFO_TRAILER_SIZE];
    int64_t total, size, sum = 0, stream_at;
    uint32_t count, second, classes, index;
    size_t length, byte;
    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_rt_memset(info, 0, sizeof(*info));
    if (!afo_overlay(format->device, format->base_address, size,
                     &info->overlay) ||
        size - info->overlay < AFO_FIXED_BYTES + AFO_MIN_MEMBER)
        return false;
    afo_cursor_init(&cursor, format->device, format->base_address, size,
                    info->overlay);
    if (!afo_cursor_u32(&cursor, &count) || count == 0U ||
        count > AFO_MAX_COUNT ||
        (int64_t)count > (size - info->overlay - AFO_FIXED_BYTES) /
                             AFO_MIN_MEMBER)
        return false;
    info->names_at = cursor.position;
    for (index = 0U; index < count; ++index) {
        if ((index & 0xffU) == 0U && afo_stopped(pd)) return false;
        if (!afo_cursor_pstring(&cursor, text, &length) || length == 0U)
            return false;
        for (byte = 0U; byte < length; ++byte)
            if (text[byte] < 0x20U) return false;
    }
    if (!afo_cursor_u32(&cursor, &second) || second != count) return false;
    info->sizes_at = cursor.position;
    for (index = 0U; index < count; ++index) {
        uint32_t stream_size;
        if (!afo_cursor_u32(&cursor, &stream_size) ||
            stream_size < AFO_FINEAR_SIZE)
            return false;
        sum += (int64_t)stream_size;
        if (sum > size) return false;
    }
    if (!afo_cursor_u32(&cursor, &classes) || classes > AFO_MAX_COUNT ||
        (int64_t)classes > size - cursor.position)
        return false;
    for (index = 0U; index < classes + AFO_EXTRA_STRINGS; ++index) {
        if ((index & 0xffU) == 0U && afo_stopped(pd)) return false;
        if (!afo_cursor_pstring(&cursor, text, &length)) return false;
    }
    if (!afo_cursor_read(&cursor, NULL, AFO_DATES_SIZE)) return false;
    info->streams_at = cursor.position;
    if (sum > size - info->streams_at - (int64_t)AFO_TRAILER_SIZE)
        return false;
    info->trailer_at = info->streams_at + sum;
    if (!afo_read_at(format->device,
                     format->base_address + info->trailer_at, trailer,
                     sizeof(trailer)) ||
        (int64_t)afo_le32(trailer) != info->overlay ||
        xx_rt_memcmp(trailer + 4, afo_tag, AFO_TAG_SIZE + 1U) != 0)
        return false;
    /* Every member must start with a FINEAR header that fits its size. */
    cursor.position = info->sizes_at;
    stream_at = info->streams_at;
    for (index = 0U; index < count; ++index) {
        uint8_t header[AFO_FINEAR_SIZE];
        uint32_t stream_size;
        if ((index & 0xffU) == 0U && afo_stopped(pd)) return false;
        if (!afo_cursor_u32(&cursor, &stream_size) ||
            !afo_read_at(format->device, format->base_address + stream_at,
                         header, sizeof(header)) ||
            !afo_finear_ok(header, stream_size, NULL, NULL))
            return false;
        stream_at += (int64_t)stream_size;
    }
    info->count = count;
    info->classes = classes;
    return true;
}

/* --- names ----------------------------------------------------------------- */

/* Windows-1251 0x80..0xFF.  The undefined 0x98 keeps its C1 code point; the
 * name check refuses it. */
static const uint16_t afo_cp1251_high[128] = {
    0x0402U, 0x0403U, 0x201AU, 0x0453U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x20ACU, 0x2030U, 0x0409U, 0x2039U, 0x040AU, 0x040CU, 0x040BU, 0x040FU,
    0x0452U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x0098U, 0x2122U, 0x0459U, 0x203AU, 0x045AU, 0x045CU, 0x045BU, 0x045FU,
    0x00A0U, 0x040EU, 0x045EU, 0x0408U, 0x00A4U, 0x0490U, 0x00A6U, 0x00A7U,
    0x0401U, 0x00A9U, 0x0404U, 0x00ABU, 0x00ACU, 0x00ADU, 0x00AEU, 0x0407U,
    0x00B0U, 0x00B1U, 0x0406U, 0x0456U, 0x0491U, 0x00B5U, 0x00B6U, 0x00B7U,
    0x0451U, 0x2116U, 0x0454U, 0x00BBU, 0x0458U, 0x0405U, 0x0455U, 0x0457U,
    0x0410U, 0x0411U, 0x0412U, 0x0413U, 0x0414U, 0x0415U, 0x0416U, 0x0417U,
    0x0418U, 0x0419U, 0x041AU, 0x041BU, 0x041CU, 0x041DU, 0x041EU, 0x041FU,
    0x0420U, 0x0421U, 0x0422U, 0x0423U, 0x0424U, 0x0425U, 0x0426U, 0x0427U,
    0x0428U, 0x0429U, 0x042AU, 0x042BU, 0x042CU, 0x042DU, 0x042EU, 0x042FU,
    0x0430U, 0x0431U, 0x0432U, 0x0433U, 0x0434U, 0x0435U, 0x0436U, 0x0437U,
    0x0438U, 0x0439U, 0x043AU, 0x043BU, 0x043CU, 0x043DU, 0x043EU, 0x043FU,
    0x0440U, 0x0441U, 0x0442U, 0x0443U, 0x0444U, 0x0445U, 0x0446U, 0x0447U,
    0x0448U, 0x0449U, 0x044AU, 0x044BU, 0x044CU, 0x044DU, 0x044EU, 0x044FU};

/* Upper case as Windows compares names, applied to Windows-1251 bytes:
 * ASCII, the Cyrillic alphabet and the extra Cyrillic letters of the
 * 0x80..0xBF block. */
static uint8_t afo_fold(uint8_t c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 0x20U);
    if (c >= 0xE0U) return (uint8_t)(c - 0x20U);
    switch (c) {
    case 0x83U: return 0x81U;
    case 0x90U: return 0x80U;
    case 0x9AU: return 0x8AU;
    case 0x9CU: return 0x8CU;
    case 0x9DU: return 0x8DU;
    case 0x9EU: return 0x8EU;
    case 0x9FU: return 0x8FU;
    case 0xA2U: return 0xA1U;
    case 0xB3U: return 0xB2U;
    case 0xB4U: return 0xA5U;
    case 0xB8U: return 0xA8U;
    case 0xBAU: return 0xAAU;
    case 0xBCU: return 0xA3U;
    case 0xBEU: return 0xBDU;
    case 0xBFU: return 0xAFU;
    default: return c;
    }
}

static bool afo_is_device_stem(const uint8_t *name, size_t stem) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t k, i;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k) {
        const char *word = devices[k];
        for (i = 0U; i < stem && word[i]; ++i)
            if (afo_fold(name[i]) != (uint8_t)word[i]) break;
        if (i == stem && word[i] == 0) return true;
    }
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((afo_fold(name[0]) == 'C' && afo_fold(name[1]) == 'O' &&
             afo_fold(name[2]) == 'M') ||
            (afo_fold(name[0]) == 'L' && afo_fold(name[1]) == 'P' &&
             afo_fold(name[2]) == 'T'));
}

/* Whether a '/'-separated Windows-1251 name may become a path below the
 * output directory: no empty, "." or ".." component (so nothing absolute and
 * nothing that climbs out), no drive colon or other character Windows
 * refuses, no control byte, no component that Windows would silently trim
 * (trailing dot or space) and no device name in any component. */
static bool afo_safe_name(const uint8_t *name, size_t length) {
    size_t start = 0U, index;
    if (length == 0U) return false;
    for (index = 0U; index <= length; ++index) {
        uint8_t c = index < length ? name[index] : (uint8_t)'/';
        if (c == '/') {
            size_t part = index - start, stem = 0U;
            if (part == 0U) return false;
            if (name[index - 1U] == '.' || name[index - 1U] == ' ')
                return false;
            while (stem < part && name[start + stem] != '.') ++stem;
            while (stem > 0U && name[start + stem - 1U] == ' ') --stem;
            if (afo_is_device_stem(name + start, stem)) return false;
            start = index + 1U;
        } else if (c < 0x20U || c == 0x7fU || c == 0x98U || c == ':' ||
                   c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                   c == '|') {
            return false;
        }
    }
    return true;
}

/* Windows-1251 to UTF-8. */
static char *afo_utf8(const uint8_t *name, size_t length) {
    char *out = (char *)xx_mem_alloc(length * 3U + 1U);
    size_t index, used = 0U;
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint32_t code = name[index] < 0x80U
                            ? name[index]
                            : afo_cp1251_high[name[index] - 0x80U];
        if (code < 0x80U) {
            out[used++] = (char)code;
        } else if (code < 0x800U) {
            out[used++] = (char)(0xC0U | (code >> 6U));
            out[used++] = (char)(0x80U | (code & 0x3FU));
        } else {
            out[used++] = (char)(0xE0U | (code >> 12U));
            out[used++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[used++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    out[used] = 0;
    return out;
}

/* --- member table ---------------------------------------------------------- */

typedef struct afo_member_s {
    int64_t offset;       /* FINEAR header, relative to the base */
    uint32_t stream_size; /* whole FINEAR stream */
    uint32_t plain_size;
    uint32_t crc;
    uint32_t suffix;      /* last "_<n>" given to a duplicate of this name */
    bool safe;            /* the name may be used as an output path */
    uint8_t *raw;         /* Windows-1251, '/' separators, NUL-terminated */
    size_t raw_size;
    char *name;           /* UTF-8, final */
} afo_member;

typedef struct afo_stream_s {
    afo_member *items;
    size_t count;
    size_t index;
} afo_stream;

static void afo_members_free(afo_member *items, size_t count) {
    size_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index) {
        if (items[index].raw) xx_mem_free(items[index].raw);
        if (items[index].name) xx_mem_free(items[index].name);
    }
    xx_mem_free(items);
}

static void afo_stream_free(void *opaque) {
    afo_stream *stream = (afo_stream *)opaque;
    if (!stream) return;
    afo_members_free(stream->items, stream->count);
    xx_mem_free(stream);
}

static uint32_t afo_hash(const uint8_t *name, size_t length) {
    uint32_t hash = 2166136261U;
    size_t index;
    for (index = 0U; index < length; ++index) {
        hash ^= afo_fold(name[index]);
        hash *= 16777619U;
    }
    return hash;
}

static bool afo_same(const afo_member *a, const uint8_t *name, size_t length) {
    size_t index;
    if (a->raw_size != length) return false;
    for (index = 0U; index < length; ++index)
        if (afo_fold(a->raw[index]) != afo_fold(name[index])) return false;
    return true;
}

/* "<name>_<number>", the number going in front of the last component's
 * extension. */
static uint8_t *afo_with_suffix(const uint8_t *name, size_t length,
                                size_t number, size_t *out_length) {
    uint8_t digits[24];
    size_t count = 0U, dot = length, index, used;
    uint8_t *out;
    for (index = length; index > 0U; --index) {
        if (name[index - 1U] == '/') break;
        if (name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    if (dot == 0U || name[dot - 1U] == '/') dot = length;
    do {
        digits[count++] = (uint8_t)('0' + number % 10U);
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    out = (uint8_t *)xx_mem_alloc(length + count + 2U);
    if (!out) return NULL;
    xx_rt_memcpy(out, name, dot);
    used = dot;
    out[used++] = '_';
    while (count != 0U) out[used++] = digits[--count];
    xx_rt_memcpy(out + used, name + dot, length - dot);
    used += length - dot;
    out[used] = 0;
    *out_length = used;
    return out;
}

/* Later duplicates (compared as Windows compares names) get "_2", "_3", ...
 * in front of their extension, so no member overwrites another.  Only names
 * that can be extracted take part.  The member that holds a name keeps the
 * last suffix handed out for it, so n copies of one name cost O(n) probes,
 * not O(n^2); a global budget bounds whatever is left. */
static bool afo_unique_names(afo_member *items, size_t count) {
    size_t slots = 16U, index;
    size_t budget = count * 4U + 64U;
    size_t *table;
    while (slots < count * 2U) slots <<= 1U;
    table = (size_t *)xx_mem_alloc(slots * sizeof(*table));
    if (!table) return false;
    for (index = 0U; index < slots; ++index) table[index] = SIZE_MAX;
    for (index = 0U; index < count; ++index) {
        afo_member *member = &items[index];
        uint8_t *stored = member->raw;
        size_t stored_size = member->raw_size, owner = SIZE_MAX, slot;
        if (!member->safe) continue;
        for (;;) {
            bool taken = false;
            size_t number;
            slot = afo_hash(member->raw, member->raw_size) & (slots - 1U);
            while (table[slot] != SIZE_MAX) {
                if (afo_same(&items[table[slot]], member->raw,
                             member->raw_size)) {
                    taken = true;
                    break;
                }
                slot = (slot + 1U) & (slots - 1U);
            }
            if (!taken) break;
            if (owner == SIZE_MAX) owner = table[slot];
            if (budget == 0U || items[owner].suffix >= AFO_RENAME_LIMIT) {
                member->safe = false;
                break;
            }
            --budget;
            if (items[owner].suffix < 1U) items[owner].suffix = 1U;
            number = ++items[owner].suffix;
            {
                size_t next_size = 0U;
                uint8_t *next = afo_with_suffix(stored, stored_size, number,
                                                &next_size);
                if (!next) {
                    if (member->raw != stored) xx_mem_free(member->raw);
                    member->raw = stored;
                    member->raw_size = stored_size;
                    xx_mem_free(table);
                    return false;
                }
                if (member->raw != stored) xx_mem_free(member->raw);
                member->raw = next;
                member->raw_size = next_size;
            }
        }
        if (member->raw != stored) {
            /* A member that found no free name keeps its stored one. */
            if (member->safe) {
                xx_mem_free(stored);
            } else {
                xx_mem_free(member->raw);
                member->raw = stored;
                member->raw_size = stored_size;
            }
        }
        if (member->safe) table[slot] = index;
    }
    xx_mem_free(table);
    return true;
}

/* Reads the member table the scan has accepted. */
static bool afo_load(Abstractformat *format, const afo_info *info,
                     afo_member *items, xx_pd_struct *pd) {
    afo_cursor cursor;
    uint8_t text[256];
    int64_t size = xx_io_total_size(format->device) - format->base_address;
    int64_t stream_at = info->streams_at;
    uint32_t index;
    afo_cursor_init(&cursor, format->device, format->base_address, size,
                    info->names_at);
    for (index = 0U; index < info->count; ++index) {
        afo_member *member = &items[index];
        size_t length = 0U, byte;
        if ((index & 0xffU) == 0U && afo_stopped(pd)) return false;
        if (!afo_cursor_pstring(&cursor, text, &length) || length == 0U)
            return false;
        member->raw = (uint8_t *)xx_mem_alloc(length + 1U);
        if (!member->raw) return false;
        for (byte = 0U; byte < length; ++byte)
            member->raw[byte] = text[byte] == '\\' ? (uint8_t)'/' : text[byte];
        member->raw[length] = 0U;
        member->raw_size = length;
        member->safe = afo_safe_name(member->raw, length);
    }
    cursor.position = info->sizes_at;
    for (index = 0U; index < info->count; ++index) {
        afo_member *member = &items[index];
        uint8_t header[AFO_FINEAR_SIZE];
        if (!afo_cursor_u32(&cursor, &member->stream_size) ||
            !afo_read_at(format->device, format->base_address + stream_at,
                         header, sizeof(header)) ||
            !afo_finear_ok(header, member->stream_size, &member->plain_size,
                           &member->crc))
            return false;
        member->offset = stream_at;
        stream_at += (int64_t)member->stream_size;
    }
    if (stream_at != info->trailer_at ||
        !afo_unique_names(items, info->count))
        return false;
    for (index = 0U; index < info->count; ++index) {
        items[index].name = afo_utf8(items[index].raw, items[index].raw_size);
        if (!items[index].name) return false;
    }
    return true;
}

/* --- decoding -------------------------------------------------------------- */

/* Decodes one member into a new buffer.  Fails closed: a body that does not
 * reproduce the stored size and CRC-16/ARC is not returned at all. */
static bool afo_decode(Abstractformat *format, const afo_member *member,
                       uint8_t **plain_out, xx_pd_struct *pd) {
    uint8_t header[AFO_FINEAR_SIZE];
    uint32_t plain = 0U, crc = 0U;
    size_t body, written = 0U;
    uint8_t *packed = NULL, *output = NULL;
    int64_t at = format->base_address + member->offset;
    if (afo_stopped(pd) || !afo_read_at(format->device, at, header,
                                        sizeof(header)) ||
        !afo_finear_ok(header, member->stream_size, &plain, &crc) ||
        plain != member->plain_size || crc != member->crc)
        return false;
    body = (size_t)member->stream_size - AFO_FINEAR_SIZE;
    packed = (uint8_t *)xx_mem_alloc(body != 0U ? body : 1U);
    output = (uint8_t *)xx_mem_alloc(plain != 0U ? (size_t)plain : 1U);
    if (!packed || !output ||
        !afo_read_at(format->device, at + (int64_t)AFO_FINEAR_SIZE, packed,
                     body) ||
        !xx_lzh1_decode_memory(packed, body, output, (size_t)plain,
                               &written) ||
        written != (size_t)plain ||
        (uint32_t)xx_crc16(XX_CRC_TYPE_CRC16_ARC, output, written) != crc)
        goto fail;
    xx_mem_free(packed);
    *plain_out = output;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

static bool afo_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* --- records --------------------------------------------------------------- */

static bool afo_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *afo_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool afo_set_record(Abstractformat *format, xx_archive_record *record,
                           const afo_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->offset;
    record->header_size = (int64_t)AFO_FINEAR_SIZE;
    record->data_offset =
        format->base_address + member->offset + (int64_t)AFO_FINEAR_SIZE;
    record->compressed_size =
        (int64_t)member->stream_size - (int64_t)AFO_FINEAR_SIZE;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->plain_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          AFO_METHOD_LH1) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* --- lifecycle ------------------------------------------------------------- */

void xx_sfx_abbyy_fine_objects_init(xx_sfx_abbyy_fine_objects *archive,
                                    xx_io_device *device,
                                    int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_ABBYY_FINE_OBJECTS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_abbyy_fine_objects_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_abbyy_fine_objects_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_abbyy_fine_objects_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_abbyy_fine_objects_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_abbyy_fine_objects_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_abbyy_fine_objects_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_abbyy_fine_objects_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_abbyy_fine_objects_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_abbyy_fine_objects_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->streams_offset = -1;
    archive->trailer_offset = -1;
}

xx_sfx_abbyy_fine_objects *xx_sfx_abbyy_fine_objects_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_abbyy_fine_objects *archive =
        (xx_sfx_abbyy_fine_objects *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_abbyy_fine_objects_init(archive, device, base_address);
    return archive;
}

void xx_sfx_abbyy_fine_objects_destroy(xx_sfx_abbyy_fine_objects *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_abbyy_fine_objects_free(xx_sfx_abbyy_fine_objects *archive) {
    if (!archive) return;
    xx_sfx_abbyy_fine_objects_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_abbyy_fine_objects_check_is_valid(Abstractformat *format,
                                              xx_pd_struct *pd) {
    afo_info info;
    return afo_scan(format, &info, pd);
}

bool xx_sfx_abbyy_fine_objects_handle_base_info(Abstractformat *format,
                                                xx_pd_struct *pd) {
    afo_info info;
    xx_sfx_abbyy_fine_objects *archive;
    if (!format || !afo_scan(format, &info, pd)) return false;
    archive = (xx_sfx_abbyy_fine_objects *)format;
    archive->number_of_records = info.count;
    archive->overlay_offset = info.overlay;
    archive->streams_offset = info.streams_at;
    archive->trailer_offset = info.trailer_at;
    archive->class_count = info.classes;
    format->number_of_archive_records = info.count;
    format->format_size = info.trailer_at + (int64_t)AFO_TRAILER_SIZE;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_abbyy_fine_objects_get_format_size(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_abbyy_fine_objects_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_abbyy_fine_objects_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_abbyy_fine_objects_handle_base_info(format, pd))
               ? ((xx_sfx_abbyy_fine_objects *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_abbyy_fine_objects_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    afo_info info;
    afo_stream *stream;
    xx_archive_record_state *state;
    if (!afo_scan(format, &info, pd)) return NULL;
    stream = (afo_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->items =
        (afo_member *)xx_mem_calloc(info.count, sizeof(*stream->items));
    stream->count = info.count;
    if (!stream->items || !afo_load(format, &info, stream->items, pd)) {
        afo_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        afo_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = afo_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!afo_copy_options(&state->options, options) ||
        !afo_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_abbyy_fine_objects_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_abbyy_fine_objects_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    afo_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (afo_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    if (!afo_set_record(format, &state->current_record,
                        &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sfx_abbyy_fine_objects_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    afo_stream *stream;
    const afo_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (afo_stream *)state->internal_state) ||
        stream->index >= stream->count || afo_stopped(pd))
        return false;
    member = &stream->items[stream->index];
    path_option = afo_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (!afo_decode(format, member, &plain, pd)) return false;
        xx_mem_free(plain);
        return true;
    }
    if (!member->safe || !afo_safe_name(member->raw, member->raw_size))
        return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base || !afo_decode(format, member, &plain, pd)) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = afo_write_all(destination, plain, member->plain_size);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    if (plain) xx_mem_free(plain);
    return result;
}

void xx_sfx_abbyy_fine_objects_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
