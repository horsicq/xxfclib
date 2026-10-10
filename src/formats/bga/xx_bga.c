/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BGA32 archive (.gza / .bza).  xx_bga.h carries the field table.
 *
 * Written from the member layout documented in QBga32's bga_melter.d (by
 * k.inaba, NYSL), used here as a description only: the signed-char header
 * checksum, the arc-type rule for stored members (including its list of
 * archive extensions) and the '\\'-terminated directory entries.
 *
 * Deliberate differences from QBga32:
 *   - the gzip header is parsed (FEXTRA, FNAME, FCOMMENT, FHCRC) instead of
 *     skipping a fixed 10 bytes, and the CRC-32 and ISIZE trailer is checked
 *     (the reference reader checks the CRC too);
 *   - a member must decode to exactly its original size (QBga32 truncates
 *     longer output and keeps shorter output);
 *   - the walk starts at the base address only: QBga32 also searches the
 *     first 64 KiB for a header, which is only needed for self-extractors.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bga/xx_bga.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef BGA
#define XX_BGA_FILE_TYPE XX_FILE_TYPE_BGA
#else
#define XX_BGA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BGA_HEADER_SIZE 28
#define BGA_MAX_RAW_NAME 1024U
#define BGA_MAX_PATH (BGA_MAX_RAW_NAME * 3U)
#define BGA_MAX_MEMBERS 1048576U
#define BGA_MAX_POOL (64U * 1024U * 1024U)
#define BGA_POLL_MASK 0x3ffU
#define BGA_COPY_CHUNK 65536U

#define BGA_METHOD_GZIP 1U
#define BGA_METHOD_BZ2 2U

#define BGA_ATTR_DIRECTORY 0x10U

#define GZ_FTEXT 0x01U
#define GZ_FHCRC 0x02U
#define GZ_FEXTRA 0x04U
#define GZ_FNAME 0x08U
#define GZ_FCOMMENT 0x10U

typedef struct bga_member_s {
    int64_t header; /**< Absolute offset of the member header. */
    int64_t data;   /**< Absolute offset of the member data. */
    uint32_t packed;
    uint32_t original;
    uint16_t date;
    uint16_t time;
    uint8_t attributes;
    uint8_t method;
    uint16_t arc_type;
    uint32_t header_size; /**< 28 + name bytes. */
    uint32_t name_at;
    uint32_t name_length;
    bool stored;
    bool folder;
    bool truncated; /**< The data runs past the end of the device. */
    bool renamed;
} bga_member;

typedef struct bga_key_s {
    uint64_t hash;
    uint32_t index;
} bga_key;

typedef struct bga_walk_s {
    bga_member *items;
    size_t count;
    size_t capacity;
    char *pool;
    size_t pool_length;
    size_t pool_capacity;
    int64_t extent; /**< Absolute end of the last listed member. */
} bga_walk;

typedef struct bga_stream_s {
    bga_member *items;
    size_t count;
    size_t index;
    char *pool;
    char *name; /**< BGA_MAX_PATH + 16 bytes: the current (renamed) path. */
} bga_stream;

typedef struct bga_parsed_s {
    uint32_t packed;
    uint32_t original;
    uint16_t date;
    uint16_t time;
    uint8_t attributes;
    uint8_t method;
    uint16_t arc_type;
    uint32_t raw_length;
    uint8_t raw[BGA_MAX_RAW_NAME];
} bga_parsed;

static bool bga_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t n = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

static bool bga_write_all(xx_io_device *device, const uint8_t *buffer, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t n = xx_io_write(device, buffer + done, size - done);
        if (n <= 0 || (size_t)n > size - done) return false;
        done += (size_t)n;
    }
    return true;
}

static int32_t bga_signed_sum(const uint8_t *data, size_t size)
{
    uint32_t sum = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) sum += (uint32_t)(int32_t)(int8_t)data[index];
    return (int32_t)sum;
}

/* Read and verify the header at `offset`.  Returns 1 when a member header
 * (with its name) is there, 0 when the bytes are not a member header. */
static int bga_parse_header(xx_io_device *device, int64_t offset, int64_t end, bga_parsed *out)
{
    uint8_t head[BGA_HEADER_SIZE];
    uint32_t dir_length, file_length;
    int32_t sum;
    if (end - offset < BGA_HEADER_SIZE || !bga_read_at(device, offset, head, sizeof(head))) return 0;
    if (xx_rt_memcmp(head + 4, "GZIP", 4) == 0) out->method = BGA_METHOD_GZIP;
    else if (xx_rt_memcmp(head + 4, "BZ2\0", 4) == 0) out->method = BGA_METHOD_BZ2;
    else return 0;
    dir_length = xx_data_get_u16(head + 24, 2, 0, false);
    file_length = xx_data_get_u16(head + 26, 2, 0, false);
    out->raw_length = dir_length + file_length;
    if (out->raw_length == 0U || out->raw_length > BGA_MAX_RAW_NAME || (int64_t)out->raw_length > end - offset - BGA_HEADER_SIZE ||
        !bga_read_at(device, offset + BGA_HEADER_SIZE, out->raw, out->raw_length))
        return 0;
    sum = (int32_t)((uint32_t)bga_signed_sum(head + 4, BGA_HEADER_SIZE - 4) + (uint32_t)bga_signed_sum(out->raw, out->raw_length));
    if ((uint32_t)sum != xx_data_get_u32(head, 4, 0, false)) return 0;
    out->packed = xx_data_get_u32(head + 8, 4, 0, false);
    out->original = xx_data_get_u32(head + 12, 4, 0, false);
    out->date = xx_data_get_u16(head + 16, 2, 0, false);
    out->time = xx_data_get_u16(head + 18, 2, 0, false);
    out->attributes = head[20];
    out->arc_type = xx_data_get_u16(head + 22, 2, 0, false);
    return 1;
}

/* QBga32's rule for a member that is stored rather than compressed. */
static bool bga_is_stored(const bga_parsed *p)
{
    static const char *const extensions[] = {"arc", "arj", "bz2", "bza", "cab", "gz", "gza", "lzh", "lzs", "pak", "rar", "taz", "tbz", "tgz", "z", "zip", "zoo"};
    size_t dot, length, index, at;
    char ext[4];
    if (p->arc_type == 2U) return true;
    if (p->arc_type != 0U || p->packed != p->original) return false;
    dot = p->raw_length;
    while (dot > 0U && p->raw[dot - 1U] != '.') --dot;
    if (dot == 0U) return false;
    length = p->raw_length - dot;
    if (length == 0U || length > 3U) return false;
    for (at = 0U; at < length; ++at) {
        uint8_t c = p->raw[dot + at];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a');
        ext[at] = (char)c;
    }
    ext[length] = 0;
    for (index = 0U; index < sizeof(extensions) / sizeof(extensions[0]); ++index)
        if (xx_rt_strcmp(ext, extensions[index]) == 0) return true;
    return false;
}

/* ---- member names ------------------------------------------------------ */

static bool bga_is_sjis_lead(uint8_t c)
{
    return (c >= 0x81U && c <= 0x9fU) || (c >= 0xe0U && c <= 0xfcU);
}

static bool bga_is_sjis_trail(uint8_t c)
{
    return (c >= 0x40U && c <= 0x7eU) || (c >= 0x80U && c <= 0xfcU);
}

static size_t bga_put_escape(char *out, uint8_t c)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* Convert a raw Shift-JIS name; `out` has room for BGA_MAX_PATH + 1. */
static size_t bga_convert_name(const uint8_t *raw, size_t length, char *out)
{
    size_t index = 0U, at = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        if (bga_is_sjis_lead(c) && index + 1U < length && bga_is_sjis_trail(raw[index + 1U])) {
            at += bga_put_escape(out + at, c);
            at += bga_put_escape(out + at, raw[index + 1U]);
            index += 2U;
            continue;
        }
        if (c >= 0x80U || c == (uint8_t)'%') at += bga_put_escape(out + at, c);
        else if (c == (uint8_t)'\\') out[at++] = '/';
        else out[at++] = (char)c;
        ++index;
    }
    out[at] = 0;
    return at;
}

static uint64_t bga_name_hash(const char *name, size_t length)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "%_<index>" before the extension of the last component.  `name`
 * has room for BGA_MAX_PATH + 16 bytes and holds at most BGA_MAX_PATH. */
static void bga_insert_suffix(char *name, size_t length, uint32_t index)
{
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
    for (at = tail + 1U; at > 0U; --at) name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool bga_reserved_component(const char *segment, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ') --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length && xx_rt_memcmp(stem, devices[index], stem_length) == 0) return true;
    return false;
}

static bool bga_safe_name(const char *name)
{
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' || c == 0x7fU || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || bga_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- walk -------------------------------------------------------------- */

static bool bga_add_member(bga_walk *walk, const bga_member *member, const char *name, size_t name_length)
{
    if (walk->count >= BGA_MAX_MEMBERS) return false;
    if (walk->count == walk->capacity) {
        size_t grow = walk->capacity ? walk->capacity * 2U : 64U;
        bga_member *items;
        if (grow > BGA_MAX_MEMBERS) grow = BGA_MAX_MEMBERS;
        items = (bga_member *)xx_mem_alloc(grow * sizeof(*items));
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
        if (grow > BGA_MAX_POOL) return false;
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
    xx_rt_memcpy(walk->pool + walk->pool_length, name, name_length);
    walk->pool[walk->pool_length + name_length] = 0;
    walk->pool_length += name_length + 1U;
    ++walk->count;
    return true;
}

static void bga_walk_free(bga_walk *walk)
{
    if (walk->items) xx_mem_free(walk->items);
    if (walk->pool) xx_mem_free(walk->pool);
    walk->items = NULL;
    walk->pool = NULL;
}

/* The data of a compressed member must start like its codec's stream. */
static bool bga_data_looks_right(xx_io_device *device, const bga_member *m)
{
    uint8_t head[3];
    if (m->stored || m->folder || m->packed == 0U) return true;
    if (m->packed < 3U || !bga_read_at(device, m->data, head, 3U)) return false;
    if (m->method == BGA_METHOD_GZIP) return head[0] == 0x1fU && head[1] == 0x8bU && head[2] == 0x08U;
    return head[0] == 'B' && head[1] == 'Z' && head[2] == 'h';
}

/* Walk the members.  With `first_only` only the first member is examined
 * (the probe); otherwise all members are collected into `walk`.  The first
 * member must be complete and its data must start like its codec's
 * stream. */
static bool bga_run(Abstractformat *format, bga_walk *walk, bool first_only, xx_pd_struct *pd)
{
    bga_parsed *parsed;
    char *name;
    int64_t end, offset;
    bool ok = false;
    xx_mem_zero(walk, sizeof(*walk));
    if (!format || !format->device || format->base_address < 0) return false;
    end = xx_io_total_size(format->device);
    if (end < format->base_address + BGA_HEADER_SIZE + 1) return false;
    parsed = (bga_parsed *)xx_mem_alloc(sizeof(*parsed));
    name = (char *)xx_mem_alloc(BGA_MAX_PATH + 1U);
    if (!parsed || !name) goto done;
    offset = format->base_address;
    walk->extent = offset;
    while (offset <= end - BGA_HEADER_SIZE) {
        bga_member member;
        size_t name_length;
        if ((walk->count & BGA_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!bga_parse_header(format->device, offset, end, parsed)) break;
        xx_mem_zero(&member, sizeof(member));
        member.header = offset;
        member.header_size = BGA_HEADER_SIZE + parsed->raw_length;
        member.data = offset + (int64_t)member.header_size;
        member.packed = parsed->packed;
        member.original = parsed->original;
        member.date = parsed->date;
        member.time = parsed->time;
        member.attributes = parsed->attributes;
        member.method = parsed->method;
        member.arc_type = parsed->arc_type;
        member.stored = bga_is_stored(parsed);
        name_length = bga_convert_name(parsed->raw, parsed->raw_length, name);
        if (name_length != 0U && name[name_length - 1U] == '/') {
            member.folder = true;
            while (name_length != 0U && name[name_length - 1U] == '/') name[--name_length] = 0;
        } else if ((member.attributes & BGA_ATTR_DIRECTORY) != 0U && member.packed == 0U && member.original == 0U) {
            member.folder = true;
        }
        member.truncated = (int64_t)member.packed > end - member.data;
        if (walk->count == 0U) {
            if (member.truncated || !bga_data_looks_right(format->device, &member)) goto done;
            if (first_only) {
                walk->count = 1U;
                walk->extent = member.data + (int64_t)member.packed;
                ok = true;
                goto done;
            }
        }
        if (!bga_add_member(walk, &member, name, name_length)) {
            if (walk->count >= BGA_MAX_MEMBERS) break;
            goto done;
        }
        if (member.truncated) {
            walk->extent = end;
            break;
        }
        offset = member.data + (int64_t)member.packed;
        walk->extent = offset;
    }
    ok = walk->count != 0U;
done:
    if (parsed) xx_mem_free(parsed);
    if (name) xx_mem_free(name);
    if (!ok) bga_walk_free(walk);
    return ok;
}

static int bga_compare_keys(const void *left, const void *right)
{
    const bga_key *a = (const bga_key *)left;
    const bga_key *b = (const bga_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static bool bga_mark_duplicates(bga_member *items, const char *pool, size_t count)
{
    bga_key *keys;
    size_t index;
    if (count < 2U) return true;
    keys = (bga_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (index = 0U; index < count; ++index) {
        keys[index].hash = bga_name_hash(pool + items[index].name_at, items[index].name_length);
        keys[index].index = (uint32_t)index;
    }
    xx_rt_qsort(keys, count, sizeof(*keys), bga_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash && !items[keys[index].index].folder) items[keys[index].index].renamed = true;
    xx_mem_free(keys);
    return true;
}

/* ---- decoding ---------------------------------------------------------- */

/* A write-only device in front of the destination: it counts, checksums
 * and refuses to grow past the member's original size. */
typedef struct bga_sink_s {
    xx_io_device device;
    xx_io_device *target;
    xx_pd_struct *pd;
    uint64_t written;
    uint64_t limit;
    uint32_t crc;
} bga_sink;

static ssize_t bga_sink_write(xx_io_device *self, const void *buffer, size_t size)
{
    bga_sink *sink = (bga_sink *)self->priv;
    if (!sink || (size != 0U && !buffer)) return -1;
    if ((uint64_t)size > sink->limit - sink->written) return -1;
    if (sink->pd && xx_pd_is_stopped(sink->pd)) return -1;
    if (size == 0U) return 0;
    sink->crc = xx_crc32_calc(sink->crc, buffer, size);
    if (sink->target && !bga_write_all(sink->target, (const uint8_t *)buffer, size)) return -1;
    sink->written += (uint64_t)size;
    return (ssize_t)size;
}

static ssize_t bga_sink_read(xx_io_device *self, void *buffer, size_t size)
{
    (void)self;
    (void)buffer;
    (void)size;
    return -1;
}

static int bga_sink_seek64(xx_io_device *self, int64_t offset, int whence)
{
    bga_sink *sink = (bga_sink *)self->priv;
    int64_t at;
    if (!sink) return -1;
    if (whence == SEEK_SET) at = offset;
    else if (whence == SEEK_CUR || whence == SEEK_END) at = (int64_t)sink->written + offset;
    else return -1;
    return at == (int64_t)sink->written ? 0 : -1;
}

static int bga_sink_seek(xx_io_device *self, long offset, int whence)
{
    return bga_sink_seek64(self, (int64_t)offset, whence);
}

static int64_t bga_sink_tell(xx_io_device *self)
{
    bga_sink *sink = (bga_sink *)self->priv;
    return sink ? (int64_t)sink->written : -1;
}

static int bga_sink_close(xx_io_device *self)
{
    (void)self;
    return 0;
}

static void bga_sink_init(bga_sink *sink, xx_io_device *target, uint64_t limit, xx_pd_struct *pd)
{
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->pd = pd;
    sink->limit = limit;
    sink->device.read = bga_sink_read;
    sink->device.write = bga_sink_write;
    sink->device.seek = bga_sink_seek;
    sink->device.seek64 = bga_sink_seek64;
    sink->device.tell = bga_sink_tell;
    sink->device.close = bga_sink_close;
    sink->device.total_size = bga_sink_tell;
    sink->device.priv = sink;
}

static bool bga_copy(xx_io_device *source, int64_t offset, int64_t size, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(BGA_COPY_CHUNK);
    if (!buffer) return false;
    while (done < size) {
        size_t chunk = size - done > (int64_t)BGA_COPY_CHUNK ? BGA_COPY_CHUNK : (size_t)(size - done);
        if ((pd && xx_pd_is_stopped(pd)) || !bga_read_at(source, offset + done, buffer, chunk) || (destination && !bga_write_all(destination, buffer, chunk))) {
            ok = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* Length of the gzip member header at `offset`, bounded by `limit` bytes;
 * 0 when it is not a gzip header. */
static int64_t bga_gzip_header(xx_io_device *device, int64_t offset, int64_t limit)
{
    uint8_t head[10];
    uint8_t buffer[256];
    int64_t at = 10;
    uint8_t flags;
    int pass;
    if (limit < 18 || !bga_read_at(device, offset, head, sizeof(head)) || head[0] != 0x1fU || head[1] != 0x8bU || head[2] != 0x08U || (head[3] & 0xe0U) != 0U) return 0;
    flags = head[3];
    if (flags & GZ_FEXTRA) {
        uint8_t length[2];
        if (limit - at < 2 || !bga_read_at(device, offset + at, length, 2U)) return 0;
        at += 2 + (int64_t)xx_data_get_u16(length, 2, 0, false);
        if (at > limit) return 0;
    }
    for (pass = 0; pass < 2; ++pass) {
        bool found = false;
        if (!(flags & (pass == 0 ? GZ_FNAME : GZ_FCOMMENT))) continue;
        while (!found) {
            size_t chunk = sizeof(buffer), index;
            if (at >= limit) return 0;
            if (limit - at < (int64_t)chunk) chunk = (size_t)(limit - at);
            if (!bga_read_at(device, offset + at, buffer, chunk)) return 0;
            for (index = 0U; index < chunk; ++index)
                if (buffer[index] == 0U) {
                    found = true;
                    break;
                }
            at += (int64_t)index + (found ? 1 : 0);
        }
    }
    if (flags & GZ_FHCRC) at += 2;
    return at + 8 <= limit ? at : 0;
}

/* Decode the member's data into `destination` (validate only when NULL). */
static bool bga_extract(Abstractformat *format, const bga_member *m, xx_io_device *destination, xx_pd_struct *pd)
{
    bga_sink sink;
    int64_t end;
    if (m->folder) return true;
    end = xx_io_total_size(format->device);
    if (m->truncated || (int64_t)m->packed > end - m->data) return false;
    if (m->stored) {
        if (m->packed != m->original) return false;
        return bga_copy(format->device, m->data, (int64_t)m->packed, destination, pd);
    }
    if (m->packed == 0U) return m->original == 0U;
    bga_sink_init(&sink, destination, (uint64_t)m->original, pd);
    if (m->method == BGA_METHOD_GZIP) {
        uint8_t trailer[8];
        int64_t header = bga_gzip_header(format->device, m->data, (int64_t)m->packed);
        int64_t body;
        if (header == 0) return false;
        body = (int64_t)m->packed - header - 8;
        if (!xx_deflate_unpack_device(format->device, m->data + header, body, &sink.device, false, pd)) return false;
        if (!bga_read_at(format->device, m->data + (int64_t)m->packed - 8, trailer, sizeof(trailer))) return false;
        return sink.written == (uint64_t)m->original && xx_data_get_u32(trailer, 4, 0, false) == sink.crc && xx_data_get_u32(trailer + 4, 4, 0, false) == m->original;
    }
    return xx_bzip2_unpack_device(format->device, m->data, (int64_t)m->packed, &sink.device, pd) && sink.written == (uint64_t)m->original;
}

/* ---- records ----------------------------------------------------------- */

static void bga_stream_free(void *opaque)
{
    bga_stream *stream = (bga_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->pool) xx_mem_free(stream->pool);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool bga_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *bga_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool bga_set_record(xx_archive_record *record, bga_stream *stream, size_t index)
{
    const bga_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    xx_rt_memcpy(stream->name, stream->pool + member->name_at, (size_t)member->name_length + 1U);
    if (member->renamed) bga_insert_suffix(stream->name, member->name_length, (uint32_t)index);
    record->header_offset = member->header;
    record->header_size = member->header_size;
    record->data_offset = member->data;
    record->compressed_size = member->packed;
    return xx_archive_record_set_original_name(record, stream->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->original) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->stored ? 0U : member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE, member->date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME, member->time) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->folder);
}

/* ---- public API -------------------------------------------------------- */

void xx_bga_init(xx_bga *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BGA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "gza");
    archive->format.check_is_valid = xx_bga_check_is_valid;
    archive->format.handle_base_info = xx_bga_handle_base_info;
    archive->format.get_format_size = xx_bga_get_format_size;
    archive->format.get_number_of_archive_records = xx_bga_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_bga_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_bga_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_bga_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_bga_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_bga_free_archive_records_reading;
}

xx_bga *xx_bga_create(xx_io_device *device, int64_t base_address)
{
    xx_bga *archive = (xx_bga *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_bga_init(archive, device, base_address);
    return archive;
}

void xx_bga_destroy(xx_bga *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_bga_free(xx_bga *archive)
{
    if (!archive) return;
    xx_bga_destroy(archive);
    xx_mem_free(archive);
}

bool xx_bga_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    bga_walk walk;
    bool ok = bga_run(format, &walk, true, pd);
    bga_walk_free(&walk);
    return ok;
}

bool xx_bga_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    bga_walk walk;
    bool ok = bga_run(format, &walk, false, pd);
    if (ok) {
        xx_bga *archive = (xx_bga *)format;
        const bga_member *first = &walk.items[0];
        archive->number_of_records = walk.count;
        format->number_of_archive_records = walk.count;
        format->format_size = walk.extent - format->base_address;
        xx_format_set_extension(format, first->method == BGA_METHOD_BZ2 ? "bza" : "gza");
        format->is_valid = true;
        format->base_info_handled = true;
    }
    bga_walk_free(&walk);
    return ok;
}

int64_t xx_bga_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_bga_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_bga_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_bga_handle_base_info(format, pd)) ? ((xx_bga *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_bga_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    bga_walk walk;
    bga_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!bga_run(format, &walk, false, pd)) return NULL;
    if (!bga_mark_duplicates(walk.items, walk.pool, walk.count) || !(stream = (bga_stream *)xx_mem_calloc(1U, sizeof(*stream))) ||
        !(stream->name = (char *)xx_mem_alloc(BGA_MAX_PATH + 16U))) {
        if (stream) xx_mem_free(stream);
        bga_walk_free(&walk);
        return NULL;
    }
    stream->items = walk.items;
    stream->pool = walk.pool;
    stream->count = walk.count;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        bga_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = bga_stream_free;
    state->total_records = stream->count;
    if (!bga_copy_options(&state->options, options) || !bga_set_record(&state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_bga_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_bga_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    bga_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (bga_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = bga_set_record(&state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_bga_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    bga_stream *stream;
    const bga_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (bga_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = bga_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return bga_extract(format, member, NULL, pd);
    if (!bga_safe_name(stream->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->name)
                                                                                                  : xx_str_concat(base, stream->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = bga_extract(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_bga_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
