/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Visionaire Studio VIS3 game archive.  xx_visionaire_studio_vis.h carries
 * the field table.
 *
 * Written from the archive structure as documented by Luigi Auriemma's
 * QuickBMS script (Visionaire Player/Studio 0.3.6) and as observed from the
 * behaviour of Deniz Oezmen's VIS3Ext 2.2 on generated archives; no code was
 * taken from either.  The list of game keys is the public vis.key /
 * QuickBMS key list (plain constants).  The record plumbing and the
 * output-limit device follow this library's ypf reader
 * (src/formats/ypf/xx_ypf.c, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/visionaire_studio_vis/xx_visionaire_studio_vis.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef VISIONAIRE_STUDIO_VIS
#define XX_VISIONAIRE_STUDIO_VIS_FILE_TYPE XX_FILE_TYPE_VISIONAIRE_STUDIO_VIS
#else
#define XX_VISIONAIRE_STUDIO_VIS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VIS_PREFIX 8
#define VIS_ENTRY 16U
#define VIS_KEY_SIZE 16U
/* Real games hold tens of thousands of members; this keeps the decrypted
 * index at 16 MiB at most. */
#define VIS_MAX_COUNT 0x100000U
#define VIS_CHUNK_END UINT32_C(0xFFEEFFEE)
#define VIS_FLAG_XOR 0x02U
#define VIS_FLAG_IMAGE 0x08U
#define VIS_FLAG_CHUNKED 0x10U
#define VIS_POLL_MASK 0x3ffU
/* Distinct candidates for the size-field key bytes tried per derivation. */
#define VIS_MAX_CANDIDATES 16U
#define VIS_NAME_SIZE 32U

enum { VIS_METHOD_STORE = 0, VIS_METHOD_ZLIB = 1, VIS_METHOD_CHUNKED = 2 };

/* vis.key (VIS3Ext) and the QuickBMS script list. */
static const char *const vis_known_keys[] = {
    "76093af7c458e3c5", "98d1a1f120d8ce55", "d501025337a850e0",
    "32e617da0a210263", "5b905e9ae76ed5d1", "eae80125d94d10e9",
    "0b5250b3a2ff1440", "af0512966ae16580", "155c703a508c1c8a",
    "8f1ce06b545b33ee", "d0e0111bf880e80f", "d9ef88157bcbcae0",
    "885974be81048e9b", "008d8599a3a27c55", "4c64c6a81b41023b",
    "dcff854efc3e840c", "5ecdd8875a17aeb6", "e09f0ab2d3a24b93",
    "dbd41139252cfd20", "db94f6d45fc49b11", "a986265a4cba51d3",
    "0c994edf1c03d376", "216dd6c65e471ca4", "6b7d3fb08202c948",
    "ce374042a0c24e0d", "485f13a285b3f109",
};

typedef struct vis_member_s {
    uint32_t offset;
    uint32_t stored;
    uint32_t size;
    uint32_t flags;
} vis_member;

typedef struct vis_layout_s {
    int64_t origin;    /**< Absolute offset of "VIS3". */
    int64_t available; /**< Bytes from origin to EOF. */
    int64_t data_base; /**< Absolute offset of member data. */
    int64_t data_size; /**< Sum of the stored sizes. */
    uint32_t count;
    bool big_endian;
    uint8_t key[VIS_KEY_SIZE];
} vis_layout;

typedef struct vis_stream_s {
    vis_member *items;
    size_t count;
    size_t index;
    int64_t data_base;
    uint8_t key[VIS_KEY_SIZE];
    char name[VIS_NAME_SIZE];
} vis_stream;

/* ---- helpers ------------------------------------------------------------ */

static size_t vis_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}

static uint32_t vis_rd32(const uint8_t *p, bool big_endian) {
    if (big_endian)
        return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
               ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void vis_wr32(uint8_t *p, uint32_t v, bool big_endian) {
    if (big_endian) {
        p[0] = (uint8_t)(v >> 24U);
        p[1] = (uint8_t)(v >> 16U);
        p[2] = (uint8_t)(v >> 8U);
        p[3] = (uint8_t)v;
    } else {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8U);
        p[2] = (uint8_t)(v >> 16U);
        p[3] = (uint8_t)(v >> 24U);
    }
}

static bool vis_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vis_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    if (!destination) return true;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool vis_is_hex(uint8_t c) {
    return (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
           (c >= (uint8_t)'a' && c <= (uint8_t)'f');
}

/* ---- XOR view device ----------------------------------------------------- */

/* A read-only device over `source` in the source's own coordinates that
 * XORs the bytes of [start, end) with the key, key index 0 at `start`. */
typedef struct vis_view_s {
    xx_io_device device;
    xx_io_device *source;
    int64_t start;
    int64_t end;
    int64_t pos;
    const uint8_t *key;
} vis_view;

static ssize_t vis_view_read(xx_io_device *self, void *buffer, size_t size) {
    vis_view *view = (vis_view *)self;
    ssize_t amount;
    uint8_t *bytes = (uint8_t *)buffer;
    ssize_t index;
    if (!view || (!buffer && size) || view->pos < 0 ||
        xx_io_seek64(view->source, view->pos, SEEK_SET) != 0)
        return -1;
    amount = xx_io_read(view->source, buffer, size);
    if (amount <= 0) return amount;
    for (index = 0; index < amount; ++index) {
        int64_t at = view->pos + index;
        if (at >= view->start && at < view->end)
            bytes[index] ^= view->key[(size_t)((at - view->start) &
                                               (int64_t)(VIS_KEY_SIZE - 1U))];
    }
    view->pos += amount;
    return amount;
}

static int64_t vis_view_total(xx_io_device *self) {
    vis_view *view = (vis_view *)self;
    return view ? xx_io_total_size(view->source) : -1;
}

static int vis_view_seek64(xx_io_device *self, int64_t offset, int whence) {
    vis_view *view = (vis_view *)self;
    int64_t base;
    if (!view) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = view->pos;
    else if (whence == SEEK_END) base = xx_io_total_size(view->source);
    else return -1;
    if (base < 0 || (offset < 0 && base < -offset) ||
        (offset > 0 && base > INT64_MAX - offset))
        return -1;
    view->pos = base + offset;
    return 0;
}

static int vis_view_seek(xx_io_device *self, long offset, int whence) {
    return vis_view_seek64(self, (int64_t)offset, whence);
}

static int64_t vis_view_tell(xx_io_device *self) {
    vis_view *view = (vis_view *)self;
    return view ? view->pos : -1;
}

/* Returns the device to read the range from: the view when XOR applies. */
static xx_io_device *vis_view_setup(vis_view *view, xx_io_device *source,
                                    int64_t start, int64_t size,
                                    const uint8_t *key, bool apply) {
    if (!apply) return source;
    xx_mem_zero(view, sizeof(*view));
    view->device.read = vis_view_read;
    view->device.seek = vis_view_seek;
    view->device.seek64 = vis_view_seek64;
    view->device.tell = vis_view_tell;
    view->device.total_size = vis_view_total;
    view->source = source;
    view->start = start;
    view->end = start + size;
    view->pos = start;
    view->key = key;
    return &view->device;
}

/* ---- output limit -------------------------------------------------------- */

typedef struct vis_limit_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} vis_limit;

static ssize_t vis_limit_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    vis_limit *limit = (vis_limit *)self;
    if (size > (SIZE_MAX >> 1) ||
        (uint64_t)size > limit->limit - limit->written)
        return -1;
    if (!vis_write_all(limit->target, (const uint8_t *)buffer, size))
        return -1;
    limit->written += (uint64_t)size;
    return (ssize_t)size;
}

/* ---- index --------------------------------------------------------------- */

/* The whole decrypted index must read "HDR" ... "END" with member 0 at
 * offset 0 and every member following the previous one inside the data. */
static bool vis_validate(const uint8_t *plain, uint32_t count, bool big_endian,
                         int64_t data_available, int64_t *data_size) {
    const uint8_t *entry = plain + 3;
    uint64_t expected = 0U;
    uint32_t index;
    if (plain[0] != (uint8_t)'H' || plain[1] != (uint8_t)'D' ||
        plain[2] != (uint8_t)'R')
        return false;
    entry = plain + 3 + (size_t)count * VIS_ENTRY;
    if (entry[0] != (uint8_t)'E' || entry[1] != (uint8_t)'N' ||
        entry[2] != (uint8_t)'D')
        return false;
    entry = plain + 3;
    for (index = 0U; index < count; ++index, entry += VIS_ENTRY) {
        if (vis_rd32(entry, big_endian) != expected) return false;
        expected += vis_rd32(entry + 4, big_endian);
        if (expected > (uint64_t)data_available) return false;
    }
    if (data_size) *data_size = (int64_t)expected;
    return true;
}

static void vis_decrypt(const uint8_t *cipher, uint8_t *plain, size_t size,
                        const uint8_t *key) {
    size_t index;
    for (index = 0U; index < size; ++index)
        plain[index] = (uint8_t)(cipher[index] ^ key[index & (VIS_KEY_SIZE - 1U)]);
}

/* Known plaintext: "HDR" (key 0..2), offset 0 of member 0 (key 3..6), the
 * stored size of member 0 = offset of member 1 (key 7..10), a zero high
 * flags byte of member 0 (key 15, big-endian layout), and one stored member
 * whose size equals its stored size (key 11..14).  Every key byte must be a
 * lowercase hex digit, as all Visionaire keys are. */
static bool vis_derive_key(const uint8_t *cipher, uint8_t *plain,
                           uint32_t count, bool big_endian,
                           int64_t data_available, uint8_t *key,
                           int64_t *data_size) {
    uint8_t candidates[VIS_MAX_CANDIDATES][4];
    size_t candidate_count = 0U, pick, best = 0U, k;
    uint32_t best_score = 0U, index;
    uint8_t first_size[4];
    bool have_best = false;
    const size_t size = 6U + (size_t)count * VIS_ENTRY;

    key[0] = (uint8_t)(cipher[0] ^ (uint8_t)'H');
    key[1] = (uint8_t)(cipher[1] ^ (uint8_t)'D');
    key[2] = (uint8_t)(cipher[2] ^ (uint8_t)'R');
    for (k = 3U; k < 7U; ++k) key[k] = cipher[k];
    if (count >= 2U) {
        uint8_t next[4];
        for (k = 0U; k < 4U; ++k)
            next[k] = (uint8_t)(cipher[19U + k] ^ key[3U + k]);
        for (k = 0U; k < 4U; ++k) key[7U + k] = (uint8_t)(cipher[7U + k] ^ next[k]);
    } else {
        if (data_available > (int64_t)UINT32_MAX) return false;
        vis_wr32(first_size, (uint32_t)data_available, big_endian);
        for (k = 0U; k < 4U; ++k)
            key[7U + k] = (uint8_t)(cipher[7U + k] ^ first_size[k]);
    }
    key[15] = cipher[15];
    for (k = 0U; k < 11U; ++k)
        if (!vis_is_hex(key[k])) return false;
    if (!vis_is_hex(key[15])) return false;
    for (k = 11U; k < 15U; ++k) key[k] = 0U;

    /* The offsets are keyed by bytes 3..10 only, so the chain can be
     * checked before the size-field bytes are known. */
    vis_decrypt(cipher, plain, size, key);
    if (!vis_validate(plain, count, big_endian, data_available, data_size))
        return false;

    for (index = 0U; index < count && candidate_count < VIS_MAX_CANDIDATES;
         ++index) {
        const uint8_t *entry = cipher + 3 + (size_t)index * VIS_ENTRY;
        const uint8_t *stored = plain + 3 + (size_t)index * VIS_ENTRY + 4;
        uint8_t candidate[4];
        size_t seen;
        bool dup = false;
        for (k = 0U; k < 4U; ++k) {
            candidate[k] = (uint8_t)(entry[8U + k] ^ stored[k]);
            if (!vis_is_hex(candidate[k])) break;
        }
        if (k < 4U) continue;
        for (seen = 0U; seen < candidate_count && !dup; ++seen)
            dup = xx_rt_memcmp(candidates[seen], candidate, 4U) == 0;
        if (!dup) xx_rt_memcpy(candidates[candidate_count++], candidate, 4U);
    }
    /* The right bytes make every stored member's two sizes agree. */
    for (pick = 0U; pick < candidate_count; ++pick) {
        uint32_t score = 0U;
        for (index = 0U; index < count; ++index) {
            const uint8_t *entry = cipher + 3 + (size_t)index * VIS_ENTRY;
            const uint8_t *stored = plain + 3 + (size_t)index * VIS_ENTRY + 4;
            for (k = 0U; k < 4U; ++k)
                if ((uint8_t)(entry[8U + k] ^ candidates[pick][k]) != stored[k])
                    break;
            if (k == 4U) ++score;
        }
        if (!have_best || score > best_score) {
            best = pick;
            best_score = score;
            have_best = true;
        }
    }
    if (!have_best) return false;
    xx_rt_memcpy(key + 11, candidates[best], 4U);
    vis_decrypt(cipher, plain, size, key);
    return true;
}

static bool vis_try_order(Abstractformat *format, vis_layout *layout,
                          uint32_t count, bool big_endian, uint8_t **index_out,
                          xx_pd_struct *pd) {
    const int64_t index_size = 6 + (int64_t)count * VIS_ENTRY;
    int64_t data_available;
    uint8_t *cipher = NULL, *plain = NULL;
    size_t key_index;
    bool ok = false;
    if (count == 0U || count > VIS_MAX_COUNT ||
        layout->available - VIS_PREFIX < index_size)
        return false;
    data_available = layout->available - VIS_PREFIX - index_size;
    cipher = (uint8_t *)xx_mem_alloc((size_t)index_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)index_size);
    if (!cipher || !plain ||
        !vis_read_at(format->device, layout->origin + VIS_PREFIX, cipher,
                     (size_t)index_size))
        goto done;
    for (key_index = 0U;
         key_index < sizeof(vis_known_keys) / sizeof(vis_known_keys[0]);
         ++key_index) {
        const uint8_t *key = (const uint8_t *)vis_known_keys[key_index];
        /* "HDR" first: rejects a wrong key after three bytes. */
        if ((uint8_t)(cipher[0] ^ key[0]) != (uint8_t)'H' ||
            (uint8_t)(cipher[1] ^ key[1]) != (uint8_t)'D' ||
            (uint8_t)(cipher[2] ^ key[2]) != (uint8_t)'R')
            continue;
        vis_decrypt(cipher, plain, (size_t)index_size, key);
        if (vis_validate(plain, count, big_endian, data_available,
                         &layout->data_size)) {
            xx_rt_memcpy(layout->key, key, VIS_KEY_SIZE);
            ok = true;
            goto done;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) goto done;
    ok = vis_derive_key(cipher, plain, count, big_endian, data_available,
                        layout->key, &layout->data_size);
done:
    if (cipher) xx_mem_free(cipher);
    if (ok) {
        layout->count = count;
        layout->big_endian = big_endian;
        layout->data_base = layout->origin + VIS_PREFIX + index_size;
        if (index_out) *index_out = plain;
        else xx_mem_free(plain);
    } else if (plain) {
        xx_mem_free(plain);
    }
    return ok;
}

/* Resolve the byte order and key.  With index_out the decrypted index is
 * handed to the caller (xx_mem_free it). */
static bool vis_resolve(Abstractformat *format, vis_layout *layout,
                        uint8_t **index_out, xx_pd_struct *pd) {
    uint8_t head[VIS_PREFIX];
    int64_t total;
    uint32_t be, le;
    bool first_be;
    if (index_out) *index_out = NULL;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    layout->origin = format->base_address;
    layout->available = total - format->base_address;
    if (layout->available < VIS_PREFIX + 6 + (int64_t)VIS_ENTRY ||
        !vis_read_at(format->device, layout->origin, head, sizeof(head)) ||
        head[0] != (uint8_t)'V' || head[1] != (uint8_t)'I' ||
        head[2] != (uint8_t)'S' || head[3] != (uint8_t)'3')
        return false;
    be = vis_rd32(head + 4, true);
    le = vis_rd32(head + 4, false);
    /* VIS3Ext: big-endian unless the little-endian count is smaller. */
    first_be = !(le < be);
    if (vis_try_order(format, layout, first_be ? be : le, first_be,
                      index_out, pd))
        return true;
    if (be == le || (pd && xx_pd_is_stopped(pd))) return false;
    return vis_try_order(format, layout, first_be ? le : be, !first_be,
                         index_out, pd);
}

/* ---- member names ---------------------------------------------------------- */

/* VIS3Ext's default naming: flags 0x10 marks script/XML data, 0x08 an
 * image. */
static void vis_make_name(char *out, size_t index, uint32_t offset,
                          uint32_t flags) {
    static const char hex[] = "0123456789abcdef";
    const char *extension = (flags & VIS_FLAG_CHUNKED) ? "xml"
                            : (flags & VIS_FLAG_IMAGE) ? "png"
                                                       : "dat";
    char digits[24];
    size_t count = 0U, at = 0U, k;
    size_t value = index;
    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count < 5U) digits[count++] = '0';
    while (count) out[at++] = digits[--count];
    out[at++] = '_';
    for (k = 0U; k < 8U; ++k)
        out[at++] = hex[(offset >> (28U - 4U * (uint32_t)k)) & 0x0fU];
    out[at++] = '.';
    for (k = 0U; extension[k]; ++k) out[at++] = extension[k];
    out[at] = 0;
}

/* ---- unpacking ------------------------------------------------------------- */

static bool vis_copy(xx_io_device *source, int64_t offset, int64_t size,
                     xx_io_device *destination, xx_pd_struct *pd) {
    const size_t capacity = vis_capacity();
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
            !vis_read_at(source, offset + done, buffer, chunk) ||
            !vis_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* One zlib stream of `packed` bytes at `offset` whose output must end
 * exactly at limit->limit. */
static bool vis_inflate(xx_io_device *source, int64_t offset, int64_t packed,
                        vis_limit *limit, xx_pd_struct *pd) {
    uint8_t header[2];
    if (packed < 2 || !vis_read_at(source, offset, header, sizeof(header)) ||
        !xx_zlib_stream_header_is_valid(header, sizeof(header)))
        return false;
    return xx_deflate_unpack_device(source, offset + 2, packed - 2,
                                    &limit->device, false, pd) &&
           limit->written == limit->limit;
}

static bool vis_unpack_chunks(xx_io_device *device, int64_t data,
                              const vis_member *member, const uint8_t *key,
                              xx_io_device *destination, xx_pd_struct *pd) {
    vis_limit limit;
    int64_t pos = 0;
    const int64_t stored = (int64_t)member->stored;
    uint32_t steps = 0U;
    xx_mem_zero(&limit, sizeof(limit));
    limit.device.write = vis_limit_write;
    limit.target = destination;
    while (pos + 8 <= stored) {
        uint8_t head[8];
        uint32_t usize, csize;
        vis_view view;
        xx_io_device *source;
        if ((++steps & VIS_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (!vis_read_at(device, data + pos, head, sizeof(head))) return false;
        usize = vis_rd32(head, true);
        csize = vis_rd32(head + 4, true);
        pos += 8;
        if (csize == VIS_CHUNK_END) break;
        if ((int64_t)csize > stored - pos ||
            (uint64_t)usize > (uint64_t)member->size - limit.written)
            return false;
        if (usize == 0U && csize == 0U) continue;
        limit.limit = limit.written + usize;
        source = vis_view_setup(&view, device, data + pos, (int64_t)csize, key,
                                (member->flags & VIS_FLAG_XOR) != 0U);
        if (!vis_inflate(source, data + pos, (int64_t)csize, &limit, pd))
            return false;
        pos += (int64_t)csize;
    }
    return limit.written == (uint64_t)member->size;
}

static int vis_method(xx_io_device *device, int64_t data,
                      const vis_member *member, const uint8_t *key) {
    uint8_t header[2];
    if (member->stored == member->size) return VIS_METHOD_STORE;
    if (member->flags & VIS_FLAG_CHUNKED) return VIS_METHOD_CHUNKED;
    /* VIS3Ext inflates the whole member; the QuickBMS script also knows
     * members that are one chunk with its 8-byte size prefix. */
    if (member->stored >= 2U && vis_read_at(device, data, header, 2U)) {
        if (member->flags & VIS_FLAG_XOR) {
            header[0] ^= key[0];
            header[1] ^= key[1];
        }
        if (xx_zlib_stream_header_is_valid(header, 2U)) return VIS_METHOD_ZLIB;
        return VIS_METHOD_CHUNKED;
    }
    return VIS_METHOD_ZLIB;
}

static bool vis_unpack_member(xx_io_device *device, int64_t data,
                              const vis_member *member, const uint8_t *key,
                              xx_io_device *destination, xx_pd_struct *pd) {
    vis_view view;
    xx_io_device *source;
    vis_limit limit;
    const bool apply = (member->flags & VIS_FLAG_XOR) != 0U;
    switch (vis_method(device, data, member, key)) {
    case VIS_METHOD_STORE:
        source = vis_view_setup(&view, device, data, (int64_t)member->stored,
                                key, apply);
        return vis_copy(source, data, (int64_t)member->stored, destination, pd);
    case VIS_METHOD_ZLIB:
        source = vis_view_setup(&view, device, data, (int64_t)member->stored,
                                key, apply);
        xx_mem_zero(&limit, sizeof(limit));
        limit.device.write = vis_limit_write;
        limit.target = destination;
        limit.limit = member->size;
        return vis_inflate(source, data, (int64_t)member->stored, &limit, pd);
    default:
        return vis_unpack_chunks(device, data, member, key, destination, pd);
    }
}

/* ---- records --------------------------------------------------------------- */

static void vis_stream_free(void *opaque) {
    vis_stream *stream = (vis_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool vis_open_stream(Abstractformat *format, vis_stream **result,
                            xx_pd_struct *pd) {
    vis_layout layout;
    uint8_t *plain = NULL;
    vis_stream *stream = NULL;
    uint32_t index;
    if (!result || !vis_resolve(format, &layout, &plain, pd)) return false;
    stream = (vis_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->items = (vis_member *)xx_mem_calloc(layout.count,
                                                sizeof(*stream->items));
    if (!stream->items) goto fail;
    for (index = 0U; index < layout.count; ++index) {
        const uint8_t *entry = plain + 3 + (size_t)index * VIS_ENTRY;
        stream->items[index].offset = vis_rd32(entry, layout.big_endian);
        stream->items[index].stored = vis_rd32(entry + 4, layout.big_endian);
        stream->items[index].size = vis_rd32(entry + 8, layout.big_endian);
        stream->items[index].flags = vis_rd32(entry + 12, layout.big_endian);
    }
    xx_mem_free(plain);
    stream->count = layout.count;
    stream->data_base = layout.data_base;
    xx_rt_memcpy(stream->key, layout.key, VIS_KEY_SIZE);
    *result = stream;
    return true;
fail:
    if (plain) xx_mem_free(plain);
    vis_stream_free(stream);
    return false;
}

static bool vis_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vis_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool vis_set_record(Abstractformat *format, xx_archive_record *record,
                           vis_stream *stream, size_t index) {
    const vis_member *member = &stream->items[index];
    const int64_t data = stream->data_base + (int64_t)member->offset;
    int method = vis_method(format->device, data, member, stream->key);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    vis_make_name(stream->name, index, member->offset, member->flags);
    record->header_offset =
        format->base_address + VIS_PREFIX + 3 + (int64_t)index * VIS_ENTRY;
    record->header_size = VIS_ENTRY;
    record->data_offset = data;
    record->compressed_size = (int64_t)member->stored;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->stored) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          (uint64_t)method) &&
           xx_archive_record_set_meta_bool(
               record, XX_META_ID_IS_ENCRYPTED,
               (member->flags & VIS_FLAG_IMAGE) != 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API ------------------------------------------------------------ */

void xx_visionaire_studio_vis_init(xx_visionaire_studio_vis *archive,
                                   xx_io_device *device,
                                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_VISIONAIRE_STUDIO_VIS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "vis");
    archive->format.check_is_valid = xx_visionaire_studio_vis_check_is_valid;
    archive->format.handle_base_info =
        xx_visionaire_studio_vis_handle_base_info;
    archive->format.get_format_size = xx_visionaire_studio_vis_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_visionaire_studio_vis_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_visionaire_studio_vis_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_visionaire_studio_vis_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_visionaire_studio_vis_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_visionaire_studio_vis_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_visionaire_studio_vis_free_archive_records_reading;
}

xx_visionaire_studio_vis *xx_visionaire_studio_vis_create(
    xx_io_device *device, int64_t base_address) {
    xx_visionaire_studio_vis *archive =
        (xx_visionaire_studio_vis *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_visionaire_studio_vis_init(archive, device, base_address);
    return archive;
}

void xx_visionaire_studio_vis_destroy(xx_visionaire_studio_vis *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_visionaire_studio_vis_free(xx_visionaire_studio_vis *archive) {
    if (!archive) return;
    xx_visionaire_studio_vis_destroy(archive);
    xx_mem_free(archive);
}

bool xx_visionaire_studio_vis_check_is_valid(Abstractformat *format,
                                             xx_pd_struct *pd) {
    vis_layout layout;
    return vis_resolve(format, &layout, NULL, pd);
}

bool xx_visionaire_studio_vis_handle_base_info(Abstractformat *format,
                                               xx_pd_struct *pd) {
    vis_layout layout;
    xx_visionaire_studio_vis *archive;
    size_t k;
    if (!vis_resolve(format, &layout, NULL, pd)) return false;
    archive = (xx_visionaire_studio_vis *)format;
    archive->number_of_records = layout.count;
    archive->big_endian = layout.big_endian;
    for (k = 0U; k < VIS_KEY_SIZE; ++k) archive->key[k] = (char)layout.key[k];
    archive->key[VIS_KEY_SIZE] = 0;
    format->endian = layout.big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.data_base - layout.origin + layout.data_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_visionaire_studio_vis_get_format_size(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_visionaire_studio_vis_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_visionaire_studio_vis_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_visionaire_studio_vis_handle_base_info(format, pd))
               ? ((xx_visionaire_studio_vis *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_visionaire_studio_vis_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vis_stream *stream;
    xx_archive_record_state *state;
    if (!vis_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vis_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vis_stream_free;
    state->total_records = stream->count;
    if (!vis_copy_options(&state->options, options) ||
        !vis_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_visionaire_studio_vis_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_visionaire_studio_vis_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    vis_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vis_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = vis_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_visionaire_studio_vis_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    vis_stream *stream;
    const vis_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    int64_t data;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vis_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    data = stream->data_base + (int64_t)member->offset;
    path_option = vis_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode into nothing, which verifies the member. */
        return vis_unpack_member(format->device, data, member, stream->key,
                                 NULL, pd);
    /* Names are generated ("00000_00000000.dat" style), so they are always safe. */
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
        created = destination != NULL;
        if (!destination) goto done;
        result = vis_unpack_member(format->device, data, member, stream->key,
                                   destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_visionaire_studio_vis_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
