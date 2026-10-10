/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield Developer 7 (ISWI 7.0) InstallScript-MSI Setup Launcher.
 * xx_installshield_developer.h carries the payload layout.
 *
 * Locating and walking the payload follows XArchive's
 * core/xlegacystorearchive.cpp, FT_INSTALLSHIELD_LAUNCHER branch (MIT,
 * Copyright (c) 2026 hors): the table starts at the exact end of the PE
 * image's last section, and every record is checked for the zero fields
 * that surround its name and size.  The reference reader's "SFX IS2" handler checks the same
 * header.  This reader adds what an SFX reader needs on hostile input and in
 * a raw-data search: a truncated or damaged chain keeps the complete members
 * in front of the damage, a chain that ends before the file does (an
 * Authenticode table, or data following an embedded launcher) is measured
 * rather than refused, and member names are made unique and are checked
 * before anything is written.
 *
 * The executable itself is parsed only as far as its section table and the
 * certificate data directory; no code in it is looked at.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_developer/xx_installshield_developer.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the type is registered. */
#ifdef INSTALLSHIELD_DEVELOPER

#define XX_INSTALLSHIELD_DEVELOPER_FILE_TYPE XX_FILE_TYPE_INSTALLSHIELD_DEVELOPER
#else
#define XX_INSTALLSHIELD_DEVELOPER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* "InstallShield" and its terminating NUL. */
#define ISD_SIGNATURE_SIZE 14U
#define ISD_COUNT_OFFSET 0x0EU
#define ISD_HEADER_SIZE ((size_t)XX_INSTALLSHIELD_DEVELOPER_HEADER_SIZE)
#define ISD_RECORD_SIZE ((size_t)XX_INSTALLSHIELD_DEVELOPER_RECORD_SIZE)
/* The name and everything up to the size field. */
#define ISD_NAME_FIELD 0x10CU
#define ISD_SIZE_OFFSET 0x10CU
#define ISD_TAIL_OFFSET 0x110U
/* The count field of this header family is 16 bits wide (the two bytes
 * above it are zero in every launcher), so this is the real maximum. */
#define ISD_MAX_FILES 65535U

#define ISD_DOS_HEADER_SIZE 0x40U
#define ISD_LFANEW_OFFSET 0x3CU
#define ISD_FILE_HEADER_SIZE 24U
#define ISD_SECTION_SIZE 40U
/* The PE/COFF specification's limit, and XArchive's. */
#define ISD_MAX_SECTIONS 96U
#define ISD_PE32_MAGIC 0x10BU
#define ISD_PE64_MAGIC 0x20BU
/* Bytes of the optional header needed to reach the certificate directory
 * (data directory 4) of a PE32+ image, the larger of the two layouts. */
#define ISD_OPTIONAL_READ 0x98U
#define ISD_SECURITY_DIRECTORY 4U
/* An Authenticode table starts on the next 8-byte boundary. */
#define ISD_CERTIFICATE_ALIGN 8

/* A renamed duplicate gains " (<record>_<attempt>)", at most 14 bytes. */
#define ISD_SUFFIX_ROOM 16U
/* A name is at most 0x10B bytes; each byte decodes to at most three UTF-8
 * bytes (the Windows-1252 punctuation above U+07FF). */
#define ISD_UTF8_NAME_MAX ((ISD_NAME_FIELD + ISD_SUFFIX_ROOM) * 3U + 1U)
#define ISD_COPY_CHUNK 65536U

typedef struct isd_member_s {
    int64_t header_offset; /**< Absolute offset of the 0x138-byte record. */
    int64_t data_offset;   /**< Absolute offset of the stored bytes. */
    int64_t size;
    size_t name_at;  /**< Offset of the raw name in the name pool. */
    uint32_t record; /**< Position of the record in the table. */
} isd_member;

typedef struct isd_layout_s {
    int64_t overlay;   /**< Relative to base_address. */
    int64_t chain_end; /**< Relative; end of the last complete member. */
    int64_t format_size;
    int64_t certificate_offset; /**< Relative; 0 when there is none. */
    int64_t certificate_size;
    uint64_t name_bytes; /**< Sum of the present members' name lengths. */
    uint32_t declared;
    uint32_t present;
    bool damaged;
    bool has_certificate;
} isd_layout;

typedef struct isd_stream_s {
    isd_layout layout;
    isd_member *items;
    char *names;
    size_t names_size;
    size_t count;
    size_t index;
} isd_stream;

/* Windows-1252 0x80..0x9F.  0 marks a byte the code page leaves undefined;
 * those decode to '_'.  0xA0..0xFF are the Latin-1 code points. */
static const uint16_t isd_cp1252_high[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                                             0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

#include "xxfclib/global/xx_global.h"
static size_t gb_installshield_developer_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}
static ssize_t gb_installshield_developer_read(xx_io_device *device, void *buffer, size_t size, size_t capacity)
{
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}
static ssize_t gb_installshield_developer_write(xx_io_device *device, const void *buffer, size_t size, size_t capacity)
{
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_write(device, (const uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static uint32_t isd_le16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool isd_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    const size_t file_io_capacity = gb_installshield_developer_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = gb_installshield_developer_read(device, (uint8_t *)buffer + done, size - done, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool isd_all_zero(const uint8_t *bytes, size_t from, size_t to)
{
    size_t index;
    for (index = from; index < to; ++index)
        if (bytes[index] != 0U) return false;
    return true;
}

/* Find the PE overlay: the end of the furthest section's raw data.  Also
 * report the certificate table, which a signing tool appends behind the
 * payload.  Offsets are relative to `base`; `size` is what is present. */
static bool isd_locate_overlay(xx_io_device *device, int64_t base, int64_t size, isd_layout *layout)
{
    uint8_t dos[ISD_DOS_HEADER_SIZE];
    uint8_t file_header[ISD_FILE_HEADER_SIZE];
    uint8_t optional[ISD_OPTIONAL_READ];
    uint8_t table[ISD_MAX_SECTIONS * ISD_SECTION_SIZE];
    int64_t pe, table_offset, table_size, end = 0;
    uint32_t sections, optional_size, index;
    if (size < (int64_t)ISD_DOS_HEADER_SIZE || !isd_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') return false;
    pe = (int64_t)xx_data_get_u32(dos + ISD_LFANEW_OFFSET, 4, 0, false);
    if (pe < 4 || pe > size - (int64_t)ISD_FILE_HEADER_SIZE || !isd_read_at(device, base + pe, file_header, sizeof(file_header)) ||
        xx_rt_memcmp(file_header, "PE\0\0", 4U) != 0)
        return false;
    sections = isd_le16(file_header + 6U);
    optional_size = isd_le16(file_header + 20U);
    if (sections == 0U || sections > ISD_MAX_SECTIONS) return false;
    table_offset = pe + (int64_t)ISD_FILE_HEADER_SIZE + (int64_t)optional_size;
    table_size = (int64_t)sections * (int64_t)ISD_SECTION_SIZE;
    if (table_offset > size || table_size > size - table_offset || !isd_read_at(device, base + table_offset, table, (size_t)table_size)) return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + (size_t)index * ISD_SECTION_SIZE;
        int64_t raw_size = (int64_t)xx_data_get_u32(row + 16U, 4, 0, false);
        int64_t raw_offset = (int64_t)xx_data_get_u32(row + 20U, 4, 0, false);
        if (raw_size == 0) continue;
        /* A section running past the end of the file leaves no overlay. */
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    /* No section data at all, or "section data" inside the headers. */
    if (end < table_offset + table_size) return false;
    layout->overlay = end;

    /* Data directory 4 holds a file offset, not an RVA. */
    layout->certificate_offset = 0;
    layout->certificate_size = 0;
    if (optional_size >= 2U) {
        size_t amount = optional_size < ISD_OPTIONAL_READ ? (size_t)optional_size : ISD_OPTIONAL_READ;
        size_t count_at = 0U, directories_at = 0U;
        if (!isd_read_at(device, base + pe + (int64_t)ISD_FILE_HEADER_SIZE, optional, amount)) return false;
        if (isd_le16(optional) == ISD_PE32_MAGIC) {
            count_at = 92U;
            directories_at = 96U;
        } else if (isd_le16(optional) == ISD_PE64_MAGIC) {
            count_at = 108U;
            directories_at = 112U;
        }
        if (directories_at != 0U && amount >= directories_at + (ISD_SECURITY_DIRECTORY + 1U) * 8U &&
            xx_data_get_u32(optional + count_at, 4, 0, false) > ISD_SECURITY_DIRECTORY) {
            const uint8_t *entry = optional + directories_at + ISD_SECURITY_DIRECTORY * 8U;
            layout->certificate_offset = (int64_t)xx_data_get_u32(entry, 4, 0, false);
            layout->certificate_size = (int64_t)xx_data_get_u32(entry + 4U, 4, 0, false);
        }
    }
    return true;
}

/* One record header: a non-empty name without control bytes, terminated
 * inside the name field, zeros behind it and zeros behind the size. */
static bool isd_check_record(const uint8_t *record, size_t *name_length)
{
    size_t length = 0U;
    while (length < ISD_NAME_FIELD && record[length] != 0U) {
        uint8_t c = record[length];
        if (c < 0x20U || c == 0x7FU) return false;
        ++length;
    }
    if (length == 0U || length >= ISD_NAME_FIELD || !isd_all_zero(record, length, ISD_NAME_FIELD) || !isd_all_zero(record, ISD_TAIL_OFFSET, ISD_RECORD_SIZE))
        return false;
    *name_length = length;
    return true;
}

/* Walk the member chain.  With `items` NULL this validates, counts and
 * measures; otherwise it also fills `items` (room for layout->present
 * entries, from an earlier counting walk) and the name pool, where every
 * name gets ISD_SUFFIX_ROOM spare bytes for a later rename. */
static bool isd_walk(xx_io_device *device, int64_t base, int64_t size, isd_layout *layout, isd_member *items, char *names, size_t names_size, xx_pd_struct *pd)
{
    uint8_t header[ISD_HEADER_SIZE];
    uint8_t record[ISD_RECORD_SIZE];
    int64_t position;
    uint32_t count, index, present = 0U, capacity = layout->present;
    uint64_t name_bytes = 0U;
    size_t pool = 0U;
    if (layout->overlay > size || (int64_t)ISD_HEADER_SIZE > size - layout->overlay || !isd_read_at(device, base + layout->overlay, header, sizeof(header))) return false;
    if (xx_rt_memcmp(header, "InstallShield\0", ISD_SIGNATURE_SIZE) != 0) return false;
    count = xx_data_get_u32(header + ISD_COUNT_OFFSET, 4, 0, false);
    if (count == 0U || count > ISD_MAX_FILES || !isd_all_zero(header, ISD_COUNT_OFFSET + 4U, ISD_HEADER_SIZE)) return false;
    position = layout->overlay + (int64_t)ISD_HEADER_SIZE;
    for (index = 0U; index < count; ++index) {
        size_t name_length;
        int64_t data, member_size;
        if (pd && xx_pd_is_stopped(pd)) return false;
        /* The chain stops at the first record that is cut off or is not a
         * record; what precedes it is kept. */
        if ((int64_t)ISD_RECORD_SIZE > size - position) break;
        if (!isd_read_at(device, base + position, record, sizeof(record))) return false;
        if (!isd_check_record(record, &name_length)) break;
        data = position + (int64_t)ISD_RECORD_SIZE;
        member_size = (int64_t)xx_data_get_u32(record + ISD_SIZE_OFFSET, 4, 0, false);
        if (member_size > size - data) break;
        if (items) {
            size_t index_char;
            if (present >= capacity || name_length + ISD_SUFFIX_ROOM > names_size - pool) return false;
            items[present].header_offset = base + position;
            items[present].data_offset = base + data;
            items[present].size = member_size;
            items[present].name_at = pool;
            items[present].record = index;
            for (index_char = 0U; index_char < name_length; ++index_char) {
                char c = (char)record[index_char];
                names[pool + index_char] = c == '\\' ? '/' : c;
            }
            names[pool + name_length] = 0;
            pool += name_length + ISD_SUFFIX_ROOM;
        }
        name_bytes += name_length;
        ++present;
        position = data + member_size;
    }
    /* Not even the first record is intact: nothing identifies the table. */
    if (present == 0U) return false;
    if (items && present != capacity) return false;
    layout->declared = count;
    layout->present = present;
    layout->name_bytes = name_bytes;
    layout->chain_end = position;
    layout->damaged = present != count;
    layout->has_certificate = false;
    if (layout->damaged) {
        /* A damaged tail still belongs to the launcher. */
        layout->format_size = size;
    } else {
        layout->format_size = position;
        if (layout->certificate_size >= 8 && layout->certificate_offset >= position && layout->certificate_offset - position < ISD_CERTIFICATE_ALIGN &&
            layout->certificate_offset <= size && layout->certificate_size <= size - layout->certificate_offset) {
            layout->has_certificate = true;
            layout->format_size = layout->certificate_offset + layout->certificate_size;
        }
    }
    return true;
}

static bool isd_measure(Abstractformat *format, isd_layout *layout, int64_t *size_out, xx_pd_struct *pd)
{
    int64_t total, size;
    if (!format || !format->device || !layout || format->base_address < 0) return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(ISD_DOS_HEADER_SIZE + ISD_HEADER_SIZE + ISD_RECORD_SIZE) || !isd_locate_overlay(format->device, format->base_address, size, layout) ||
        !isd_walk(format->device, format->base_address, size, layout, NULL, NULL, 0U, pd))
        return false;
    if (size_out) *size_out = size;
    return true;
}

/* ---- member names ------------------------------------------------------ */

/* Fold a raw name byte the way a case-insensitive Windows volume compares
 * it: ASCII and the Windows-1252 letter pairs, with the undefined bytes
 * folded onto the '_' they decode to. */
static uint8_t isd_fold(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 0x20U);
    if (c == 0x8AU || c == 0x8CU || c == 0x8EU) return (uint8_t)(c + 0x10U);
    if (c == 0x9FU) return 0xFFU;
    if (c >= 0xC0U && c <= 0xDEU && c != 0xD7U) return (uint8_t)(c + 0x20U);
    if (c == 0x81U || c == 0x8DU || c == 0x8FU || c == 0x90U || c == 0x9DU) return (uint8_t)'_';
    return c;
}

/* Three-way comparison of two names under isd_fold. */
static int isd_compare_names(const char *left, const char *right)
{
    for (;;) {
        uint8_t a, b;
        if (*left == *right) {
            /* Equal bytes fold equally: skip the fold on common prefixes. */
            if (*left == 0) return 0;
            ++left;
            ++right;
            continue;
        }
        a = isd_fold((uint8_t)*left);
        b = isd_fold((uint8_t)*right);
        if (a != b) return a < b ? -1 : 1;
        if (a == 0U) return 0;
        ++left;
        ++right;
    }
}

/* Stable bottom-up merge sort of member indices by folded name; `scratch`
 * holds `count` entries.  O(n log n) comparisons whatever the names are,
 * so crafted names cannot make the dedupe quadratic. */
static void isd_sort_by_name(uint32_t *order, uint32_t *scratch, size_t count, const isd_member *items, const char *names)
{
    size_t width;
    uint32_t *from = order, *to = scratch;
    for (width = 1U; width < count; width *= 2U) {
        size_t start;
        for (start = 0U; start < count; start += 2U * width) {
            size_t middle = start + width < count ? start + width : count;
            size_t end = middle + width < count ? middle + width : count;
            size_t left = start, right = middle, out = start;
            while (left < middle && right < end) {
                if (isd_compare_names(names + items[from[right]].name_at, names + items[from[left]].name_at) < 0) to[out++] = from[right++];
                else to[out++] = from[left++];
            }
            while (left < middle) to[out++] = from[left++];
            while (right < end) to[out++] = from[right++];
        }
        {
            uint32_t *swap = from;
            from = to;
            to = swap;
        }
    }
    if (from != order) {
        size_t index;
        for (index = 0U; index < count; ++index) order[index] = from[index];
    }
}

/* Whether `name` equals (folded) any original name; `order` is sorted. */
static bool isd_sorted_contains(const uint32_t *order, size_t count, const isd_member *items, const char *names, const char *name)
{
    size_t low = 0U, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        int order_cmp = isd_compare_names(names + items[order[middle]].name_at, name);
        if (order_cmp == 0) return true;
        if (order_cmp < 0) low = middle + 1U;
        else high = middle;
    }
    return false;
}

static size_t isd_put_decimal(char *out, uint32_t value)
{
    char digits[10];
    size_t count = 0U, length = 0U;
    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U) out[length++] = digits[--count];
    return length;
}

/* Append " (<record>)" or, for attempt > 0, " (<record>_<attempt>)". */
static size_t isd_put_suffix(char *out, uint32_t record, uint32_t attempt)
{
    size_t at = 0U;
    out[at++] = ' ';
    out[at++] = '(';
    at += isd_put_decimal(out + at, record + 1U);
    if (attempt > 0U) {
        out[at++] = '_';
        at += isd_put_decimal(out + at, attempt);
    }
    out[at++] = ')';
    out[at] = 0;
    return at;
}

/* Of the members whose names a Windows volume would treat as equal, the
 * first in record order keeps its name; each later one gets
 * " (<record>)", or " (<record>_<n>)" when that equals any original name.
 * Candidates for different records never coincide (the text after the last
 * '(' spells the record), and candidates avoid every original name, so the
 * result is unique.  Each original name blocks at most one candidate in the
 * whole archive, so the retries stay below the member count.  Cost is a
 * merge sort plus one binary search per candidate: O(n log n) name
 * comparisons for any input. */
static bool isd_make_names_unique(isd_member *items, size_t count, char *names)
{
    uint32_t *order, *scratch;
    char candidate[ISD_NAME_FIELD + ISD_SUFFIX_ROOM];
    size_t index, retries = 0U;
    if (count < 2U) return true;
    if (count > ISD_MAX_FILES) return false;
    order = (uint32_t *)xx_mem_alloc(count * 2U * sizeof(uint32_t));
    if (!order) return false;
    scratch = order + count;
    for (index = 0U; index < count; ++index) order[index] = (uint32_t)index;
    isd_sort_by_name(order, scratch, count, items, names);
    /* scratch[i] = attempt + 1 for a member to rename, 0 to keep. */
    xx_rt_memset(scratch, 0, count * sizeof(uint32_t));
    for (index = 1U; index < count; ++index) {
        /* Stable sort: within a run of equal names, record order holds. */
        if (isd_compare_names(names + items[order[index - 1U]].name_at, names + items[order[index]].name_at) == 0) scratch[order[index]] = 1U;
    }
    for (index = 0U; index < count; ++index) {
        const char *name = names + items[index].name_at;
        size_t length = xx_str_len(name);
        uint32_t attempt = 0U;
        if (scratch[index] == 0U) continue;
        if (length >= ISD_NAME_FIELD) {
            xx_mem_free(order);
            return false;
        }
        xx_rt_memcpy(candidate, name, length);
        for (;;) {
            isd_put_suffix(candidate + length, items[index].record, attempt);
            if (!isd_sorted_contains(order, count, items, names, candidate)) break;
            if (++retries > count) {
                xx_mem_free(order);
                return false;
            }
            ++attempt;
        }
        scratch[index] = attempt + 1U;
    }
    /* Only now rewrite the pool, which the searches above read. */
    for (index = 0U; index < count; ++index) {
        char *name = names + items[index].name_at;
        if (scratch[index] == 0U) continue;
        isd_put_suffix(name + xx_str_len(name), items[index].record, scratch[index] - 1U);
    }
    xx_mem_free(order);
    return true;
}

/* Raw ANSI name (Windows-1252) to UTF-8. */
static bool isd_name_to_utf8(const char *raw, char *out, size_t out_size)
{
    size_t used = 0U;
    for (; *raw; ++raw) {
        uint32_t code = (uint8_t)*raw;
        if (code >= 0x80U && code < 0xA0U) {
            code = isd_cp1252_high[code - 0x80U];
            if (code == 0U) code = '_';
        }
        if (code < 0x80U) {
            if (used + 1U >= out_size) return false;
            out[used++] = (char)code;
        } else if (code < 0x800U) {
            if (used + 2U >= out_size) return false;
            out[used++] = (char)(0xC0U | (code >> 6U));
            out[used++] = (char)(0x80U | (code & 0x3FU));
        } else {
            if (used + 3U >= out_size) return false;
            out[used++] = (char)(0xE0U | (code >> 12U));
            out[used++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[used++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    out[used] = 0;
    return true;
}

static char isd_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool isd_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || isd_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* One path component of a decoded name, `length` bytes at `name`.  Refused:
 * empty, "." and "..", a trailing dot or space (Windows strips them, so the
 * component would land on another name), control bytes, the characters
 * Windows reserves (which includes the drive colon), and device names such
 * as CON, LPT1.TXT, COM¹ or CONIN$, with or without an extension and in any
 * case. */
static bool isd_safe_component(const char *name, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index, stem = 0U;
    if (length == 0U || name[length - 1U] == '.' || name[length - 1U] == ' ') return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c < 0x20U || c == 0x7FU || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (isd_stem_is(name, stem, devices[index])) return false;
    if ((stem == 4U || stem == 5U) && ((isd_upper(name[0]) == 'C' && isd_upper(name[1]) == 'O' && isd_upper(name[2]) == 'M') ||
                                       (isd_upper(name[0]) == 'L' && isd_upper(name[1]) == 'P' && isd_upper(name[2]) == 'T'))) {
        /* COM0..COM9 / LPT0..LPT9, and the superscript ¹ ² ³ forms
         * (UTF-8 C2 B9, C2 B2, C2 B3) Windows also maps to devices. */
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return false;
        if (stem == 5U && (uint8_t)name[3] == 0xC2U && ((uint8_t)name[4] == 0xB9U || (uint8_t)name[4] == 0xB2U || (uint8_t)name[4] == 0xB3U)) return false;
    }
    return true;
}

/* A decoded name is a relative path with '/' separators; every component
 * must be safe, which also rules out a leading '/' and empty components. */
static bool isd_safe_output_name(const char *name)
{
    size_t start = 0U, index = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        if (name[index] == '/' || name[index] == 0) {
            if (!isd_safe_component(name + start, index - start)) return false;
            if (name[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

/* ---- parsing into a member table --------------------------------------- */

static void isd_stream_free(void *opaque)
{
    isd_stream *stream = (isd_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->names) xx_mem_free(stream->names);
    xx_mem_free(stream);
}

static bool isd_parse(Abstractformat *format, isd_stream **result, xx_pd_struct *pd)
{
    isd_stream *stream;
    int64_t size;
    uint64_t pool;
    if (!result) return false;
    *result = NULL;
    stream = (isd_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!isd_measure(format, &stream->layout, &size, pd)) goto fail;
    /* Sized by the first walk, which only counted what the file holds. */
    pool = stream->layout.name_bytes + (uint64_t)stream->layout.present * ISD_SUFFIX_ROOM;
    if (pool > (uint64_t)((size_t)-1)) goto fail;
    stream->names_size = (size_t)pool;
    stream->items = (isd_member *)xx_mem_calloc(stream->layout.present, sizeof(isd_member));
    stream->names = (char *)xx_mem_alloc(stream->names_size);
    if (!stream->items || !stream->names || !isd_walk(format->device, format->base_address, size, &stream->layout, stream->items, stream->names, stream->names_size, pd))
        goto fail;
    stream->count = stream->layout.present;
    if (!isd_make_names_unique(stream->items, stream->count, stream->names)) goto fail;
    *result = stream;
    return true;
fail:
    isd_stream_free(stream);
    return false;
}

/* Stream `size` bytes at `offset` into `destination` (or just read them
 * through when it is NULL) in fixed chunks. */
static bool isd_copy_range(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t file_io_capacity = gb_installshield_developer_capacity();
    uint8_t *buffer;
    int64_t remaining = size;
    bool ok = true;
    if (!source || offset < 0 || size < 0) return false;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(file_io_capacity);
    if (!buffer) return false;
    while (remaining > 0) {
        size_t chunk = remaining > (int64_t)file_io_capacity ? (size_t)file_io_capacity : (size_t)remaining;
        size_t written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !isd_read_at(source, offset + (size - remaining), buffer, chunk)) {
            ok = false;
            break;
        }
        while (destination && written < chunk) {
            ssize_t amount = gb_installshield_developer_write(destination, buffer + written, chunk - written, file_io_capacity);
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

static bool isd_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *isd_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool isd_set_record(xx_archive_record *record, const isd_stream *stream, const isd_member *member)
{
    char name[ISD_UTF8_NAME_MAX];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!isd_name_to_utf8(stream->names + member->name_at, name, sizeof(name))) return false;
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)ISD_RECORD_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_installshield_developer_init(xx_installshield_developer *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_DEVELOPER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msdownload");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_installshield_developer_check_is_valid;
    archive->format.handle_base_info = xx_installshield_developer_handle_base_info;
    archive->format.get_format_size = xx_installshield_developer_get_format_size;
    archive->format.get_number_of_archive_records = xx_installshield_developer_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_installshield_developer_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_installshield_developer_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_installshield_developer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_installshield_developer_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_installshield_developer_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->payload_end = -1;
}

xx_installshield_developer *xx_installshield_developer_create(xx_io_device *device, int64_t base_address)
{
    xx_installshield_developer *archive = (xx_installshield_developer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_developer_init(archive, device, base_address);
    return archive;
}

void xx_installshield_developer_destroy(xx_installshield_developer *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_developer_free(xx_installshield_developer *archive)
{
    if (!archive) return;
    xx_installshield_developer_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_developer_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    isd_layout layout;
    return isd_measure(format, &layout, NULL, pd);
}

bool xx_installshield_developer_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    isd_layout layout;
    xx_installshield_developer *archive;
    if (!format || !isd_measure(format, &layout, NULL, pd)) return false;
    archive = (xx_installshield_developer *)format;
    archive->number_of_records = layout.present;
    archive->payload_offset = layout.overlay;
    archive->payload_end = layout.chain_end;
    archive->declared_count = layout.declared;
    archive->damaged = layout.damaged;
    archive->has_certificate = layout.has_certificate;
    format->number_of_archive_records = layout.present;
    format->format_size = layout.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_developer_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_installshield_developer_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_installshield_developer_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_installshield_developer_handle_base_info(format, pd)) ? ((xx_installshield_developer *)format)->number_of_records
                                                                                                            : 0U;
}

xx_archive_record_state *xx_installshield_developer_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    isd_stream *stream;
    xx_archive_record_state *state;
    if (!isd_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        isd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = isd_stream_free;
    state->total_records = stream->count;
    if (!isd_copy_options(&state->options, options) || !isd_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installshield_developer_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_installshield_developer_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    isd_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (isd_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = isd_set_record(&state->current_record, stream, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_installshield_developer_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    isd_stream *stream;
    const isd_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char name[ISD_UTF8_NAME_MAX];
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (isd_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = isd_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) /* No destination: read the member through, which verifies it. */
        return isd_copy_range(format->device, member->data_offset, member->size, NULL, pd);
    if (!isd_name_to_utf8(stream->names + member->name_at, name, sizeof(name)) || !isd_safe_output_name(name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = isd_copy_range(format->device, member->data_offset, member->size, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_developer_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
