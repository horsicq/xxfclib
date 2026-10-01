/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PEA archive (PeaZip's pea.exe).  xx_pea.h carries the layout.
 *
 * The layout was worked out from archives written by pea.exe 0.72 with every
 * compression level and control algorithm it offers, and the member names
 * from what its UNPEA ... EXTRACT2DIR produces.  No pea source was used.
 *
 * Identification is unchanged: the two magic bytes are not enough on their
 * own, so the version ceiling, the two control-byte alphabets and the
 * "POD\0" trigger at offset 12 are all required.  The object walk only
 * decides what is listed; an archive whose objects cannot be walked
 * (password-protected stream, unknown layout) is still identified and
 * simply lists nothing.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pea/xx_pea.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_PEA_ARCHIVE_HEADER_SIZE 10
#define XX_PEA_STREAM_FIXED_SIZE 10
#define XX_PEA_MAX_VERSION 6U
#define XX_PEA_MAX_COMPRESSION 3U

#define PEA_TRIGGER_OFFSET 12
#define PEA_ATTR_DIRECTORY 0x10U
#define PEA_MIN_BLOCK 1024U
#define PEA_MAX_BLOCK (32U * 1024U * 1024U)
#define PEA_MAX_MEMBERS 1048576U
#define PEA_MAX_POOL (64U * 1024U * 1024U)
#define PEA_MAX_RAW_NAME 65535U
/* A raw byte becomes at most "%XX". */
#define PEA_MAX_NAME (PEA_MAX_RAW_NAME * 3U)
#define PEA_SUFFIX_ROOM 16U
#define PEA_COPY_CHUNK 65536U
#define PEA_POLL_MASK 0x3ffU

#define PEA_ALGO_ADLER32 0x01U
#define PEA_ALGO_CRC32 0x02U

static void xx_pea_vtable_destroy(Abstractformat *self);

/*
 * The control alphabet used by object headers and by the archive header's
 * byte 3: 0x00..0x03 and 0x10..0x19.
 */
static bool xx_pea_is_non_stream_control(uint8_t value) {
    return value <= 3U || (value >= 0x10U && value <= 0x19U);
}

/*
 * Stream headers accept everything an object header does, plus 0x30..0x33
 * and 0x41..0x4C.
 */
static bool xx_pea_is_stream_control(uint8_t value) {
    return xx_pea_is_non_stream_control(value) ||
           (value >= 0x30U && value <= 0x33U) ||
           (value >= 0x41U && value <= 0x4CU);
}

/* Tag size of a hash / checksum control algorithm, -1 for the others. */
static int pea_tag_size(uint8_t algo) {
    switch (algo) {
    case 0x00U: return 0;
    case 0x01U: case 0x02U: return 4;
    case 0x03U: return 8;
    case 0x10U: return 16;
    case 0x11U: case 0x12U: return 20;
    case 0x13U: case 0x16U: case 0x18U: return 32;
    case 0x14U: case 0x15U: case 0x17U: case 0x19U: return 64;
    default: return -1;
    }
}

static bool pea_read_dev(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t completed = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (completed < size) {
        ssize_t received = xx_io_read(device, (uint8_t *)buffer + completed,
                                      size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_pea_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    return self && pea_read_dev(self->device, offset, buffer, size);
}

static bool pea_write_all(xx_io_device *device, const uint8_t *buffer,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t n = xx_io_write(device, buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

static uint16_t pea_u16(const uint8_t *p, bool big) {
    return big ? (uint16_t)(((uint16_t)p[0] << 8) | p[1])
               : (uint16_t)(((uint16_t)p[1] << 8) | p[0]);
}

static uint32_t pea_u32(const uint8_t *p, bool big) {
    return big ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8) | (uint32_t)p[3]
               : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static uint64_t pea_u64(const uint8_t *p, bool big) {
    return big ? ((uint64_t)pea_u32(p, true) << 32) | pea_u32(p + 4, true)
               : ((uint64_t)pea_u32(p + 4, false) << 32) | pea_u32(p, false);
}

static bool xx_pea_probe(Abstractformat *self, xx_pea *out) {
    uint8_t header[XX_PEA_ARCHIVE_HEADER_SIZE];
    uint8_t name_size_raw[2];
    uint8_t stream[8];
    bool big_endian;
    uint16_t name_size;
    int64_t trigger;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < XX_PEA_ARCHIVE_HEADER_SIZE + XX_PEA_STREAM_FIXED_SIZE) {
        return false;
    }

    if (!xx_pea_read_at(self, self->base_address, header, sizeof(header))) {
        return false;
    }
    if (header[0] != 0xEAU || header[1] != 0x01U ||
        header[2] > XX_PEA_MAX_VERSION ||
        !xx_pea_is_non_stream_control(header[3])) {
        return false;
    }

    /* Byte 8's top bit chooses the byte order of every size field that
     * follows, including the name size read next. */
    big_endian = (header[8] & 0x80U) != 0U;

    if (!xx_pea_read_at(self, self->base_address + XX_PEA_ARCHIVE_HEADER_SIZE,
                        name_size_raw, sizeof(name_size_raw))) {
        return false;
    }
    name_size = pea_u16(name_size_raw, big_endian);
    /* The stream opens with a nameless trigger object. */
    if (name_size != 0U) {
        return false;
    }
    trigger = PEA_TRIGGER_OFFSET;
    if (trigger > span - 8) {
        return false;
    }

    if (!xx_pea_read_at(self, self->base_address + trigger, stream,
                        sizeof(stream))) {
        return false;
    }
    if (stream[0] != 'P' || stream[1] != 'O' || stream[2] != 'D' ||
        stream[3] != 0x00U || stream[4] > XX_PEA_MAX_COMPRESSION ||
        !xx_pea_is_stream_control(stream[6]) ||
        !xx_pea_is_non_stream_control(stream[7])) {
        return false;
    }

    if (out) {
        out->version = header[2];
        out->object_control = header[3];
        out->compression = stream[4];
        out->stream_control = stream[6];
        out->member_control = stream[7];
        out->big_endian = big_endian;
        out->first_stream_offset = trigger;
    }
    return true;
}

/* ---- walk -------------------------------------------------------------- */

typedef struct pea_member_s {
    int64_t header;    /**< Absolute offset of the name-size field. */
    uint32_t header_size; /**< Name size field through attributes (+ size). */
    int64_t data;      /**< Absolute offset of the data part. */
    int64_t packed;    /**< Bytes of the data part. */
    uint64_t size;     /**< Unpacked size. */
    uint32_t date_time;
    uint32_t attributes;
    uint32_t name_at;
    uint32_t name_length;
    bool folder;
    bool truncated;    /**< Runs past the end of this file. */
    bool unsafe;       /**< No usable relative name. */
    bool renamed;
} pea_member;

typedef struct pea_info_s {
    bool big;
    uint8_t volume_control;
    uint8_t compression;
    uint8_t stream_control;
    uint8_t member_control;
    uint32_t block_size;
    int64_t objects;   /**< Absolute offset of the first object. */
} pea_info;

typedef struct pea_walk_s {
    pea_info info;
    pea_member *items;
    size_t count;
    size_t capacity;
    char *pool;
    size_t pool_length;
    size_t pool_capacity;
    int64_t extent;    /**< Absolute end of the archive, as far as known. */
    bool complete;
    bool encrypted;
    /* EXTRACT2DIR state: the most recent stored folder, as normalised. */
    char *root;
    size_t root_length;
    size_t root_parent;
} pea_walk;

typedef struct pea_stream_s {
    pea_info info;
    pea_member *items;
    size_t count;
    size_t index;
    char *pool;
    char *name;        /**< Current name, with room for a suffix. */
} pea_stream;

static void pea_walk_free(pea_walk *walk) {
    if (walk->items) xx_mem_free(walk->items);
    if (walk->pool) xx_mem_free(walk->pool);
    if (walk->root) xx_mem_free(walk->root);
    walk->items = NULL;
    walk->pool = NULL;
    walk->root = NULL;
}

static bool pea_add_member(pea_walk *walk, const pea_member *member,
                           const char *name, size_t name_length) {
    if (walk->count >= PEA_MAX_MEMBERS) return false;
    if (walk->count == walk->capacity) {
        size_t grow = walk->capacity ? walk->capacity * 2U : 64U;
        pea_member *items;
        if (grow > PEA_MAX_MEMBERS) grow = PEA_MAX_MEMBERS;
        items = (pea_member *)xx_mem_alloc(grow * sizeof(*items));
        if (!items) return false;
        if (walk->items) {
            xx_rt_memcpy(items, walk->items, walk->count * sizeof(*items));
            xx_mem_free(walk->items);
        }
        walk->items = items;
        walk->capacity = grow;
    }
    if (walk->pool_length + name_length + 1U > walk->pool_capacity) {
        size_t grow = walk->pool_capacity ? walk->pool_capacity : 4096U;
        char *pool;
        while (grow < walk->pool_length + name_length + 1U) grow *= 2U;
        if (grow > PEA_MAX_POOL) return false;
        pool = (char *)xx_mem_alloc(grow);
        if (!pool) return false;
        if (walk->pool) {
            xx_rt_memcpy(pool, walk->pool, walk->pool_length);
            xx_mem_free(walk->pool);
        }
        walk->pool = pool;
        walk->pool_capacity = grow;
    }
    walk->items[walk->count] = *member;
    walk->items[walk->count].name_at = (uint32_t)walk->pool_length;
    walk->items[walk->count].name_length = (uint32_t)name_length;
    if (name_length) xx_rt_memcpy(walk->pool + walk->pool_length, name,
                                  name_length);
    walk->pool[walk->pool_length + name_length] = 0;
    walk->pool_length += name_length + 1U;
    ++walk->count;
    return true;
}

static bool pea_valid_utf8(const uint8_t *s, size_t n) {
    size_t i = 0U;
    while (i < n) {
        uint8_t c = s[i];
        size_t extra, k;
        uint32_t cp;
        if (c < 0x80U) { ++i; continue; }
        if (c >= 0xC2U && c <= 0xDFU) { extra = 1U; cp = c & 0x1FU; }
        else if (c >= 0xE0U && c <= 0xEFU) { extra = 2U; cp = c & 0x0FU; }
        else if (c >= 0xF0U && c <= 0xF4U) { extra = 3U; cp = c & 0x07U; }
        else return false;
        if (n - i <= extra) return false;
        for (k = 1U; k <= extra; ++k) {
            if ((s[i + k] & 0xC0U) != 0x80U) return false;
            cp = (cp << 6) | (s[i + k] & 0x3FU);
        }
        if ((extra == 2U && (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU))) ||
            (extra == 3U && (cp < 0x10000U || cp > 0x10FFFFU)))
            return false;
        i += extra + 1U;
    }
    return true;
}

/* Normalise a raw name: '\\' becomes '/', and when the name is not UTF-8
 * every byte >= 0x80 (and '%') is written as "%XX".  `out` has room for
 * PEA_MAX_NAME + 1. */
static size_t pea_normalise(const uint8_t *raw, size_t length, char *out) {
    static const char digits[] = "0123456789ABCDEF";
    bool utf8 = pea_valid_utf8(raw, length);
    size_t i, at = 0U;
    for (i = 0U; i < length; ++i) {
        uint8_t c = raw[i];
        if (c == '\\') out[at++] = '/';
        else if (!utf8 && (c >= 0x80U || c == '%')) {
            out[at++] = '%';
            out[at++] = digits[c >> 4];
            out[at++] = digits[c & 0x0FU];
        } else
            out[at++] = (char)c;
    }
    out[at] = 0;
    return at;
}

/* pea's EXTRACT2DIR: a path under the most recent stored folder keeps its
 * part below that folder's parent; anything else is reduced to its last
 * component, and a folder so reduced becomes the new reference.  `name` is
 * normalised; the relative name is written back into it. */
static bool pea_relative_name(pea_walk *walk, char *name, size_t *length,
                              bool folder) {
    size_t n = *length, start, base, lead = 0U;
    /* A drive prefix and leading separators are never part of a name. */
    if (n >= 2U && name[1] == ':' &&
        ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')))
        lead = 2U;
    while (lead < n && name[lead] == '/') ++lead;
    if (folder) {
        /* Keep exactly one trailing '/' for the prefix test. */
        while (n > 0U && name[n - 1U] == '/') --n;
        if (n == 0U) return false;
        name[n] = '/';
        name[n + 1U] = 0;
        ++n;
    }
    if (walk->root && n > walk->root_length &&
        xx_rt_memcmp(name, walk->root, walk->root_length) == 0) {
        start = walk->root_parent;
    } else {
        size_t last = folder ? n - 1U : n;
        base = last;
        while (base > lead && name[base - 1U] != '/') --base;
        if (base >= last) return false;
        start = base;
        if (folder) {
            char *root = (char *)xx_mem_alloc(n + 1U);
            if (!root) return false;
            xx_rt_memcpy(root, name, n + 1U);
            if (walk->root) xx_mem_free(walk->root);
            walk->root = root;
            walk->root_length = n;
            walk->root_parent = base;
        }
    }
    if (folder) --n; /* drop the trailing '/' again */
    if (start >= n) return false;
    xx_rt_memmove(name, name + start, n - start);
    n -= start;
    name[n] = 0;
    *length = n;
    return true;
}

static bool pea_read_info(Abstractformat *format, pea_info *info,
                          bool *encrypted) {
    xx_pea probe;
    uint8_t raw[4];
    xx_mem_zero(&probe, sizeof(probe));
    xx_mem_zero(info, sizeof(*info));
    *encrypted = false;
    if (!xx_pea_probe(format, &probe)) return false;
    info->big = probe.big_endian;
    info->volume_control = probe.object_control;
    info->compression = probe.compression;
    info->stream_control = probe.stream_control;
    info->member_control = probe.member_control;
    info->objects = format->base_address + PEA_TRIGGER_OFFSET + 8;
    if (pea_tag_size(info->stream_control) < 0) {
        *encrypted = true;
        return true;
    }
    if (info->compression != 0U) {
        if (!pea_read_dev(format->device, info->objects, raw, 4U)) return false;
        info->block_size = pea_u32(raw, info->big);
        info->objects += 4;
    }
    return true;
}

/* Walk the objects of an identified archive.  Returns false only for an
 * allocation failure or cancellation; a malformed object ends the list. */
static bool pea_run(Abstractformat *format, pea_walk *walk, xx_pd_struct *pd) {
    uint8_t *raw = NULL;
    char *name = NULL;
    int64_t end, at;
    int object_tag;
    bool ok = false;

    xx_mem_zero(walk, sizeof(*walk));
    if (!format || !format->device || format->base_address < 0) return false;
    end = xx_io_total_size(format->device);
    walk->extent = end;
    if (!pea_read_info(format, &walk->info, &walk->encrypted)) return false;
    if (walk->encrypted) return true;
    object_tag = pea_tag_size(walk->info.member_control);
    if (object_tag < 0) return true;
    if (walk->info.compression != 0U &&
        (walk->info.block_size < PEA_MIN_BLOCK ||
         walk->info.block_size > PEA_MAX_BLOCK))
        return true;

    raw = (uint8_t *)xx_mem_alloc(PEA_MAX_RAW_NAME + 16U);
    name = (char *)xx_mem_alloc(PEA_MAX_NAME + 4U);
    if (!raw || !name) goto done;

    at = walk->info.objects;
    for (;;) {
        pea_member member;
        uint16_t name_size;
        size_t name_length;
        uint8_t fixed[8];

        if ((walk->count & PEA_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        if (end - at < 2 || !pea_read_dev(format->device, at, raw, 2U)) break;
        name_size = pea_u16(raw, walk->info.big);
        if (name_size == 0U) {
            int stream_tag = pea_tag_size(walk->info.stream_control);
            int volume_tag = pea_tag_size(walk->info.volume_control);
            if (end - at < 6 || !pea_read_dev(format->device, at + 2, raw, 4U) ||
                xx_rt_memcmp(raw, "EOA\0", 4U) != 0)
                break;
            walk->complete = true;
            at += 6;
            if (stream_tag >= 0 && volume_tag >= 0 &&
                end - at >= (int64_t)stream_tag + volume_tag)
                walk->extent = at + stream_tag + volume_tag;
            break;
        }
        xx_mem_zero(&member, sizeof(member));
        member.header = at;
        if (end - at - 2 < (int64_t)name_size + 8 ||
            !pea_read_dev(format->device, at + 2, raw, (size_t)name_size + 8U))
            break;
        member.date_time = pea_u32(raw + name_size, walk->info.big);
        member.attributes = pea_u32(raw + name_size + 4U, walk->info.big);
        member.folder = (member.attributes & PEA_ATTR_DIRECTORY) != 0U;
        member.header_size = 2U + (uint32_t)name_size + 8U;
        at += member.header_size;
        if (!member.folder) {
            if (end - at < 8 || !pea_read_dev(format->device, at, fixed, 8U))
                break;
            member.size = pea_u64(fixed, walk->info.big);
            member.header_size += 8U;
            at += 8;
        }
        member.data = at;
        if (member.folder) {
            member.packed = 0;
        } else if (walk->info.compression == 0U) {
            if (member.size > (uint64_t)(end - at)) member.truncated = true;
            else at += (int64_t)member.size;
        } else {
            uint64_t left = member.size;
            uint32_t last = 0U;
            bool bad = false;
            while (left != 0U) {
                uint32_t block = left > walk->info.block_size
                                     ? walk->info.block_size : (uint32_t)left;
                uint32_t packed;
                if (end - at < 4) { member.truncated = true; break; }
                if (!pea_read_dev(format->device, at, fixed, 4U)) {
                    member.truncated = true;
                    break;
                }
                packed = pea_u32(fixed, walk->info.big);
                if (packed == 0U || packed > block) { bad = true; break; }
                at += 4;
                if ((int64_t)packed > end - at) { member.truncated = true; break; }
                at += packed;
                left -= block;
                last = block;
            }
            if (bad) break;
            if (!member.truncated && member.size != 0U) {
                if (end - at < 4 || !pea_read_dev(format->device, at, fixed, 4U))
                    member.truncated = true;
                else if (pea_u32(fixed, walk->info.big) != last)
                    break;
                else
                    at += 4;
            }
        }
        if (!member.truncated) {
            member.packed = at - member.data;
            if (end - at < object_tag) member.truncated = true;
            else at += object_tag;
        } else {
            member.packed = end - member.data;
        }

        name_length = pea_normalise(raw, name_size, name);
        if (!pea_relative_name(walk, name, &name_length, member.folder)) {
            member.unsafe = true;
            name_length = pea_normalise(raw, name_size, name);
        }
        if (!pea_add_member(walk, &member, name, name_length)) {
            if (walk->count >= PEA_MAX_MEMBERS) break;
            goto done;
        }
        if (member.truncated) break;
    }
    ok = true;
done:
    if (raw) xx_mem_free(raw);
    if (name) xx_mem_free(name);
    if (!ok) pea_walk_free(walk);
    return ok;
}

/* ---- names ------------------------------------------------------------- */

static uint64_t pea_name_hash(const char *name, size_t length) {
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

typedef struct pea_key_s {
    uint64_t hash;
    uint32_t index;
} pea_key;

static int pea_compare_keys(const void *left, const void *right) {
    const pea_key *a = (const pea_key *)left;
    const pea_key *b = (const pea_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* A later file whose name (case-insensitively) repeats an earlier one gets a
 * suffix, so it cannot overwrite it. */
static bool pea_mark_duplicates(pea_member *items, const char *pool,
                                size_t count) {
    pea_key *keys;
    size_t index;
    if (count < 2U) return true;
    keys = (pea_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < count; ++index) {
        keys[index].hash = pea_name_hash(pool + items[index].name_at,
                                         items[index].name_length);
        keys[index].index = (uint32_t)index;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), pea_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash &&
            !items[keys[index].index].folder)
            items[keys[index].index].renamed = true;
    xx_mem_free(keys);
    return true;
}

/* Insert "%_<index>" before the extension of the last component.  `name`
 * has room for length + PEA_SUFFIX_ROOM bytes. */
static void pea_insert_suffix(char *name, size_t length, uint32_t index) {
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

static bool pea_reserved_component(const char *segment, size_t length) {
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

static bool pea_safe_name(const char *name) {
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
                pea_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- decoding ---------------------------------------------------------- */

typedef struct pea_check_s {
    uint8_t algo;
    uint32_t value;
} pea_check;

static void pea_check_init(pea_check *check, uint8_t algo) {
    check->algo = algo;
    check->value = algo == PEA_ALGO_ADLER32 ? XX_ADLER32_INIT : 0U;
}

static void pea_check_update(pea_check *check, const uint8_t *data,
                             size_t size) {
    if (size == 0U) return;
    if (check->algo == PEA_ALGO_ADLER32)
        check->value = xx_adler32_update(check->value, data, size);
    else if (check->algo == PEA_ALGO_CRC32)
        check->value = xx_crc32_calc(check->value, data, size);
}

/* Read `size` bytes at `offset` into `buffer`, feeding the check. */
static bool pea_read_checked(xx_io_device *device, int64_t offset,
                             uint8_t *buffer, size_t size, pea_check *check) {
    if (!pea_read_dev(device, offset, buffer, size)) return false;
    pea_check_update(check, buffer, size);
    return true;
}

/* Decode a member into `destination` (validate only when NULL). */
static bool pea_extract(Abstractformat *format, const pea_info *info,
                        const pea_member *m, xx_io_device *destination,
                        xx_pd_struct *pd) {
    xx_io_device *device = format->device;
    pea_check check;
    uint8_t *header = NULL;
    uint8_t *in = NULL;
    uint8_t *out = NULL;
    uint8_t fixed[4];
    int tag_size = pea_tag_size(info->member_control);
    int64_t at;
    bool verify, ok = false;

    if (m->folder) return true;
    if (m->truncated || tag_size < 0) return false;
    verify = !info->big && (info->member_control == PEA_ALGO_ADLER32 ||
                            info->member_control == PEA_ALGO_CRC32);
    pea_check_init(&check, verify ? info->member_control : 0U);

    header = (uint8_t *)xx_mem_alloc(m->header_size);
    if (!header ||
        !pea_read_checked(device, m->header, header, m->header_size, &check))
        goto done;
    at = m->data;
    if (info->compression == 0U) {
        uint64_t left = m->size;
        in = (uint8_t *)xx_mem_alloc(PEA_COPY_CHUNK);
        if (!in) goto done;
        while (left != 0U) {
            size_t chunk = left > PEA_COPY_CHUNK ? PEA_COPY_CHUNK : (size_t)left;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !pea_read_checked(device, at, in, chunk, &check) ||
                (destination && !pea_write_all(destination, in, chunk)))
                goto done;
            at += (int64_t)chunk;
            left -= chunk;
        }
    } else {
        uint64_t left = m->size;
        uint32_t last = 0U;
        size_t capacity = m->size < info->block_size ? (size_t)m->size
                                                     : info->block_size;
        if (capacity != 0U) {
            in = (uint8_t *)xx_mem_alloc(capacity);
            out = (uint8_t *)xx_mem_alloc(capacity);
            if (!in || !out) goto done;
        }
        while (left != 0U) {
            uint32_t block = left > info->block_size ? info->block_size
                                                     : (uint32_t)left;
            uint32_t packed;
            const uint8_t *plain;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !pea_read_checked(device, at, fixed, 4U, &check))
                goto done;
            packed = pea_u32(fixed, info->big);
            at += 4;
            if (packed == 0U || packed > block ||
                !pea_read_dev(device, at, in, packed))
                goto done;
            at += packed;
            if (packed == block) {
                plain = in;
            } else {
                size_t written = 0U;
                if (!xx_zlib_stream_decode_memory(in, packed, out, block,
                                                  &written) ||
                    written != block ||
                    !xx_zlib_stream_trailer_matches(in, packed, out, block))
                    goto done;
                plain = out;
            }
            pea_check_update(&check, plain, block);
            if (destination && !pea_write_all(destination, plain, block))
                goto done;
            left -= block;
            last = block;
        }
        if (m->size != 0U) {
            if (!pea_read_checked(device, at, fixed, 4U, &check) ||
                pea_u32(fixed, info->big) != last)
                goto done;
            at += 4;
        }
    }
    if (verify) {
        if (!pea_read_dev(device, at, fixed, 4U) ||
            pea_u32(fixed, false) != check.value)
            goto done;
    }
    ok = true;
done:
    if (header) xx_mem_free(header);
    if (in) xx_mem_free(in);
    if (out) xx_mem_free(out);
    return ok;
}

/* ---- records ----------------------------------------------------------- */

static void pea_stream_free(void *opaque) {
    pea_stream *stream = (pea_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->pool) xx_mem_free(stream->pool);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool pea_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *pea_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pea_set_record(xx_archive_record *record, pea_stream *stream,
                           size_t index) {
    const pea_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    xx_rt_memcpy(stream->name, stream->pool + member->name_at,
                 (size_t)member->name_length + 1U);
    if (member->renamed)
        pea_insert_suffix(stream->name, member->name_length, (uint32_t)index);
    record->header_offset = member->header;
    record->header_size = member->header_size;
    record->data_offset = member->data;
    record->compressed_size = member->packed;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          stream->info.compression) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->date_time >> 16) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->date_time & 0xFFFFU) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

/* ---- public API -------------------------------------------------------- */

void xx_pea_init(xx_pea *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* Byte 8 of the header decides this per archive; the default stands until
     * handle_base_info reads it. */
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_PEA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->first_stream_offset = -1;
    xx_format_set_mime_type(&archive->format, "application/x-pea");
    xx_format_set_extension(&archive->format, "pea");
    archive->format.check_is_valid = xx_pea_check_is_valid;
    archive->format.handle_base_info = xx_pea_handle_base_info;
    archive->format.get_format_size = xx_pea_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pea_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pea_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pea_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pea_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pea_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pea_free_archive_records_reading;
    archive->format.destroy = xx_pea_vtable_destroy;
}

xx_pea *xx_pea_create(xx_io_device *device, int64_t base_address) {
    xx_pea *archive = (xx_pea *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pea_init(archive, device, base_address);
    return archive;
}

void xx_pea_destroy(xx_pea *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it calls back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->first_stream_offset = -1;
}

void xx_pea_free(xx_pea *archive) {
    if (!archive) return;
    xx_pea_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pea_vtable_destroy(Abstractformat *self) {
    xx_pea_destroy((xx_pea *)self);
}

bool xx_pea_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    return xx_pea_probe(self, NULL);
}

bool xx_pea_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pea *archive = (xx_pea *)self;
    pea_walk walk;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    self->is_valid = xx_pea_probe(self, archive);
    if (!self->is_valid) {
        self->format_size = 0;
        return false;
    }
    self->endian = archive->big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = xx_io_total_size(self->device) - self->base_address;
    archive->number_of_records = 0U;
    if (pea_run(self, &walk, pd)) {
        archive->block_size = walk.info.block_size;
        archive->encrypted = walk.encrypted;
        archive->complete = walk.complete;
        archive->number_of_records = walk.count;
        /* Without the end trigger (split set, truncation, encrypted stream)
         * the archive is taken to run to the end of the device. */
        if (walk.complete) self->format_size = walk.extent - self->base_address;
        pea_walk_free(&walk);
    }
    self->number_of_archive_records = archive->number_of_records;
    return true;
}

int64_t xx_pea_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pea_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_pea_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_pea *)self)->number_of_records : 0U;
}

xx_archive_record_state *xx_pea_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    pea_walk walk;
    pea_stream *stream = NULL;
    xx_archive_record_state *state;
    size_t longest = 0U, index;

    if (!pea_run(self, &walk, pd)) return NULL;
    if (walk.count == 0U) {
        pea_walk_free(&walk);
        return NULL;
    }
    for (index = 0U; index < walk.count; ++index)
        if (walk.items[index].name_length > longest)
            longest = walk.items[index].name_length;
    if (!pea_mark_duplicates(walk.items, walk.pool, walk.count) ||
        !(stream = (pea_stream *)xx_mem_calloc(1U, sizeof(*stream))) ||
        !(stream->name = (char *)xx_mem_alloc(longest + PEA_SUFFIX_ROOM))) {
        if (stream) xx_mem_free(stream);
        pea_walk_free(&walk);
        return NULL;
    }
    if (walk.root) xx_mem_free(walk.root);
    stream->info = walk.info;
    stream->items = walk.items;
    stream->pool = walk.pool;
    stream->count = walk.count;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pea_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = pea_stream_free;
    state->total_records = stream->count;
    if (!pea_copy_options(&state->options, options) ||
        !pea_set_record(&state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pea_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pea_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    pea_stream *stream;
    (void)pd;
    if (!self || !state || state->format != self ||
        !(stream = (pea_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = pea_set_record(&state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_pea_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    pea_stream *stream;
    const pea_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (pea_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (member->unsafe) return false;
    path_option = pea_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return pea_extract(self, &stream->info, member, NULL, pd);
    if (!pea_safe_name(stream->name)) return false;
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
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (member->truncated) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = pea_extract(self, &stream->info, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_pea_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint8_t xx_pea_get_version(const xx_pea *archive) {
    return archive ? archive->version : 0U;
}

uint8_t xx_pea_get_compression(const xx_pea *archive) {
    return archive ? archive->compression : 0U;
}

bool xx_pea_is_big_endian(const xx_pea *archive) {
    return archive ? archive->big_endian : false;
}
