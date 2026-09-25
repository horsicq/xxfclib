/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield 3.x/5.x self-extracting EXE.  xx_installshield_3.h carries
 * the descriptor and record layout.
 *
 * Structure follows the XArchive reference module
 * installers/xis3sfxarchive.cpp (MIT, hors): the descriptor magic, the path
 * cipher, the rule that a PE stub's records start exactly at the end of its
 * image, and the requirement that the record chain end exactly at the
 * descriptor's total size.  The descriptor's string fields, its NE resource
 * location (type 1024, as deark's exe module notes) and the source-directory
 * naming were measured on the reference corpus.
 *
 * Nothing in the executable is run or emulated.  The reader reads the MZ
 * header, then either
 *   PE: the section table (to find the end of the image, where the records
 *       start), one record head there, and a bounded scan of the image for
 *       the descriptor, or
 *   NE: the resource table, whose type-1024 entries are the only descriptor
 *       candidates.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_3/xx_installshield_3.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef INSTALLSHIELD_3
#define XX_INSTALLSHIELD_3_FILE_TYPE XX_FILE_TYPE_INSTALLSHIELD_3
#else
#define XX_INSTALLSHIELD_3_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define IS3_DESC_SIZE XX_INSTALLSHIELD_3_DESCRIPTOR_SIZE
#define IS3_MAX_PATH XX_INSTALLSHIELD_3_MAX_PATH
#define IS3_MAX_RECORDS XX_INSTALLSHIELD_3_MAX_RECORDS
/* Smallest record: a 4-byte path length, one path byte, date, time, size. */
#define IS3_MIN_RECORD 13
/* Sum of all path lengths; real sets stay in the kilobytes.  Caps what the
 * member names may cost. */
#define IS3_MAX_NAME_BYTES (4U * 1024U * 1024U)
/* The descriptor sits inside the stub image; the known stubs are under
 * 0x19000 bytes.  Only this much of a PE image is ever scanned. */
#define IS3_SCAN_LIMIT (4 * 1024 * 1024)
#define IS3_SCAN_CHUNK 0x10000U
/* Descriptor candidates tried per file, PE scan hits or NE resources. */
#define IS3_MAX_CANDIDATES 32U
#define IS3_PE_MAX_SECTIONS 96U
#define IS3_NE_RESOURCE_TYPE 0x8400U /* integer type 1024 */
#define IS3_MAX_RENAME_PASSES 4U
/* Longest path component written, in UTF-8 bytes. */
#define IS3_MAX_COMPONENT 200U
/* Read-ahead window of the record walk; holds a whole record head. */
#define IS3_WINDOW 2048U
/* Candidates that pass every descriptor check get a full walk of the record
 * chain; a real file has exactly one.  Capping the walks bounds what a file
 * stuffed with copies of a descriptor can cost. */
#define IS3_MAX_WALKS 3U

typedef char is3_window_holds_a_record_head
    [(IS3_WINDOW >= 4U + IS3_MAX_PATH + 8U) ? 1 : -1];

static const uint8_t g_is3_magic[8] = {0x94, 0x01, 0x00, 0x00,
                                       0x06, 0x00, 0x00, 0x00};
static const uint8_t g_is3_key[8] = {0xCA, 0xDA, 0x7A, 0x5B,
                                     0x4A, 0x76, 0x3E, 0xA0};

/* The descriptor's obfuscated string fields: offset and size. */
static const uint16_t g_is3_fields[4][2] = {
    {0x01C, 40}, {0x044, 128}, {0x0C4, 80}, {0x114, 128}};
#define IS3_FIELD_SOURCE_DIR 3

typedef struct is3_layout_s {
    int64_t descriptor_offset; /* relative to the base address */
    int64_t data_offset;
    int64_t archive_size;
    uint32_t count;
    bool is_ne;
    uint8_t source_dir[128];
    size_t source_dir_length;
} is3_layout;

typedef struct is3_member_s {
    char *name; /* UTF-8, '/' separated, relative */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
    uint32_t index;
    uint16_t dos_date;
    uint16_t dos_time;
    bool pending; /* to be renamed in the current pass */
    bool refused; /* could not be given a unique name */
} is3_member;

typedef struct is3_stream_s {
    is3_member *items;
    size_t count;
    size_t index;
} is3_stream;

static uint16_t is3_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t is3_le32(const uint8_t *bytes) {
    return (uint32_t)is3_le16(bytes) |
           ((uint32_t)is3_le16(bytes + 2U) << 16U);
}

static bool is3_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Every offset/size pair taken from the file passes through this before it
 * is used to read, allocate or advance. */
static bool is3_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

void xx_installshield_3_decode(uint8_t *data, size_t size) {
    size_t index;
    if (!data) return;
    for (index = 0U; index < size; ++index) {
        unsigned value = (unsigned)(data[index] ^ g_is3_key[index & 7U]);
        unsigned shift = (unsigned)((index + 1U) & 7U);
        if (shift)
            value = ((value << shift) | (value >> (8U - shift))) & 0xFFU;
        data[index] = (uint8_t)value;
    }
}

/* Path and string characters: anything but C0 controls and DEL. */
static bool is3_char_ok(uint8_t c) { return c >= 0x20U && c != 0x7FU; }

/* A fixed field must hold a NUL-terminated string of printable bytes. */
static bool is3_field_ok(const uint8_t *descriptor, unsigned field,
                         uint8_t *decoded, size_t *length) {
    size_t size = g_is3_fields[field][1];
    size_t index;
    xx_rt_memcpy(decoded, descriptor + g_is3_fields[field][0], size);
    xx_installshield_3_decode(decoded, size);
    for (index = 0U; index < size; ++index) {
        if (decoded[index] == 0U) {
            if (length) *length = index;
            return true;
        }
        if (!is3_char_ok(decoded[index])) return false;
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* Member names                                                             */
/* ------------------------------------------------------------------------ */

static uint8_t is3_fold(uint8_t c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 'A');
    return c;
}

static bool is3_is_separator(uint8_t c) { return c == '\\' || c == '/'; }

static char is3_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the component's stem (up to its first dot, trailing spaces
 * dropped) is a Windows device name. */
static bool is3_is_device(const char *component, size_t length) {
    static const char *const names[] = {"CON",    "PRN",     "AUX",
                                        "NUL",    "CONIN$",  "CONOUT$",
                                        "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && component[stem] != '.') ++stem;
    while (stem > 0U && component[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        size_t at = 0U;
        while (at < stem && names[index][at] &&
               is3_upper(component[at]) == names[index][at])
            ++at;
        if (at == stem && names[index][at] == 0) return true;
    }
    if (stem >= 4U &&
        ((is3_upper(component[0]) == 'C' && is3_upper(component[1]) == 'O' &&
          is3_upper(component[2]) == 'M') ||
         (is3_upper(component[0]) == 'L' && is3_upper(component[1]) == 'P' &&
          is3_upper(component[2]) == 'T'))) {
        /* COM0..COM9, LPT0..LPT9 and the superscript-digit forms, which
         * Windows also resolves to devices (UTF-8 C2 B9 / C2 B2 / C2 B3). */
        if (stem == 4U && component[3] >= '0' && component[3] <= '9')
            return true;
        if (stem == 5U && (uint8_t)component[3] == 0xC2U &&
            ((uint8_t)component[4] == 0xB9U ||
             (uint8_t)component[4] == 0xB2U ||
             (uint8_t)component[4] == 0xB3U))
            return true;
    }
    return false;
}

/* Append one path component, made safe for any host file system:
 * reserved punctuation becomes '_', Latin-1 bytes become UTF-8, a trailing
 * dot or space (which Windows would strip, merging two names) becomes '_',
 * a device name gets a '_' prefix, and a component is cut after
 * IS3_MAX_COMPONENT bytes (whole characters), below the 255-character limit
 * of common file systems with room for a rename suffix.  @p out has room for
 * 2 * length + 2 more bytes. */
static void is3_append_component(char *out, size_t *at, const uint8_t *text,
                                 size_t length) {
    size_t start = *at, index;
    if (start != 0U) out[(*at)++] = '/';
    start = *at;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (*at - start + (c >= 0x80U ? 2U : 1U) > IS3_MAX_COMPONENT) break;
        if (c >= 0x80U) {
            out[(*at)++] = (char)(0xC0U | (c >> 6U));
            out[(*at)++] = (char)(0x80U | (c & 0x3FU));
        } else if (c == '<' || c == '>' || c == '"' || c == '|' ||
                   c == '?' || c == '*' || c == ':') {
            out[(*at)++] = '_';
        } else {
            out[(*at)++] = (char)c;
        }
    }
    if (*at > start &&
        (out[*at - 1U] == '.' || out[*at - 1U] == ' '))
        out[*at - 1U] = '_';
    if (is3_is_device(out + start, *at - start)) {
        xx_mem_move(out + start + 1U, out + start, *at - start);
        out[start] = '_';
        ++*at;
    }
}

/* The member name is the stored path relative to the descriptor's source
 * directory - where the stub itself writes the file.  A path outside that
 * directory, or one with a ".." or drive component in its relative part,
 * keeps only its last component.  Empty results are numbered. */
static char *is3_make_name(const is3_layout *layout, const uint8_t *path,
                           size_t length, uint32_t index) {
    size_t source = layout->source_dir_length;
    size_t start = 0U, at = 0U, position;
    bool relative = false;
    char *out;
    if (source != 0U && length > source) {
        size_t k;
        bool match = true;
        for (k = 0U; k < source && match; ++k)
            match = is3_fold(path[k]) == is3_fold(layout->source_dir[k]) ||
                    (is3_is_separator(path[k]) &&
                     is3_is_separator(layout->source_dir[k]));
        if (match) {
            if (is3_is_separator(layout->source_dir[source - 1U])) {
                start = source;
                relative = true;
            } else if (is3_is_separator(path[source])) {
                start = source + 1U;
                relative = true;
            }
        }
    }
    if (relative) {
        /* Refuse a relative part that climbs or names a drive. */
        size_t segment = start;
        for (position = start;; ++position) {
            if (position == length || is3_is_separator(path[position])) {
                size_t part = position - segment;
                if (part == 2U && path[segment] == '.' &&
                    path[segment + 1U] == '.')
                    relative = false;
                if (position == length) break;
                segment = position + 1U;
            } else if (path[position] == ':') {
                relative = false;
            }
        }
    }
    if (!relative) {
        start = length;
        while (start > 0U && !is3_is_separator(path[start - 1U]) &&
               path[start - 1U] != ':')
            --start;
    }
    out = (char *)xx_mem_alloc(3U * (length - start) + 32U);
    if (!out) return NULL;
    position = start;
    while (position < length) {
        size_t end = position;
        while (end < length && !is3_is_separator(path[end])) ++end;
        if (end > position &&
            !(end - position == 1U && path[position] == '.'))
            is3_append_component(out, &at, path + position, end - position);
        position = end + 1U;
    }
    if (at == 0U) {
        at = (size_t)xx_rt_snprintf(out, 32U, "file_%u", (unsigned)index);
        if (at >= 32U) at = 31U;
    }
    out[at] = 0;
    return out;
}

/* Comparison key: ASCII case folded, every non-ASCII byte one class, so
 * names that any file system could treat as equal compare equal. */
static int is3_key_compare(const char *a, const char *b) {
    for (;; ++a, ++b) {
        uint8_t x = (uint8_t)*a, y = (uint8_t)*b;
        if (x >= 0x80U) x = 0x80U;
        if (y >= 0x80U) y = 0x80U;
        x = is3_fold(x);
        y = is3_fold(y);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}

static int is3_member_compare(const void *left, const void *right) {
    const is3_member *a = *(const is3_member *const *)left;
    const is3_member *b = *(const is3_member *const *)right;
    int result = is3_key_compare(a->name, b->name);
    if (result) return result;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* "dir/NAME.EXT" -> "dir/NAME_<index>[_<pass>].EXT". */
static bool is3_rename(is3_member *member, unsigned pass) {
    char suffix[32];
    size_t length = xx_str_len(member->name);
    size_t last = length, dot = length, suffix_length;
    char *grown;
    while (last > 0U && member->name[last - 1U] != '/') --last;
    {
        size_t k;
        for (k = length; k > last + 1U; --k)
            if (member->name[k - 1U] == '.') {
                dot = k - 1U;
                break;
            }
    }
    if (pass == 0U)
        suffix_length = (size_t)xx_rt_snprintf(suffix, sizeof(suffix), "_%u",
                                               (unsigned)member->index);
    else
        suffix_length = (size_t)xx_rt_snprintf(
            suffix, sizeof(suffix), "_%u_%u", (unsigned)member->index, pass);
    if (suffix_length >= sizeof(suffix)) return false;
    grown = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!grown) return false;
    xx_rt_memcpy(grown, member->name, dot);
    xx_rt_memcpy(grown + dot, suffix, suffix_length);
    xx_rt_memcpy(grown + dot + suffix_length, member->name + dot,
                 length - dot);
    grown[length + suffix_length] = 0;
    xx_mem_free(member->name);
    member->name = grown;
    return true;
}

/* Compare @p name with "<directory>/" under the same key as
 * is3_key_compare; with @p prefix_only, only the first
 * length(directory) + 1 bytes of @p name take part. */
static int is3_key_compare_dir(const char *name, const char *directory,
                               bool prefix_only) {
    const char *at = directory;
    bool slash_done = false;
    for (;; ++name) {
        uint8_t x = (uint8_t)*name, y;
        if (*at) {
            y = (uint8_t)*at++;
        } else if (!slash_done) {
            y = '/';
            slash_done = true;
        } else {
            if (prefix_only) return 0;
            y = 0U;
        }
        if (x >= 0x80U) x = 0x80U;
        if (y >= 0x80U) y = 0x80U;
        x = is3_fold(x);
        y = is3_fold(y);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}

/* True when some member's name lies below @p order[at]'s name, which would
 * need that name as a directory.  @p order is sorted by key, so the names
 * that start with "<name>/" form one run; a binary search finds its head. */
static bool is3_has_descendant(is3_member *const *order, size_t count,
                               size_t at) {
    const char *directory = order[at]->name;
    size_t low = 0U, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (is3_key_compare_dir(order[middle]->name, directory, false) < 0)
            low = middle + 1U;
        else
            high = middle;
    }
    return low < count &&
           is3_key_compare_dir(order[low]->name, directory, true) == 0;
}

/* Rename members until every name differs from every other and no name is
 * also needed as a directory by another member: every later member of a
 * group of equal names, and every member some other name lies below, is
 * renamed.  Whatever still conflicts after the last pass is refused, so no
 * member can ever overwrite, or block, another. */
static bool is3_make_unique(is3_stream *stream) {
    is3_member **order;
    unsigned pass;
    size_t index;
    bool clean = false;
    if (stream->count < 2U) return true;
    order = (is3_member **)xx_mem_alloc(stream->count * sizeof(*order));
    if (!order) return false;
    for (pass = 0U; pass <= IS3_MAX_RENAME_PASSES && !clean; ++pass) {
        for (index = 0U; index < stream->count; ++index) {
            order[index] = &stream->items[index];
            order[index]->pending = false;
        }
        xx_rt_qsort(order, stream->count, sizeof(*order),
                    is3_member_compare);
        clean = true;
        for (index = 0U; index < stream->count; ++index) {
            if ((index > 0U && is3_key_compare(order[index]->name,
                                               order[index - 1U]->name) ==
                                   0) ||
                is3_has_descendant(order, stream->count, index)) {
                order[index]->pending = true;
                clean = false;
            }
        }
        if (clean) break;
        for (index = 0U; index < stream->count; ++index) {
            if (!order[index]->pending) continue;
            if (pass == IS3_MAX_RENAME_PASSES) {
                order[index]->refused = true;
            } else if (!is3_rename(order[index], pass)) {
                xx_mem_free(order);
                return false;
            }
        }
    }
    xx_mem_free(order);
    return true;
}

/* A final check before a name reaches the file system. */
static bool is3_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.') ||
                is3_is_device(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        } else if (c < 0x20U || c == '\\' || c == ':') {
            return false;
        }
    }
}

static void is3_stream_free(void *opaque) {
    is3_stream *stream = (is3_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Records                                                                  */
/* ------------------------------------------------------------------------ */

/* A small read-ahead window over the record chain, so that a chain of many
 * short records costs one read per window rather than two per record.  It
 * holds any whole record head (4 + IS3_MAX_PATH + 8 bytes). */
typedef struct is3_window_s {
    xx_io_device *device;
    int64_t base;
    int64_t start; /* relative offset of buffer[0]; -1 when empty */
    size_t length;
    uint8_t buffer[IS3_WINDOW];
} is3_window;

/* @p size bytes at relative @p offset, which the caller has checked to lie
 * below @p limit; a reload reads at most IS3_WINDOW bytes, never past
 * @p limit. */
static const uint8_t *is3_window_get(is3_window *window, int64_t limit,
                                     int64_t offset, size_t size) {
    int64_t want;
    if (size > IS3_WINDOW || offset < 0) return NULL;
    if (window->start >= 0 && offset >= window->start &&
        (uint64_t)(offset - window->start) + size <= window->length)
        return window->buffer + (size_t)(offset - window->start);
    want = limit - offset;
    if (want > (int64_t)IS3_WINDOW) want = (int64_t)IS3_WINDOW;
    if (want < (int64_t)size) return NULL;
    window->start = -1;
    if (!is3_read_at(window->device, window->base + offset, window->buffer,
                     (size_t)want))
        return NULL;
    window->start = offset;
    window->length = (size_t)want;
    return window->buffer;
}

/* Walk the record chain.  With @p stream, also collect the members. */
static bool is3_walk(xx_io_device *device, int64_t base,
                     const is3_layout *layout, is3_stream *stream,
                     xx_pd_struct *pd) {
    uint8_t buffer[IS3_MAX_PATH + 8U];
    is3_window window;
    const uint8_t *view;
    int64_t position = layout->data_offset;
    int64_t end = layout->archive_size;
    uint64_t name_bytes = 0U;
    uint32_t index;
    window.device = device;
    window.base = base;
    window.start = -1;
    window.length = 0U;
    if (stream) {
        stream->items = (is3_member *)xx_mem_calloc(layout->count,
                                                    sizeof(is3_member));
        if (!stream->items) return false;
    }
    for (index = 0U; index < layout->count; ++index) {
        uint32_t length, size;
        size_t k;
        int64_t data;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!is3_range_within(end, position, IS3_MIN_RECORD) ||
            !(view = is3_window_get(&window, end, position, 4U)))
            return false;
        length = is3_le32(view);
        if (length == 0U || length > IS3_MAX_PATH ||
            !is3_range_within(end, position + 4, (int64_t)length + 8) ||
            !(view = is3_window_get(&window, end, position + 4,
                                    (size_t)length + 8U)))
            return false;
        xx_rt_memcpy(buffer, view, (size_t)length + 8U);
        name_bytes += length;
        if (name_bytes > IS3_MAX_NAME_BYTES) return false;
        xx_installshield_3_decode(buffer, length);
        for (k = 0U; k < length; ++k)
            if (!is3_char_ok(buffer[k])) return false;
        size = is3_le32(buffer + length + 4U);
        data = position + 4 + (int64_t)length + 8;
        if (!is3_range_within(end, data, (int64_t)size)) return false;
        if (stream) {
            is3_member *member = &stream->items[stream->count];
            member->name = is3_make_name(layout, buffer, length, index);
            if (!member->name) return false;
            member->header_offset = base + position;
            member->header_size = data - position;
            member->data_offset = base + data;
            member->size = (int64_t)size;
            member->index = index;
            member->dos_date = is3_le16(buffer + length);
            member->dos_time = is3_le16(buffer + length + 2U);
            ++stream->count;
        }
        position = data + (int64_t)size;
    }
    return position == end;
}

/* Validate a descriptor candidate at @p offset.  The records must start at
 * @p data_exact when it is not negative, and never before @p data_min.
 * @p walks counts the record walks spent on this file. */
static bool is3_check_descriptor(xx_io_device *device, int64_t base,
                                 int64_t available, int64_t offset,
                                 int64_t data_min, int64_t data_exact,
                                 bool is_ne, is3_layout *layout,
                                 unsigned *walks, xx_pd_struct *pd) {
    uint8_t descriptor[IS3_DESC_SIZE];
    uint8_t decoded[128];
    is3_layout candidate;
    unsigned field;
    if (!is3_range_within(available, offset, IS3_DESC_SIZE) ||
        !is3_read_at(device, base + offset, descriptor, sizeof(descriptor)) ||
        xx_rt_memcmp(descriptor, g_is3_magic, sizeof(g_is3_magic)) != 0)
        return false;
    xx_mem_zero(&candidate, sizeof(candidate));
    candidate.descriptor_offset = offset;
    candidate.data_offset = (int64_t)is3_le32(descriptor + 0x08);
    candidate.count = is3_le32(descriptor + 0x0C);
    candidate.archive_size = (int64_t)is3_le32(descriptor + 0x10);
    candidate.is_ne = is_ne;
    if ((data_exact >= 0 && candidate.data_offset != data_exact) ||
        candidate.data_offset < data_min || candidate.count == 0U ||
        candidate.count > IS3_MAX_RECORDS ||
        candidate.archive_size > available ||
        candidate.archive_size <= candidate.data_offset ||
        (uint64_t)(candidate.archive_size - candidate.data_offset) /
                IS3_MIN_RECORD <
            candidate.count)
        return false;
    for (field = 0U; field < 4U; ++field) {
        size_t length = 0U;
        if (!is3_field_ok(descriptor, field, decoded, &length)) return false;
        if (field == IS3_FIELD_SOURCE_DIR) {
            xx_rt_memcpy(candidate.source_dir, decoded, length);
            candidate.source_dir_length = length;
        }
    }
    if (*walks >= IS3_MAX_WALKS) return false;
    ++*walks;
    if (!is3_walk(device, base, &candidate, NULL, pd)) return false;
    *layout = candidate;
    return true;
}

/* The first record head at the end of a PE image: a cheap gate that turns
 * away every PE without this payload before the image is scanned. */
static bool is3_first_record_ok(xx_io_device *device, int64_t base,
                                int64_t available, int64_t position) {
    uint8_t buffer[IS3_MAX_PATH + 8U];
    uint8_t head[4];
    uint32_t length;
    size_t k;
    if (!is3_range_within(available, position, IS3_MIN_RECORD) ||
        !is3_read_at(device, base + position, head, sizeof(head)))
        return false;
    length = is3_le32(head);
    if (length == 0U || length > IS3_MAX_PATH ||
        !is3_range_within(available, position + 4, (int64_t)length + 8) ||
        !is3_read_at(device, base + position + 4, buffer,
                     (size_t)length + 8U))
        return false;
    xx_installshield_3_decode(buffer, length);
    for (k = 0U; k < length; ++k)
        if (!is3_char_ok(buffer[k])) return false;
    return is3_range_within(available, position + 4 + (int64_t)length + 8,
                            (int64_t)is3_le32(buffer + length + 4U));
}

static bool is3_locate_pe(xx_io_device *device, int64_t base,
                          int64_t available, int64_t header,
                          is3_layout *layout, xx_pd_struct *pd) {
    uint8_t coff[24];
    uint8_t optional[64];
    uint8_t sections[IS3_PE_MAX_SECTIONS * 40U];
    uint8_t *chunk = NULL;
    uint32_t section_count, optional_size, index;
    uint16_t optional_magic;
    int64_t section_table, image_end, scan_end, position;
    unsigned tried = 0U, walks = 0U;
    bool found = false;
    if (!is3_range_within(available, header, (int64_t)sizeof(coff)) ||
        !is3_read_at(device, base + header, coff, sizeof(coff)))
        return false;
    section_count = is3_le16(coff + 6U);
    optional_size = is3_le16(coff + 20U);
    if (section_count == 0U || section_count > IS3_PE_MAX_SECTIONS ||
        optional_size < sizeof(optional) ||
        !is3_range_within(available, header + 24, (int64_t)sizeof(optional)) ||
        !is3_read_at(device, base + header + 24, optional, sizeof(optional)))
        return false;
    optional_magic = is3_le16(optional);
    if (optional_magic != 0x10BU && optional_magic != 0x20BU) return false;
    section_table = header + 24 + (int64_t)optional_size;
    if (!is3_range_within(available, section_table,
                          (int64_t)section_count * 40) ||
        !is3_read_at(device, base + section_table, sections,
                     (size_t)section_count * 40U))
        return false;
    image_end = (int64_t)is3_le32(optional + 60U);
    for (index = 0U; index < section_count; ++index) {
        const uint8_t *entry = sections + (size_t)index * 40U;
        int64_t raw_size = (int64_t)is3_le32(entry + 16U);
        int64_t raw_offset = (int64_t)is3_le32(entry + 20U);
        if (raw_size == 0) continue;
        if (!is3_range_within(available, raw_offset, raw_size)) return false;
        if (raw_offset + raw_size > image_end)
            image_end = raw_offset + raw_size;
    }
    /* The records start exactly where the image ends. */
    if (image_end < (int64_t)IS3_DESC_SIZE ||
        !is3_first_record_ok(device, base, available, image_end))
        return false;

    scan_end = image_end < IS3_SCAN_LIMIT ? image_end : IS3_SCAN_LIMIT;
    chunk = (uint8_t *)xx_mem_alloc(IS3_SCAN_CHUNK + 8U);
    if (!chunk) return false;
    /* Chunks overlap by 7 bytes so a magic across a boundary is seen. */
    for (position = 0; position + 8 <= scan_end && !found;
         position += IS3_SCAN_CHUNK) {
        int64_t want = scan_end - position;
        size_t amount, at;
        if (want > (int64_t)IS3_SCAN_CHUNK + 7) want = IS3_SCAN_CHUNK + 7;
        amount = (size_t)want;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!is3_read_at(device, base + position, chunk, amount)) break;
        for (at = 0U; at + 8U <= amount && !found; ++at) {
            if (chunk[at] != 0x94U ||
                xx_rt_memcmp(chunk + at, g_is3_magic, 8U) != 0)
                continue;
            if (++tried > IS3_MAX_CANDIDATES) {
                position = scan_end;
                break;
            }
            found = is3_check_descriptor(device, base, available,
                                         position + (int64_t)at,
                                         (int64_t)at + position +
                                             IS3_DESC_SIZE,
                                         image_end, false, layout, &walks,
                                         pd);
        }
    }
    xx_mem_free(chunk);
    return found;
}

static bool is3_locate_ne(xx_io_device *device, int64_t base,
                          int64_t available, int64_t header,
                          is3_layout *layout, xx_pd_struct *pd) {
    uint8_t ne[0x40];
    uint8_t *table;
    uint32_t resource_table, resident_names, table_size, shift;
    size_t position = 2U;
    unsigned tried = 0U, walks = 0U;
    bool found = false;
    if (!is3_range_within(available, header, (int64_t)sizeof(ne)) ||
        !is3_read_at(device, base + header, ne, sizeof(ne)))
        return false;
    resource_table = is3_le16(ne + 0x24U);
    resident_names = is3_le16(ne + 0x26U);
    if (resource_table == 0U || resident_names <= resource_table) return false;
    table_size = resident_names - resource_table;
    if (table_size < 2U + 8U + 12U ||
        !is3_range_within(available, header + (int64_t)resource_table,
                          (int64_t)table_size))
        return false;
    table = (uint8_t *)xx_mem_alloc(table_size);
    if (!table) return false;
    if (!is3_read_at(device, base + header + (int64_t)resource_table, table,
                     table_size)) {
        xx_mem_free(table);
        return false;
    }
    shift = is3_le16(table);
    if (shift > 15U) {
        xx_mem_free(table);
        return false;
    }
    /* TYPEINFO { u16 type, u16 count, u32 0, NAMEINFO[count] } until a zero
     * type; NAMEINFO { u16 offset, u16 length, u16 flags, u16 id, u32 0 }
     * with offset and length in (1 << shift) units.  Each step advances at
     * least 8 bytes through the table, so the walk is bounded by it. */
    while (!found && position + 8U <= table_size) {
        uint32_t type = is3_le16(table + position);
        uint32_t count = is3_le16(table + position + 2U);
        uint32_t k;
        if (type == 0U) break;
        position += 8U;
        if (count > (table_size - position) / 12U) break;
        for (k = 0U; k < count && !found; ++k) {
            const uint8_t *entry = table + position + (size_t)k * 12U;
            int64_t offset = (int64_t)is3_le16(entry) << shift;
            int64_t length = (int64_t)is3_le16(entry + 2U) << shift;
            if (type != IS3_NE_RESOURCE_TYPE || length < IS3_DESC_SIZE)
                continue;
            if (++tried > IS3_MAX_CANDIDATES) break;
            found = is3_check_descriptor(device, base, available, offset,
                                         offset + IS3_DESC_SIZE, -1, true,
                                         layout, &walks, pd);
        }
        if (tried > IS3_MAX_CANDIDATES) break;
        position += (size_t)count * 12U;
    }
    xx_mem_free(table);
    return found;
}

static bool is3_locate(Abstractformat *format, is3_layout *layout,
                       xx_pd_struct *pd) {
    uint8_t mz[0x40];
    uint8_t signature[4];
    int64_t total, available, header;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    available = total - format->base_address;
    if (available < 0x40 + IS3_DESC_SIZE + IS3_MIN_RECORD ||
        !is3_read_at(format->device, format->base_address, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z')
        return false;
    header = (int64_t)is3_le32(mz + 0x3CU);
    if (header < 0x40 ||
        !is3_range_within(available, header, (int64_t)sizeof(signature)) ||
        !is3_read_at(format->device, format->base_address + header,
                     signature, sizeof(signature)))
        return false;
    if (signature[0] == 'P' && signature[1] == 'E' && signature[2] == 0U &&
        signature[3] == 0U)
        return is3_locate_pe(format->device, format->base_address, available,
                             header, layout, pd);
    if (signature[0] == 'N' && signature[1] == 'E')
        return is3_locate_ne(format->device, format->base_address, available,
                             header, layout, pd);
    return false;
}

static bool is3_parse(Abstractformat *format, is3_layout *layout,
                      is3_stream **result, xx_pd_struct *pd) {
    is3_stream *stream;
    if (!is3_locate(format, layout, pd)) return false;
    if (!result) return true;
    stream = (is3_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!is3_walk(format->device, format->base_address, layout, stream, pd) ||
        stream->count != layout->count || !is3_make_unique(stream)) {
        is3_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Archive API                                                              */
/* ------------------------------------------------------------------------ */

static bool is3_copy_options(xx_list_s *destination,
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

static const xx_var *is3_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool is3_set_record(xx_archive_record *record,
                           const is3_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_installshield_3_init(xx_installshield_3 *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_3_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-installshield-3-sfx");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_installshield_3_check_is_valid;
    archive->format.handle_base_info = xx_installshield_3_handle_base_info;
    archive->format.get_format_size = xx_installshield_3_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_3_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_3_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_3_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_3_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_3_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_3_free_archive_records_reading;
    archive->descriptor_offset = -1;
    archive->data_offset = -1;
    archive->archive_size = -1;
}

xx_installshield_3 *xx_installshield_3_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_installshield_3 *archive =
        (xx_installshield_3 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_3_init(archive, device, base_address);
    return archive;
}

void xx_installshield_3_destroy(xx_installshield_3 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_3_free(xx_installshield_3 *archive) {
    if (!archive) return;
    xx_installshield_3_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_3_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    is3_layout layout;
    return is3_parse(format, &layout, NULL, pd);
}

bool xx_installshield_3_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    is3_layout layout;
    xx_installshield_3 *archive;
    if (!format) return false;
    if (!is3_parse(format, &layout, NULL, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_installshield_3 *)format;
    archive->number_of_records = layout.count;
    archive->descriptor_offset =
        format->base_address + layout.descriptor_offset;
    archive->data_offset = format->base_address + layout.data_offset;
    archive->archive_size = layout.archive_size;
    archive->is_ne = layout.is_ne;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_INSTALLSHIELD_3_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_3_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_3_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_installshield_3_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_3_handle_base_info(format, pd))
               ? ((xx_installshield_3 *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_installshield_3_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    is3_layout layout;
    is3_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!is3_parse(format, &layout, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        is3_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = is3_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!is3_copy_options(&state->options, options) ||
        !is3_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installshield_3_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_installshield_3_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    is3_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (is3_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        is3_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_installshield_3_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    is3_stream *stream;
    is3_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (is3_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->refused || !is3_safe_output_name(member->name)) return false;
    path_option = is3_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true; /* A dry run: the member is readable. */
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
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset, member->size,
                                            path, pd);
    if (result && (member->dos_date || member->dos_time))
        (void)xx_store_apply_dos_time_and_attrs_a(path, member->dos_date,
                                                  member->dos_time, 0U);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_3_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
