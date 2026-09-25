/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield "Setup Player 2K2" All-in-One single-exe (U3 "SFX IS14").
 * xx_installshield_7_setup.h carries the record layout.
 *
 * The acceptance rules -- payload at the PE overlay, four NUL-terminated
 * ANSI strings per record, a dotted version, a decimal size, the base name
 * equal to the last path component, and the chain ending exactly at end of
 * file -- follow XArchive's installers/xis14sfxarchive.cpp (MIT, hors).  This
 * is a fresh C implementation of those rules for xxfclib; the certificate
 * tail tolerance, the windowed reads and the name handling are its own.
 *
 * The executable is parsed only as far as its section table and security
 * directory, to find where the overlay starts and where it has to end.  No
 * code in it is run or emulated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_7_setup/xx_installshield_7_setup.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef INSTALLSHIELD_7_SETUP
#define XX_INSTALLSHIELD_7_SETUP_FILE_TYPE XX_FILE_TYPE_INSTALLSHIELD_7_SETUP
#else
#define XX_INSTALLSHIELD_7_SETUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS7_DOS_HEADER 64
#define IS7_PE_HEADER 24
#define IS7_SECTION_SIZE 40
#define IS7_MAX_SECTIONS 96
/* Enough of the optional header to reach the security directory of both
 * PE32 (+128) and PE32+ (+144). */
#define IS7_OPTIONAL_READ 160
#define IS7_PE32_MAGIC 0x010bU
#define IS7_PE64_MAGIC 0x020bU
#define IS7_SECURITY_DIRECTORY 4U
/* A string holds 1..IS7_MAX_STRING bytes before its NUL (MAX_PATH). */
#define IS7_MAX_STRING 260
#define IS7_MAX_HEADER (4 * (IS7_MAX_STRING + 1))
/* "a\0a\0" + "0\0" + "0\0": the shortest possible record (an empty member). */
#define IS7_MIN_RECORD 8
/* Real releases carry a few dozen files; this bounds the walk and the
 * bookkeeping (about 768 KiB at the cap) on hostile input. */
#define IS7_MAX_RECORDS 16384U
#define IS7_WINDOW 2048
#define IS7_COPY_CHUNK 65536U
#define IS7_MAX_VERSION_PARTS 4U
#define IS7_MAX_VERSION_DIGITS 10U
/* Eighteen digits always fit in an int64_t (nineteen can overflow it). */
#define IS7_MAX_SIZE_DIGITS 18U
/* Authenticode pads the image to 8 bytes before the certificate table. */
#define IS7_CERT_ALIGN 8
/* A converted name: at most "%XX" per raw byte, then "%_" and up to ten
 * digits of record index, then the terminator. */
#define IS7_NAME_BUFFER (3 * IS7_MAX_STRING + 2 + 10 + 1)
#define IS7_POLL_MASK 0xffU

typedef struct is7_layout_s {
    int64_t size;           /**< base_address to end of file. */
    int64_t payload_offset; /**< Overlay start, relative. */
    int64_t limit;          /**< End of file, or the certificate table. */
    bool certificate;       /**< limit is where a certificate table starts. */
} is7_layout;

typedef struct is7_fields_s {
    size_t base_length;
    size_t path_offset;
    size_t path_length;
    size_t version_offset;
    size_t version_length;
    size_t header_size;
    int64_t size;
} is7_fields;

typedef struct is7_member_s {
    int64_t header_offset; /**< Absolute. */
    int64_t data_offset;   /**< Absolute. */
    int64_t size;
    uint32_t header_size;
    bool renamed; /**< Name clash: "%_<index>" is inserted. */
} is7_member;

typedef struct is7_key_s {
    uint64_t hash; /**< Of the converted name, ASCII folded to lower case. */
    uint32_t index;
} is7_key;

typedef struct is7_window_s {
    xx_io_device *device;
    int64_t origin; /**< Absolute offset of relative offset 0. */
    int64_t limit;  /**< Relative end of the record area. */
    int64_t start;  /**< Relative offset of buffer[0]. */
    size_t length;
    uint8_t buffer[IS7_WINDOW];
} is7_window;

typedef struct is7_stream_s {
    is7_member *items;
    size_t count;
    size_t index;
    char name[IS7_NAME_BUFFER];
    char version[IS7_MAX_STRING + 1];
} is7_stream;

static uint32_t is7_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t is7_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool is7_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Stream @p size bytes at @p offset into @p destination (or just read them
 * through when it is NULL) in fixed chunks, never as one allocation. */
static bool is7_copy_range(xx_io_device *source, int64_t offset, int64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(IS7_COPY_CHUNK);
    if (!buffer) return false;
    while (remaining > 0) {
        size_t chunk = remaining > (int64_t)IS7_COPY_CHUNK
                           ? (size_t)IS7_COPY_CHUNK
                           : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !is7_read_at(source, offset + (size - remaining), buffer, chunk)) {
            ok = false;
            break;
        }
        while (destination && written < chunk) {
            ssize_t amount = xx_io_write(destination, buffer + written,
                                         chunk - written);
            if (amount <= 0 || (size_t)amount > chunk - written) {
                ok = false;
                break;
            }
            written += (size_t)amount;
        }
        if (!ok) break;
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ---- the executable ---------------------------------------------------- */

/* Find the overlay: the end of the furthest section's raw data.  Every
 * section must lie inside the file.  When the security directory points at
 * a certificate table that starts at or behind the overlay and runs exactly
 * to end of file, the record chain has to stop there instead. */
static bool is7_locate(Abstractformat *format, is7_layout *layout) {
    uint8_t dos[IS7_DOS_HEADER];
    uint8_t pe[IS7_PE_HEADER];
    uint8_t optional[IS7_OPTIONAL_READ];
    uint8_t sections[IS7_MAX_SECTIONS * IS7_SECTION_SIZE];
    int64_t total, size, nt, table, overlay = 0;
    uint32_t section_count, optional_size, magic, index;
    size_t optional_read;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < IS7_DOS_HEADER + IS7_PE_HEADER + IS7_MIN_RECORD ||
        !is7_read_at(format->device, format->base_address, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    nt = (int64_t)is7_le32(dos + 0x3c);
    if (nt < 4 || nt > size - IS7_PE_HEADER ||
        !is7_read_at(format->device, format->base_address + nt, pe,
                     sizeof(pe)) ||
        pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return false;
    section_count = is7_le16(pe + 6U);
    optional_size = is7_le16(pe + 20U);
    if (section_count == 0U || section_count > IS7_MAX_SECTIONS ||
        optional_size < 2U)
        return false;
    table = nt + IS7_PE_HEADER + (int64_t)optional_size;
    if (table > size ||
        (int64_t)section_count * IS7_SECTION_SIZE > size - table)
        return false;
    optional_read = optional_size < IS7_OPTIONAL_READ ? (size_t)optional_size
                                                      : IS7_OPTIONAL_READ;
    if (!is7_read_at(format->device,
                     format->base_address + nt + IS7_PE_HEADER, optional,
                     optional_read))
        return false;
    magic = is7_le16(optional);
    if (magic != IS7_PE32_MAGIC && magic != IS7_PE64_MAGIC) return false;
    if (!is7_read_at(format->device, format->base_address + table, sections,
                     (size_t)section_count * IS7_SECTION_SIZE))
        return false;
    for (index = 0U; index < section_count; ++index) {
        const uint8_t *entry = sections + (size_t)index * IS7_SECTION_SIZE;
        int64_t raw_size = (int64_t)is7_le32(entry + 16U);
        int64_t raw_offset = (int64_t)is7_le32(entry + 20U);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > overlay) overlay = raw_offset + raw_size;
    }
    if (overlay <= 0 || overlay >= size || size - overlay < IS7_MIN_RECORD)
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    layout->size = size;
    layout->payload_offset = overlay;
    layout->limit = size;
    {
        /* Data directories start at +96 (PE32) or +112 (PE32+), preceded by
         * their count; each is {u32 address, u32 size}, and the security
         * directory's "address" is a file offset. */
        size_t directories = magic == IS7_PE32_MAGIC ? 96U : 112U;
        size_t entry = directories + 8U * IS7_SECURITY_DIRECTORY;
        if (optional_read >= entry + 8U &&
            is7_le32(optional + directories - 4U) > IS7_SECURITY_DIRECTORY) {
            int64_t cert_offset = (int64_t)is7_le32(optional + entry);
            int64_t cert_size = (int64_t)is7_le32(optional + entry + 4U);
            if (cert_offset != 0 && cert_size != 0 && cert_offset >= overlay &&
                cert_offset <= size && cert_size == size - cert_offset) {
                layout->limit = cert_offset;
                layout->certificate = true;
            }
        }
    }
    return layout->limit - layout->payload_offset >= IS7_MIN_RECORD;
}

/* ---- records ----------------------------------------------------------- */

/* Return a pointer to relative offset @p pos with
 * min(IS7_MAX_HEADER, limit - pos) bytes behind it; *avail gets the real
 * count.  NULL at or past the limit, or on a read error. */
static const uint8_t *is7_view(is7_window *window, int64_t pos,
                               size_t *avail) {
    int64_t want = window->limit - pos;
    if (pos < 0 || want <= 0) return NULL;
    if (want > IS7_MAX_HEADER) want = IS7_MAX_HEADER;
    if (pos < window->start ||
        pos + want > window->start + (int64_t)window->length) {
        int64_t chunk = window->limit - pos;
        if (chunk > IS7_WINDOW) chunk = IS7_WINDOW;
        window->length = 0U;
        if (!is7_read_at(window->device, window->origin + pos, window->buffer,
                         (size_t)chunk))
            return NULL;
        window->start = pos;
        window->length = (size_t)chunk;
    }
    *avail = (size_t)(window->start + (int64_t)window->length - pos);
    return window->buffer + (pos - window->start);
}

/* Length of the NUL-terminated string at view[at], 1..IS7_MAX_STRING, or 0
 * when it is empty, too long or runs past the available bytes. */
static size_t is7_string(const uint8_t *view, size_t avail, size_t at) {
    size_t length = 0U;
    while (at < avail && length < avail - at && length <= IS7_MAX_STRING) {
        if (view[at + length] == 0U) return length;
        ++length;
    }
    return 0U;
}

static uint8_t is7_fold(uint8_t c) {
    return (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
               ? (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a')
               : c;
}

/* 1..4 dot-separated groups of 1..10 decimal digits. */
static bool is7_dotted_version(const uint8_t *text, size_t length) {
    size_t index, digits = 0U, parts = 1U;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c >= (uint8_t)'0' && c <= (uint8_t)'9') {
            if (++digits > IS7_MAX_VERSION_DIGITS) return false;
        } else if (c == (uint8_t)'.' && digits != 0U &&
                   parts < IS7_MAX_VERSION_PARTS) {
            ++parts;
            digits = 0U;
        } else {
            return false;
        }
    }
    return digits != 0U;
}

/* Decimal digits, no sign, no leading zero (except "0" itself). */
static bool is7_decimal(const uint8_t *text, size_t length, int64_t *value) {
    size_t index;
    int64_t result = 0;
    if (length == 0U || length > IS7_MAX_SIZE_DIGITS ||
        (length > 1U && text[0] == (uint8_t)'0'))
        return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c < (uint8_t)'0' || c > (uint8_t)'9') return false;
        result = result * 10 + (int64_t)(c - (uint8_t)'0');
    }
    *value = result;
    return true;
}

/* Split one record header out of @p view.  The base name carries no
 * separator, neither name carries a control byte, the path's last component
 * is the base name (ASCII case folded), the version is dotted decimal and
 * the size decimal. */
static bool is7_parse_record(const uint8_t *view, size_t avail,
                             is7_fields *fields) {
    size_t index, component = 0U, size_offset, size_length;
    const uint8_t *path;
    fields->base_length = is7_string(view, avail, 0U);
    if (fields->base_length == 0U) return false;
    for (index = 0U; index < fields->base_length; ++index) {
        uint8_t c = view[index];
        if (c < 0x20U || c == 0x7fU || c == (uint8_t)'/' ||
            c == (uint8_t)'\\')
            return false;
    }
    fields->path_offset = fields->base_length + 1U;
    fields->path_length = is7_string(view, avail, fields->path_offset);
    if (fields->path_length == 0U) return false;
    path = view + fields->path_offset;
    for (index = 0U; index < fields->path_length; ++index) {
        uint8_t c = path[index];
        if (c < 0x20U || c == 0x7fU) return false;
        if (c == (uint8_t)'\\' || c == (uint8_t)'/') component = index + 1U;
    }
    if (fields->path_length - component != fields->base_length) return false;
    for (index = 0U; index < fields->base_length; ++index)
        if (is7_fold(path[component + index]) != is7_fold(view[index]))
            return false;
    fields->version_offset = fields->path_offset + fields->path_length + 1U;
    fields->version_length = is7_string(view, avail, fields->version_offset);
    if (!is7_dotted_version(view + fields->version_offset,
                            fields->version_length))
        return false;
    size_offset = fields->version_offset + fields->version_length + 1U;
    size_length = is7_string(view, avail, size_offset);
    if (!is7_decimal(view + size_offset, size_length, &fields->size))
        return false;
    fields->header_size = size_offset + size_length + 1U;
    return true;
}

/* ---- member names ------------------------------------------------------ */

/* Raw ANSI path -> output name: '\\' becomes '/', bytes 0x80-0xFF and '%'
 * become "%XX" (so the mapping is one-to-one and never meets a code page).
 * @p out holds at least 3 * length + 1 bytes; returns the converted
 * length. */
static size_t is7_convert_name(const uint8_t *raw, size_t length, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0U, index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c >= 0x80U || c == (uint8_t)'%') {
            out[at++] = '%';
            out[at++] = digits[(c >> 4U) & 0x0fU];
            out[at++] = digits[c & 0x0fU];
        } else if (c == (uint8_t)'\\') {
            out[at++] = '/';
        } else {
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
    return at;
}

/* 64-bit FNV-1a with ASCII folded to lower case, so names a
 * case-insensitive file system treats as one hash alike.  A collision
 * between different names only renames a member needlessly. */
static uint64_t is7_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        hash ^= (uint64_t)is7_fold((uint8_t)name[index]);
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "%_<index>" before the extension of the last component (or append
 * it when there is none).  @p name has room for IS7_NAME_BUFFER bytes. */
static void is7_insert_suffix(char *name, size_t length, uint32_t index) {
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= IS7_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$,
 * CONIN$, CONOUT$) as the part of a component before its first '.',
 * trailing spaces ignored. */
static bool is7_reserved_component(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ')
        --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

/* Refuse absolute paths, drive letters and streams (any ':'), empty
 * components, components ending in '.' or ' ' (this covers "." and ".."),
 * device names, control characters and the characters no Windows path may
 * carry. */
static bool is7_safe_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7fU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                is7_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- chain walk -------------------------------------------------------- */

/* Walk the record chain from the overlay to the limit.  With @p items NULL
 * this is the probe and keeps nothing; otherwise it fills items[] and
 * keys[] (@p capacity entries each), using @p name to hash every converted
 * name.  *count gets the number of records, *end where the chain ended. */
static bool is7_walk(Abstractformat *format, const is7_layout *layout,
                     is7_member *items, is7_key *keys, size_t capacity,
                     char *name, size_t *count, int64_t *end,
                     xx_pd_struct *pd) {
    is7_window *window;
    int64_t pos = layout->payload_offset;
    size_t records = 0U;
    bool ok = false;
    window = (is7_window *)xx_mem_alloc(sizeof(*window));
    if (!window) return false;
    window->device = format->device;
    window->origin = format->base_address;
    window->limit = layout->limit;
    window->start = 0;
    window->length = 0U;
    while (pos < layout->limit) {
        const uint8_t *view;
        size_t avail = 0U;
        is7_fields fields;
        int64_t data;
        if (layout->certificate && layout->limit - pos < IS7_CERT_ALIGN) {
            /* Too short for a record: the alignment padding in front of
             * the certificate table, which must be zero. */
            view = is7_view(window, pos, &avail);
            if (!view || avail != (size_t)(layout->limit - pos)) goto done;
            while (avail != 0U)
                if (view[--avail] != 0U) goto done;
            break;
        }
        if (records >= IS7_MAX_RECORDS ||
            ((records & IS7_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)))
            goto done;
        view = is7_view(window, pos, &avail);
        if (!view || !is7_parse_record(view, avail, &fields)) goto done;
        data = pos + (int64_t)fields.header_size;
        if (fields.size > layout->limit - data) goto done;
        if (items) {
            size_t converted;
            if (records >= capacity) goto done;
            converted = is7_convert_name(view + fields.path_offset,
                                         fields.path_length, name);
            items[records].header_offset = format->base_address + pos;
            items[records].data_offset = format->base_address + data;
            items[records].size = fields.size;
            items[records].header_size = (uint32_t)fields.header_size;
            items[records].renamed = false;
            keys[records].hash = is7_name_hash(name, converted);
            keys[records].index = (uint32_t)records;
        }
        pos = data + fields.size;
        ++records;
    }
    if (records == 0U) goto done;
    if (count) *count = records;
    if (end) *end = pos;
    ok = true;
done:
    xx_mem_free(window);
    return ok;
}

static int is7_compare_keys(const void *left, const void *right) {
    const is7_key *a = (const is7_key *)left;
    const is7_key *b = (const is7_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Sorting puts every group of equal (case-folded) names together, lowest
 * record index first; that one keeps its name and the rest are renamed. */
static void is7_mark_duplicates(is7_member *items, is7_key *keys,
                                size_t count) {
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), is7_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            keys[index].index < count)
            items[keys[index].index].renamed = true;
}

static void is7_stream_free(void *opaque) {
    is7_stream *stream = (is7_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool is7_open_stream(Abstractformat *format, is7_stream **result,
                            xx_pd_struct *pd) {
    is7_layout layout;
    is7_key *keys = NULL;
    is7_stream *stream = NULL;
    size_t count = 0U, filled = 0U;
    if (!result || !is7_locate(format, &layout) ||
        !is7_walk(format, &layout, NULL, NULL, 0U, NULL, &count, NULL, pd))
        return false;
    stream = (is7_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items = (is7_member *)xx_mem_calloc(count, sizeof(*stream->items));
    keys = (is7_key *)xx_mem_calloc(count, sizeof(*keys));
    if (!stream->items || !keys ||
        !is7_walk(format, &layout, stream->items, keys, count, stream->name,
                  &filled, NULL, pd) ||
        filled != count)
        goto fail;
    is7_mark_duplicates(stream->items, keys, count);
    xx_mem_free(keys);
    stream->count = count;
    *result = stream;
    return true;
fail:
    if (keys) xx_mem_free(keys);
    is7_stream_free(stream);
    return false;
}

static bool is7_copy_options(xx_list_s *destination,
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

static const xx_var *is7_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* Re-read record @p index's header and leave its converted (and, for a
 * clash, suffixed) name in stream->name and its version in
 * stream->version. */
static bool is7_load_record(Abstractformat *format, is7_stream *stream,
                            size_t index) {
    const is7_member *member = &stream->items[index];
    uint8_t header[IS7_MAX_HEADER];
    is7_fields fields;
    size_t length;
    if (member->header_size == 0U || member->header_size > IS7_MAX_HEADER ||
        !is7_read_at(format->device, member->header_offset, header,
                     member->header_size) ||
        !is7_parse_record(header, member->header_size, &fields) ||
        fields.header_size != member->header_size ||
        fields.size != member->size)
        return false;
    length = is7_convert_name(header + fields.path_offset, fields.path_length,
                              stream->name);
    if (member->renamed)
        is7_insert_suffix(stream->name, length, (uint32_t)index);
    xx_rt_memcpy(stream->version, header + fields.version_offset,
                 fields.version_length);
    stream->version[fields.version_length] = 0;
    return true;
}

static bool is7_set_record(Abstractformat *format, xx_archive_record *record,
                           is7_stream *stream, size_t index) {
    const is7_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!is7_load_record(format, stream, index)) return false;
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          stream->version) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_installshield_7_setup_init(xx_installshield_7_setup *archive,
                                   xx_io_device *device,
                                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_7_SETUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/vnd.microsoft.portable-executable");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_installshield_7_setup_check_is_valid;
    archive->format.handle_base_info =
        xx_installshield_7_setup_handle_base_info;
    archive->format.get_format_size =
        xx_installshield_7_setup_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_7_setup_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_7_setup_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_7_setup_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_7_setup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_7_setup_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_7_setup_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
}

xx_installshield_7_setup *xx_installshield_7_setup_create(
    xx_io_device *device, int64_t base_address) {
    xx_installshield_7_setup *archive =
        (xx_installshield_7_setup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_7_setup_init(archive, device, base_address);
    return archive;
}

void xx_installshield_7_setup_destroy(xx_installshield_7_setup *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_7_setup_free(xx_installshield_7_setup *archive) {
    if (!archive) return;
    xx_installshield_7_setup_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_7_setup_check_is_valid(Abstractformat *format,
                                             xx_pd_struct *pd) {
    is7_layout layout;
    return is7_locate(format, &layout) &&
           is7_walk(format, &layout, NULL, NULL, 0U, NULL, NULL, NULL, pd);
}

bool xx_installshield_7_setup_handle_base_info(Abstractformat *format,
                                               xx_pd_struct *pd) {
    is7_layout layout;
    xx_installshield_7_setup *archive;
    size_t count = 0U;
    int64_t end = 0;
    if (!is7_locate(format, &layout) ||
        !is7_walk(format, &layout, NULL, NULL, 0U, NULL, &count, &end, pd))
        return false;
    archive = (xx_installshield_7_setup *)format;
    archive->number_of_records = (uint64_t)count;
    archive->payload_offset = layout.payload_offset;
    archive->payload_end = end;
    format->number_of_archive_records = (uint64_t)count;
    format->format_size = layout.size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_7_setup_get_format_size(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_7_setup_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_installshield_7_setup_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_7_setup_handle_base_info(format, pd))
               ? ((xx_installshield_7_setup *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_installshield_7_setup_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is7_stream *stream;
    xx_archive_record_state *state;
    if (!is7_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is7_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is7_stream_free;
    state->total_records = stream->count;
    if (!is7_copy_options(&state->options, options) ||
        !is7_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installshield_7_setup_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_installshield_7_setup_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    is7_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is7_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = is7_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_installshield_7_setup_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    is7_stream *stream;
    const is7_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is7_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->size < 0 || member->data_offset < 0) return false;
    path_option = is7_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the member through, which verifies it. */
        return is7_copy_range(format->device, member->data_offset,
                              member->size, NULL, pd);
    /* stream->name came from the file: refuse it before anything is created
     * when it could escape the output folder or name a device. */
    if (!is7_safe_name(stream->name)) return false;
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
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = is7_copy_range(format->device, member->data_offset,
                                member->size, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_7_setup_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
