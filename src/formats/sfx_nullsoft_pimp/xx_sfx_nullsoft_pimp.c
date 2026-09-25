/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Nullsoft PiMP (Plug-in Mini Packager) installers.  xx_sfx_nullsoft_pimp.h
 * carries the payload layout.
 *
 * The payload is found the way XArchive's FT_PIMP_SFX branch finds it
 * (core/xlegacystorearchive.cpp, MIT, Copyright (c) 2026 hors): "PIMPFILE"
 * at the end of the last section's raw data, and a member chain that is
 * accepted only when every record fits and the command block closes it.
 * Where XArchive searches up to 64 KiB for the member count, this reader
 * computes its position from the install-directory flag, which is how the
 * stub and U3 read it, and it also accepts the older layout without the
 * unpacked sizes.  The code is written from the file structure.
 *
 * The stub executable is parsed only as far as its section table; nothing
 * in it is executed.  Each member is inflated through a sink that counts,
 * caps and checksums the output, so a member is extracted only when its
 * size and its Adler-32 trailer both match.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_nullsoft_pimp/xx_sfx_nullsoft_pimp.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef SFX_NULLSOFT_PIMP
#define XX_SFX_NULLSOFT_PIMP_FILE_TYPE XX_FILE_TYPE_SFX_NULLSOFT_PIMP
#else
#define XX_SFX_NULLSOFT_PIMP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- limits -------------------------------------------------------------- */

#define PIMP_MIN_FILE 0x200
#define PIMP_MAX_LFANEW INT64_C(0x10000000)
#define PIMP_MAX_SECTIONS 96U
#define PIMP_SIGNATURE_SIZE 8U
#define PIMP_DIR_FIELD 0x104U
#define PIMP_TEXT_FIELD 0x80U
/* Title, description and the two u32 that may follow them. */
#define PIMP_FIXED_BLOCK (2U * PIMP_TEXT_FIELD + 8U)
/* The stub and U3 both cap the count below 0xFFFF and a name at 0x400. */
#define PIMP_MAX_COUNT 0xFFFEU
#define PIMP_MIN_NAME 2U
#define PIMP_MAX_NAME 0x400U
/* The smallest zlib stream: header, one empty fixed block, Adler-32. */
#define PIMP_MIN_PACKED 8U
#define PIMP_MAX_COMMAND 0x10000U
/* Deflate cannot expand one input byte to more than 1032 output bytes
 * (a 258-byte match coded in two bits); a size beyond that is garbage. */
#define PIMP_DEFLATE_RATIO UINT64_C(1032)
#define PIMP_DEFLATE_SLACK UINT64_C(258)
#define PIMP_METHOD_DEFLATE 8U
#define PIMP_RENAME_LIMIT 100000U

/* --- small helpers --------------------------------------------------------- */

static uint32_t pimp_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t pimp_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool pimp_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool pimp_stopped(xx_pd_struct *pd) {
    return pd && xx_pd_is_stopped(pd);
}

/* RFC 1950 header: Deflate, a window of at most 32 KiB, no preset
 * dictionary, and the check bits. */
static bool pimp_zlib_header_ok(const uint8_t *header) {
    return (header[0] & 0x0fU) == 8U && (header[0] >> 4U) <= 7U &&
           (header[1] & 0x20U) == 0U &&
           (((uint32_t)header[0] << 8U) | header[1]) % 31U == 0U;
}

/* --- text ------------------------------------------------------------------ */

/* Windows-1252 0x80..0x9F.  The five undefined bytes keep their C1 code
 * point, which the name check refuses. */
static const uint16_t pimp_cp1252_high[32] = {
    0x20ACU, 0x0081U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x008DU, 0x017DU, 0x008FU,
    0x0090U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x009DU, 0x017EU, 0x0178U};

static uint32_t pimp_cp1252(uint8_t c) {
    return (c >= 0x80U && c < 0xA0U) ? pimp_cp1252_high[c - 0x80U] : c;
}

/* Appends one code point below 0x10000; returns the bytes written. */
static size_t pimp_put_utf8(char *out, uint32_t code) {
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

/* A NUL-padded text field to UTF-8; control bytes become spaces.  @p out
 * holds at least 3 * @p size + 1 bytes. */
static void pimp_text(const uint8_t *field, size_t size, char *out) {
    size_t index, used = 0U;
    for (index = 0U; index < size && field[index] != 0U; ++index) {
        uint8_t c = field[index];
        used += pimp_put_utf8(out + used, (c < 0x20U || c == 0x7fU)
                                              ? (uint32_t)' '
                                              : pimp_cp1252(c));
    }
    out[used] = 0;
}

/* A stored member name to UTF-8 with '/' separators.  The caller has
 * checked that it holds no byte below 0x20. */
static char *pimp_name(const uint8_t *bytes, size_t length) {
    char *out = (char *)xx_mem_alloc(length * 3U + 1U);
    size_t index, used = 0U;
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = bytes[index];
        if (c == '\\' || c == '/')
            out[used++] = '/';
        else
            used += pimp_put_utf8(out + used, pimp_cp1252(c));
    }
    out[used] = 0;
    return out;
}

/* --- output names ---------------------------------------------------------- */

static char pimp_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool pimp_is_device_stem(const char *name, size_t stem) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t k, i;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k) {
        const char *word = devices[k];
        for (i = 0U; i < stem && word[i]; ++i)
            if (pimp_upper(name[i]) != word[i]) break;
        if (i == stem && word[i] == 0) return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((pimp_upper(name[0]) == 'C' && pimp_upper(name[1]) == 'O' &&
          pimp_upper(name[2]) == 'M') ||
         (pimp_upper(name[0]) == 'L' && pimp_upper(name[1]) == 'P' &&
          pimp_upper(name[2]) == 'T')))
        return true;
    return false;
}

/* Whether a '/'-separated UTF-8 name may become a path below the output
 * directory: no empty, "." or ".." component (so nothing absolute and
 * nothing that climbs out), no drive colon or other character Windows
 * refuses, no C0/C1 control, no component that Windows would silently trim
 * (trailing dot or space), and no device name in any component. */
static bool pimp_safe_output_name(const char *name) {
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
            if (pimp_is_device_stem(name + start, stem)) return false;
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
 * produce: ASCII, Latin-1 and the four Windows-1252 letter pairs.  Every
 * mapping keeps the UTF-8 length, so names fold in place. */
static void pimp_fold(const char *name, char *out) {
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

static uint32_t pimp_hash(const char *folded) {
    uint32_t hash = 2166136261U;
    for (; *folded; ++folded) {
        hash ^= (uint8_t)*folded;
        hash *= 16777619U;
    }
    return hash;
}

/* "<name>_<number>", the number going in front of the last component's
 * extension. */
static char *pimp_with_suffix(const char *name, size_t number) {
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
    xx_mem_copy(out, name, dot);
    used = dot;
    out[used++] = '_';
    while (count != 0U) out[used++] = digits[--count];
    xx_mem_copy(out + used, name + dot, length - dot + 1U);
    return out;
}

/* --- member table ---------------------------------------------------------- */

typedef struct pimp_member_s {
    int64_t header_offset; /**< Relative to the base. */
    int64_t data_offset;
    uint32_t header_size;
    uint32_t packed_size;
    uint32_t raw_size;
    bool has_raw_size;
    bool safe;             /**< The name may be used as an output path. */
    char *name;            /**< UTF-8, '/' separators, unique if safe. */
    char *folded;          /**< Case-folded name, only while deduplicating. */
} pimp_member;

typedef struct pimp_info_s {
    int64_t overlay;
    int64_t members_at;
    int64_t command_at;
    int64_t end;
    uint32_t count;
    uint32_t command_size;
    uint32_t layout;
    bool has_install_dir;
    uint8_t dir_field[PIMP_DIR_FIELD];
    uint8_t text_fields[2U * PIMP_TEXT_FIELD];
} pimp_info;

typedef struct pimp_stream_s {
    pimp_member *items;
    size_t count;
    size_t index;
} pimp_stream;

static void pimp_members_free(pimp_member *items, size_t count) {
    size_t index;
    if (!items) return;
    for (index = 0U; index < count; ++index) {
        if (items[index].name) xx_mem_free(items[index].name);
        if (items[index].folded) xx_mem_free(items[index].folded);
    }
    xx_mem_free(items);
}

static void pimp_stream_free(void *opaque) {
    pimp_stream *stream = (pimp_stream *)opaque;
    if (!stream) return;
    pimp_members_free(stream->items, stream->count);
    xx_mem_free(stream);
}

/* Walks @p count member records from @p at.  Every field is checked against
 * the bytes that are present before it is used, so the walk reads at most
 * two short records per member and never past @p size.  With @p items the
 * members are also recorded (names converted, not yet deduplicated). */
static bool pimp_walk(xx_io_device *device, int64_t base, int64_t size,
                      int64_t at, uint32_t count, bool raw_sizes,
                      pimp_member *items, int64_t *command_at,
                      xx_pd_struct *pd) {
    uint8_t record[PIMP_MAX_NAME + 8U + 2U];
    const uint32_t sizes = raw_sizes ? 8U : 4U;
    int64_t position = at;
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        uint8_t head[4];
        uint32_t name_size, packed, raw = 0U, byte;
        size_t need;
        int64_t data;
        if ((index & 0xFFU) == 0U && pimp_stopped(pd)) return false;
        if (size - position < 4 ||
            !pimp_read_at(device, base + position, head, sizeof(head)))
            return false;
        name_size = pimp_le32(head);
        if (name_size < PIMP_MIN_NAME || name_size > PIMP_MAX_NAME)
            return false;
        need = (size_t)name_size + sizes + 2U;
        if ((int64_t)need > size - position - 4 ||
            !pimp_read_at(device, base + position + 4, record, need))
            return false;
        if (record[name_size - 1U] != 0U) return false;
        for (byte = 0U; byte + 1U < name_size; ++byte)
            if (record[byte] < 0x20U || record[byte] == 0x7fU) return false;
        packed = pimp_le32(record + name_size);
        if (raw_sizes) raw = pimp_le32(record + name_size + 4U);
        data = position + 4 + (int64_t)name_size + (int64_t)sizes;
        if (packed < PIMP_MIN_PACKED || (int64_t)packed > size - data ||
            !pimp_zlib_header_ok(record + name_size + sizes))
            return false;
        if (raw_sizes && (uint64_t)raw > (uint64_t)packed * PIMP_DEFLATE_RATIO +
                                             PIMP_DEFLATE_SLACK)
            return false;
        if (items) {
            pimp_member *member = &items[index];
            member->header_offset = position;
            member->header_size = 4U + name_size + sizes;
            member->data_offset = data;
            member->packed_size = packed;
            member->raw_size = raw;
            member->has_raw_size = raw_sizes;
            member->name = pimp_name(record, name_size - 1U);
            if (!member->name) return false;
            member->safe = pimp_safe_output_name(member->name);
        }
        position = data + (int64_t)packed;
    }
    *command_at = position;
    return true;
}

/* The command block: a u32 size, then the NUL-terminated command line. */
static bool pimp_command(xx_io_device *device, int64_t base, int64_t size,
                         int64_t at, uint32_t *command_size, int64_t *end) {
    uint8_t head[4], last;
    uint32_t length;
    if (size - at < 4 || !pimp_read_at(device, base + at, head, sizeof(head)))
        return false;
    length = pimp_le32(head);
    if (length < 1U || length > PIMP_MAX_COMMAND ||
        (int64_t)length > size - at - 4 ||
        !pimp_read_at(device, base + at + 4 + (int64_t)length - 1, &last, 1U) ||
        last != 0U)
        return false;
    *command_size = length;
    *end = at + 4 + (int64_t)length;
    return true;
}

/* The overlay: where the last section's raw data ends.  A section that
 * claims bytes beyond the file cannot belong to a complete package. */
static bool pimp_overlay(xx_io_device *device, int64_t base, int64_t size,
                         int64_t *overlay) {
    uint8_t dos[0x40], nt[24], table[PIMP_MAX_SECTIONS * 40U];
    uint32_t sections, optional, index;
    int64_t lfanew, table_at, end = 0;
    if (size < PIMP_MIN_FILE ||
        !pimp_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    lfanew = (int64_t)pimp_le32(dos + 0x3c);
    if (lfanew < 4 || lfanew > PIMP_MAX_LFANEW || lfanew > size - 24 ||
        !pimp_read_at(device, base + lfanew, nt, sizeof(nt)) || nt[0] != 'P' ||
        nt[1] != 'E' || nt[2] != 0U || nt[3] != 0U)
        return false;
    sections = pimp_le16(nt + 6);
    optional = pimp_le16(nt + 20);
    table_at = lfanew + 24 + (int64_t)optional;
    if (sections == 0U || sections > PIMP_MAX_SECTIONS ||
        table_at > size - (int64_t)sections * 40 ||
        !pimp_read_at(device, base + table_at, table, (size_t)sections * 40U))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * 40U;
        int64_t raw_size = (int64_t)pimp_le32(row + 16);
        int64_t raw_offset = (int64_t)pimp_le32(row + 20);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    if (end <= table_at) return false;
    *overlay = end;
    return true;
}

/* Finds and checks the whole payload.  Layout 2 (count after an extra u32,
 * unpacked sizes present) is tried before layout 1; either is accepted only
 * when its complete member chain and the command block fit. */
static bool pimp_scan(Abstractformat *format, pimp_info *info,
                      xx_pd_struct *pd) {
    uint8_t head[PIMP_SIGNATURE_SIZE + 1U];
    uint8_t fixed[PIMP_FIXED_BLOCK];
    int64_t total, size, block;
    uint32_t layout;
    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    xx_mem_zero(info, sizeof(*info));
    if (!pimp_overlay(format->device, format->base_address, size,
                      &info->overlay) ||
        size - info->overlay < (int64_t)sizeof(head) ||
        !pimp_read_at(format->device, format->base_address + info->overlay,
                      head, sizeof(head)) ||
        xx_rt_memcmp(head, "PIMPFILE", PIMP_SIGNATURE_SIZE) != 0)
        return false;
    block = info->overlay + (int64_t)sizeof(head);
    info->has_install_dir = head[PIMP_SIGNATURE_SIZE] != 0U;
    if (info->has_install_dir) {
        if (size - block < (int64_t)PIMP_DIR_FIELD ||
            !pimp_read_at(format->device, format->base_address + block,
                          info->dir_field, PIMP_DIR_FIELD))
            return false;
        block += PIMP_DIR_FIELD;
    }
    if (size - block < (int64_t)sizeof(fixed) ||
        !pimp_read_at(format->device, format->base_address + block, fixed,
                      sizeof(fixed)))
        return false;
    xx_rt_memcpy(info->text_fields, fixed, sizeof(info->text_fields));
    for (layout = 2U; layout >= 1U; --layout) {
        uint32_t count = pimp_le32(fixed + 2U * PIMP_TEXT_FIELD +
                                   (layout == 2U ? 4U : 0U));
        int64_t members_at = block + 2 * (int64_t)PIMP_TEXT_FIELD +
                             (layout == 2U ? 8 : 4);
        int64_t command_at = 0;
        /* Each record takes at least its fixed fields and a minimal zlib
         * stream, which bounds the count by the bytes left. */
        int64_t smallest = 4 + (int64_t)PIMP_MIN_NAME +
                           (layout == 2U ? 8 : 4) + (int64_t)PIMP_MIN_PACKED;
        if (count == 0U || count > PIMP_MAX_COUNT ||
            (int64_t)count > (size - members_at) / smallest)
            continue;
        if (!pimp_walk(format->device, format->base_address, size, members_at,
                       count, layout == 2U, NULL, &command_at, pd) ||
            !pimp_command(format->device, format->base_address, size,
                          command_at, &info->command_size, &info->end))
            continue;
        info->layout = layout;
        info->count = count;
        info->members_at = members_at;
        info->command_at = command_at;
        return true;
    }
    return false;
}

/* Folds @p member's current name and looks it up; *slot receives the slot
 * holding the same name, or the free slot where it would go. */
static bool pimp_name_taken(pimp_member *items, const size_t *table,
                            size_t slots, pimp_member *member, size_t *slot,
                            bool *taken) {
    member->folded = (char *)xx_mem_alloc(xx_str_len(member->name) + 1U);
    if (!member->folded) return false;
    pimp_fold(member->name, member->folded);
    *taken = false;
    *slot = pimp_hash(member->folded) & (slots - 1U);
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
static bool pimp_unique_names(pimp_member *items, size_t count) {
    size_t slots = 16U, index;
    size_t *table;
    bool result = false;
    while (slots < count * 2U) slots <<= 1U;
    table = (size_t *)xx_mem_alloc(slots * sizeof(*table));
    if (!table) return false;
    for (index = 0U; index < slots; ++index) table[index] = SIZE_MAX;
    for (index = 0U; index < count; ++index) {
        pimp_member *member = &items[index];
        char *stored = NULL;
        size_t slot = 0U, number = 1U;
        bool taken = false;
        if (!member->safe) continue;
        for (;;) {
            if (!pimp_name_taken(items, table, slots, member, &slot, &taken))
                break;
            if (!taken) break;
            xx_mem_free(member->folded);
            member->folded = NULL;
            /* At most count names are taken, so one of the next count
             * suffixes is free; the limit only guards the arithmetic. */
            if (++number > count + 1U || number > PIMP_RENAME_LIMIT) break;
            /* Every suffix derives from the stored name. */
            if (!stored) {
                stored = member->name;
            } else {
                xx_mem_free(member->name);
            }
            member->name = pimp_with_suffix(stored, number);
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

/* A write-only device between the inflater and the destination: it counts
 * the output, refuses anything past the limit and keeps the Adler-32. */
typedef struct pimp_sink_s {
    xx_io_device device; /* first, so the sink is its own device */
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
    uint32_t adler_a;
    uint32_t adler_b;
    bool failed;
} pimp_sink;

static ssize_t pimp_sink_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    pimp_sink *sink = (pimp_sink *)self;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t index = 0U, done = 0U;
    if (!sink || (!buffer && size != 0U) || sink->failed ||
        (uint64_t)size > sink->limit - sink->written ||
        size > ((size_t)-1 >> 1U)) {
        if (sink) sink->failed = true;
        return -1;
    }
    while (index < size) {
        size_t chunk = size - index < 5552U ? size - index : 5552U;
        size_t stop = index + chunk;
        for (; index < stop; ++index) {
            sink->adler_a += bytes[index];
            sink->adler_b += sink->adler_a;
        }
        sink->adler_a %= 65521U;
        sink->adler_b %= 65521U;
    }
    while (sink->target && done < size) {
        ssize_t amount = xx_io_write(sink->target, bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            sink->failed = true;
            return -1;
        }
        done += (size_t)amount;
    }
    sink->written += size;
    return (ssize_t)size;
}

static bool pimp_decode(Abstractformat *format, const pimp_member *member,
                        xx_io_device *target, xx_pd_struct *pd) {
    uint8_t header[2], trailer[4];
    pimp_sink sink;
    int64_t data = format->base_address + member->data_offset;
    uint32_t expected;
    if (member->packed_size < PIMP_MIN_PACKED ||
        !pimp_read_at(format->device, data, header, sizeof(header)) ||
        !pimp_zlib_header_ok(header) ||
        !pimp_read_at(format->device,
                      data + (int64_t)member->packed_size - 4, trailer,
                      sizeof(trailer)))
        return false;
    expected = ((uint32_t)trailer[0] << 24U) | ((uint32_t)trailer[1] << 16U) |
               ((uint32_t)trailer[2] << 8U) | (uint32_t)trailer[3];
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = pimp_sink_write;
    sink.target = target;
    sink.limit = member->has_raw_size
                     ? (uint64_t)member->raw_size
                     : (uint64_t)member->packed_size * PIMP_DEFLATE_RATIO +
                           PIMP_DEFLATE_SLACK;
    sink.adler_a = 1U;
    if (!xx_deflate_unpack_device(format->device, data + 2,
                                  (int64_t)member->packed_size - 6,
                                  &sink.device, false, pd))
        return false;
    if (sink.failed) return false;
    if (member->has_raw_size && sink.written != (uint64_t)member->raw_size)
        return false;
    return ((sink.adler_b << 16U) | sink.adler_a) == expected;
}

/* --- records --------------------------------------------------------------- */

static bool pimp_copy_options(xx_list_s *destination,
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

static const xx_var *pimp_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pimp_set_record(Abstractformat *format, xx_archive_record *record,
                            const pimp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        PIMP_METHOD_DEFLATE) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* Layout 1 does not record the unpacked size. */
    return !member->has_raw_size ||
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->raw_size);
}

/* --- lifecycle ------------------------------------------------------------- */

void xx_sfx_nullsoft_pimp_init(xx_sfx_nullsoft_pimp *archive,
                               xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_NULLSOFT_PIMP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdos-program");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_nullsoft_pimp_check_is_valid;
    archive->format.handle_base_info = xx_sfx_nullsoft_pimp_handle_base_info;
    archive->format.get_format_size = xx_sfx_nullsoft_pimp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_nullsoft_pimp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_nullsoft_pimp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_nullsoft_pimp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_nullsoft_pimp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_nullsoft_pimp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_nullsoft_pimp_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->directory_offset = -1;
    archive->command_offset = -1;
}

xx_sfx_nullsoft_pimp *xx_sfx_nullsoft_pimp_create(xx_io_device *device,
                                                  int64_t base_address) {
    xx_sfx_nullsoft_pimp *archive =
        (xx_sfx_nullsoft_pimp *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_nullsoft_pimp_init(archive, device, base_address);
    return archive;
}

void xx_sfx_nullsoft_pimp_destroy(xx_sfx_nullsoft_pimp *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_nullsoft_pimp_free(xx_sfx_nullsoft_pimp *archive) {
    if (!archive) return;
    xx_sfx_nullsoft_pimp_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_nullsoft_pimp_check_is_valid(Abstractformat *format,
                                         xx_pd_struct *pd) {
    pimp_info info;
    return pimp_scan(format, &info, pd);
}

bool xx_sfx_nullsoft_pimp_handle_base_info(Abstractformat *format,
                                           xx_pd_struct *pd) {
    pimp_info info;
    xx_sfx_nullsoft_pimp *archive;
    if (!format || !pimp_scan(format, &info, pd)) return false;
    archive = (xx_sfx_nullsoft_pimp *)format;
    archive->number_of_records = info.count;
    archive->overlay_offset = info.overlay;
    archive->directory_offset = info.members_at;
    archive->command_offset = info.command_at;
    archive->command_size = info.command_size;
    archive->layout = info.layout;
    archive->has_install_dir = info.has_install_dir;
    pimp_text(info.text_fields, PIMP_TEXT_FIELD, archive->title);
    pimp_text(info.text_fields + PIMP_TEXT_FIELD, PIMP_TEXT_FIELD,
              archive->description);
    if (info.has_install_dir)
        pimp_text(info.dir_field, PIMP_DIR_FIELD, archive->install_dir);
    else
        archive->install_dir[0] = 0;
    format->number_of_archive_records = info.count;
    format->format_size = info.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_nullsoft_pimp_get_format_size(Abstractformat *format,
                                             xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_nullsoft_pimp_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_nullsoft_pimp_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_nullsoft_pimp_handle_base_info(format, pd))
               ? ((xx_sfx_nullsoft_pimp *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_sfx_nullsoft_pimp_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pimp_info info;
    pimp_stream *stream;
    xx_archive_record_state *state;
    int64_t size, command_at = 0;
    if (!pimp_scan(format, &info, pd)) return NULL;
    size = xx_io_total_size(format->device) - format->base_address;
    stream = (pimp_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->items = (pimp_member *)xx_mem_calloc(info.count,
                                                 sizeof(*stream->items));
    stream->count = info.count;
    if (!stream->items ||
        !pimp_walk(format->device, format->base_address, size,
                   info.members_at, info.count, info.layout == 2U,
                   stream->items, &command_at, pd) ||
        command_at != info.command_at ||
        !pimp_unique_names(stream->items, stream->count)) {
        pimp_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pimp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pimp_stream_free;
    state->total_records = stream->count;
    if (!pimp_copy_options(&state->options, options) ||
        !pimp_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_nullsoft_pimp_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_nullsoft_pimp_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    pimp_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pimp_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    if (!pimp_set_record(format, &state->current_record,
                         &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_sfx_nullsoft_pimp_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    pimp_stream *stream;
    const pimp_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pimp_stream *)state->internal_state) ||
        stream->index >= stream->count || pimp_stopped(pd))
        return false;
    member = &stream->items[stream->index];
    path_option = pimp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return pimp_decode(format, member, NULL, pd);
    if (!member->safe || !pimp_safe_output_name(member->name)) return false;
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
        result = pimp_decode(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_nullsoft_pimp_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
