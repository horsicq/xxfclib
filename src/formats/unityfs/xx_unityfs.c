/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Perfare/AssetStudio/master/AssetStudio/BundleFile.cs
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/unityfs/xx_unityfs.h"
#include "../bethesda_bsa/xx_game_table.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[24], b[20], r[10];
    uint32_t ver, packed, plain, flags, blocks, nodes, i;
    uint64_t at = 8, used, size, info, data, limit, position, sum = 0, n;
    int64_t total = pm_available(f);
    if (!gm_read(f, total, 0, h, 12) || xx_rt_memcmp(h, "UnityFS\0", 8)) return false;
    ver = xx_data_get_u32(h + 8, 4, 0, true);
    if (ver < 6 || ver > 8) return false;
    at = 12;
    for (i = 0; i < 2; ++i) {
        limit = (uint64_t)total - at;
        if (limit > 256) limit = 256;
        if (!gm_string(f, total, at, limit, &used)) return false;
        at += used;
    }
    if (!gm_read(f, total, at, h, 20)) return false;
    size = xx_data_get_u64(h, 8, 0, true);
    packed = xx_data_get_u32(h + 8, 4, 0, true);
    plain = xx_data_get_u32(h + 12, 4, 0, true);
    flags = xx_data_get_u32(h + 16, 4, 0, true);
    at += 20;
    if (size > (uint64_t)total || size < at || !packed || packed != plain || packed > 16777216 || (flags & 0x3f) || (flags & ~0x2c0U) || !(flags & 0x40)) return false;
    if (ver >= 7) at = (at + 15) & ~(uint64_t)15;
    if (flags & 0x80) {
        if (size < packed || size - packed < at) return false;
        info = size - packed;
        data = at;
        limit = info;
    } else {
        info = at;
        data = at + packed;
        limit = size;
    }
    if (flags & 0x200) data = (data + 15) & ~(uint64_t)15;
    if (!gm_range((int64_t)size, info, packed) || !gm_read(f, (int64_t)size, info, b, 20)) return false;
    blocks = xx_data_get_u32(b + 16, 4, 0, true);
    if (!blocks || blocks > 65536 || (uint64_t)blocks * 10 + 24 > packed) return false;
    position = info + 20;
    for (i = 0; i < blocks; ++i) {
        uint32_t a, c;
        if (gm_stopped(pd) || !gm_read(f, (int64_t)(info + packed), position, r, 10)) return false;
        position += 10;
        a = xx_data_get_u32(r, 4, 0, true);
        c = xx_data_get_u32(r + 4, 4, 0, true);
        if (a != c || (xx_data_get_u16(r + 8, 2, 0, true) & ~0x40U)) return false;
        sum += a;
    }
    if (data > limit || sum > limit - data || !gm_read(f, (int64_t)(info + packed), position, b, 4)) {
        return false;
    }
    nodes = xx_data_get_u32(b, 4, 0, true);
    position += 4;
    if (nodes > 65536) {
        return false;
    }
    s->size = (int64_t)size;
    for (i = 0; i < nodes; ++i) {
        uint64_t off;
        if (gm_stopped(pd) || !gm_read(f, (int64_t)(info + packed), position, b, 20)) {
            return false;
        }
        off = xx_data_get_u64(b, 8, 0, true);
        n = xx_data_get_u64(b + 8, 8, 0, true);
        position += 20;
        limit = info + packed - position;
        if (limit > 4096) limit = 4096;
        if (!gm_string(f, (int64_t)(info + packed), position, limit, &used) || used == 1) {
            return false;
        }
        position += used;
        if (off > sum || n > sum - off || !gm_add(f, s, "asset.bin", data + off, n, data, (int64_t)size)) return false;
    }
    return position == info + packed;
}
void xx_unityfs_init(xx_unityfs *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UNITYFS, "bin");
    }
}
xx_unityfs *xx_unityfs_create(xx_io_device *d, int64_t b)
{
    xx_unityfs *r = (xx_unityfs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_unityfs_init(r, d, b);
    return r;
}
void xx_unityfs_destroy(xx_unityfs *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_unityfs_free(xx_unityfs *r)
{
    if (r) {
        xx_unityfs_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_unityfs_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_unityfs_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
