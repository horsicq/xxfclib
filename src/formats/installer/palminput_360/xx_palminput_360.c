/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * 360 PalmInput (360 输入法) setup (PalmInputSetup.exe, versions 2.7 and
 * 3.1 measured). The installed files travel in the resource of the custom
 * type "COMPRESSEDDATA" (id 138): a directory, then the compressed groups.
 *
 *   u32 group count, u64 tag
 *   per group (a wildcard pattern such as "..\..\Files\Tool\*.dll"):
 *     u8 LZMA properties, u32 dictionary size
 *     u32 name length (UTF-16 units), the name, u16 0
 *     u32 unpacked size, u32 packed size, u16 check, u16 id, u32 file count
 *     per file: u32 name length, the name (UTF-16, "..\..\Files\Tool\
 *               360Base.dll"), u16 0, u32 size, u32 0, u16 0, u16 0
 *   then the groups' blobs back to back in directory order: raw LZMA1 (no
 *   end marker) of exactly the packed size, decoding to the unpacked size,
 *   which is the concatenation of the group's files in order
 *
 * The last blob ends at the resource end and every group's file sizes add
 * up to its unpacked size. Members are the files, named by their path below
 * "Files\"; a member is read by decoding its group once (the last decoded
 * group is kept) and copying its slice. Nothing is executed.
 */
#include "xxfclib/formats/installer/palminput_360/xx_palminput_360.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_walk.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/io/xx_io.h"

#define PI_HEADER 12U
#define PI_MAX_GROUPS 4096U
#define PI_MAX_NAME 600U
#define PI_MAX_FILES 65536U
#define PI_CHUNK 65536U
#define PI_MAX_UNPACKED (INT64_C(1) * 1024 * 1024 * 1024)

static const char pi_type[] = "COMPRESSEDDATA";
static const char pi_files_prefix[] = "Files\\";

typedef struct pi_member {
    uint32_t group;
    uint8_t props[5];
    int64_t group_at;       /* the blob, relative to the carrier's base */
    int64_t group_packed;
    int64_t group_unpacked;
    int64_t slice;          /* the file's offset inside the decoded group */
} pi_member;
typedef struct pi_cache {
    uint32_t group;
    xx_io_device *store;
} pi_cache;
typedef struct pi_parse {
    const rw_leaf_info *leaf;
    bool done;
} pi_parse;

static void pi_cache_free(xx_palminput_360 *r)
{
    pi_cache *c = (pi_cache *)r->cache;
    if (c) {
        if (c->store) xx_io_close(c->store);
        xx_mem_free(c);
        r->cache = NULL;
    }
}

/* The decoded group of a member, from the cache or decoded now. */
static xx_io_device *pi_group(Abstractformat *f, const pi_member *ctx, xx_pd_struct *pd)
{
    xx_palminput_360 *r = (xx_palminput_360 *)f;
    pi_cache *c = (pi_cache *)r->cache;
    xx_io_device *store;
    if (c && c->store && c->group == ctx->group) return c->store;
    if (!c) {
        c = (pi_cache *)xx_mem_calloc(1U, sizeof(*c));
        if (!c) return NULL;
        r->cache = c;
    }
    if (c->store) {
        xx_io_close(c->store);
        c->store = NULL;
    }
    store = xx_io_temp_open();
    if (!store) return NULL;
    if (!xx_lzma_unpack_device(f->device, f->base_address + ctx->group_at, ctx->group_packed, ctx->props, 5U, ctx->group_unpacked, store, pd) ||
        xx_io_total_size(store) != ctx->group_unpacked) {
        xx_io_close(store);
        return NULL;
    }
    c->store = store;
    c->group = ctx->group;
    return store;
}

/* The member's slice of its decoded group into the destination (none:
 * validation decodes the group only). */
static bool pi_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    pi_member *ctx = (pi_member *)m->context;
    xx_io_device *store;
    uint8_t *buffer;
    int64_t done = 0;
    bool ok = true;
    if (!ctx || m->size < 0 || ctx->slice < 0 || ctx->slice > ctx->group_unpacked - m->size) return false;
    store = pi_group(f, ctx, pd);
    if (!store) return false;
    if (!device) return !(pd && xx_pd_is_stopped(pd));
    buffer = (uint8_t *)xx_mem_alloc(PI_CHUNK);
    if (!buffer) return false;
    while (ok && done < m->size) {
        size_t n = m->size - done > (int64_t)PI_CHUNK ? PI_CHUNK : (size_t)(m->size - done);
        if ((pd && xx_pd_is_stopped(pd)) || !xx_io_read_at(store, ctx->slice + done, buffer, n)) {
            ok = false;
            break;
        }
        {
            size_t written = 0;
            while (written < n) {
                ssize_t w = xx_io_write(device, buffer + written, n - written);
                if (w <= 0 || (size_t)w > n - written) {
                    ok = false;
                    break;
                }
                written += (size_t)w;
            }
        }
        done += (int64_t)n;
    }
    xx_mem_free(buffer);
    return ok && !(pd && xx_pd_is_stopped(pd));
}

/* A UTF-16LE name as UTF-8 (BMP; surrogates become '_'), with "..\" steps
 * and the "Files\" root dropped and '\' turned into '/'. The label gets the
 * last path element. */
static char *pi_name(const uint8_t *raw, size_t units, const char **label)
{
    char *out = (char *)xx_mem_alloc(units * 3U + 1U);
    size_t i, n = 0, start = 0;
    if (!out) return NULL;
    for (i = 0; i < units; ++i) {
        uint16_t c = xx_data_get_u16(raw + 2U * i, 2, 0, false);
        if (c == '\\' || c == '/') {
            if (n >= 2 && out[n - 1] == '.' && out[n - 2] == '.' && (n == 2 || out[n - 3] == '/')) {
                n -= 2; /* a "..\" step: dropped */
                continue;
            }
            out[n++] = '/';
        } else if (c < 0x80U) {
            out[n++] = c < 0x20U || c == 0x7FU ? '_' : (char)c;
        } else if (c < 0x800U) {
            out[n++] = (char)(0xC0U | (c >> 6));
            out[n++] = (char)(0x80U | (c & 0x3FU));
        } else if (c >= 0xD800U && c < 0xE000U) {
            out[n++] = '_';
        } else {
            out[n++] = (char)(0xE0U | (c >> 12));
            out[n++] = (char)(0x80U | ((c >> 6) & 0x3FU));
            out[n++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[n] = 0;
    while (start < n && out[start] == '/') ++start;
    if (n - start > sizeof(pi_files_prefix) - 1U && xx_rt_memcmp(out + start, "Files/", 6) == 0) start += 6;
    if (start) xx_rt_memmove(out, out + start, n - start + 1U);
    n -= start;
    *label = out;
    for (i = 0; i < n; ++i) {
        if (out[i] == '/') *label = out + i + 1;
    }
    return out;
}

/* The directory and the groups of the resource at `leaf`. */
static bool pi_parse_resource(Abstractformat *f, pm_stream *s, const rw_leaf_info *leaf, xx_pd_struct *pd)
{
    uint8_t h[24], *name = NULL;
    int64_t at = leaf->at, end = leaf->at + leaf->bytes, blob;
    uint32_t groups, g, files = 0;
    size_t first = s->count, index;
    if (leaf->bytes < PI_HEADER + 32U || !pm_read(f, at, h, PI_HEADER)) return false;
    groups = xx_data_get_u32(h, 4, 0, false);
    if (!groups || groups > PI_MAX_GROUPS) return false;
    at += PI_HEADER;
    name = (uint8_t *)xx_mem_alloc(2U * PI_MAX_NAME);
    if (!name) return false;
    for (g = 0; g < groups; ++g) {
        uint8_t props[5];
        uint32_t units, unpacked, packed, count, i;
        int64_t slice = 0;
        if (carrier_stop(pd) || end - at < 9 || !pm_read(f, at, props, 5) || !pm_read(f, at + 5, h, 4)) goto fail;
        units = xx_data_get_u32(h, 4, 0, false);
        if (props[0] >= 225U || !units || units > PI_MAX_NAME || end - at < 9 + 2 * (int64_t)units + 2 + 16) goto fail;
        at += 9 + 2 * (int64_t)units; /* the pattern: not a member */
        if (!pm_read(f, at, h, 18)) goto fail;
        if (xx_data_get_u16(h, 2, 0, false) != 0) goto fail;
        unpacked = xx_data_get_u32(h + 2, 4, 0, false);
        packed = xx_data_get_u32(h + 6, 4, 0, false);
        count = xx_data_get_u32(h + 14, 4, 0, false);
        at += 18;
        if (!packed || !unpacked || (uint64_t)unpacked > (uint64_t)PI_MAX_UNPACKED || count > PI_MAX_FILES - files) goto fail;
        files += count;
        for (i = 0; i < count; ++i) {
            uint32_t n, size;
            pi_member *ctx;
            pm_member *m;
            char *path;
            const char *label;
            if (carrier_stop(pd) || end - at < 4 || !pm_read(f, at, h, 4)) goto fail;
            n = xx_data_get_u32(h, 4, 0, false);
            if (!n || n > PI_MAX_NAME || end - at < 4 + 2 * (int64_t)n + 14 || !pm_read(f, at + 4, name, 2U * n) || !pm_read(f, at + 4 + 2 * (int64_t)n, h, 14)) goto fail;
            if (xx_data_get_u16(h, 2, 0, false) != 0) goto fail;
            size = xx_data_get_u32(h + 2, 4, 0, false);
            if (size > unpacked - (uint64_t)slice) goto fail;
            at += 4 + 2 * (int64_t)n + 14;
            path = pi_name(name, n, &label);
            if (!path) goto fail;
            if (!*label || !pm_add(f, s, label, leaf->at, 0)) {
                xx_mem_free(path);
                goto fail;
            }
            m = &s->items[s->count - 1U];
            m->display_name = path;
            ctx = (pi_member *)xx_mem_calloc(1U, sizeof(*ctx));
            if (!ctx) goto fail;
            ctx->group = g;
            xx_rt_memcpy(ctx->props, props, 5U);
            ctx->group_packed = (int64_t)packed;
            ctx->group_unpacked = (int64_t)unpacked;
            ctx->slice = slice;
            m->context = ctx;
            m->free_context = xx_mem_free;
            m->read_all = pi_read_all;
            m->size = m->packed_size = (int64_t)size; /* the group's share is not known per file */
            m->compression_method = 14U;
            slice += size;
        }
        if ((uint64_t)slice != unpacked) goto fail;
    }
    /* The blobs follow the directory in group order and fill the resource. */
    blob = at;
    for (g = 0, index = first; g < groups; ++g) {
        int64_t packed = -1;
        for (; index < s->count; ++index) {
            pi_member *ctx = (pi_member *)s->items[index].context;
            if (ctx->group != g) break;
            packed = ctx->group_packed;
            ctx->group_at = blob;
        }
        if (packed < 0) goto fail; /* a group without files: not seen; rejected */
        if (packed > end - blob) goto fail;
        blob += packed;
    }
    if (blob != end) goto fail;
    xx_mem_free(name);
    return s->count > first;
fail:
    xx_mem_free(name);
    return false;
}

static bool pi_visit(Abstractformat *f, pm_stream *s, const rw_leaf_info *leaf, void *context, xx_pd_struct *pd)
{
    pi_parse *p = (pi_parse *)context;
    if (p->done || !leaf->type_name || xx_rt_strlen(leaf->type_name) != sizeof(pi_type) - 1U || xx_rt_memcmp(leaf->type_name, pi_type, sizeof(pi_type) - 1U) != 0) return true;
    if (!pi_parse_resource(f, s, leaf, pd)) return false;
    p->done = true;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    rw_walk w;
    pi_parse p = {NULL, false};
    if (!rw_open(f, &w, pd) || !rw_walk_leaves(f, s, &w, pi_visit, &p, pd)) return false;
    if (s->size < w.image_end) s->size = w.image_end;
    /* The members share the resource's extent (they are slices of decoded
     * groups), so the extent check of carrier_members does not apply. */
    return p.done && !carrier_stop(pd);
}

void xx_palminput_360_init(xx_palminput_360 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PALMINPUT_360, "exe");
    }
}
xx_palminput_360 *xx_palminput_360_create(xx_io_device *d, int64_t b)
{
    xx_palminput_360 *r = (xx_palminput_360 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_palminput_360_init(r, d, b);
    return r;
}
void xx_palminput_360_destroy(xx_palminput_360 *r)
{
    if (!r) return;
    pi_cache_free(r);
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_palminput_360_free(xx_palminput_360 *r)
{
    if (r) {
        xx_palminput_360_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_palminput_360_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_palminput_360_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
