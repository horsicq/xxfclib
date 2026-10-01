/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Entis GLS / ERISA-Archive (NOA).  xx_noa.h carries the field table.
 *
 * The directory walk follows GARbro's ArcFormats/Entis/ArcNOA.cs (MIT,
 * Copyright (C) 2015-2018 morkt): entry layout, offsets relative to the
 * DirEntry record, attribute 0x10 recursing into record + 0x10, attributes
 * 0x20/0x40 ending a directory, and the coded stream being the record body
 * minus its last 4 bytes.  The ERISA-Nemesis decoder below is a port of
 * GARbro's ArcFormats/Entis/ErisaNemesis.cs and the ProbDecodeContext /
 * ErisaProbModel / ERISADecodeContext classes of EriReader.cs and ArcNOA.cs
 * (MIT, Copyright (C) 2015-2018 morkt), themselves a translation of the
 * Entis ERISA library.  Every table index the port touches is checked, and
 * each place where GARbro would throw (including an index that would fall
 * outside its arrays) is a decode error here.
 *
 * Deliberate differences from GARbro:
 *   - a stored member of 1..4 bytes is extracted with its bytes (GARbro
 *     returns an empty stream for any record body of 4 bytes or less);
 *   - a coded member that decodes to fewer bytes than its declared size is
 *     an extraction error (GARbro writes the short output);
 *   - password-encrypted members are refused (GARbro without a key writes
 *     the ciphertext);
 *   - a directory visited twice rejects the archive (GARbro recurses until
 *     its stack overflows).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/noa/xx_noa.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef NOA
#define XX_NOA_FILE_TYPE XX_FILE_TYPE_NOA
#else
#define XX_NOA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NOA_HEADER_SIZE 0x40
#define NOA_FILE_ID 0x02000400U
#define NOA_RECORD_HEADER 16
/* u64 size, u32 attribute, u32 encode type, u64 offset, u64 time, u32 extra
 * length, u32 name length. */
#define NOA_ENTRY_FIXED 40
#define NOA_MAX_NAME 1024
#define NOA_MAX_PATH 4096
#define NOA_MAX_DEPTH 32
#define NOA_MAX_DIRS 65536U
#define NOA_MAX_MEMBERS 262144U
#define NOA_MAX_DIR_BODY (64LL * 1024 * 1024)
#define NOA_MAX_LIVE (64LL * 1024 * 1024)
#define NOA_MAX_POOL (64U * 1024U * 1024U)
#define NOA_VISITED_SLOTS 131072U
#define NOA_POLL_MASK 0x3ffU

#define NOA_ATTR_DIRECTORY 0x10U
#define NOA_ATTR_END_A 0x20U
#define NOA_ATTR_END_B 0x40U
#define NOA_ENC_RAW 0x00000000U
#define NOA_ENC_NEMESIS 0x80000010U

/* A coded member with no declared size decodes until the stream ends, but
 * never past this. */
#define NOA_MAX_UNSIZED (256LL * 1024 * 1024)
#define NOA_MAX_DECLARED INT64_C(0x7fffffff)

typedef struct noa_member_s {
    int64_t record; /**< Absolute offset of the member's record. */
    uint64_t original_size;
    uint32_t attribute;
    uint32_t encoding;
    uint32_t name_at; /**< Offset of the converted path in the pool. */
    uint32_t name_length;
    bool renamed;
} noa_member;

typedef struct noa_key_s {
    uint64_t hash;
    uint32_t index;
} noa_key;

typedef struct noa_walk_s {
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t base;   /**< format->base_address. */
    int64_t end;    /**< Absolute end of the device. */
    bool collect;   /**< Fill items[] and the name pool. */
    bool measure;   /**< Track the furthest record end (reads file headers). */
    noa_member *items;
    size_t count;
    size_t capacity;
    char *pool;
    size_t pool_length;
    size_t pool_capacity;
    int64_t *visited; /**< Open-addressed set of DirEntry offsets, -1 empty;
                           allocated at the first sub-directory. */
    int64_t root;
    size_t directories;
    int64_t live;       /**< Directory bodies currently held. */
    int64_t body_total; /**< Directory body bytes read so far. */
    int64_t extent;     /**< Furthest absolute record end seen. */
    char path[NOA_MAX_PATH + 1];
} noa_walk;

typedef struct noa_stream_s {
    noa_member *items;
    size_t count;
    size_t index;
    char *pool;
    char *name; /**< NOA_MAX_PATH + 16 bytes: the current (renamed) path. */
} noa_stream;

static uint32_t noa_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint64_t noa_le64(const uint8_t *p) {
    return (uint64_t)noa_le32(p) | ((uint64_t)noa_le32(p + 4) << 32U);
}

static size_t noa_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > ((size_t)1 << 24) ? ((size_t)1 << 24) : n;
}

static bool noa_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t n = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

static bool noa_write_all(xx_io_device *device, const uint8_t *buffer,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t n = xx_io_write(device, buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

/* ---- member names ------------------------------------------------------ */

static bool noa_is_sjis_lead(uint8_t c) {
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool noa_is_sjis_trail(uint8_t c) {
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t noa_put_escape(char *out, uint8_t c) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* Append the converted raw name (up to its first NUL) to out[at..], which
 * has room for `room` bytes.  Returns the new length or SIZE_MAX if it does
 * not fit. */
static size_t noa_convert_name(const uint8_t *raw, size_t length, char *out,
                               size_t at, size_t room) {
    size_t index = 0U;
    while (index < length && raw[index] != 0U) {
        uint8_t c = raw[index];
        if (room - at < 7U) return SIZE_MAX;
        if (noa_is_sjis_lead(c) && index + 1U < length &&
            noa_is_sjis_trail(raw[index + 1U])) {
            at += noa_put_escape(out + at, c);
            at += noa_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%')
            at += noa_put_escape(out + at, c);
        else if (c == (uint8_t)'\\')
            out[at++] = '/';
        else
            out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

static uint64_t noa_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
            c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "%_<index>" before the extension of the last component.  `name`
 * has room for NOA_MAX_PATH + 16 bytes and holds at most NOA_MAX_PATH. */
static void noa_insert_suffix(char *name, size_t length, uint32_t index) {
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
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool noa_reserved_component(const char *segment, size_t length) {
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

static bool noa_safe_name(const char *name) {
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
                noa_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- directory walk ---------------------------------------------------- */

static bool noa_read_header(Abstractformat *format, int64_t *end) {
    uint8_t header[NOA_HEADER_SIZE + NOA_RECORD_HEADER];
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address ||
        total - format->base_address <
            (int64_t)NOA_HEADER_SIZE + NOA_RECORD_HEADER + 4 ||
        !noa_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, "Entis\x1a", 6) != 0 &&
        xx_rt_memcmp(header, "VIST\x1a", 5) != 0)
        return false;
    if (noa_le32(header + 8) != NOA_FILE_ID ||
        xx_rt_memcmp(header + NOA_HEADER_SIZE, "DirEntry", 8) != 0)
        return false;
    *end = total;
    return true;
}

/* Insert `offset` into the visited set; false if it is already there. */
static bool noa_visit(noa_walk *walk, int64_t offset) {
    uint64_t h = (uint64_t)offset * UINT64_C(0x9e3779b97f4a7c15);
    size_t slot = (size_t)(h >> 47U) & (NOA_VISITED_SLOTS - 1U);
    size_t probes;
    if (walk->directories == 0U) {
        walk->root = offset;
        return true;
    }
    if (!walk->visited) {
        size_t index;
        walk->visited =
            (int64_t *)xx_mem_alloc(NOA_VISITED_SLOTS * sizeof(int64_t));
        if (!walk->visited) return false;
        for (index = 0U; index < NOA_VISITED_SLOTS; ++index)
            walk->visited[index] = -1;
        if (!noa_visit(walk, walk->root)) return false;
    }
    for (probes = 0U; probes < NOA_VISITED_SLOTS; ++probes) {
        if (walk->visited[slot] == offset) return false;
        if (walk->visited[slot] < 0) {
            walk->visited[slot] = offset;
            return true;
        }
        slot = (slot + 1U) & (NOA_VISITED_SLOTS - 1U);
    }
    return false;
}

static bool noa_add_member(noa_walk *walk, int64_t record, uint64_t size,
                           uint32_t attribute, uint32_t encoding,
                           size_t path_length) {
    noa_member *member;
    if (walk->count >= NOA_MAX_MEMBERS) return false;
    if (!walk->collect) {
        ++walk->count;
        return true;
    }
    if (walk->count == walk->capacity) {
        size_t grow = walk->capacity ? walk->capacity * 2U : 64U;
        noa_member *items;
        if (grow > NOA_MAX_MEMBERS) grow = NOA_MAX_MEMBERS;
        items = (noa_member *)xx_mem_alloc(grow * sizeof(*items));
        if (!items) return false;
        if (walk->items) {
            xx_rt_memcpy(items, walk->items, walk->count * sizeof(*items));
            xx_mem_free(walk->items);
        }
        walk->items = items;
        walk->capacity = grow;
    }
    if (walk->pool_length + path_length + 1U > walk->pool_capacity) {
        size_t grow = walk->pool_capacity ? walk->pool_capacity : 4096U;
        char *pool;
        while (grow < walk->pool_length + path_length + 1U) grow *= 2U;
        if (grow > NOA_MAX_POOL) return false;
        pool = (char *)xx_mem_alloc(grow);
        if (!pool) return false;
        if (walk->pool) {
            xx_rt_memcpy(pool, walk->pool, walk->pool_length);
            xx_mem_free(walk->pool);
        }
        walk->pool = pool;
        walk->pool_capacity = grow;
    }
    member = &walk->items[walk->count];
    member->record = record;
    member->original_size = size;
    member->attribute = attribute;
    member->encoding = encoding;
    member->name_at = (uint32_t)walk->pool_length;
    member->name_length = (uint32_t)path_length;
    member->renamed = false;
    xx_rt_memcpy(walk->pool + walk->pool_length, walk->path, path_length);
    walk->pool[walk->pool_length + path_length] = 0;
    walk->pool_length += path_length + 1U;
    ++walk->count;
    return true;
}

static void noa_extend(noa_walk *walk, int64_t end) {
    if (end > walk->extent) walk->extent = end;
}

/* Parse the DirEntry record at absolute `record`; `prefix` bytes of
 * walk->path are the parent path. */
static bool noa_walk_dir(noa_walk *walk, int64_t record, size_t prefix,
                         unsigned depth) {
    uint8_t head[NOA_RECORD_HEADER];
    uint8_t *body = NULL;
    uint64_t length;
    int64_t available, pos;
    int32_t count;
    int32_t index;
    bool ok = false;

    if (depth > NOA_MAX_DEPTH || walk->directories >= NOA_MAX_DIRS ||
        record < walk->base || record > walk->end - NOA_RECORD_HEADER)
        return false;
    if (walk->pd && xx_pd_is_stopped(walk->pd)) return false;
    if (!noa_read_at(walk->device, record, head, sizeof(head)) ||
        xx_rt_memcmp(head, "DirEntry", 8) != 0)
        return false;
    length = noa_le64(head + 8);
    /* GARbro: 0 < size <= INT_MAX and record + 8 + size inside the file. */
    if (length == 0U || length > (uint64_t)0x7fffffff ||
        (int64_t)length > walk->end - record - 8)
        return false;
    available = walk->end - record - NOA_RECORD_HEADER;
    if ((int64_t)length < available) available = (int64_t)length;
    if (available < 4 || available > NOA_MAX_DIR_BODY ||
        walk->live + available > NOA_MAX_LIVE ||
        walk->body_total + available > walk->end - walk->base)
        return false;
    if (!noa_visit(walk, record)) return false;
    ++walk->directories;
    if (walk->measure) noa_extend(walk, record + NOA_RECORD_HEADER + available);

    body = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!body) return false;
    walk->live += available;
    walk->body_total += available;
    if (!noa_read_at(walk->device, record + NOA_RECORD_HEADER, body,
                     (size_t)available))
        goto done;
    count = (int32_t)noa_le32(body);
    if (count > 0 &&
        (int64_t)count > (available - 4) / (int64_t)NOA_ENTRY_FIXED)
        goto done;
    pos = 4;
    for (index = 0; index < count; ++index) {
        uint64_t size, relative;
        uint32_t attribute, encoding, extra, name_length;
        int64_t target;
        size_t path_length;
        if ((walk->count & NOA_POLL_MASK) == 0U && walk->pd &&
            xx_pd_is_stopped(walk->pd))
            goto done;
        if (available - pos < NOA_ENTRY_FIXED) goto done;
        size = noa_le64(body + pos);
        attribute = noa_le32(body + pos + 8);
        encoding = noa_le32(body + pos + 12);
        relative = noa_le64(body + pos + 16);
        extra = noa_le32(body + pos + 32);
        pos += 36;
        if ((uint64_t)extra > (uint64_t)(available - pos - 4)) goto done;
        pos += (int64_t)extra;
        name_length = noa_le32(body + pos);
        pos += 4;
        if (name_length > NOA_MAX_NAME ||
            (int64_t)name_length > available - pos)
            goto done;
        /* The record offset is signed and relative to this DirEntry. */
        if ((int64_t)relative < -(record - walk->base) ||
            (int64_t)relative > walk->end - record)
            target = -1;
        else
            target = record + (int64_t)relative;
        if (attribute == NOA_ATTR_END_A || attribute == NOA_ATTR_END_B) break;
        path_length = prefix;
        if (prefix != 0U) {
            if (prefix + 1U > NOA_MAX_PATH) goto done;
            walk->path[path_length++] = '/';
        }
        path_length = noa_convert_name(body + pos, name_length, walk->path,
                                       path_length, NOA_MAX_PATH);
        if (path_length == SIZE_MAX) goto done;
        pos += (int64_t)name_length;
        if (attribute == NOA_ATTR_DIRECTORY) {
            if (target < 0 ||
                !noa_walk_dir(walk, target + NOA_RECORD_HEADER, path_length,
                              depth + 1U))
                goto done;
        } else {
            if (!noa_add_member(walk, target, size, attribute, encoding,
                                path_length))
                goto done;
            if (walk->measure && target >= walk->base &&
                target <= walk->end - NOA_RECORD_HEADER) {
                uint8_t file_head[NOA_RECORD_HEADER];
                uint64_t file_length;
                if (noa_read_at(walk->device, target, file_head,
                                sizeof(file_head))) {
                    file_length = noa_le64(file_head + 8);
                    if (file_length <= (uint64_t)(walk->end - target -
                                                  NOA_RECORD_HEADER))
                        noa_extend(walk, target + NOA_RECORD_HEADER +
                                             (int64_t)file_length);
                }
            }
        }
    }
    ok = true;
done:
    walk->path[prefix] = 0;
    walk->live -= available;
    xx_mem_free(body);
    return ok;
}

static void noa_walk_free(noa_walk *walk) {
    if (walk->items) xx_mem_free(walk->items);
    if (walk->pool) xx_mem_free(walk->pool);
    if (walk->visited) xx_mem_free(walk->visited);
    walk->items = NULL;
    walk->pool = NULL;
    walk->visited = NULL;
}

/* Run the whole walk; on success walk->count > 0.  The caller frees. */
static bool noa_run(Abstractformat *format, noa_walk *walk, bool collect,
                    bool measure, xx_pd_struct *pd) {
    int64_t end;
    xx_mem_zero(walk, sizeof(*walk));
    if (!noa_read_header(format, &end)) return false;
    walk->device = format->device;
    walk->pd = pd;
    walk->base = format->base_address;
    walk->end = end;
    walk->collect = collect;
    walk->measure = measure;
    walk->extent = format->base_address + NOA_HEADER_SIZE;
    return noa_walk_dir(walk, format->base_address + NOA_HEADER_SIZE, 0U, 0U) &&
           walk->count != 0U;
}

static int noa_compare_keys(const void *left, const void *right) {
    const noa_key *a = (const noa_key *)left;
    const noa_key *b = (const noa_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static bool noa_mark_duplicates(noa_member *items, const char *pool,
                                size_t count) {
    noa_key *keys;
    size_t index;
    if (count < 2U) return true;
    keys = (noa_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < count; ++index) {
        keys[index].hash = noa_name_hash(pool + items[index].name_at,
                                         items[index].name_length);
        keys[index].index = (uint32_t)index;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), noa_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash)
            items[keys[index].index].renamed = true;
    xx_mem_free(keys);
    return true;
}

/* ---- ERISA-Nemesis decoder (port of GARbro, see the file comment) ------- */

#define ERISA_TOTAL_LIMIT 0x2000U
#define ERISA_SYMBOL_SORTS 0x101
#define ERISA_SUB_SORTS 0x80
#define ERISA_ESC (-1)
#define ERISA_SLOT_MAX 0x800U
#define NEMESIS_BUF_SIZE 0x10000U
#define NEMESIS_BUF_MASK 0xffffU
#define NEMESIS_INDEX_LIMIT 0x100U
#define NEMESIS_INDEX_MASK 0xffU
#define NEMESIS_READ_CHUNK 0x10000U

typedef struct erisa_symbol_s {
    uint16_t occured;
    int16_t symbol;
} erisa_symbol;

typedef struct erisa_model_s {
    uint32_t total;
    int32_t sorts;
    erisa_symbol table[ERISA_SYMBOL_SORTS];
    erisa_symbol sub[ERISA_SUB_SORTS];
} erisa_model;

typedef struct nemesis_phrase_s {
    uint32_t first;
    uint32_t index[NEMESIS_INDEX_LIMIT];
} nemesis_phrase;

typedef struct nemesis_s {
    /* bit input */
    xx_io_device *device;
    int64_t position;
    int64_t remaining;
    uint8_t buffer[NEMESIS_READ_CHUNK];
    uint32_t buffer_count;
    uint32_t next;
    int32_t int_count;
    uint32_t int_buffer;
    bool io_error;
    /* arithmetic decoder */
    uint32_t code;
    uint32_t augend;
    int32_t post_bits;
    erisa_model phrase_length;
    erisa_model phrase_index;
    erisa_model run_length;
    /* context model */
    uint32_t work_used;
    erisa_model base;
    erisa_model slots[ERISA_SLOT_MAX];
    /* phrase dictionary */
    int32_t last_symbol;
    uint8_t last_bytes[4];
    int32_t left;
    int32_t next_index;
    uint32_t window_index;
    bool eof;
    uint8_t window[NEMESIS_BUF_SIZE];
    nemesis_phrase lookup[256];
} nemesis;

static const int nemesis_shift[4] = {1, 3, 4, 5};
static const int nemesis_new_limit[4] = {0x01, 0x08, 0x10, 0x20};

static void erisa_model_init(erisa_model *model) {
    int i;
    model->total = ERISA_SYMBOL_SORTS;
    model->sorts = ERISA_SYMBOL_SORTS;
    for (i = 0; i < 0x100; ++i) {
        model->table[i].occured = 1;
        model->table[i].symbol = (int16_t)i;
    }
    model->table[0x100].occured = 1;
    model->table[0x100].symbol = ERISA_ESC;
    for (i = 0; i < ERISA_SUB_SORTS; ++i) {
        model->sub[i].occured = 0;
        model->sub[i].symbol = -1;
    }
}

static void erisa_model_half(erisa_model *model) {
    int i;
    model->total = 0;
    for (i = 0; i < model->sorts; ++i) {
        model->table[i].occured =
            (uint16_t)((model->table[i].occured + 1U) >> 1U);
        model->total += model->table[i].occured;
    }
    for (i = 0; i < ERISA_SUB_SORTS; ++i) model->sub[i].occured >>= 1U;
}

static int erisa_model_increase(erisa_model *model, int index) {
    uint16_t occured = ++model->table[index].occured;
    int16_t symbol = model->table[index].symbol;
    while (--index >= 0) {
        if (model->table[index].occured >= occured) break;
        model->table[index + 1] = model->table[index];
    }
    ++index;
    model->table[index].occured = occured;
    model->table[index].symbol = symbol;
    if (++model->total >= ERISA_TOTAL_LIMIT) erisa_model_half(model);
    return index;
}

static bool erisa_model_add(erisa_model *model, int16_t symbol) {
    int index = model->sorts;
    if (index < 0 || index >= ERISA_SYMBOL_SORTS) return false;
    model->sorts++;
    model->total++;
    model->table[index].symbol = symbol;
    model->table[index].occured = 1;
    return true;
}

static bool nemesis_prefetch(nemesis *n) {
    if (n->int_count == 0) {
        if (n->buffer_count == 0U) {
            uint32_t want = NEMESIS_READ_CHUNK;
            n->next = 0U;
            if (n->remaining < (int64_t)want) want = (uint32_t)n->remaining;
            if (want == 0U) return false;
            if (!noa_read_at(n->device, n->position, n->buffer, want)) {
                n->io_error = true;
                n->remaining = 0;
                return false;
            }
            n->position += want;
            n->remaining -= want;
            n->buffer_count = want;
            while (n->buffer_count & 3U) n->buffer[n->buffer_count++] = 0;
        }
        n->int_count = 32;
        n->int_buffer = ((uint32_t)n->buffer[n->next] << 24U) |
                        ((uint32_t)n->buffer[n->next + 1U] << 16U) |
                        ((uint32_t)n->buffer[n->next + 2U] << 8U) |
                        (uint32_t)n->buffer[n->next + 3U];
        n->next += 4U;
        n->buffer_count -= 4U;
    }
    return true;
}

/* 1 at end of input, otherwise 0 or -1 (the sign of the next bit). */
static int nemesis_bit(nemesis *n) {
    int value;
    if (!nemesis_prefetch(n)) return 1;
    value = (n->int_buffer & 0x80000000U) ? -1 : 0;
    --n->int_count;
    n->int_buffer <<= 1U;
    return value;
}

static uint32_t nemesis_bits(nemesis *n, int count) {
    uint32_t code = 0U;
    while (count != 0) {
        int copy;
        if (!nemesis_prefetch(n)) break;
        copy = count < n->int_count ? count : n->int_count;
        if (copy >= 32) {
            code = n->int_buffer;
            n->int_buffer = 0U;
        } else {
            code = (code << copy) | (n->int_buffer >> (32 - copy));
            n->int_buffer <<= copy;
        }
        count -= copy;
        n->int_count -= copy;
    }
    return code;
}

/* Index of the next symbol in `model`, -1 at the end of the code, -2 on a
 * corrupt stream. */
static int nemesis_index(nemesis *n, const erisa_model *model) {
    uint32_t acc, fs = 0U, occured = 0U;
    uint16_t w;
    int sym = 0;
    if (model->total == 0U || n->augend == 0U || model->sorts <= 0 ||
        model->sorts > ERISA_SYMBOL_SORTS)
        return -2;
    acc = n->code * model->total / n->augend;
    if (acc >= ERISA_TOTAL_LIMIT) return -1;
    w = (uint16_t)acc;
    for (;;) {
        occured = model->table[sym].occured;
        if (w < occured) break;
        w = (uint16_t)(w - occured);
        fs += occured;
        if (++sym >= model->sorts) return -1;
    }
    n->code -= (n->augend * fs + model->total - 1U) / model->total;
    n->augend = n->augend * occured / model->total;
    if (n->augend == 0U) return -2;
    while ((n->augend & 0x8000U) == 0U) {
        int bit = nemesis_bit(n);
        if (bit == 1) {
            if (++n->post_bits >= 256) return -1;
            bit = 0;
        }
        n->code = (n->code << 1U) | ((uint32_t)bit & 1U);
        n->augend <<= 1U;
    }
    n->code &= 0xffffU;
    return sym;
}

/* Decode one symbol value from `model`: >= 0 a symbol, ERISA_ESC for the
 * escape or the end of the code, -2 on a corrupt stream. */
static int nemesis_code(nemesis *n, erisa_model *model) {
    int sym = nemesis_index(n, model);
    int value;
    if (sym == -2) return -2;
    if (sym < 0) return ERISA_ESC;
    value = model->table[sym].symbol;
    erisa_model_increase(model, sym);
    return value;
}

static void nemesis_prepare(nemesis *n, xx_io_device *device, int64_t offset,
                            int64_t size) {
    int i;
    n->device = device;
    n->position = offset;
    n->remaining = size;
    n->buffer_count = 0U;
    n->next = 0U;
    n->int_count = 0;
    n->int_buffer = 0U;
    n->io_error = false;
    n->last_symbol = 0;
    for (i = 0; i < 4; ++i) n->last_bytes[i] = 0;
    n->work_used = 0U;
    erisa_model_init(&n->base);
    erisa_model_init(&n->phrase_length);
    erisa_model_init(&n->phrase_index);
    erisa_model_init(&n->run_length);
    n->code = nemesis_bits(n, 32);
    n->augend = 0xffffU;
    n->post_bits = 0;
    xx_rt_memset(n->window, 0, sizeof(n->window));
    xx_rt_memset(n->lookup, 0, sizeof(n->lookup));
    n->window_index = 0U;
    n->left = 0;
    n->next_index = 0;
    n->eof = false;
}

static void nemesis_emit(nemesis *n, uint8_t symbol) {
    nemesis_phrase *phrase = &n->lookup[symbol];
    n->last_bytes[n->last_symbol++] = symbol;
    n->last_symbol &= 3;
    phrase->index[phrase->first] = n->window_index;
    phrase->first = (phrase->first + 1U) & NEMESIS_INDEX_MASK;
    n->window[n->window_index++] = symbol;
    n->window_index &= NEMESIS_BUF_MASK;
}

/* Decode up to `count` bytes; returns the number decoded (fewer means the
 * stream ended) or -1 on a corrupt stream. */
static int64_t nemesis_decode(nemesis *n, uint8_t *out, uint32_t count) {
    uint32_t decoded = 0U;
    /* Every pass of the loop either emits bytes or decodes at least one
     * symbol from a model with more than one symbol, which shrinks the
     * augend; the cap is only a backstop against a stall. */
    uint64_t guard = 0U;
    if (n->eof) return 0;
    while (decoded < count) {
        erisa_model *model;
        int deg, sym, symbol;
        bool phrase_mode = false;
        uint8_t byte;
        if (++guard > (uint64_t)count * 8U + 0x100000U) return -1;
        if (n->left > 0) {
            uint32_t run = (uint32_t)n->left;
            uint32_t i;
            uint8_t last = n->window[(n->window_index - 1U) & NEMESIS_BUF_MASK];
            if (run > count - decoded) run = count - decoded;
            for (i = 0U; i < run; ++i) {
                byte = last;
                if (n->next_index >= 0) {
                    byte = n->window[n->next_index++];
                    n->next_index &= (int32_t)NEMESIS_BUF_MASK;
                }
                nemesis_emit(n, byte);
                last = byte;
                out[decoded++] = byte;
            }
            n->left -= (int32_t)run;
            continue;
        }
        model = &n->base;
        for (deg = 0; deg < 4; ++deg) {
            int last = n->last_bytes[(n->last_symbol + 3 - deg) & 3] >>
                       nemesis_shift[deg];
            int slot = model->sub[last].symbol;
            if (slot < 0) break;
            if ((uint32_t)slot >= n->work_used) return -1;
            model = &n->slots[slot];
        }
        sym = nemesis_index(n, model);
        if (sym == -2) return -1;
        if (sym < 0) return (int64_t)decoded;
        symbol = model->table[sym].symbol;
        erisa_model_increase(model, sym);
        if (symbol == ERISA_ESC) {
            if (model != &n->base) {
                sym = nemesis_index(n, &n->base);
                if (sym == -2) return -1;
                if (sym < 0) return (int64_t)decoded;
                symbol = n->base.table[sym].symbol;
                erisa_model_increase(&n->base, sym);
                if (symbol != ERISA_ESC) {
                    if (!erisa_model_add(model, (int16_t)symbol)) return -1;
                } else {
                    phrase_mode = true;
                }
            } else {
                phrase_mode = true;
            }
        }
        if (phrase_mode) {
            int length;
            uint8_t last;
            nemesis_phrase *phrase;
            int index = nemesis_code(n, &n->phrase_index);
            if (index == -2) return -1;
            if (index == ERISA_ESC) {
                n->eof = true;
                return (int64_t)decoded;
            }
            length = nemesis_code(n, index == 0 ? &n->run_length
                                                : &n->phrase_length);
            if (length == -2) return -1;
            if (length == ERISA_ESC) return (int64_t)decoded;
            last = n->window[(n->window_index - 1U) & NEMESIS_BUF_MASK];
            phrase = &n->lookup[last];
            n->left = length;
            if (index == 0) {
                n->next_index = -1;
            } else {
                uint32_t at = phrase->index[(phrase->first - (uint32_t)index) &
                                            NEMESIS_INDEX_MASK];
                if (at >= NEMESIS_BUF_SIZE || n->window[at] != last) return -1;
                n->next_index = (int32_t)((at + 1U) & NEMESIS_BUF_MASK);
            }
            continue;
        }
        if (symbol < 0 || symbol > 0xff) return -1;
        byte = (uint8_t)symbol;
        nemesis_emit(n, byte);
        out[decoded++] = byte;

        if (n->work_used < ERISA_SLOT_MAX && deg < 4) {
            int value_symbol = byte >> nemesis_shift[deg];
            if (value_symbol >= ERISA_SUB_SORTS) return -1;
            if (++model->sub[value_symbol].occured >=
                (uint16_t)nemesis_new_limit[deg]) {
                int i;
                erisa_model *parent = model;
                model = &n->base;
                for (i = 0; i <= deg; ++i) {
                    int slot;
                    value_symbol = n->last_bytes[(n->last_symbol + 3 - i) & 3] >>
                                   nemesis_shift[i];
                    slot = model->sub[value_symbol].symbol;
                    if (slot < 0) break;
                    if ((uint32_t)slot >= n->work_used) return -1;
                    model = &n->slots[slot];
                }
                if (i <= deg && model->sub[value_symbol].symbol < 0) {
                    erisa_model *created = &n->slots[n->work_used];
                    int j = 0;
                    model->sub[value_symbol].symbol = (int16_t)(n->work_used++);
                    created->total = 0U;
                    for (i = 0; i < parent->sorts; ++i) {
                        uint16_t occured =
                            (uint16_t)(parent->table[i].occured >> 4U);
                        if (occured > 0U &&
                            parent->table[i].symbol != ERISA_ESC) {
                            if (j >= ERISA_SYMBOL_SORTS - 1) return -1;
                            created->total += occured;
                            created->table[j].occured = occured;
                            created->table[j].symbol = parent->table[i].symbol;
                            j++;
                        }
                    }
                    created->total++;
                    created->table[j].occured = 1U;
                    created->table[j].symbol = ERISA_ESC;
                    created->sorts = ++j;
                    for (i = 0; i < ERISA_SUB_SORTS; ++i) {
                        created->sub[i].occured = 0U;
                        created->sub[i].symbol = -1;
                    }
                }
            }
        }
    }
    return (int64_t)decoded;
}

/* Decode the coded stream of `size` bytes at `offset` into `destination`
 * (or only validate it when NULL).  `target` is the declared size, 0 for
 * "until the stream ends". */
static bool noa_unpack_nemesis(xx_io_device *source, int64_t offset,
                               int64_t size, uint64_t target,
                               xx_io_device *destination, xx_pd_struct *pd) {
    nemesis *n;
    uint8_t *out;
    int64_t limit = target ? (int64_t)target : NOA_MAX_UNSIZED;
    int64_t produced = 0;
    bool ok = false;
    n = (nemesis *)xx_mem_alloc(sizeof(*n));
    out = (uint8_t *)xx_mem_alloc(NEMESIS_READ_CHUNK);
    if (!n || !out) goto done;
    nemesis_prepare(n, source, offset, size);
    while (produced < limit) {
        uint32_t want = NEMESIS_READ_CHUNK;
        int64_t got;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (limit - produced < (int64_t)want) want = (uint32_t)(limit - produced);
        got = nemesis_decode(n, out, want);
        if (got < 0 || n->io_error) goto done;
        if (got > 0 && destination &&
            !noa_write_all(destination, out, (size_t)got))
            goto done;
        produced += got;
        if ((uint32_t)got < want) break;
    }
    /* A declared size must be met; an undeclared one must end on its own. */
    ok = target ? produced == (int64_t)target : produced < limit;
done:
    if (n) xx_mem_free(n);
    if (out) xx_mem_free(out);
    return ok;
}

static bool noa_copy(xx_io_device *source, int64_t offset, int64_t size,
                     xx_io_device *destination, xx_pd_struct *pd) {
    const size_t capacity = noa_capacity();
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (done < size) {
        size_t chunk = size - done > (int64_t)capacity ? capacity
                                                       : (size_t)(size - done);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !noa_read_at(source, offset + done, buffer, chunk) ||
            (destination && !noa_write_all(destination, buffer, chunk))) {
            ok = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* Where a member's data lives: body offset and length, from its record. */
static bool noa_member_body(Abstractformat *format, const noa_member *member,
                            int64_t *offset, int64_t *length) {
    uint8_t head[NOA_RECORD_HEADER];
    int64_t end = xx_io_total_size(format->device);
    uint64_t size;
    if (member->record < format->base_address ||
        member->record > end - NOA_RECORD_HEADER ||
        !noa_read_at(format->device, member->record, head, sizeof(head)))
        return false;
    size = noa_le64(head + 8);
    if (size > (uint64_t)0x7fffffff ||
        (int64_t)size > end - member->record - NOA_RECORD_HEADER)
        return false;
    *offset = member->record + NOA_RECORD_HEADER;
    *length = (int64_t)size;
    return true;
}

static bool noa_extract(Abstractformat *format, const noa_member *member,
                        xx_io_device *destination, xx_pd_struct *pd) {
    int64_t offset, length;
    if (member->encoding != NOA_ENC_RAW && member->encoding != NOA_ENC_NEMESIS)
        return false;
    if (!noa_member_body(format, member, &offset, &length)) return false;
    if (member->encoding == NOA_ENC_RAW)
        return noa_copy(format->device, offset, length, destination, pd);
    if (length <= 4) return true;
    if (member->original_size > (uint64_t)NOA_MAX_DECLARED) return false;
    return noa_unpack_nemesis(format->device, offset, length - 4,
                              member->original_size, destination, pd);
}

/* ---- records ----------------------------------------------------------- */

static void noa_stream_free(void *opaque) {
    noa_stream *stream = (noa_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->pool) xx_mem_free(stream->pool);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool noa_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *noa_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool noa_set_record(Abstractformat *format, xx_archive_record *record,
                           noa_stream *stream, size_t index) {
    const noa_member *member = &stream->items[index];
    int64_t offset = -1, length = 0;
    bool encrypted = member->encoding != NOA_ENC_RAW &&
                     member->encoding != NOA_ENC_NEMESIS;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    xx_rt_memcpy(stream->name, stream->pool + member->name_at,
                 (size_t)member->name_length + 1U);
    if (member->renamed)
        noa_insert_suffix(stream->name, member->name_length, (uint32_t)index);
    if (!noa_member_body(format, member, &offset, &length)) {
        offset = -1;
        length = 0;
    }
    record->header_offset = member->record;
    record->header_size = NOA_RECORD_HEADER;
    record->data_offset = offset;
    record->compressed_size = length;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)length) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               member->encoding == NOA_ENC_RAW ? (uint64_t)length
                                               : member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->encoding) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_noa_init(xx_noa *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_NOA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "noa");
    archive->format.check_is_valid = xx_noa_check_is_valid;
    archive->format.handle_base_info = xx_noa_handle_base_info;
    archive->format.get_format_size = xx_noa_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_noa_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_noa_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_noa_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_noa_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_noa_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_noa_free_archive_records_reading;
}

xx_noa *xx_noa_create(xx_io_device *device, int64_t base_address) {
    xx_noa *archive = (xx_noa *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_noa_init(archive, device, base_address);
    return archive;
}

void xx_noa_destroy(xx_noa *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_noa_free(xx_noa *archive) {
    if (!archive) return;
    xx_noa_destroy(archive);
    xx_mem_free(archive);
}

bool xx_noa_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    noa_walk *walk = (noa_walk *)xx_mem_alloc(sizeof(noa_walk));
    bool ok;
    if (!walk) return false;
    ok = noa_run(format, walk, false, false, pd);
    noa_walk_free(walk);
    xx_mem_free(walk);
    return ok;
}

bool xx_noa_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    noa_walk *walk = (noa_walk *)xx_mem_alloc(sizeof(noa_walk));
    bool ok;
    if (!walk) return false;
    ok = noa_run(format, walk, false, true, pd);
    if (ok) {
        xx_noa *archive = (xx_noa *)format;
        archive->number_of_records = walk->count;
        format->number_of_archive_records = walk->count;
        format->format_size = walk->extent - format->base_address;
        format->is_valid = true;
        format->base_info_handled = true;
    }
    noa_walk_free(walk);
    xx_mem_free(walk);
    return ok;
}

int64_t xx_noa_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_noa_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_noa_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_noa_handle_base_info(format, pd))
               ? ((xx_noa *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_noa_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    noa_walk *walk;
    noa_stream *stream = NULL;
    xx_archive_record_state *state;
    walk = (noa_walk *)xx_mem_alloc(sizeof(noa_walk));
    if (!walk) return NULL;
    if (!noa_run(format, walk, true, false, pd) ||
        !noa_mark_duplicates(walk->items, walk->pool, walk->count) ||
        !(stream = (noa_stream *)xx_mem_calloc(1U, sizeof(*stream))) ||
        !(stream->name = (char *)xx_mem_alloc(NOA_MAX_PATH + 16U))) {
        if (stream) xx_mem_free(stream);
        noa_walk_free(walk);
        xx_mem_free(walk);
        return NULL;
    }
    stream->items = walk->items;
    stream->pool = walk->pool;
    stream->count = walk->count;
    walk->items = NULL;
    walk->pool = NULL;
    noa_walk_free(walk);
    xx_mem_free(walk);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        noa_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = noa_stream_free;
    state->total_records = stream->count;
    if (!noa_copy_options(&state->options, options) ||
        !noa_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_noa_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_noa_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    noa_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (noa_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = noa_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_noa_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    noa_stream *stream;
    const noa_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (noa_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = noa_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return noa_extract(format, member, NULL, pd);
    if (!noa_safe_name(stream->name)) return false;
    if (member->encoding != NOA_ENC_RAW && member->encoding != NOA_ENC_NEMESIS)
        return false;
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
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = noa_extract(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_noa_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
