/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ephtracy/voxel-model/master/MagicaVoxel-file-format-vox.txt
 * MagicaVoxel VOX150 flat MAIN with SIZE/XYZI model pairs, optional PACK and RGBA, up to256 models/256-cubed dimensions/1million voxels per model. Checks chunk extents,
 * voxel coordinates/colors and duplicate occupancy. Exports stored size/voxel/palette components; scene/material/transform extensions, other revisions and rendering
 * unsupported.
 */
#include "xxfclib/formats/magicavoxel_vox/xx_magicavoxel_vox.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
typedef struct rg {
    uint64_t at, n;
} rg;
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20], p[12], v[4];
    uint32_t children, i, models = 0, packed = 0, w = 0, height = 0, depth = 0, voxels;
    uint64_t at = 20, total, next;
    bool size = false, palette = false;
    uint8_t *seen = NULL;
    char label[40];
    if (!pm_read(f, 0, h, 20) || xx_rt_memcmp(h, "VOX ", 4) || xx_data_get_u32(h + 4, 4, 0, false) != 150 || xx_rt_memcmp(h + 8, "MAIN", 4) ||
        xx_data_get_u32(h + 12, 4, 0, false)) {
        return false;
    }
    children = xx_data_get_u32(h + 16, 4, 0, false);
    total = 20U + (uint64_t)children;
    if (total > (uint64_t)pm_available(f)) return false;
    while (at < total) {
        uint32_t n;
        if (stop(pd) || !span(at, 12, total) || !pm_read(f, (int64_t)at, p, 12) || xx_data_get_u32(p + 8, 4, 0, false) ||
            !span(at + 12, n = xx_data_get_u32(p + 4, 4, 0, false), total))
            return false;
        next = at + 12 + n;
        if (!xx_rt_memcmp(p, "PACK", 4)) {
            if (packed || models || size || n != 4 || !pm_read(f, (int64_t)at + 12, v, 4) || !(packed = xx_data_get_u32(v, 4, 0, false)) || packed > 256) return false;
        } else if (!xx_rt_memcmp(p, "SIZE", 4)) {
            if (size || palette || models >= 256 || n != 12 || !pm_read(f, (int64_t)at + 12, p, 12) || !(w = xx_data_get_u32(p, 4, 0, false)) || w > 256 ||
                !(height = xx_data_get_u32(p + 4, 4, 0, false)) || height > 256 || !(depth = xx_data_get_u32(p + 8, 4, 0, false)) || depth > 256)
                return false;
            size = true;
            xx_rt_snprintf(label, sizeof(label), "model-%u-size.bin", models);
            if (!emit(f, s, label, at + 12, n, total)) return false;
        } else if (!xx_rt_memcmp(p, "XYZI", 4)) {
            uint64_t cells;
            if (!size || palette || n < 4 || !pm_read(f, (int64_t)at + 12, v, 4) || !(voxels = xx_data_get_u32(v, 4, 0, false)) || voxels > 1048576 ||
                n != 4U + (uint64_t)voxels * 4)
                return false;
            cells = (uint64_t)w * height * depth;
            seen = (uint8_t *)xx_mem_alloc((size_t)((cells + 7) / 8));
            if (!seen) return false;
            xx_mem_zero(seen, (size_t)((cells + 7) / 8));
            for (i = 0; i < voxels; ++i) {
                uint32_t index;
                if (stop(pd) || !pm_read(f, (int64_t)(at + 16 + (uint64_t)i * 4), v, 4) || v[0] >= w || v[1] >= height || v[2] >= depth || !v[3]) {
                    xx_mem_free(seen);
                    return false;
                }
                index = v[0] + w * (v[1] + height * v[2]);
                if (seen[index / 8] & (1U << (index % 8))) {
                    xx_mem_free(seen);
                    return false;
                }
                seen[index / 8] |= (uint8_t)(1U << (index % 8));
            }
            xx_mem_free(seen);
            seen = NULL;
            xx_rt_snprintf(label, sizeof(label), "model-%u-voxels.bin", models++);
            if (!emit(f, s, label, at + 12, n, total)) return false;
            size = false;
        } else if (!xx_rt_memcmp(p, "RGBA", 4)) {
            if (palette || size || !models || n != 1024 || !emit(f, s, "palette.rgba", at + 12, n, total)) return false;
            palette = true;
        } else {
            return false;
        }
        at = next;
    }
    if (size || !models || (packed && packed != models)) {
        return false;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_magicavoxel_vox_init(xx_magicavoxel_vox *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MAGICAVOXEL_VOX, "vox");
    }
}
xx_magicavoxel_vox *xx_magicavoxel_vox_create(xx_io_device *d, int64_t b)
{
    xx_magicavoxel_vox *r = (xx_magicavoxel_vox *)xx_mem_alloc(sizeof(*r));
    if (r) xx_magicavoxel_vox_init(r, d, b);
    return r;
}
void xx_magicavoxel_vox_destroy(xx_magicavoxel_vox *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_magicavoxel_vox_free(xx_magicavoxel_vox *r)
{
    if (r) {
        xx_magicavoxel_vox_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_magicavoxel_vox_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_magicavoxel_vox_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
