/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KRZIP setups (Kryloff Technologies).  xx_sfx_krzip.h carries the payload
 * layout.
 *
 * Written from the file structure.  The marker test and the record walk
 * follow what U3's "SFX KRZIP" handler checks (a name length of 1..0x400,
 * signed sizes, records until fewer than five bytes remain, then a u32
 * check); the keystream is Delphi's Random(256) from RandSeed 0, and the
 * two data encodings were identified from the files themselves.
 *
 * The stub executable is parsed only as far as its section table; nothing
 * in it is executed.  The keystream is positional, so the state before any
 * byte of the stream is reached by composing the generator's affine step
 * (a logarithmic number of multiplications) instead of running it over the
 * skipped data.  A member is decoded in memory, checked against its CRC-32
 * and only then written.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_krzip/xx_sfx_krzip.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef SFX_KRZIP
#define XX_SFX_KRZIP_FILE_TYPE XX_FILE_TYPE_SFX_KRZIP
#else
#define XX_SFX_KRZIP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- limits -------------------------------------------------------------- */

#define KRZ_MARKER "<KRZIP FILE BEGINS HERE>"
#define KRZ_MARKER_SIZE 24U
#define KRZ_CHECK_SIZE 4U
#define KRZ_FIELDS_SIZE 20U
#define KRZ_MAX_NAME 0x400U
#define KRZ_MIN_FILE 0x80
#define KRZ_MAX_LFANEW INT64_C(0x10000000)
#define KRZ_MAX_SECTIONS 96U
/* The known setups hold 39 and 135 members with short names; the caps only
 * keep a hostile stream from driving the walk or the name allocations. */
#define KRZ_MAX_RECORDS 65536U
#define KRZ_MAX_NAME_TOTAL (16U * 1024U * 1024U)
#define KRZ_MAX_PACKED UINT32_C(0x10000000)
#define KRZ_MAX_RAW UINT32_C(0x10000000)
/* Neither coder turns one input byte into more than 1032 output bytes
 * (Deflate's 258-byte match in two bits; DCL does far less). */
#define KRZ_RATIO UINT64_C(1032)
#define KRZ_RATIO_SLACK UINT64_C(1032)
/* zlib encoding: u8 1, u32 inflated size, u32 stream size, u32 check. */
#define KRZ_ZLIB_HEAD 13U
#define KRZ_ZLIB_TAG 1U
/* The inflated data: length prefix, member data, five-byte trailer. */
#define KRZ_PREFIX_MAX 5U
#define KRZ_ZLIB_TRAILER 5U
#define KRZ_RENAME_LIMIT 100000U
#define KRZ_LCG_MUL UINT32_C(0x08088405)
#define KRZ_LCG_ADD UINT32_C(1)

/* --- small helpers --------------------------------------------------------- */

static uint32_t krz_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint16_t krz_le16(const uint8_t *bytes) {
    return (uint16_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U));
}

static bool krz_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool krz_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

/* --- keystream ------------------------------------------------------------- */

/* One step per byte: state = state * 0x08088405 + 1, key = state >> 24. */
static void krz_unmask(uint8_t *bytes, size_t size, uint32_t *state) {
    uint32_t value = *state;
    size_t index;
    for (index = 0U; index < size; ++index) {
        value = value * KRZ_LCG_MUL + KRZ_LCG_ADD;
        bytes[index] ^= (uint8_t)(value >> 24U);
    }
    *state = value;
}

/* The state after @p steps more bytes.  The step is x -> m*x + c; applying
 * it 2^k times is again affine, so squaring the map walks the bits of the
 * count (all arithmetic modulo 2^32). */
static uint32_t krz_advance(uint32_t state, uint64_t steps) {
    uint32_t mul = KRZ_LCG_MUL, add = KRZ_LCG_ADD;
    while (steps != 0U) {
        if (steps & 1U) state = state * mul + add;
        add = add * mul + add;
        mul = mul * mul;
        steps >>= 1U;
    }
    return state;
}

/* --- text ------------------------------------------------------------------ */

/* Windows-1252 0x80..0x9F.  The five undefined bytes keep their C1 code
 * point, which the name check refuses. */
static const uint16_t krz_cp1252_high[32] = {
    0x20ACU, 0x0081U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x008DU, 0x017DU, 0x008FU,
    0x0090U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x009DU, 0x017EU, 0x0178U};

static size_t krz_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

/* A stored name to UTF-8 with '/' separators.  The walk has checked that it
 * holds no byte below 0x20. */
static char *krz_name(const uint8_t *bytes, size_t length) {
    char *out = (char *)xx_mem_alloc(length * 3U + 1U);
    size_t index, used = 0U;
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = bytes[index];
        if (c == '\\' || c == '/')
            out[used++] = '/';
        else
            used += krz_put_utf8(out + used,
                                 (c >= 0x80U && c < 0xA0U)
                                     ? krz_cp1252_high[c - 0x80U]
                                     : (uint32_t)c);
    }
    out[used] = 0;
    return out;
}

/* --- output names ---------------------------------------------------------- */

static char krz_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool krz_is_device_stem(const char *name, size_t stem) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t k, i;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k) {
        const char *word = devices[k];
        for (i = 0U; i < stem && word[i]; ++i)
            if (krz_upper(name[i]) != word[i]) break;
        if (i == stem && word[i] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((krz_upper(name[0]) == 'C' && krz_upper(name[1]) == 'O' &&
          krz_upper(name[2]) == 'M') ||
         (krz_upper(name[0]) == 'L' && krz_upper(name[1]) == 'P' &&
          krz_upper(name[2]) == 'T')))
        return true;
    return false;
}

/* Whether a '/'-separated UTF-8 name may become a path below the output
 * directory: no empty, "." or ".." component (so nothing absolute and
 * nothing that climbs out), no drive colon or other character Windows
 * refuses, no C0/C1 control, no component that Windows would silently trim
 * (trailing dot or space), and no device name in any component. */
static bool krz_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        unsigned char c = (unsigned char)name[index];
        if (c == '/' || c == 0U) {
            size_t length = index - start, stem = 0U;
            if (length == 0U) return false;
            if (name[index - 1U] == '.' || name[index - 1U] == ' ')
                return false;
            while (stem < length && name[start + stem] != '.') ++stem;
            while (stem > 0U && name[start + stem - 1U] == ' ') --stem;
            if (krz_is_device_stem(name + start, stem)) return false;
            if (c == 0U) break;
            start = index + 1U;
        } else if (c < 0x20U || c == 0x7fU || c == '\\' || c == ':' ||
                   c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                   c == '|') {
            return false;
        } else if (c == 0xC2U && (unsigned char)name[index + 1U] >= 0x80U &&
                   (unsigned char)name[index + 1U] <= 0x9FU) {
            return false; /* U+0080..U+009F */
        }
        ++index;
    }
    return true;
}

/* Case folding as NTFS does it for the characters a Windows-1252 name can
 * produce: ASCII, Latin-1 and the Windows-1252 letter pairs.  Every mapping
 * keeps the UTF-8 length, so names fold in place. */
static void krz_fold(const char *name, char *out) {
    const unsigned char *in = (const unsigned char *)name;
    unsigned char *folded = (unsigned char *)out;
    size_t index = 0U;
    while (in[index]) {
        unsigned char c = in[index];
        unsigned char d = in[index + 1U];
        if (c == 0xC3U && d != 0U) {
            folded[index] = c;
            folded[index + 1U] = d;
            if (d >= 0xA0U && d <= 0xBEU && d != 0xB7U) {
                folded[index + 1U] = (unsigned char)(d - 0x20U);
            } else if (d == 0xBFU) { /* y-diaeresis to U+0178 */
                folded[index] = 0xC5U;
                folded[index + 1U] = 0xB8U;
            }
            index += 2U;
        } else if (c == 0xC5U && d != 0U) {
            folded[index] = c;
            folded[index + 1U] = (d == 0xA1U || d == 0x93U || d == 0xBEU)
                                     ? (unsigned char)(d - 1U) : d;
            index += 2U;
        } else {
            folded[index] = (c >= 'a' && c <= 'z') ? (unsigned char)(c - 0x20U)
                                                   : c;
            ++index;
        }
    }
    folded[index] = 0U;
}

static uint32_t krz_hash(const char *folded) {
    uint32_t hash = 2166136261U;
    for (; *folded; ++folded) {
        hash ^= (uint8_t)*folded;
        hash *= 16777619U;
    }
    return hash;
}

/* "<name>_<number>", the number going in front of the last component's
 * extension. */
static char *krz_with_suffix(const char *name, size_t number) {
    char digits[24];
    size_t count = 0U, length = xx_str_len(name), dot = length, index, used;
    char *out;
    for (index = length; index > 0U; --index) {
        if (name[index - 1U] == '/') break;
        if (name[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    if (dot == 0U || name[dot - 1U] == '/') dot = length;
    do {
        digits[count++] = (char)('0' + number % 10U);
        number /= 10U;
    } while (number != 0U && count < sizeof(digits));
    out = (char *)xx_mem_alloc(length + count + 2U);
    if (!out) return NULL;
    xx_rt_memcpy(out, name, dot);
    used = dot;
    out[used++] = '_';
    while (count != 0U) out[used++] = digits[--count];
    xx_rt_memcpy(out + used, name + dot, length - dot + 1U);
    return out;
}

/* --- member table ---------------------------------------------------------- */

typedef struct krz_member_s {
    int64_t header_offset; /**< Relative to the base. */
    int64_t data_offset;   /**< Relative to the base. */
    uint32_t header_size;
    uint32_t data_state;   /**< Keystream state before the first data byte. */
    uint32_t packed_size;
    uint32_t raw_size;
    uint32_t attributes;
    uint32_t stored_check; /**< ~CRC-32 of the data, as stored. */
    uint16_t dos_time;
    uint16_t dos_date;
    uint32_t method;
    bool safe;             /**< The name may be used as an output path. */
    char *name;            /**< UTF-8, '/' separators, unique if safe. */
    char *folded;          /**< Case-folded name, only while deduplicating. */
} krz_member;

typedef struct krz_info_s {
    int64_t overlay;     /**< The marker, relative to the base. */
    int64_t stream_at;   /**< First record, relative to the base. */
    int64_t stream_size; /**< Record bytes, the final check excluded. */
    uint32_t check;
    uint32_t count;
} krz_info;

typedef struct krz_stream_s {
    krz_member *items;
    size_t count;
    size_t index;
} krz_stream;

static void krz_members_free(krz_member *items, size_t count) {
    size_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index) {
        if (items[index].name) xx_mem_free(items[index].name);
        if (items[index].folded) xx_mem_free(items[index].folded);
    }
    xx_mem_free(items);
}

static void krz_stream_free(void *opaque) {
    krz_stream *stream = (krz_stream *)opaque;
    if (!stream) return;
    krz_members_free(stream->items, stream->count);
    xx_mem_free(stream);
}

/* Walks the record stream.  Every field is checked against the bytes that
 * are present before it is used: a record must end inside the stream, and
 * the walk must end exactly where the final check begins.  With @p items
 * (room for @p capacity members) the members are also recorded, their
 * names converted but not yet deduplicated. */
static bool krz_walk(xx_io_device *device, int64_t base, const krz_info *info,
                     krz_member *items, uint32_t capacity, uint32_t *count,
                     xx_pd_struct *pd) {
    uint8_t record[2U + KRZ_MAX_NAME + KRZ_FIELDS_SIZE];
    const int64_t size = info->stream_size;
    int64_t position = 0;
    uint64_t name_total = 0U;
    uint32_t state = 0U, index = 0U;
    while (position < size) {
        uint32_t name_size, need, byte, packed, raw;
        const uint8_t *fields;
        int64_t data;
        if (index >= KRZ_MAX_RECORDS || (items && index >= capacity))
            return false;
        if ((index & 0xFFU) == 0U && krz_stopped(pd)) return false;
        if (size - position < (int64_t)(2U + 1U + KRZ_FIELDS_SIZE) ||
            !krz_read_at(device, base + info->stream_at + position, record,
                         2U))
            return false;
        krz_unmask(record, 2U, &state);
        name_size = krz_le16(record);
        if (name_size == 0U || name_size > KRZ_MAX_NAME) return false;
        name_total += name_size;
        if (name_total > KRZ_MAX_NAME_TOTAL) return false;
        need = name_size + KRZ_FIELDS_SIZE;
        if ((int64_t)need > size - position - 2 ||
            !krz_read_at(device, base + info->stream_at + position + 2,
                         record + 2U, need))
            return false;
        krz_unmask(record + 2U, need, &state);
        for (byte = 0U; byte < name_size; ++byte)
            if (record[2U + byte] < 0x20U || record[2U + byte] == 0x7fU)
                return false;
        fields = record + 2U + name_size;
        raw = krz_le32(fields + 4U);
        packed = krz_le32(fields + 12U);
        /* The stub reads both sizes as signed. */
        if (raw > (uint32_t)INT32_MAX || packed > (uint32_t)INT32_MAX)
            return false;
        data = position + 2 + (int64_t)need;
        if ((int64_t)packed > size - data) return false;
        if ((uint64_t)raw > (uint64_t)packed * KRZ_RATIO + KRZ_RATIO_SLACK)
            return false;
        if (items) {
            krz_member *member = &items[index];
            member->header_offset = info->stream_at + position;
            member->header_size = 2U + need;
            member->data_offset = info->stream_at + data;
            member->data_state = state;
            member->dos_time = krz_le16(fields);
            member->dos_date = krz_le16(fields + 2U);
            member->raw_size = raw;
            member->attributes = krz_le32(fields + 8U);
            member->packed_size = packed;
            member->stored_check = krz_le32(fields + 16U);
            member->name = krz_name(record + 2U, name_size);
            if (!member->name) return false;
            member->safe = krz_safe_output_name(member->name);
        }
        state = krz_advance(state, packed);
        position = data + (int64_t)packed;
        ++index;
    }
    if (index == 0U || position != size) return false;
    *count = index;
    return true;
}

/* The overlay: where the last section's raw data ends.  A section that
 * claims bytes beyond the file cannot belong to a complete package. */
static bool krz_overlay(xx_io_device *device, int64_t base, int64_t size,
                        int64_t *overlay) {
    uint8_t dos[0x40], nt[24], table[KRZ_MAX_SECTIONS * 40U];
    uint32_t sections, optional, index;
    int64_t lfanew, table_at, end = 0;
    if (size < KRZ_MIN_FILE || !krz_read_at(device, base, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    lfanew = (int64_t)krz_le32(dos + 0x3c);
    if (lfanew < 4 || lfanew > KRZ_MAX_LFANEW || lfanew > size - 24 ||
        !krz_read_at(device, base + lfanew, nt, sizeof(nt)) || nt[0] != 'P' ||
        nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    sections = krz_le16(nt + 6);
    optional = krz_le16(nt + 20);
    table_at = lfanew + 24 + (int64_t)optional;
    if (sections == 0U || sections > KRZ_MAX_SECTIONS ||
        table_at > size - (int64_t)sections * 40 ||
        !krz_read_at(device, base + table_at, table, (size_t)sections * 40U))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * 40U;
        int64_t raw_size = (int64_t)krz_le32(row + 16);
        int64_t raw_offset = (int64_t)krz_le32(row + 20);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    if (end <= table_at) return false;
    *overlay = end;
    return true;
}

/* Finds the marker at the overlay and checks the whole record chain. */
static bool krz_scan(Abstractformat *format, krz_info *info,
                     xx_pd_struct *pd) {
    uint8_t marker[KRZ_MARKER_SIZE], check[KRZ_CHECK_SIZE];
    int64_t total, size, rest;
    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_mem_zero(info, sizeof(*info));
    if (!krz_overlay(format->device, format->base_address, size,
                     &info->overlay))
        return false;
    rest = size - info->overlay;
    /* Marker, the smallest record (a one-byte name and its fields) and the
     * final check. */
    if (rest < (int64_t)(KRZ_MARKER_SIZE + 2U + 1U + KRZ_FIELDS_SIZE +
                         KRZ_CHECK_SIZE) ||
        !krz_read_at(format->device, format->base_address + info->overlay,
                     marker, sizeof(marker)) ||
        xx_rt_memcmp(marker, KRZ_MARKER, KRZ_MARKER_SIZE) != 0)
        return false;
    info->stream_at = info->overlay + (int64_t)KRZ_MARKER_SIZE;
    info->stream_size = rest - (int64_t)KRZ_MARKER_SIZE - (int64_t)KRZ_CHECK_SIZE;
    if (!krz_walk(format->device, format->base_address, info, NULL, 0U,
                  &info->count, pd) ||
        !krz_read_at(format->device,
                     format->base_address + info->stream_at +
                         info->stream_size,
                     check, sizeof(check)))
        return false;
    info->check = krz_le32(check);
    return true;
}

/* Folds @p member's current name and looks it up; *slot receives the slot
 * holding the same name, or the free slot where it would go. */
static bool krz_name_taken(krz_member *items, const size_t *table,
                           size_t slots, krz_member *member, size_t *slot,
                           bool *taken) {
    member->folded = (char *)xx_mem_alloc(xx_str_len(member->name) + 1U);
    if (!member->folded) return false;
    krz_fold(member->name, member->folded);
    *taken = false;
    *slot = krz_hash(member->folded) & (slots - 1U);
    while (table[*slot] != SIZE_MAX) {
        if (xx_str_cmp(items[table[*slot]].folded, member->folded) == 0) {
            *taken = true;
            break;
        }
        *slot = (*slot + 1U) & (slots - 1U);
    }
    return true;
}

/* Later duplicates (compared as Windows compares names) get "_2", "_3", ...
 * in front of their extension, so no member overwrites another.  Only
 * names that can be extracted take part. */
static bool krz_unique_names(krz_member *items, size_t count) {
    size_t slots = 16U, index;
    size_t *table;
    bool result = false;
    while (slots < count * 2U) slots <<= 1U;
    table = (size_t *)xx_mem_alloc(slots * sizeof(*table));
    if (!table) return false;
    for (index = 0U; index < slots; ++index) table[index] = SIZE_MAX;
    for (index = 0U; index < count; ++index) {
        krz_member *member = &items[index];
        char *stored = NULL;
        size_t slot = 0U, number = 1U;
        bool taken = false;
        if (!member->safe) continue;
        for (;;) {
            if (!krz_name_taken(items, table, slots, member, &slot, &taken))
                break;
            if (!taken) break;
            xx_mem_free(member->folded);
            member->folded = NULL;
            /* At most count names are taken, so one of the next count
             * suffixes is free; the limit only guards the arithmetic. */
            if (++number > count + 1U || number > KRZ_RENAME_LIMIT) break;
            /* Every suffix derives from the stored name. */
            if (!stored) {
                stored = member->name;
            } else {
                xx_mem_free(member->name);
            }
            member->name = krz_with_suffix(stored, number);
            if (!member->name) {
                member->name = stored;
                stored = NULL;
                break;
            }
        }
        if (stored) xx_mem_free(stored);
        if (!member->folded || taken) goto done;
        table[slot] = index;
    }
    result = true;
done:
    for (index = 0U; index < count; ++index) {
        if (items[index].folded) {
            xx_mem_free(items[index].folded);
            items[index].folded = NULL;
        }
    }
    xx_mem_free(table);
    return result;
}

/* --- decoding -------------------------------------------------------------- */

/* The zlib encoding carries its own sizes; a DCL stream begins with a
 * literal-mode byte of 0 or 1, so the tag byte, the 'x' of the zlib header
 * and a stream size that exactly fills the member together keep a DCL
 * stream from being taken for one. */
static uint32_t krz_method(const uint8_t *head, size_t available,
                           uint32_t packed) {
    if (available >= KRZ_ZLIB_HEAD + 2U && head[0] == KRZ_ZLIB_TAG &&
        head[KRZ_ZLIB_HEAD] == 0x78U &&
        (uint64_t)krz_le32(head + 5U) + KRZ_ZLIB_HEAD == (uint64_t)packed)
        return XX_SFX_KRZIP_METHOD_ZLIB;
    return XX_SFX_KRZIP_METHOD_DCL;
}

/* Reads the first data bytes of @p member to learn its encoding. */
static bool krz_probe_method(xx_io_device *device, int64_t base,
                             krz_member *member) {
    uint8_t head[KRZ_ZLIB_HEAD + 2U];
    size_t available = member->packed_size < sizeof(head)
                           ? (size_t)member->packed_size : sizeof(head);
    uint32_t state = member->data_state;
    member->method = XX_SFX_KRZIP_METHOD_DCL;
    if (available == 0U) return true;
    if (!krz_read_at(device, base + member->data_offset, head, available))
        return false;
    krz_unmask(head, available, &state);
    member->method = krz_method(head, available, member->packed_size);
    return true;
}

/* zlib encoding: inflate to the declared size, then strip the length
 * prefix and the trailer.  *plain points into @p *output. */
static bool krz_decode_zlib(const uint8_t *packed, uint32_t packed_size,
                            uint32_t raw_size, uint64_t budget,
                            uint8_t **output, const uint8_t **plain) {
    uint32_t inflated = krz_le32(packed + 1U);
    uint32_t stream = krz_le32(packed + 5U);
    uint32_t stream_check = krz_le32(packed + 9U);
    uint32_t prefix, length = 0U, index;
    size_t written = 0U;
    uint8_t *buffer;
    if (packed_size < KRZ_ZLIB_HEAD + 2U ||
        (uint64_t)stream + KRZ_ZLIB_HEAD != (uint64_t)packed_size ||
        inflated > KRZ_MAX_RAW ||
        (uint64_t)inflated < (uint64_t)raw_size + 2U ||
        (uint64_t)inflated > (uint64_t)raw_size + KRZ_PREFIX_MAX +
                                 KRZ_ZLIB_TRAILER ||
        (uint64_t)inflated > budget)
        return false;
    /* The encoder's own check of the stream it wrote. */
    if ((xx_crc32(XX_CRC_TYPE_CRC32, packed + KRZ_ZLIB_HEAD, stream) ^
         UINT32_C(0xFFFFFFFF)) != stream_check)
        return false;
    buffer = (uint8_t *)xx_mem_alloc(inflated);
    if (!buffer) return false;
    if (!xx_zlib_stream_decode_memory(packed + KRZ_ZLIB_HEAD, stream, buffer,
                                      inflated, &written) ||
        written != (size_t)inflated || buffer[0] < 0x20U ||
        buffer[0] > 0x23U) {
        xx_mem_free(buffer);
        return false;
    }
    prefix = (uint32_t)buffer[0] - 0x1FU;
    if (1U + prefix > inflated) {
        xx_mem_free(buffer);
        return false;
    }
    for (index = prefix; index > 0U; --index)
        length = (length << 8U) | buffer[index];
    if (length != raw_size || (uint64_t)1U + prefix + length > inflated) {
        xx_mem_free(buffer);
        return false;
    }
    *output = buffer;
    *plain = buffer + 1U + prefix;
    return true;
}

/* DCL encoding: the stream decodes to exactly the unpacked size. */
static bool krz_decode_dcl(const uint8_t *packed, uint32_t packed_size,
                           uint32_t raw_size, uint8_t **output,
                           const uint8_t **plain) {
    uint8_t *buffer;
    size_t written = 0U;
    if (raw_size == 0U) {
        size_t consumed = 0U, produced = 0U;
        /* An empty member is a stream that holds only the end code; the
         * decoder refuses a zero-sized output, the scan does not. */
        if (!xx_dcl_scan_memory(packed, packed_size, 1U, &consumed,
                                &produced) ||
            produced != 0U)
            return false;
        buffer = (uint8_t *)xx_mem_alloc(1U);
        if (!buffer) return false;
        *output = buffer;
        *plain = buffer;
        return true;
    }
    buffer = (uint8_t *)xx_mem_alloc(raw_size);
    if (!buffer) return false;
    if (!xx_dcl_decode_memory(packed, packed_size, buffer, raw_size,
                              &written) ||
        written != (size_t)raw_size) {
        xx_mem_free(buffer);
        return false;
    }
    *output = buffer;
    *plain = buffer;
    return true;
}

/* Decodes @p member into memory and checks it.  On success *output is the
 * allocation to free and *plain the member's raw_size bytes inside it. */
static bool krz_decode(Abstractformat *format, const krz_member *member,
                       uint64_t max_member, uint64_t memory_limit,
                       uint8_t **output, const uint8_t **plain,
                       xx_pd_struct *pd) {
    uint8_t *packed;
    uint32_t state = member->data_state;
    uint64_t budget;
    bool decoded;
    *output = NULL;
    *plain = NULL;
    if ((uint64_t)member->raw_size > max_member ||
        member->packed_size > KRZ_MAX_PACKED || member->raw_size > KRZ_MAX_RAW ||
        (uint64_t)member->packed_size > memory_limit || krz_stopped(pd))
        return false;
    budget = memory_limit - member->packed_size;
    if ((uint64_t)member->raw_size > budget) return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0U
                                         ? member->packed_size : 1U);
    if (!packed) return false;
    if (!krz_read_at(format->device,
                     format->base_address + member->data_offset, packed,
                     member->packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    krz_unmask(packed, member->packed_size, &state);
    if (member->method == XX_SFX_KRZIP_METHOD_ZLIB)
        decoded = krz_decode_zlib(packed, member->packed_size,
                                  member->raw_size, budget, output, plain);
    else
        decoded = member->packed_size >= 3U &&
                  krz_decode_dcl(packed, member->packed_size,
                                 member->raw_size, output, plain);
    xx_mem_free(packed);
    if (!decoded) return false;
    if ((xx_crc32(XX_CRC_TYPE_CRC32, *plain, member->raw_size) ^
         UINT32_C(0xFFFFFFFF)) != member->stored_check) {
        xx_mem_free(*output);
        *output = NULL;
        *plain = NULL;
        return false;
    }
    return true;
}

static bool krz_write_all(xx_io_device *destination, const uint8_t *bytes,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* --- records --------------------------------------------------------------- */

static bool krz_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool krz_set_record(Abstractformat *format, xx_archive_record *record,
                           const krz_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->raw_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_CRC32,
               member->stored_check ^ UINT32_C(0xFFFFFFFF)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* --- lifecycle ------------------------------------------------------------- */

void xx_sfx_krzip_init(xx_sfx_krzip *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_KRZIP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_krzip_check_is_valid;
    archive->format.handle_base_info = xx_sfx_krzip_handle_base_info;
    archive->format.get_format_size = xx_sfx_krzip_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_krzip_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_krzip_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_krzip_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_krzip_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_krzip_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_krzip_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

xx_sfx_krzip *xx_sfx_krzip_create(xx_io_device *device, int64_t base_address) {
    xx_sfx_krzip *archive = (xx_sfx_krzip *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_krzip_init(archive, device, base_address);
    return archive;
}

void xx_sfx_krzip_destroy(xx_sfx_krzip *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_krzip_free(xx_sfx_krzip *archive) {
    if (!archive) return;
    xx_sfx_krzip_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_krzip_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    krz_info info;
    return krz_scan(format, &info, pd);
}

bool xx_sfx_krzip_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    krz_info info;
    xx_sfx_krzip *archive;
    if (!format || !krz_scan(format, &info, pd)) return false;
    archive = (xx_sfx_krzip *)format;
    archive->number_of_records = info.count;
    archive->overlay_offset = info.overlay;
    archive->stream_offset = info.stream_at;
    archive->stream_size = info.stream_size;
    archive->stream_check = info.check;
    format->number_of_archive_records = info.count;
    format->format_size =
        info.stream_at + info.stream_size + (int64_t)KRZ_CHECK_SIZE;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_krzip_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_krzip_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_krzip_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_krzip_handle_base_info(format, pd))
               ? ((xx_sfx_krzip *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_sfx_krzip_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    krz_info info;
    krz_stream *stream;
    xx_archive_record_state *state;
    uint32_t count = 0U;
    size_t index;
    if (!krz_scan(format, &info, pd)) return NULL;
    stream = (krz_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->items = (krz_member *)xx_mem_calloc(info.count,
                                                sizeof(*stream->items));
    stream->count = info.count;
    if (!stream->items ||
        !krz_walk(format->device, format->base_address, &info, stream->items,
                  info.count, &count, pd) ||
        count != info.count ||
        !krz_unique_names(stream->items, stream->count)) {
        krz_stream_free(stream);
        return NULL;
    }
    for (index = 0U; index < stream->count; ++index) {
        if (!krz_probe_method(format->device, format->base_address,
                              &stream->items[index])) {
            krz_stream_free(stream);
            return NULL;
        }
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        krz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = krz_stream_free;
    state->total_records = stream->count;
    if (!krz_copy_options(&state->options, options) ||
        !krz_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_krzip_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_krzip_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    krz_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (krz_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    if (!krz_set_record(format, &state->current_record,
                        &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sfx_krzip_unpack_current_archive_record(Abstractformat *format,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    krz_stream *stream;
    const krz_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *output = NULL;
    const uint8_t *plain = NULL;
    uint64_t max_member = UINT64_MAX, memory_limit = UINT64_MAX;
    size_t base_length;
    xx_io_device *destination;
    bool overwrite, created = false, result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (krz_stream *)state->internal_state) ||
        stream->index >= stream->count || krz_stopped(pd))
        return false;
    member = &stream->items[stream->index];

    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option) max_member = xx_var_get_u64(option);
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MEMORY_LIMIT);
    if (option) memory_limit = xx_var_get_u64(option);

    /* The member is decoded and checked before any file is created, so a
     * stream that fails leaves nothing behind. */
    if (!krz_decode(format, member, max_member, memory_limit, &output, &plain,
                    pd))
        return false;

    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: the member was verified, nothing is written. */
        result = true;
        goto done;
    }
    if (!member->safe || !krz_safe_output_name(member->name)) goto done;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' &&
            base[base_length - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    destination = xx_io_file_open(path, overwrite ? "wb" : "wbx");
    if (!destination) goto done;
    /* Only a file this call opened is removed again on failure. */
    created = true;
    result = krz_write_all(destination, plain, member->raw_size);
    if (xx_io_close(destination) != 0) result = false;
    if (!result && created) xx_rt_remove(path);
done:
    if (output) xx_mem_free(output);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_krzip_free_archive_records_reading(Abstractformat *format,
                                               xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
