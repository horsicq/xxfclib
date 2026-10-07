/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original implementation from published BSD on-disk structures and macros.
 */
#include "xxfclib/formats/ufs1/xx_ufs1.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/store/xx_store.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"
#ifdef UFS1
#define U1_TYPE XX_FILE_TYPE_UFS1
#else
#define U1_TYPE XX_FILE_TYPE_UNKNOWN
#endif
#define U1_FRAGS UINT32_C(0x08000000)
#define U1_STEPS UINT32_C(0x01000000)
#define U1_ITEMS 100000U
#define U1_RUNS 1000000U
#define U1_MEMORY UINT64_C(134217728)
#define U1_NAMES UINT64_C(67108864)
#define U1_PATH 4096U
#define U1_DEPTH 64U
#define U1_COPY 65536U
#define U1_REG 0100000U
#define U1_DIR 0040000U
#define U1_LNK 0120000U
typedef struct u1_geo_s {
    int64_t base;
    uint64_t size;
    uint32_t blocks, block, fragment, frag, groups, ipg, fpg;
    uint32_t sblk, cblk, iblk, dblk, cgoffset, cgmask, cgsize;
    uint32_t csaddr, cssize, nindir, inopb, maxlink;
    bool be;
} u1_geo;
typedef struct u1_run_s { uint64_t logical; uint32_t first, count; } u1_run;
typedef struct u1_inode_s {
    uint32_t number, disk_blocks, flags, direct[12], indirect[3];
    uint16_t mode, nlink;
    uint64_t size, allocated;
    int64_t header;
    u1_run *runs;
    size_t count, capacity;
    uint32_t links, children;
    bool walked;
} u1_inode;
typedef struct u1_member_s { char *name; uint32_t inode; } u1_member;
typedef struct u1_view_s {
    u1_geo geo;
    uint8_t *allocated, *claimed, *inodes;
    size_t bitmap, inode_bitmap;
    u1_inode *nodes;
    size_t nodes_count, nodes_capacity;
    uint32_t *inode_hash;
    size_t inode_hash_capacity;
    u1_member *members;
    size_t count, capacity;
    uint32_t *name_hash;
    size_t name_hash_capacity;
    uint32_t steps, slots, runs;
    uint64_t memory, name_bytes;
    size_t refs;
} u1_view;
typedef struct u1_cursor_s { u1_view *view; size_t index; } u1_cursor;
static bool u1_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool u1_read(xx_io_device *dev, int64_t at, void *buf, size_t n,
                    xx_pd_struct *pd) {
    int64_t saved;
    size_t done = 0;
    bool ok = false;
    if (!dev || at < 0 || u1_stop(pd) || (!buf && n)) return false;
    saved = xx_io_tell(dev);
    if (saved < 0) return false;
    if (xx_io_seek64(dev, at, SEEK_SET) == 0) {
        while (done < n && !u1_stop(pd)) {
            ssize_t got = xx_io_read(dev, (uint8_t *)buf + done, n - done);
            if (got <= 0 || (size_t)got > n - done || u1_stop(pd)) break;
            done += (size_t)got;
        }
        ok = done == n && !u1_stop(pd);
    }
    if (xx_io_seek64(dev, saved, SEEK_SET) != 0) ok = false;
    return ok;
}
static bool u1_rel(xx_io_device *dev, const u1_geo *g, uint64_t at, void *buf,
                   size_t n, xx_pd_struct *pd) {
    return at <= g->size && n <= g->size - at &&
           at <= (uint64_t)INT64_MAX - (uint64_t)g->base &&
           u1_read(dev, g->base + (int64_t)at, buf, n, pd);
}
static bool u1_power(uint32_t n) { return n && !(n & (n - 1U)); }
static uint64_t u1_cgstart(const u1_geo *g, uint32_t cg) {
    return (uint64_t)cg * g->fpg + (uint64_t)g->cgoffset * (cg & ~g->cgmask);
}
static bool u1_geometry(Abstractformat *f, u1_geo *g, xx_pd_struct *pd) {
    uint8_t b[1376];
    int64_t total;
    uint32_t i, bshift = 0, fshift = 0, fragshift = 0, x;
    if (!f || !f->device || f->base_address < 0 || u1_stop(pd)) return false;
    total = xx_io_total_size(f->device);
    if (total < f->base_address || total - f->base_address < 8192 + 1376 ||
        !u1_read(f->device, f->base_address + 8192, b, sizeof(b), pd)) return false;
    xx_mem_zero(g, sizeof(*g));
    if (xx_data_get_u32(b + 1372, 4, 0, false) == UINT32_C(0x011954)) g->be = false;
    else if (xx_data_get_u32(b + 1372, 4, 0, true) == UINT32_C(0x011954)) g->be = true;
    else return false;
    g->base = f->base_address;
#define U1_FS(field, offset) g->field = xx_data_get_u32(b + offset, 4, 0, g->be)
    U1_FS(sblk,8); U1_FS(cblk,12); U1_FS(iblk,16); U1_FS(dblk,20);
    U1_FS(cgoffset,24); U1_FS(cgmask,28); U1_FS(blocks,36); U1_FS(groups,44);
    U1_FS(block,48); U1_FS(fragment,52); U1_FS(frag,56);
    U1_FS(nindir,116); U1_FS(inopb,120); U1_FS(csaddr,152);
    U1_FS(cssize,156); U1_FS(cgsize,160); U1_FS(ipg,184); U1_FS(fpg,188);
    U1_FS(maxlink,1320);
#undef U1_FS
    if (!u1_power(g->block) || g->block < 4096U || g->block > 65536U ||
        !u1_power(g->fragment) || g->fragment < 512U || g->fragment > g->block ||
        g->frag != g->block / g->fragment || g->frag > 8U ||
        !g->blocks || g->blocks > U1_FRAGS || !g->groups || g->groups > 4096U ||
        !g->fpg || g->fpg % g->frag ||
        (uint64_t)(g->groups - 1U) * g->fpg >= g->blocks ||
        (uint64_t)g->groups * g->fpg < g->blocks ||
        !g->ipg || g->ipg % (g->block / 128U) ||
        (uint64_t)g->ipg * g->groups > UINT32_C(0x01000000) ||
        g->nindir != g->block / 4U || g->inopb != g->block / 128U ||
        g->sblk >= g->cblk || g->cblk >= g->iblk || g->iblk >= g->dblk ||
        g->sblk % g->frag || g->cblk % g->frag || g->iblk % g->frag ||
        (uint64_t)g->iblk + ((uint64_t)g->ipg * 128U + g->fragment - 1U) /
           g->fragment > g->dblk ||
        g->cgsize < 104U || g->cgsize > g->block ||
        xx_data_get_u32(b + 104, 4, 0, g->be) < sizeof(b) ||
        xx_data_get_u32(b + 104, 4, 0, g->be) > 8192U ||
        (uint64_t)(g->cblk - g->sblk) * g->fragment < xx_data_get_u32(b + 104, 4, 0, g->be) ||
        (uint64_t)g->dblk * g->fragment < 8192U + xx_data_get_u32(b + 104, 4, 0, g->be) ||
        xx_data_get_u32(b + 1324, 4, 0, g->be) != 2U || g->maxlink > 60U ||
        b[208] || b[209] != 1U || b[210] > 1U || (b[211] & ~0x80U) ||
        ((b[211] & 0x80U) && (xx_data_get_u32(b + 1312, 4, 0, g->be) ||
            xx_data_get_u64(b + 1104, 8, 0, g->be) || xx_data_get_u32(b + 1112, 4, 0, g->be)))) return false;
    for (i = 0; i < 20U; ++i)
        if (xx_data_get_u32(b + 1116U + i * 4U, 4, 0, g->be)) return false;
    for (x = g->block; x > 1U; x >>= 1) ++bshift;
    for (x = g->fragment; x > 1U; x >>= 1) ++fshift;
    for (x = g->frag; x > 1U; x >>= 1) ++fragshift;
    if (xx_data_get_u32(b + 80, 4, 0, g->be) != bshift ||
        xx_data_get_u32(b + 84, 4, 0, g->be) != fshift ||
        xx_data_get_u32(b + 96, 4, 0, g->be) != fragshift ||
        xx_data_get_u32(b + 100, 4, 0, g->be) != fshift - 9U) return false;
    g->size = (uint64_t)g->blocks * g->fragment;
    if (g->size > (uint64_t)(total - g->base) ||
        g->size < 8192U + xx_data_get_u32(b + 104, 4, 0, g->be) ||
        !g->cssize || g->cssize < (uint64_t)g->groups * 16U ||
        g->cssize % g->fragment || g->csaddr >= g->blocks ||
        (uint64_t)g->csaddr + g->cssize / g->fragment > g->blocks) return false;
    for (i = 0; i < g->groups; ++i) {
        uint64_t start = u1_cgstart(g, i);
        uint64_t end = (uint64_t)(i + 1U) * g->fpg;
        if (end > g->blocks) end = g->blocks;
        if (start + g->dblk > end || start + g->cblk +
            (g->cgsize + g->fragment - 1U) / g->fragment > start + g->iblk)
            return false;
    }
    return true;
}
static void *u1_alloc(u1_view *v, size_t n) {
    void *p;
    if (!n || n > U1_MEMORY - v->memory) return NULL;
    p = xx_mem_calloc(1U, n);
    if (p) v->memory += n;
    return p;
}
static bool u1_grow(u1_view *v, void **p, size_t old, size_t n) {
    void *q;
    if (n <= old || n - old > U1_MEMORY - v->memory) return false;
    q = xx_mem_realloc(*p, n);
    if (!q) return false;
    xx_mem_zero((uint8_t *)q + old, n - old);
    v->memory += n - old;
    *p = q;
    return true;
}
static void u1_drop(u1_view *v, void *p, size_t n) {
    if (p) { xx_mem_free(p); v->memory -= n; }
}
static void u1_release(u1_view *v) {
    size_t i;
    if (!v || --v->refs) return;
    for (i = 0; i < v->nodes_count; ++i) xx_mem_free(v->nodes[i].runs);
    for (i = 0; i < v->count; ++i) xx_mem_free(v->members[i].name);
    xx_mem_free(v->nodes); xx_mem_free(v->members);
    xx_mem_free(v->inode_hash); xx_mem_free(v->name_hash);
    xx_mem_free(v->allocated); xx_mem_free(v->claimed); xx_mem_free(v->inodes);
    xx_mem_free(v);
}
static bool u1_bit(const uint8_t *p, uint32_t n) { return (p[n / 8U] & (1U << (n % 8U))) != 0; }
static void u1_set(uint8_t *p, uint32_t n) { p[n / 8U] |= (uint8_t)(1U << (n % 8U)); }
static bool u1_metadata(const u1_geo *g, uint32_t frag) {
    uint32_t cg = frag / g->fpg;
    uint64_t start = u1_cgstart(g, cg);
    return (cg == 0U ? frag < start + g->dblk :
            frag >= start + g->sblk && frag < start + g->dblk) ||
           (frag >= g->csaddr && frag < (uint64_t)g->csaddr + g->cssize / g->fragment);
}
static bool u1_maps(xx_io_device *dev, u1_view *v, xx_pd_struct *pd) {
    const u1_geo *g = &v->geo;
    uint8_t *buf;
    uint32_t c;
    v->bitmap = ((size_t)g->blocks + 7U) / 8U;
    v->inode_bitmap = ((size_t)g->groups * g->ipg + 7U) / 8U;
    v->allocated = (uint8_t *)u1_alloc(v, v->bitmap);
    v->claimed = (uint8_t *)u1_alloc(v, v->bitmap);
    v->inodes = (uint8_t *)u1_alloc(v, v->inode_bitmap);
    buf = (uint8_t *)u1_alloc(v, g->cgsize);
    if (!v->allocated || !v->claimed || !v->inodes || !buf) {
        u1_drop(v, buf, g->cgsize); return false;
    }
    for (c = 0; c < g->groups; ++c) {
        uint32_t nd, used, free_at, next, i;
        uint64_t base = (uint64_t)c * g->fpg;
        nd = (uint32_t)(g->blocks - base < g->fpg ? g->blocks - base : g->fpg);
        if (u1_stop(pd) || !u1_rel(dev, g, (u1_cgstart(g,c) + g->cblk) *
                  g->fragment, buf, g->cgsize, pd) ||
            xx_data_get_u32(buf + 4, 4, 0, g->be) != UINT32_C(0x090255) ||
            xx_data_get_u32(buf + 12, 4, 0, g->be) != c || xx_data_get_u32(buf + 20, 4, 0, g->be) != nd)
            goto bad;
        used = xx_data_get_u32(buf + 92, 4, 0, g->be);
        free_at = xx_data_get_u32(buf + 96, 4, 0, g->be);
        next = xx_data_get_u32(buf + 100, 4, 0, g->be);
        if (used < 104U || used > g->cgsize || free_at > g->cgsize ||
            (uint64_t)used + (g->ipg + 7U) / 8U > free_at ||
            (uint64_t)free_at + (nd + 7U) / 8U > next || next > g->cgsize)
            goto bad;
        for (i = 0; i < g->ipg; ++i) {
            if ((i & 1023U) == 0U && u1_stop(pd)) goto bad;
            if (u1_bit(buf + used, i)) u1_set(v->inodes, c * g->ipg + i);
        }
        for (i = 0; i < nd; ++i) {
            if ((i & 1023U) == 0U && u1_stop(pd)) goto bad;
            if (!u1_bit(buf + free_at, i)) u1_set(v->allocated, (uint32_t)base + i);
            else if (u1_metadata(g, (uint32_t)base + i)) goto bad;
        }
    }
    u1_drop(v, buf, g->cgsize);
    return true;
bad:
    u1_drop(v, buf, g->cgsize);
    return false;
}
static bool u1_claim(u1_view *v, uint32_t first, uint32_t count, bool full,
                      xx_pd_struct *pd) {
    uint32_t i;
    const u1_geo *g = &v->geo;
    if (!first || first >= g->blocks || !count || count > g->blocks - first ||
        first & UINT32_C(0x80000000) || count > g->frag ||
        (full ? first % g->frag != 0U || count != g->frag :
                first % g->frag + count > g->frag)) return false;
    for (i = 0; i < count; ++i) {
        uint32_t f = first + i;
        if (++v->steps > U1_STEPS || u1_stop(pd) || u1_metadata(g, f) ||
            !u1_bit(v->allocated, f) || u1_bit(v->claimed, f)) return false;
        u1_set(v->claimed, f);
    }
    return true;
}
static bool u1_add_run(u1_view *v, u1_inode *node, uint64_t logical,
                        uint32_t first, uint32_t count) {
    u1_run *run;
    if (node->count) {
        run = &node->runs[node->count - 1U];
        if (run->logical + (uint64_t)run->count * v->geo.fragment == logical &&
            (uint64_t)run->first + run->count == first) {
            run->count += count; return true;
        }
    }
    if (++v->runs > U1_RUNS) return false;
    if (node->count == node->capacity) {
        size_t n = node->capacity ? node->capacity * 2U : 8U;
        if (!u1_grow(v, (void **)&node->runs, node->capacity * sizeof(*run),
                      n * sizeof(*run))) return false;
        node->capacity = n;
    }
    run = &node->runs[node->count++];
    run->logical = logical; run->first = first; run->count = count;
    return true;
}
static bool u1_data_block(u1_view *v, u1_inode *node, uint64_t lbn,
                           uint32_t ptr, xx_pd_struct *pd) {
    uint64_t offset = lbn * v->geo.block;
    uint32_t count;
    if (offset >= node->size) return ptr == 0U;
    if (!ptr) return (node->mode & 0170000U) == U1_REG;
    count = lbn < 12U && node->size - offset < v->geo.block ?
        (uint32_t)((node->size - offset + v->geo.fragment - 1U) / v->geo.fragment)
        : v->geo.frag;
    if (!u1_claim(v, ptr, count, count == v->geo.frag, pd) ||
        !u1_add_run(v, node, offset, ptr, count)) return false;
    node->allocated += (uint64_t)count * (v->geo.fragment / 512U);
    return true;
}
static bool u1_indirect(xx_io_device *dev, u1_view *v, u1_inode *node,
                        uint32_t ptr, unsigned level, uint64_t first,
                        uint64_t span, xx_pd_struct *pd) {
    uint8_t *buf;
    uint32_t i;
    uint64_t blocks = (node->size + v->geo.block - 1U) / v->geo.block;
    bool ok = false;
    if (first >= blocks) return ptr == 0U;
    if (!ptr) return (node->mode & 0170000U) == U1_REG;
    if (!u1_claim(v, ptr, v->geo.frag, true, pd)) return false;
    node->allocated += v->geo.block / 512U;
    buf = (uint8_t *)u1_alloc(v, v->geo.block);
    if (!buf) return false;
    if (!u1_rel(dev, &v->geo, (uint64_t)ptr * v->geo.fragment, buf,
                v->geo.block, pd)) goto done;
    for (i = 0; i < v->geo.nindir; ++i) {
        uint32_t child = xx_data_get_u32(buf + (size_t)i * 4U, 4, 0, v->geo.be);
        uint64_t at = first + (uint64_t)i * span;
        if (++v->steps > U1_STEPS || u1_stop(pd)) goto done;
        if (at >= blocks) { if (child) goto done; continue; }
        if (level == 1U ? !u1_data_block(v,node,at,child,pd) :
            !u1_indirect(dev,v,node,child,level-1U,at,span/v->geo.nindir,pd))
            goto done;
    }
    ok = true;
done:
    u1_drop(v, buf, v->geo.block);
    return ok;
}
static uint32_t u1_ihash(uint32_t n) { return n * UINT32_C(2654435761); }
static bool u1_inode_hash_grow(u1_view *v) {
    size_t cap = v->inode_hash_capacity ? v->inode_hash_capacity * 2U : 32U;
    uint32_t *p = (uint32_t *)u1_alloc(v, cap * sizeof(*p));
    size_t i;
    if (!p) return false;
    for (i = 0; i < v->nodes_count; ++i) {
        size_t slot = u1_ihash(v->nodes[i].number) & (cap - 1U);
        while (p[slot]) slot = (slot + 1U) & (cap - 1U);
        p[slot] = (uint32_t)i + 1U;
    }
    u1_drop(v,v->inode_hash,v->inode_hash_capacity * sizeof(*p));
    v->inode_hash = p; v->inode_hash_capacity = cap;
    return true;
}
static bool u1_inode_get(xx_io_device *dev, u1_view *v, uint32_t ino,
                         uint32_t *index, xx_pd_struct *pd) {
    const u1_geo *g = &v->geo;
    u1_inode node;
    uint8_t b[128];
    size_t slot;
    uint64_t at, maxblocks, first, span;
    uint32_t i, kind;
    if (ino < 2U || ino >= (uint64_t)g->groups * g->ipg ||
        !u1_bit(v->inodes, ino) || u1_stop(pd)) return false;
    if (!v->inode_hash_capacity || v->nodes_count * 2U >= v->inode_hash_capacity)
        if (!u1_inode_hash_grow(v)) return false;
    slot = u1_ihash(ino) & (v->inode_hash_capacity - 1U);
    while (v->inode_hash[slot]) {
        uint32_t n = v->inode_hash[slot] - 1U;
        if (v->nodes[n].number == ino) { *index = n; return true; }
        slot = (slot + 1U) & (v->inode_hash_capacity - 1U);
    }
    if (v->nodes_count == U1_ITEMS) return false;
    at = (u1_cgstart(g,ino/g->ipg) + g->iblk) * g->fragment +
          (uint64_t)(ino % g->ipg) * 128U;
    if (!u1_rel(dev,g,at,b,sizeof(b),pd)) return false;
    xx_mem_zero(&node,sizeof(node));
    node.number = ino; node.header = g->base + (int64_t)at;
    node.mode = xx_data_get_u16(b, 2, 0, g->be); node.nlink = xx_data_get_u16(b+2, 2, 0, g->be);
    node.size = xx_data_get_u64(b+8, 8, 0, g->be); node.flags = xx_data_get_u32(b+100, 4, 0, g->be);
    node.disk_blocks = xx_data_get_u32(b+104, 4, 0, g->be);
    kind = node.mode & 0170000U;
    maxblocks = 12U + (uint64_t)g->nindir + (uint64_t)g->nindir*g->nindir +
                (uint64_t)g->nindir*g->nindir*g->nindir;
    if (!node.nlink || node.nlink > INT16_MAX || node.flags ||
        node.size > (uint64_t)INT64_MAX || node.size > maxblocks * g->block ||
        (kind != U1_REG && kind != U1_DIR && kind != U1_LNK &&
         kind != 0010000U && kind != 0020000U && kind != 0060000U && kind != 0140000U) ||
        (kind == U1_DIR && (!node.size || node.size % 512U))) return false;
    for (i=0;i<12U;++i) node.direct[i]=xx_data_get_u32(b+40U+i*4U, 4, 0, g->be);
    for (i=0;i<3U;++i) node.indirect[i]=xx_data_get_u32(b+88U+i*4U, 4, 0, g->be);
    if (kind == U1_REG || kind == U1_DIR ||
        (kind == U1_LNK && node.size >= g->maxlink)) {
        for (i=0;i<12U;++i)
            if (!u1_data_block(v,&node,i,node.direct[i],pd)) goto bad;
        first=12U; span=1U;
        for (i=0;i<3U;++i) {
            if (!u1_indirect(dev,v,&node,node.indirect[i],i+1U,first,span,pd)) goto bad;
            first += span*g->nindir; span *= g->nindir;
        }
        if (node.allocated != node.disk_blocks) goto bad;
    } else if (kind == U1_LNK) {
        if (!node.size || node.size > 60U || node.disk_blocks ||
            memchr(b+40,0,(size_t)node.size)) goto bad;
    } else if (node.size || node.disk_blocks) goto bad;
    if (v->nodes_count == v->nodes_capacity) {
        size_t cap = v->nodes_capacity ? v->nodes_capacity*2U : 16U;
        if (!u1_grow(v,(void **)&v->nodes,v->nodes_capacity*sizeof(node),cap*sizeof(node))) goto bad;
        v->nodes_capacity=cap;
    }
    *index=(uint32_t)v->nodes_count;
    v->nodes[v->nodes_count++]=node;
    v->inode_hash[slot]=*index+1U;
    return true;
bad:
    u1_drop(v,node.runs,node.capacity*sizeof(*node.runs));
    return false;
}
/* Missing runs are sparse holes; a directory/long symlink cannot contain them. */
static bool u1_location(const u1_view *v, const u1_inode *node, uint64_t at,
                         uint64_t *physical, uint64_t *available, bool *hole) {
    size_t lo=0,hi=node->count;
    if (at >= node->size) return false;
    while(lo<hi) { size_t mid=lo+(hi-lo)/2U;
        if(node->runs[mid].logical<=at) lo=mid+1U; else hi=mid; }
    if(lo) {
        const u1_run *r=&node->runs[lo-1U];
        uint64_t length=(uint64_t)r->count*v->geo.fragment;
        if(at-r->logical<length) {
            *physical=(uint64_t)r->first*v->geo.fragment+at-r->logical;
            *available=length-(at-r->logical); *hole=false;
            if(*available>node->size-at) *available=node->size-at;
            return true;
        }
    }
    *physical=0; *hole=true;
    *available=lo<node->count ? node->runs[lo].logical-at : node->size-at;
    return *available!=0U;
}
static bool u1_data_read(xx_io_device *dev,const u1_view *v,const u1_inode *node,
                          uint64_t at,void *buf,size_t n,xx_pd_struct *pd) {
    size_t done=0;
    while(done<n) {
        uint64_t physical,available;
        bool hole;
        size_t part;
        if(u1_stop(pd) || !u1_location(v,node,at+done,&physical,&available,&hole)) return false;
        part=(size_t)(available<n-done ? available : n-done);
        if(hole) xx_mem_zero((uint8_t *)buf+done,part);
        else if(!u1_rel(dev,&v->geo,physical,(uint8_t *)buf+done,part,pd)) return false;
        done+=part;
    }
    return !u1_stop(pd);
}
static char u1_fold(char c) { return c>='A'&&c<='Z' ? (char)(c+32) : c; }
static uint32_t u1_nhash(const char *p) {
    uint32_t h=UINT32_C(2166136261);
    while(*p) { h^=(uint8_t)u1_fold(*p++); h*=UINT32_C(16777619); }
    return h;
}
static bool u1_equal(const char *a,const char *b) {
    while(*a && u1_fold(*a)==u1_fold(*b)) { ++a; ++b; }
    return *a==*b;
}
static bool u1_name_hash_grow(u1_view *v) {
    size_t cap=v->name_hash_capacity ? v->name_hash_capacity*2U : 32U,i;
    uint32_t *p=(uint32_t *)u1_alloc(v,cap*sizeof(*p));
    if(!p) return false;
    for(i=0;i<v->count;++i) {
        size_t slot=u1_nhash(v->members[i].name)&(cap-1U);
        while(p[slot]) slot=(slot+1U)&(cap-1U);
        p[slot]=(uint32_t)i+1U;
    }
    u1_drop(v,v->name_hash,v->name_hash_capacity*sizeof(*p));
    v->name_hash=p;v->name_hash_capacity=cap;return true;
}
static bool u1_reserved(const char *p) {
    char base[9];size_t n=0;
    while(*p && *p!='.' && n<8U) { base[n++]=u1_fold(*p++); }
    base[n]=0;
    return !strcmp(base,"con") || !strcmp(base,"prn") || !strcmp(base,"aux") ||
        !strcmp(base,"nul") || !strcmp(base,"conin$") || !strcmp(base,"conout$") ||
        (n==4U && (!memcmp(base,"com",3)||!memcmp(base,"lpt",3)) &&
         base[3]>='1' && base[3]<='9');
}
static bool u1_component(const uint8_t *raw,size_t n,char out[256],uint32_t ino) {
    static const char hex[]="0123456789ABCDEF";
    size_t i,k=0;
    bool shorten=false;
    if(!n || n>255U) return false;
    for(i=0;i<n;++i) {
        uint8_t c=raw[i]; bool escape;
        if(!c || c=='/') return false;
        escape=c<32U || c>=127U || c=='%' || c=='\\' || c==':' || c=='"' ||
               c=='<' || c=='>' || c=='|' || c=='?' || c=='*' ||
               (i+1U==n && (c=='.'||c==' '));
        if(k+(escape?3U:1U)>230U) { shorten=true; break; }
        if(escape) { out[k++]='%';out[k++]=hex[c>>4];out[k++]=hex[c&15U]; }
        else out[k++]=(char)c;
    }
    out[k]=0;
    if(u1_reserved(out)) { memmove(out+1,out,k+1U);out[0]='_';++k; }
    if(shorten) {
        uint32_t h=UINT32_C(2166136261);
        for(i=0;i<n;++i) { h^=raw[i];h*=UINT32_C(16777619); }
        (void)xx_rt_snprintf(out+k,256U-k,"~%08x.%u",h,ino);
    }
    return true;
}
static bool u1_member_add(u1_view *v,const char *parent,const uint8_t *raw,
                           size_t n,uint32_t index,const char **path) {
    char component[256],base[256];
    size_t pn=strlen(parent),cn,slot;
    unsigned attempt;
    char *name;
    if(v->count==U1_ITEMS || !u1_component(raw,n,component,v->nodes[index].number)) return false;
    memcpy(base,component,strlen(component)+1U);
    if(!v->name_hash_capacity || v->count*2U>=v->name_hash_capacity)
        if(!u1_name_hash_grow(v)) return false;
    for(attempt=0;attempt<128U;++attempt) {
        if(attempt) (void)xx_rt_snprintf(component,sizeof(component),"%.230s~%u.%u",
                         base,v->nodes[index].number,attempt);
        cn=strlen(component);
        if(pn+(pn?1U:0U)+cn>U1_PATH) return false;
        name=(char *)u1_alloc(v,pn+(pn?1U:0U)+cn+1U);
        if(!name) return false;
        memcpy(name,parent,pn); if(pn) name[pn++]='/';
        memcpy(name+pn,component,cn+1U); if(pn) --pn;
        slot=u1_nhash(name)&(v->name_hash_capacity-1U);
        while(v->name_hash[slot] && !u1_equal(name,v->members[v->name_hash[slot]-1U].name))
            slot=(slot+1U)&(v->name_hash_capacity-1U);
        if(!v->name_hash[slot]) break;
        u1_drop(v,name,strlen(name)+1U);
    }
    if(attempt==128U) return false;
    cn=strlen(name)+1U;
    if(cn>U1_NAMES-v->name_bytes) { u1_drop(v,name,cn);return false; }
    if(v->count==v->capacity) {
        size_t cap=v->capacity ? v->capacity*2U : 16U;
        if(!u1_grow(v,(void **)&v->members,v->capacity*sizeof(*v->members),cap*sizeof(*v->members))) {
            u1_drop(v,name,cn);return false;
        }
        v->capacity=cap;
    }
    v->name_bytes+=cn;
    v->members[v->count].name=name;v->members[v->count].inode=index;
    v->name_hash[slot]=(uint32_t)v->count+1U;++v->count;*path=name;
    return true;
}
static unsigned u1_dtype(uint16_t mode) {
    switch(mode&0170000U) {
    case U1_REG:return 8U;case U1_DIR:return 4U;case U1_LNK:return 10U;
    case 0010000U:return 1U;case 0020000U:return 2U;case 0060000U:return 6U;
    case 0140000U:return 12U;default:return 0U;
    }
}
typedef struct u1_raw_names_s { char **items; size_t count, capacity; } u1_raw_names;
static uint32_t u1_raw_hash(const char *p) {
    uint32_t h=UINT32_C(2166136261);
    while(*p) { h^=(uint8_t)*p++;h*=UINT32_C(16777619); }
    return h;
}
static bool u1_raw_name(u1_view *v,u1_raw_names *set,const uint8_t *p,size_t n) {
    char value[256];size_t slot;
    memcpy(value,p,n);value[n]=0;
    if(set->count==U1_ITEMS) return false;
    if(!set->capacity || set->count*2U>=set->capacity) {
        size_t cap=set->capacity?set->capacity*2U:32U,i;
        char **items=(char **)u1_alloc(v,cap*sizeof(*items));
        if(!items) return false;
        for(i=0;i<set->capacity;++i) if(set->items[i]) {
            slot=u1_raw_hash(set->items[i])&(cap-1U);
            while(items[slot]) slot=(slot+1U)&(cap-1U);
            items[slot]=set->items[i];
        }
        u1_drop(v,set->items,set->capacity*sizeof(*items));
        set->items=items;set->capacity=cap;
    }
    slot=u1_raw_hash(value)&(set->capacity-1U);
    while(set->items[slot]) {
        if(!strcmp(set->items[slot],value)) return false;
        slot=(slot+1U)&(set->capacity-1U);
    }
    set->items[slot]=(char *)u1_alloc(v,n+1U);
    if(!set->items[slot]) return false;
    memcpy(set->items[slot],value,n+1U);++set->count;return true;
}
static void u1_raw_free(u1_view *v,u1_raw_names *set) {
    size_t i;
    for(i=0;i<set->capacity;++i) if(set->items[i])
        u1_drop(v,set->items[i],strlen(set->items[i])+1U);
    u1_drop(v,set->items,set->capacity*sizeof(*set->items));
}
static bool u1_walk(xx_io_device *dev,u1_view *v,uint32_t index,uint32_t parent,
                     const char *path,unsigned depth,xx_pd_struct *pd) {
    uint64_t at, size=v->nodes[index].size;
    bool dot=false,dotdot=false,ok=false;
    u1_raw_names raw_names;
    if(depth>U1_DEPTH || v->nodes[index].walked || u1_stop(pd)) return false;
    xx_mem_zero(&raw_names,sizeof(raw_names));
    v->nodes[index].walked=true;
    for(at=0;at<size;at+=512U) {
        uint8_t block[512];size_t pos=0;
        if(!u1_data_read(dev,v,&v->nodes[index],at,block,sizeof(block),pd)) goto done;
        while(pos<sizeof(block)) {
            const uint8_t *entry=block+pos;
            uint32_t ino, child;
            uint16_t rec;
            unsigned n,type;
            const char *child_path;
            if(++v->slots>U1_STEPS || u1_stop(pd) || sizeof(block)-pos<8U) goto done;
            ino=xx_data_get_u32(entry, 4, 0, v->geo.be);rec=xx_data_get_u16(entry+4, 2, 0, v->geo.be);
            if(rec<8U || rec%4U || rec>sizeof(block)-pos) goto done;
            pos+=rec;
            if(!ino) continue;
            n=entry[7];type=entry[6];
            if(!n || n+9U>rec || entry[8U+n] || memchr(entry+8,0,n) ||
                memchr(entry+8,'/',n) || !u1_raw_name(v,&raw_names,entry+8,n)) goto done;
            if(n==1U && entry[8]=='.') {
                if(dot || ino!=v->nodes[index].number || (type && type!=4U)) goto done;
                dot=true;continue;
            }
            if(n==2U && entry[8]=='.' && entry[9]=='.') {
                if(dotdot || ino!=parent || (type && type!=4U)) goto done;
                dotdot=true;continue;
            }
            if(type==14U && ino==1U) continue; /* BSD whiteout, no inode payload. */
            if(!u1_inode_get(dev,v,ino,&child,pd) ||
                (type && type!=u1_dtype(v->nodes[child].mode))) goto done;
            if(++v->nodes[child].links>v->nodes[child].nlink) goto done;
            if((v->nodes[child].mode&0170000U)!=U1_DIR &&
               (v->nodes[child].mode&0170000U)!=U1_REG) continue;
            if(!u1_member_add(v,path,entry+8,n,child,&child_path)) goto done;
            if((v->nodes[child].mode&0170000U)==U1_DIR) {
                ++v->nodes[index].children;
                if(v->nodes[child].links!=1U ||
                   !u1_walk(dev,v,child,v->nodes[index].number,child_path,depth+1U,pd)) goto done;
            }
        }
    }
    ok=dot&&dotdot;
done:
    u1_raw_free(v,&raw_names);return ok;
}
static u1_view *u1_parse(Abstractformat *f,xx_pd_struct *pd) {
    u1_geo g;u1_view *v;uint32_t root;size_t i;
    if(!u1_geometry(f,&g,pd)) return NULL;
    v=(u1_view *)xx_mem_calloc(1U,sizeof(*v));if(!v) return NULL;
    v->geo=g;v->refs=1U;v->memory=sizeof(*v);
    if(!u1_maps(f->device,v,pd) || !u1_inode_get(f->device,v,2U,&root,pd) ||
       (v->nodes[root].mode&0170000U)!=U1_DIR ||
       !u1_walk(f->device,v,root,2U,"",0U,pd)) goto bad;
    for(i=0;i<v->nodes_count;++i) {
        u1_inode *n=&v->nodes[i];
        if(u1_stop(pd) || ((n->mode&0170000U)==U1_DIR ?
            n->nlink!=2U+n->children : n->nlink!=n->links)) goto bad;
    }
    return v;
bad:u1_release(v);return NULL;
}
static void u1_destroy_format(Abstractformat *f) { xx_ufs1_destroy((xx_ufs1 *)f); }
void xx_ufs1_init(xx_ufs1 *v,xx_io_device *d,int64_t base) {
    if(!v) { return; } xx_mem_zero(v,sizeof(*v));xx_format_init(&v->format,d,base);
    v->format.file_type=U1_TYPE;v->format.format_type=XX_TYPE_ARCHIVE;
    v->format.is_archive=true;xx_format_set_mime_type(&v->format,"application/x-ufs1-fs");
    xx_format_set_extension(&v->format,"img");
    v->format.check_is_valid=xx_ufs1_check_is_valid;
    v->format.handle_base_info=xx_ufs1_handle_base_info;
    v->format.get_format_size=xx_ufs1_get_format_size;
    v->format.get_number_of_archive_records=xx_ufs1_get_number_of_archive_records;
    v->format.create_archive_records_reading=xx_ufs1_create_archive_records_reading;
    v->format.get_current_archive_record=xx_ufs1_get_current_archive_record;
    v->format.archive_record_move_to_next=xx_ufs1_archive_record_move_to_next;
    v->format.unpack_current_archive_record=xx_ufs1_unpack_current_archive_record;
    v->format.free_archive_records_reading=xx_ufs1_free_archive_records_reading;
    v->format.destroy=u1_destroy_format;
}
xx_ufs1 *xx_ufs1_create(xx_io_device *d,int64_t base) {
    xx_ufs1 *v=(xx_ufs1 *)xx_mem_alloc(sizeof(*v));if(v) xx_ufs1_init(v,d,base);return v;
}
void xx_ufs1_destroy(xx_ufs1 *v) {
    if(!v) { return; } u1_release((u1_view *)v->internal);v->internal=NULL;
    xx_format_cleanup_extra_parameters(&v->format);
}
void xx_ufs1_free(xx_ufs1 *v) { if(v) { xx_ufs1_destroy(v);xx_mem_free(v); } }
bool xx_ufs1_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {
    u1_view *v=u1_parse(f,pd);if(!v) return false;u1_release(v);return true;
}
bool xx_ufs1_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {
    xx_ufs1 *volume=(xx_ufs1 *)f;u1_view *v;int64_t total,end;
    if(!f || u1_stop(pd)) return false;
    if(f->base_info_handled && volume->internal) return f->is_valid;
    v=u1_parse(f,pd);if(!v) { f->is_valid=false;f->base_info_handled=false;return false; }
    u1_release((u1_view *)volume->internal);volume->internal=v;
    volume->number_of_records=v->count;volume->volume_size=v->geo.size;
    volume->block_size=v->geo.block;volume->fragment_size=v->geo.fragment;
    volume->cylinder_groups=v->geo.groups;volume->big_endian=v->geo.be;
    f->endian=v->geo.be?XX_ENDIAN_BIG:XX_ENDIAN_LITTLE;
    f->number_of_archive_records=v->count;f->format_size=(int64_t)v->geo.size;
    total=xx_io_total_size(f->device);end=f->base_address+f->format_size;
    f->overlay_offset=total>end?end:-1;f->overlay_size=total>end?total-end:0;
    f->is_valid=true;f->base_info_handled=true;return true;
}
int64_t xx_ufs1_get_format_size(Abstractformat *f,xx_pd_struct *pd) {
    return f&&xx_ufs1_handle_base_info(f,pd)?f->format_size:-1;
}
uint64_t xx_ufs1_get_number_of_archive_records(Abstractformat *f,xx_pd_struct *pd) {
    return f&&xx_ufs1_handle_base_info(f,pd)?((xx_ufs1 *)f)->number_of_records:0U;
}
static bool u1_record(xx_archive_record *r,const u1_view *v,size_t index) {
    const u1_member *m=&v->members[index];const u1_inode *n=&v->nodes[m->inode];
    bool folder=(n->mode&0170000U)==U1_DIR;
    uint64_t size=folder?0U:n->size;
    xx_archive_record_cleanup(r);xx_archive_record_init(r);
    r->header_offset=n->header;r->header_size=128;r->data_offset=-1;
    if(!folder && n->count && n->runs[0].logical==0U)
        r->data_offset=v->geo.base+(int64_t)((uint64_t)n->runs[0].first*v->geo.fragment);
    r->compressed_size=(int64_t)size;
    return xx_archive_record_set_original_name(r,m->name) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_UNCOMPRESSED_SIZE,size) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSED_SIZE,size) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_COMPRESSION_METHOD,0U) &&
        xx_archive_record_set_meta_u64(r,XX_META_ID_ATTRIBUTES,n->mode) &&
        xx_archive_record_set_meta_bool(r,XX_META_ID_IS_FOLDER,folder);
}
static void u1_cursor_free(void *p) {
    u1_cursor *c=(u1_cursor *)p;if(c) { u1_release(c->view);xx_mem_free(c); }
}
xx_archive_record_state *xx_ufs1_create_archive_records_reading(
    Abstractformat *f,const xx_list_s *options,xx_pd_struct *pd) {
    xx_archive_record_state *s;u1_cursor *c;u1_view *v;size_t i;
    if(!f || !xx_ufs1_handle_base_info(f,pd)) return NULL;
    v=(u1_view *)((xx_ufs1 *)f)->internal;
    c=(u1_cursor *)xx_mem_calloc(1U,sizeof(*c));
    s=(xx_archive_record_state *)xx_mem_alloc(sizeof(*s));
    if(!c||!s) { xx_mem_free(c);xx_mem_free(s);return NULL; }
    ++v->refs;c->view=v;xx_archive_record_state_init(s,f);
    s->internal_state=c;s->free_internal=u1_cursor_free;s->total_records=(int64_t)v->count;
    if(options) for(i=0;i<options->count;++i) {
        const xx_meta *m=(const xx_meta *)xx_list_at(options,i);xx_meta copy;
        if(!m) { continue; } xx_meta_init(&copy,m->meta_id);
        if(!xx_var_copy(&copy.var,&m->var) || !xx_list_append(&s->options,&copy)) {
            xx_meta_cleanup(&copy);xx_archive_record_state_free(s);return NULL;
        }
    }
    if(v->count && !u1_record(&s->current_record,v,0U)) { xx_archive_record_state_free(s);return NULL; }
    s->has_record=v->count!=0U;s->current_index=s->has_record?0:-1;return s;
}
const xx_archive_record *xx_ufs1_get_current_archive_record(Abstractformat *f,xx_archive_record_state *s) {
    return f&&s&&s->format==f&&s->has_record?&s->current_record:NULL;
}
bool xx_ufs1_archive_record_move_to_next(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {
    u1_cursor *c;
    if(!f||!s||s->format!=f||!s->has_record||!(c=(u1_cursor *)s->internal_state)||u1_stop(pd)) return false;
    if(c->index+1U>=c->view->count) {
        xx_archive_record_cleanup(&s->current_record);xx_archive_record_init(&s->current_record);
        s->has_record=false;return false;
    }
    if(!u1_record(&s->current_record,c->view,c->index+1U)) { s->has_record=false;return false; }
    ++c->index;++s->current_index;return true;
}
static bool u1_limits(Abstractformat *f,const xx_archive_record_state *s,
                        const u1_cursor *c,size_t *chunk) {
    const u1_inode *n=&c->view->nodes[c->view->members[c->index].inode];
    uint64_t size=(n->mode&0170000U)==U1_DIR?0U:n->size;
    const xx_var *max=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MAX_MEMBER_SIZE);
    const xx_var *mem=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_MEMORY_LIMIT);
    *chunk=(size_t)(size<U1_COPY?size:U1_COPY);
    return (!max||size<=xx_var_get_u64(max)) &&
        (!mem||c->view->memory+sizeof(*c)+*chunk<=xx_var_get_u64(mem));
}
bool xx_ufs1_extract_record_to_device(Abstractformat *f,xx_archive_record_state *s,
                                       xx_io_device *dst,xx_pd_struct *pd) {
    u1_cursor *c;const u1_inode *n;uint8_t *buf;size_t chunk;uint64_t at=0;bool ok=true;
    if(!f||!f->device||dst==f->device||!s||s->format!=f||!s->has_record||
       !(c=(u1_cursor *)s->internal_state)||c->index>=c->view->count||u1_stop(pd)) return false;
    n=&c->view->nodes[c->view->members[c->index].inode];
    if(!u1_limits(f,s,c,&chunk)) return false;
    if((n->mode&0170000U)==U1_DIR||!n->size) return true;
    buf=(uint8_t *)xx_mem_alloc(chunk);if(!buf) return false;
    while(at<n->size) {
        uint64_t physical,available;bool hole;size_t part,written=0;
        if(u1_stop(pd)||!u1_location(c->view,n,at,&physical,&available,&hole)) { ok=false;break; }
        if(hole&&!dst) { at+=available;continue; }
        part=(size_t)(available<chunk?available:chunk);
        if(hole) xx_mem_zero(buf,part);
        else if(!u1_rel(f->device,&c->view->geo,physical,buf,part,pd)) { ok=false;break; }
        while(dst&&written<part&&!u1_stop(pd)) {
            ssize_t got=xx_io_write(dst,buf+written,part-written);
            if(got<=0||(size_t)got>part-written||u1_stop(pd)) { ok=false;break; }
            written+=(size_t)got;
        }
        if(!ok||u1_stop(pd)) { ok=false;break; }at+=part;
    }
    xx_mem_free(buf);return ok&&!u1_stop(pd);
}
static xx_io_device *u1_stage(const char *dest,char **path) {
    size_t i,parent=0;unsigned attempt;char *dir=xx_str_dup(dest);*path=NULL;
    if(!dir) return NULL;
    for(i=0;dir[i];++i) if(dir[i]=='/'||dir[i]=='\\') parent=i+1U;
    dir[parent]=0;
    for(attempt=0;attempt<128U;++attempt) {
        char suffix[40];char *candidate;xx_io_device *d;
        (void)xx_rt_snprintf(suffix,sizeof(suffix),".xx_ufs1.tmp.%u",attempt);
        candidate=xx_str_concat(dir,suffix);if(!candidate) break;
        if(u1_equal(candidate,dest)) { xx_str_free(candidate);continue; }
        d=xx_io_file_open(candidate,"wbx");
        if(d) { *path=candidate;xx_str_free(dir);return d; }xx_str_free(candidate);
    }
    xx_str_free(dir);return NULL;
}
bool xx_ufs1_unpack_current_archive_record(Abstractformat *f,xx_archive_record_state *s,xx_pd_struct *pd) {
    u1_cursor *c;const u1_member *m;const u1_inode *n;const xx_var *option,*ov;
    const char *base=NULL;char *owned=NULL,*path=NULL,*stage=NULL;size_t chunk;bool ok=false,overwrite;
    if(!f||!s||s->format!=f||!s->has_record||!(c=(u1_cursor *)s->internal_state)||
       c->index>=c->view->count||u1_stop(pd)||!u1_limits(f,s,c,&chunk)) return false;
    m=&c->view->members[c->index];n=&c->view->nodes[m->inode];
    option=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_UNPACK_PATH);
    ov=xx_format_resolve_extra_parameter(f,&s->options,XX_META_ID_OPT_OVERWRITE);
    overwrite=ov&&xx_var_get_bool(ov);
    if(!option) return xx_ufs1_extract_record_to_device(f,s,NULL,pd);
    if(option->type==XX_VAR_TYPE_STRING||option->type==XX_VAR_TYPE_STRING_VIEW) base=xx_var_get_str(option);
    else if(option->type==XX_VAR_TYPE_WSTRING||option->type==XX_VAR_TYPE_WSTRING_VIEW) {
        owned=xx_str_unicode_to_utf8(xx_var_get_wstr(option));base=owned;
    }
    if(!base) goto done;
    path=*base&&base[strlen(base)-1U]!='/'&&base[strlen(base)-1U]!='\\' ?
        xx_str_concat3(base,"/",m->name):xx_str_concat(base,m->name);
    if(!path) goto done;
    if((n->mode&0170000U)==U1_DIR) { ok=xx_store_create_dirs_a(path,true)&&!u1_stop(pd);goto done; }
    if(!overwrite&&xx_io_file_exists_a(path)) goto done;
    if(!xx_store_create_dirs_a(path,false)||u1_stop(pd)) goto done;
    {
        xx_io_device *out=u1_stage(path,&stage);if(!out) goto done;
        ok=xx_ufs1_extract_record_to_device(f,s,out,pd);if(xx_io_close(out)!=0) ok=false;
    }
    if(ok&&!u1_stop(pd)) ok=xx_io_file_replace_a(stage,path,overwrite);else ok=false;
done:
    if(stage) { if(!ok) (void)xx_io_file_remove_a(stage);xx_str_free(stage); }
    xx_str_free(path);xx_str_free(owned);return ok;
}
void xx_ufs1_free_archive_records_reading(Abstractformat *f,xx_archive_record_state *s) {
    (void)f;xx_archive_record_state_free(s);
}
