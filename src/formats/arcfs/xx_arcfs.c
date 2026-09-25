/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Acorn RISC OS ArcFS archive layout.  Its directory
 * table is a pre-order stream: a zero-status entry closes the most recent
 * directory, while bit 31 of the data offset introduces one.
 *
 * Archives that were cut short (a transfer or disk copy that stopped early)
 * keep a complete table.  They are still listed in full; only the members
 * whose data runs past the end of the file fail to unpack.
 *
 * The RISC OS Latin-1 mapping of 0x80..0x9F, the CRC-16 held in the upper
 * half of the attribute word, the run filter's escape rule (an escaped 0x90
 * becomes the byte a following run repeats) and the load/exec timestamp
 * rule follow Deark (modules/arcfs.c, src/fmtutil-rle.c, src/deark-data.c
 * riscostable, src/deark-util.c de_riscos_loadexec_to_timestamp; MIT
 * licence, Copyright (C) 2016-2026 Jason Summers).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arcfs/xx_arcfs.h"

#include "xxfclib/algo/arcfs/xx_arcfs_lzw.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"

#include <limits.h>
#include <stdio.h>

#define ARCFS_HEADER_SIZE 96U
#define ARCFS_ENTRY_SIZE 36U
#define ARCFS_NAME_FIELD 11U
#define ARCFS_MAX_MEMBERS 100000U
#define ARCFS_MAX_DEPTH 64U
#define ARCFS_TABLE_BATCH 64U
/* Longest component: eleven three-byte UTF-8 sequences, a one-byte guard
 * prefix for device names, "~" plus ten serial digits, and the NUL. */
#define ARCFS_COMPONENT_MAX (ARCFS_NAME_FIELD * 3U + 1U + 11U + 1U)
/* RISC OS machines of the ArcFS era could not hold members anywhere near
 * this size; it bounds all buffers one compressed member may need, which
 * is what a hostile table can make us allocate. */
#define ARCFS_MAX_MEMBER_BYTES ((uint64_t)256U * 1024U * 1024U)
/* All member paths together.  A full table nested 64 deep could otherwise
 * spell about 200 MiB of paths from a 3.6 MB file; real archives use a few
 * KiB. */
#define ARCFS_MAX_NAME_BYTES ((uint64_t)16U * 1024U * 1024U)
#define ARCFS_COPY_CHUNK 65536U
#define ARCFS_STATUS_END 0x00U
#define ARCFS_STATUS_DELETED 0x01U
#define ARCFS_METHOD_STORED 0x82U
#define ARCFS_METHOD_PACKED 0x83U
#define ARCFS_METHOD_CRUNCHED 0x88U
#define ARCFS_METHOD_COMPRESSED 0xffU
/* Seconds from 1900-01-01 (RISC OS epoch) to 1970-01-01. */
#define ARCFS_EPOCH_DELTA INT64_C(2208988800)

typedef struct arcfs_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;      /* bytes of member data present in the file */
    int64_t timestamp;        /* Unix seconds, or -1 */
    uint32_t declared_packed; /* packed size the table declares */
    uint32_t original_size;
    uint32_t attributes;
    uint16_t crc;             /* CRC-16/ARC of the plain data, 0 = none */
    uint8_t method;
    uint8_t max_bits;
    bool folder;
    bool truncated;
} arcfs_member;

typedef struct arcfs_stream_s {
    arcfs_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} arcfs_stream;

typedef struct arcfs_name_set_s {
    const char **slots;
    size_t mask;
} arcfs_name_set;

/* RISC OS Latin-1 0x80..0x9F; 0 marks an unassigned code. */
static const uint16_t arcfs_riscos_high[32] = {
    0x20ac, 0x0174, 0x0175, 0x0000, 0x0000, 0x0176, 0x0177, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x2026, 0x2122, 0x2030, 0x2022,
    0x2018, 0x2019, 0x2039, 0x203a, 0x201c, 0x201d, 0x201e, 0x2013,
    0x2014, 0x2212, 0x0152, 0x0153, 0x2020, 0x2021, 0xfb01, 0xfb02
};

/* CRC-16/ARC (reflected 0x8005, init 0), one nibble at a time. */
static const uint16_t arcfs_crc_nibble[16] = {
    0x0000, 0xcc01, 0xd801, 0x1400, 0xf001, 0x3c00, 0x2800, 0xe401,
    0xa001, 0x6c00, 0x7800, 0xb401, 0x5000, 0x9c01, 0x8801, 0x4400
};

static uint16_t arcfs_crc16(uint16_t crc, const uint8_t *data, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index) {
        crc = (uint16_t)((crc >> 4U) ^ arcfs_crc_nibble[(crc ^ data[index]) & 0x0fU]);
        crc = (uint16_t)((crc >> 4U) ^
                         arcfs_crc_nibble[(crc ^ (data[index] >> 4U)) & 0x0fU]);
    }
    return crc;
}

static uint32_t arcfs_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool arcfs_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool arcfs_write_all(xx_io_device *device, const uint8_t *data,
                            size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool arcfs_known_method(uint8_t method) {
    return method == ARCFS_METHOD_STORED || method == ARCFS_METHOD_PACKED ||
           method == ARCFS_METHOD_CRUNCHED || method == ARCFS_METHOD_COMPRESSED;
}

static int64_t arcfs_timestamp(uint32_t load, uint32_t exec) {
    int64_t seconds;
    if ((load & UINT32_C(0xfff00000)) != UINT32_C(0xfff00000)) return -1;
    seconds = ((((int64_t)(load & 0xffU)) << 32) | (int64_t)exec) / 100 -
              ARCFS_EPOCH_DELTA;
    return seconds > 0 && seconds < INT64_C(8000000000) ? seconds : -1;
}

static char arcfs_upper(char value) {
    return value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
}

static size_t arcfs_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xc0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3fU));
        return 2U;
    }
    out[0] = (char)(0xe0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3fU));
    out[2] = (char)(0x80U | (code & 0x3fU));
    return 3U;
}

/* True when the stem (text before the first dot, trailing spaces dropped)
 * is a Windows device name: CON, PRN, AUX, NUL, CONIN$, CONOUT$, CLOCK$,
 * COM0-9, LPT0-9, or COM/LPT followed by a superscript one, two or three. */
static bool arcfs_is_device_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem != 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        const char *device = devices[index];
        size_t at = 0U;
        while (at < stem && device[at] && arcfs_upper(name[at]) == device[at])
            ++at;
        if (at == stem && device[at] == 0) return true;
    }
    if (stem < 4U || stem > 5U ||
        !((arcfs_upper(name[0]) == 'C' && arcfs_upper(name[1]) == 'O' &&
           arcfs_upper(name[2]) == 'M') ||
          (arcfs_upper(name[0]) == 'L' && arcfs_upper(name[1]) == 'P' &&
           arcfs_upper(name[2]) == 'T')))
        return false;
    if (stem == 4U) return name[3] >= '0' && name[3] <= '9';
    return (uint8_t)name[3] == 0xc2U &&
           ((uint8_t)name[4] == 0xb9U || (uint8_t)name[4] == 0xb2U ||
            (uint8_t)name[4] == 0xb3U);
}

/* Converts one 11-byte ArcFS name field into a portable UTF-8 component in
 * @p out (ARCFS_COMPONENT_MAX bytes).  RISC OS spells "name.ext" as
 * "name/ext", so '/' becomes '.'; characters Windows refuses become '_';
 * trailing dots and spaces are dropped and device names gain a '_' prefix.
 * Returns the length, or 0 for an empty field. */
static size_t arcfs_component(const uint8_t *raw, char *out) {
    size_t index, length = 0U, raw_length = 0U;
    while (raw_length < ARCFS_NAME_FIELD && raw[raw_length] != 0U) ++raw_length;
    if (raw_length == 0U) return 0U;
    for (index = 0U; index < raw_length; ++index) {
        uint8_t value = raw[index];
        uint32_t code = value;
        if (value == '/')
            code = '.';
        else if (value < 0x20U || value == 0x7fU || value == '\\' ||
                 value == ':' || value == '<' || value == '>' ||
                 value == '"' || value == '|' || value == '?' || value == '*')
            code = '_';
        else if (value >= 0x80U && value <= 0x9fU)
            code = arcfs_riscos_high[value - 0x80U] != 0U
                       ? arcfs_riscos_high[value - 0x80U] : (uint32_t)'_';
        length += arcfs_put_utf8(out + length, code);
    }
    while (length != 0U && (out[length - 1U] == ' ' || out[length - 1U] == '.'))
        --length;
    if (length == 0U) out[length++] = '_';
    if (arcfs_is_device_name(out, length)) {
        for (index = length; index != 0U; --index) out[index] = out[index - 1U];
        out[0] = '_';
        ++length;
    }
    out[length] = 0;
    return length;
}

/* Next code point of a name this reader built, folded the way Windows
 * compares file names (ASCII, Latin-1 letters and the RISC OS extras). */
static uint32_t arcfs_fold_next(const char *text, size_t *at) {
    const uint8_t *s = (const uint8_t *)text + *at;
    uint32_t code;
    if (s[0] >= 0xc0U && s[0] < 0xe0U && (s[1] & 0xc0U) == 0x80U) {
        code = ((uint32_t)(s[0] & 0x1fU) << 6U) | (uint32_t)(s[1] & 0x3fU);
        *at += 2U;
    } else if (s[0] >= 0xe0U && s[0] < 0xf0U && (s[1] & 0xc0U) == 0x80U &&
               (s[2] & 0xc0U) == 0x80U) {
        code = ((uint32_t)(s[0] & 0x0fU) << 12U) |
               ((uint32_t)(s[1] & 0x3fU) << 6U) | (uint32_t)(s[2] & 0x3fU);
        *at += 3U;
    } else {
        code = s[0];
        *at += 1U;
    }
    if (code >= 'a' && code <= 'z')
        code -= 0x20U;
    else if (code >= 0xe0U && code <= 0xfeU && code != 0xf7U)
        code -= 0x20U;
    else if (code == 0xffU)
        code = 0x178U;
    else if (code == 0x153U || code == 0x175U || code == 0x177U)
        code -= 1U;
    return code;
}

static size_t arcfs_name_hash(const char *name) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t at = 0U;
    while (name[at]) {
        hash ^= arcfs_fold_next(name, &at);
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)(hash ^ (hash >> 29U));
}

static bool arcfs_name_equal(const char *left, const char *right) {
    size_t a = 0U, b = 0U;
    while (left[a] && right[b]) {
        if (arcfs_fold_next(left, &a) != arcfs_fold_next(right, &b))
            return false;
    }
    return left[a] == 0 && right[b] == 0;
}

static bool arcfs_name_set_init(arcfs_name_set *set, size_t members) {
    size_t capacity = 16U;
    while (capacity < members * 2U) capacity <<= 1U;
    set->slots = (const char **)xx_mem_calloc(capacity, sizeof(*set->slots));
    set->mask = capacity - 1U;
    return set->slots != NULL;
}

static bool arcfs_name_set_contains(const arcfs_name_set *set,
                                    const char *name) {
    size_t slot = arcfs_name_hash(name) & set->mask;
    size_t probes;
    for (probes = 0U; probes <= set->mask; ++probes) {
        const char *entry = set->slots[slot];
        if (!entry) return false;
        if (arcfs_name_equal(entry, name)) return true;
        slot = (slot + 1U) & set->mask;
    }
    return false;
}

/* The set is sized for twice the table's entry count, so it never fills. */
static void arcfs_name_set_insert(arcfs_name_set *set, const char *name) {
    size_t slot = arcfs_name_hash(name) & set->mask;
    size_t probes;
    for (probes = 0U; probes <= set->mask; ++probes) {
        if (!set->slots[slot]) {
            set->slots[slot] = name;
            return;
        }
        slot = (slot + 1U) & set->mask;
    }
}

static char *arcfs_join_path(char *const *directories, size_t depth,
                             const char *component) {
    char *result;
    size_t index, length = 0U, at = 0U;
    if (!component || !component[0]) return NULL;
    for (index = 0U; index < depth; ++index) {
        size_t part;
        if (!directories[index] ||
            (part = xx_str_len(directories[index])) > SIZE_MAX - length - 1U)
            return NULL;
        length += part + 1U;
    }
    if (xx_str_len(component) > SIZE_MAX - length - 1U) return NULL;
    length += xx_str_len(component);
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < depth; ++index) {
        size_t part = xx_str_len(directories[index]);
        xx_rt_memcpy(result + at, directories[index], part);
        at += part;
        result[at++] = '/';
    }
    xx_rt_memcpy(result + at, component, xx_str_len(component));
    result[length] = 0;
    return result;
}

/* Builds the member path and, when an earlier member already owns it (the
 * archive repeats a name, or two names became equal after sanitising or
 * case folding), appends "~<serial>" to the component until it is unique.
 * Every failed attempt matches a distinct earlier path, so the loop is
 * bounded by the member count. */
static char *arcfs_unique_path(const arcfs_name_set *set,
                               char *const *directories, size_t depth,
                               char *component, uint32_t *serial) {
    size_t base = xx_str_len(component);
    for (;;) {
        char digits[10];
        size_t count = 0U, index;
        uint32_t value;
        char *path = arcfs_join_path(directories, depth, component);
        if (!path || !arcfs_name_set_contains(set, path)) return path;
        xx_mem_free(path);
        if (*serial == UINT32_MAX) return NULL;
        value = ++*serial;
        do {
            digits[count++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value != 0U);
        component[base] = '~';
        for (index = 0U; index < count; ++index)
            component[base + 1U + index] = digits[count - 1U - index];
        component[base + 1U + count] = 0;
    }
}

static bool arcfs_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value == ':' || value == '<' || value == '>' || value == '"' ||
            value == '|' || value == '?' || value == '*' || value == 0x7fU ||
            (value != 0U && value < 0x20U))
            return false;
        if (value == '/' || value == '\\' || value == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.') ||
                segment[length - 1U] == '.' || segment[length - 1U] == ' ' ||
                arcfs_is_device_name(segment, length))
                return false;
            if (value == 0U) return true;
            segment = at + 1;
        }
    }
}

static void arcfs_stream_free(void *opaque) {
    arcfs_stream *stream = (arcfs_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool arcfs_add_member(arcfs_stream *stream, const arcfs_member *member) {
    if (!stream || !member || stream->count >= ARCFS_MAX_MEMBERS) return false;
    if (stream->count == stream->capacity) {
        /* Geometric growth: a 100000-entry table must not cost 100000
         * reallocations.  ARCFS_MAX_MEMBERS keeps the product small. */
        size_t capacity = stream->capacity ? stream->capacity * 2U : 64U;
        arcfs_member *grown;
        if (capacity > ARCFS_MAX_MEMBERS) capacity = ARCFS_MAX_MEMBERS;
        grown = (arcfs_member *)xx_mem_realloc(stream->items,
                                               capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static bool arcfs_parse(Abstractformat *format, arcfs_stream **result) {
    uint8_t header[ARCFS_HEADER_SIZE];
    uint8_t table[ARCFS_TABLE_BATCH * ARCFS_ENTRY_SIZE];
    char component[ARCFS_COMPONENT_MAX];
    char *directories[ARCFS_MAX_DEPTH] = { NULL };
    arcfs_stream *stream = NULL;
    arcfs_name_set names = { NULL, 0U };
    int64_t total, size, directory_size, data_base, archive_size;
    uint32_t entry_count, entry_index, batch_first = 0U, batch_count = 0U;
    uint32_t serial = 0U;
    uint64_t name_bytes = 0U;
    size_t depth = 0U, sound = 0U;
    bool valid = false;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)ARCFS_HEADER_SIZE ||
        !arcfs_read_at(format->device, format->base_address, header,
                       sizeof(header)) ||
        xx_rt_memcmp(header, "Archive\0", 8U) != 0)
        return false;
    directory_size = (int64_t)arcfs_le32(header + 8U);
    data_base = (int64_t)arcfs_le32(header + 12U);
    if (directory_size <= 0 || directory_size % ARCFS_ENTRY_SIZE != 0 ||
        data_base < (int64_t)ARCFS_HEADER_SIZE || data_base > size ||
        directory_size > size - (int64_t)ARCFS_HEADER_SIZE)
        return false;
    if (directory_size / ARCFS_ENTRY_SIZE > ARCFS_MAX_MEMBERS) return false;
    entry_count = (uint32_t)(directory_size / ARCFS_ENTRY_SIZE);
    stream = (arcfs_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream || !arcfs_name_set_init(&names, entry_count)) goto done;
    archive_size = data_base;
    if ((int64_t)ARCFS_HEADER_SIZE + directory_size > archive_size)
        archive_size = (int64_t)ARCFS_HEADER_SIZE + directory_size;

    for (entry_index = 0U; entry_index < entry_count; ++entry_index) {
        const uint8_t *entry;
        uint8_t status;
        uint32_t raw_offset;
        arcfs_member member;
        int64_t entry_offset = (int64_t)ARCFS_HEADER_SIZE +
                               (int64_t)entry_index * ARCFS_ENTRY_SIZE;
        if (entry_index - batch_first >= batch_count) {
            batch_first = entry_index;
            batch_count = entry_count - entry_index;
            if (batch_count > ARCFS_TABLE_BATCH) batch_count = ARCFS_TABLE_BATCH;
            if (!arcfs_read_at(format->device,
                               format->base_address + entry_offset, table,
                               (size_t)batch_count * ARCFS_ENTRY_SIZE))
                goto done;
        }
        entry = table + (size_t)(entry_index - batch_first) * ARCFS_ENTRY_SIZE;
        status = entry[0];
        if (status == ARCFS_STATUS_DELETED) continue;
        if (status == ARCFS_STATUS_END) {
            /* The root's own end marker; later slots are unused. */
            if (depth == 0U) break;
            xx_mem_free(directories[--depth]);
            directories[depth] = NULL;
            continue;
        }
        if (arcfs_component(entry + 1U, component) == 0U) goto done;
        xx_rt_memset(&member, 0, sizeof(member));
        member.name = arcfs_unique_path(&names, directories, depth, component,
                                        &serial);
        if (!member.name) goto done;
        name_bytes += (uint64_t)xx_str_len(member.name) + 1U;
        if (name_bytes > ARCFS_MAX_NAME_BYTES) {
            xx_mem_free(member.name);
            goto done;
        }
        member.header_offset = format->base_address + entry_offset;
        member.method = status;
        member.original_size = arcfs_le32(entry + 12U);
        member.timestamp = arcfs_timestamp(arcfs_le32(entry + 16U),
                                           arcfs_le32(entry + 20U));
        member.attributes = arcfs_le32(entry + 24U);
        member.max_bits = (uint8_t)((member.attributes >> 8U) & 0xffU);
        member.crc = (uint16_t)(member.attributes >> 16U);
        member.declared_packed = arcfs_le32(entry + 28U);
        raw_offset = arcfs_le32(entry + 32U);
        if (raw_offset & UINT32_C(0x80000000)) {
            size_t length = xx_str_len(component);
            char *copy;
            member.folder = true;
            member.data_offset = -1;
            if (depth >= ARCFS_MAX_DEPTH ||
                !(copy = (char *)xx_mem_alloc(length + 1U))) {
                xx_mem_free(member.name);
                goto done;
            }
            xx_rt_memcpy(copy, component, length + 1U);
            if (!arcfs_add_member(stream, &member)) {
                xx_mem_free(copy);
                xx_mem_free(member.name);
                goto done;
            }
            arcfs_name_set_insert(&names, member.name);
            directories[depth++] = copy;
            ++sound;
            continue;
        }
        {
            uint64_t relative = (uint64_t)data_base +
                                (uint64_t)(raw_offset & UINT32_C(0x7fffffff));
            uint64_t start = relative < (uint64_t)size ? relative : (uint64_t)size;
            uint64_t available = (uint64_t)size - start;
            member.truncated = (uint64_t)member.declared_packed > available;
            member.packed_size = member.truncated
                                     ? (int64_t)available
                                     : (int64_t)member.declared_packed;
            member.data_offset = format->base_address + (int64_t)relative;
            if ((int64_t)start + member.packed_size > archive_size)
                archive_size = (int64_t)start + member.packed_size;
            if (!member.truncated) ++sound;
        }
        if (!arcfs_add_member(stream, &member)) {
            xx_mem_free(member.name);
            goto done;
        }
        arcfs_name_set_insert(&names, member.name);
    }
    /* At least one directory or complete file: a table whose every data
     * pointer lies past the end of the file is not an archive we can use. */
    if (stream->count == 0U || sound == 0U) goto done;
    stream->archive_size = archive_size;
    valid = true;
done:
    while (depth != 0U) {
        xx_mem_free(directories[--depth]);
        directories[depth] = NULL;
    }
    if (names.slots) xx_mem_free((void *)names.slots);
    if (!valid) {
        arcfs_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool arcfs_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *arcfs_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

/* Reads an optional non-negative integer limit; false for a bad value. */
static bool arcfs_limit(const Abstractformat *format, const xx_list_s *options,
                        uint32_t id, uint64_t *limit) {
    const xx_var *value = xx_format_resolve_extra_parameter(format, options, id);
    int64_t signed_value;
    if (!value) return true;
    switch ((xx_var_type_t)value->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64:
            if (xx_var_get_u64(value) < *limit) *limit = xx_var_get_u64(value);
            return true;
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64:
            signed_value = xx_var_get_i64(value);
            if (signed_value < 0) return false;
            if ((uint64_t)signed_value < *limit) *limit = (uint64_t)signed_value;
            return true;
        default:
            return false;
    }
}

/* Upper bound on what @p packed bytes can expand to.  RLE90 turns two
 * bytes into at most 254; the i-th LZW code spells at most min(i, table
 * size) bytes, and a code is at least nine bits wide. */
static uint64_t arcfs_expansion_bound(const arcfs_member *member) {
    uint64_t packed = (uint64_t)member->packed_size;
    uint64_t codes, table, lzw;
    if (member->method == ARCFS_METHOD_STORED) return packed;
    if (member->method == ARCFS_METHOD_PACKED) return packed * 127U + 1U;
    if (member->max_bits < 9U || member->max_bits > 16U) return 0U;
    codes = packed * 8U / 9U + 1U;
    table = UINT64_C(1) << member->max_bits;
    lzw = codes * (codes + 1U) / 2U;
    if (lzw > codes * table) lzw = codes * table;
    return member->method == ARCFS_METHOD_CRUNCHED ? lzw * 127U + 1U : lzw;
}

static void arcfs_set_record_bounds(xx_archive_record *record,
                                    const arcfs_member *member) {
    record->header_offset = member->header_offset;
    record->header_size = ARCFS_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->folder ? 0 : member->packed_size;
}

static bool arcfs_set_record(xx_archive_record *record,
                             const arcfs_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    arcfs_set_record_bounds(record, member);
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        member->folder ? 0U :
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->folder ? 0U :
                                        member->original_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->attributes) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         member->folder))
        return false;
    return member->timestamp < 0 ||
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          (uint64_t)member->timestamp);
}

/* Copies a stored member in chunks, so its size never becomes one buffer. */
static bool arcfs_copy_stored(Abstractformat *format, const arcfs_member *member,
                              xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *chunk;
    int64_t done = 0;
    uint16_t crc = 0U;
    bool result = false;
    if ((uint64_t)member->packed_size != member->original_size) return false;
    chunk = (uint8_t *)xx_mem_alloc(ARCFS_COPY_CHUNK);
    if (!chunk) return false;
    while (done < member->packed_size) {
        size_t amount = member->packed_size - done > (int64_t)ARCFS_COPY_CHUNK
                            ? ARCFS_COPY_CHUNK
                            : (size_t)(member->packed_size - done);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !arcfs_read_at(format->device, member->data_offset + done, chunk,
                           amount) ||
            (destination && !arcfs_write_all(destination, chunk, amount)))
            goto done;
        crc = arcfs_crc16(crc, chunk, amount);
        done += (int64_t)amount;
    }
    result = member->crc == 0U || crc == member->crc;
done:
    xx_mem_free(chunk);
    return result;
}

/* ArcFS's run filter.  0x90 n repeats the previous output byte n-1 more
 * times; 0x90 0 is a literal 0x90, and that 0x90 is then the byte a
 * following run repeats (RISC OS ArcFS writes a run of 0x90 as
 * "90 00 90 n": the members' CRC-16s only match with this reading).
 * Decoding stops once @p output_size bytes exist; a run that would pass
 * that point is cut there, like Deark does, and the CRC decides. */
static bool arcfs_unrle(const uint8_t *input, size_t input_size,
                        uint8_t *output, size_t output_size, size_t *written) {
    size_t index, position = 0U;
    uint8_t last = 0U;
    bool pending = false;
    for (index = 0U; index < input_size && position < output_size; ++index) {
        uint8_t value = input[index];
        if (pending) {
            pending = false;
            if (value == 0U) {
                output[position++] = 0x90U;
                last = 0x90U;
            } else {
                size_t run = (size_t)value - 1U;
                if (run > output_size - position) run = output_size - position;
                if (run != 0U) xx_rt_memset(output + position, last, run);
                position += run;
            }
        } else if (value == 0x90U) {
            pending = true;
        } else {
            output[position++] = value;
            last = value;
        }
    }
    *written = position;
    return position == output_size;
}

/* Decodes a packed, crunched or compressed member into one buffer.  LZW is
 * the shared xx_arcfs_lzw decoder without its run filter; crunched data is
 * expanded by it into an intermediate buffer (a run-filtered stream is at
 * most two bytes per output byte) and then by arcfs_unrle.  All buffers
 * together must fit the smaller of ARCFS_MAX_MEMBER_BYTES and the caller's
 * memory limit. */
static bool arcfs_decode_member(Abstractformat *format,
                                const arcfs_member *member,
                                uint64_t memory_limit, uint8_t **plain,
                                size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *middle = NULL;
    uint8_t *output = NULL;
    uint64_t middle_size = 0U, budget = ARCFS_MAX_MEMBER_BYTES;
    size_t written = 0U, middle_written = 0U;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size || member->folder ||
        member->truncated || member->packed_size < 0 ||
        !arcfs_known_method(member->method) ||
        member->method == ARCFS_METHOD_STORED ||
        member->original_size > arcfs_expansion_bound(member))
        return false;
    if (member->method == ARCFS_METHOD_CRUNCHED)
        middle_size = (uint64_t)member->original_size * 2U + 16U;
    if (memory_limit < budget) budget = memory_limit;
    if ((uint64_t)member->packed_size > budget ||
        (uint64_t)member->original_size + middle_size >
            budget - (uint64_t)member->packed_size)
        return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0 ?
                                         (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(member->original_size != 0U ?
                                         member->original_size : 1U);
    if (middle_size != 0U) middle = (uint8_t *)xx_mem_alloc((size_t)middle_size);
    if (!packed || !output || (middle_size != 0U && !middle) ||
        (member->packed_size != 0 &&
         !arcfs_read_at(format->device, member->data_offset, packed,
                        (size_t)member->packed_size)))
        goto done;
    if (member->original_size == 0U) {
        decoded = true;
    } else if (member->method == ARCFS_METHOD_PACKED) {
        decoded = arcfs_unrle(packed, (size_t)member->packed_size, output,
                              member->original_size, &written);
    } else if (member->method == ARCFS_METHOD_COMPRESSED) {
        decoded = xx_arcfs_lzw_decode_memory(
            packed, (size_t)member->packed_size, output, member->original_size,
            member->max_bits, false, &written);
    } else {
        /* The LZW layer's own length is not stored: decode as far as the
         * stream goes (a short stream reports failure but keeps its count)
         * and let the run filter and the CRC judge the result. */
        (void)xx_arcfs_lzw_decode_memory(packed, (size_t)member->packed_size,
                                         middle, (size_t)middle_size,
                                         member->max_bits, false,
                                         &middle_written);
        if (middle_written > (size_t)middle_size) goto done;
        decoded = arcfs_unrle(middle, middle_written, output,
                              member->original_size, &written);
    }
    if (!decoded || written != member->original_size ||
        (member->crc != 0U &&
         arcfs_crc16(0U, output, written) != member->crc))
        goto done;
    xx_mem_free(packed);
    if (middle) xx_mem_free(middle);
    *plain = output;
    *plain_size = written;
    return true;
done:
    if (packed) xx_mem_free(packed);
    if (middle) xx_mem_free(middle);
    if (output) xx_mem_free(output);
    return false;
}

void xx_arcfs_init(xx_arcfs *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ARCFS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arcfs");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_arcfs_check_is_valid;
    archive->format.handle_base_info = xx_arcfs_handle_base_info;
    archive->format.get_format_size = xx_arcfs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arcfs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arcfs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_arcfs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arcfs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arcfs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arcfs_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_arcfs *xx_arcfs_create(xx_io_device *device, int64_t base_address) {
    xx_arcfs *archive = (xx_arcfs *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arcfs_init(archive, device, base_address);
    return archive;
}

void xx_arcfs_destroy(xx_arcfs *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_arcfs_free(xx_arcfs *archive) {
    if (!archive) return;
    xx_arcfs_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arcfs_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    arcfs_stream *stream;
    (void)pd;
    if (!arcfs_parse(format, &stream)) return false;
    arcfs_stream_free(stream);
    return true;
}

bool xx_arcfs_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    arcfs_stream *stream;
    xx_arcfs *archive;
    int64_t total;
    (void)pd;
    if (!format || !arcfs_parse(format, &stream)) return false;
    archive = (xx_arcfs *)format;
    total = xx_io_total_size(format->device);
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = archive->archive_end < total ? archive->archive_end : -1;
    format->overlay_size = archive->archive_end < total ?
                               total - archive->archive_end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    arcfs_stream_free(stream);
    return true;
}

int64_t xx_arcfs_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcfs_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_arcfs_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_arcfs_handle_base_info(format, pd))
               ? ((xx_arcfs *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_arcfs_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    arcfs_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!arcfs_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        arcfs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = arcfs_stream_free;
    state->total_records = stream->count;
    if (!arcfs_copy_options(&state->options, options) ||
        !arcfs_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_arcfs_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_arcfs_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    arcfs_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (arcfs_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = arcfs_set_record(&state->current_record,
                                         &stream->items[stream->index]);
    return state->has_record;
}

/* Unpacks the current member below XX_META_ID_OPT_UNPACK_PATH, or only
 * decodes and checks it when that option is absent.  Members cut off by
 * the end of the file, unknown methods, CRC mismatches and sizes above
 * XX_META_ID_OPT_MAX_MEMBER_SIZE fail without leaving a partial file
 * behind.  Compressed members are decoded in memory, so they also obey
 * XX_META_ID_OPT_MEMORY_LIMIT and the built-in 256 MiB cap; stored ones
 * are copied in 64 KiB chunks. */
bool xx_arcfs_unpack_current_archive_record(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    arcfs_stream *stream;
    arcfs_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    uint64_t member_limit = UINT64_MAX;
    uint64_t memory_limit = UINT64_MAX;
    xx_io_device *destination = NULL;
    bool created = false;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (arcfs_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!arcfs_safe_output_name(member->name) ||
        !arcfs_limit(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE,
                     &member_limit) ||
        !arcfs_limit(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT,
                     &memory_limit))
        return false;
    path_option = arcfs_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (path_option) {
        if (path_option->type == XX_VAR_TYPE_STRING ||
            path_option->type == XX_VAR_TYPE_STRING_VIEW)
            base = xx_var_get_str(path_option);
        else if (path_option->type == XX_VAR_TYPE_WSTRING ||
                 path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
            owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
            base = owned_base;
        }
        if (!base) goto done;
        path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
                base[xx_str_len(base) - 1U] != '\\')
                   ? xx_str_concat3(base, "/", member->name)
                   : xx_str_concat(base, member->name);
        if (!path) goto done;
    }
    if (member->folder) {
        result = !path || xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (member->truncated || !arcfs_known_method(member->method) ||
        member->original_size > member_limit)
        goto done;
    if (member->method != ARCFS_METHOD_STORED &&
        !arcfs_decode_member(format, member, memory_limit, &plain, &plain_size))
        goto done;
    if (path) {
        if (!xx_store_create_dirs_a(path, false)) goto done;
        destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
    }
    if (member->method == ARCFS_METHOD_STORED)
        result = arcfs_copy_stored(format, member, destination, pd);
    else
        result = !destination || arcfs_write_all(destination, plain, plain_size);
    if (destination && xx_io_close(destination) != 0) result = false;
done:
    if (!result && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arcfs_free_archive_records_reading(Abstractformat *format,
                                           xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
