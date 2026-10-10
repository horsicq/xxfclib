/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/mp3butcher/Cal3D/master/cal3d/src/cal3d/loader.cpp
 * Cal3D CSF version700 skeleton: complete counted UTF8 bone names, finite local/inverse-bind transforms, resolved reciprocal parent/child IDs and acyclic hierarchy.
 * Original descriptor and individual bone records exported; later extension versions declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/cal3d_skeleton/xx_cal3d_skeleton.h"
#include "../common/xx_component_binary.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[12];
    return n >= 12 && pm_read(f, 0, b, 12) && component_tag(b, "CSF\0", 4) && xx_data_get_u32(b + 4, 4, 0, false) == 700 && xx_data_get_u32(b + 8, 4, 0, false) > 0 &&
           xx_data_get_u32(b + 8, 4, 0, false) <= 4000;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_binary_cursor q = {b, 12, n, pd};
    uint32_t count, i, *parents = NULL, *children = NULL, *owners = NULL, edges = 0;
    bool ok = false;
    char name[48];
    if (n < 12 || !component_tag(b, "CSF\0", 4) || xx_data_get_u32(b + 4, 4, 0, false) != 700 || (count = xx_data_get_u32(b + 8, 4, 0, false)) == 0 || count > 4000)
        return false;
    parents = (uint32_t *)xx_mem_alloc((size_t)count * 4);
    children = (uint32_t *)xx_mem_alloc((size_t)count * 4);
    owners = (uint32_t *)xx_mem_alloc((size_t)count * 4);
    if (!parents || !children || !owners || !component_emit(f, s, "descriptor.csf", 0, 12, n)) goto done;
    for (i = 0; i < count; ++i) {
        uint64_t start = q.p;
        const uint8_t *p;
        uint32_t z, k, nchild;
        if (!component_binary_count(&q, 256, &z) || z < 2 || !component_binary_take(&q, z, &p) || p[z - 1] || !component_utf8(p, z - 1, false, pd)) goto done;
        for (k = 0; k + 1 < z; ++k)
            if (!p[k]) goto done;
        if (!component_binary_floats(&q, 14) || !component_binary_take(&q, 4, &p)) goto done;
        parents[i] = xx_data_get_u32(p, 4, 0, false);
        if (parents[i] != 0xffffffffU && (parents[i] >= count || parents[i] == i)) goto done;
        if (!component_binary_count(&q, count, &nchild) || nchild > count - edges) goto done;
        for (k = 0; k < nchild; ++k) {
            uint32_t j, id;
            if (!component_binary_take(&q, 4, &p) || (id = xx_data_get_u32(p, 4, 0, false)) >= count || id == i) goto done;
            for (j = 0; j < edges; ++j)
                if (children[j] == id) goto done;
            children[edges] = id;
            owners[edges++] = i;
        }
        xx_rt_snprintf(name, sizeof(name), "bone-%u.csf", i);
        if (!component_emit(f, s, name, start, q.p - start, n)) goto done;
    }
    for (i = 0; i < count; ++i) {
        uint32_t j, p = i;
        bool listed = false;
        for (j = 0; j < edges; ++j)
            if (children[j] == i) {
                if (owners[j] != parents[i]) goto done;
                listed = true;
            }
        if (listed != (parents[i] != 0xffffffffU)) goto done;
        for (j = 0; j < count && parents[p] != 0xffffffffU; ++j) {
            if (xx_component_parser_stopped(pd)) goto done;
            p = parents[p];
        }
        if (j == count) goto done;
    }
    ok = q.p == n;
done:
    if (parents) xx_mem_free(parents);
    if (children) xx_mem_free(children);
    if (owners) xx_mem_free(owners);
    return ok;
}

void xx_cal3d_skeleton_init(xx_cal3d_skeleton *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_CAL3D_SKELETON, "csf");
    }
}
xx_cal3d_skeleton *xx_cal3d_skeleton_create(xx_io_device *d, int64_t at)
{
    xx_cal3d_skeleton *r = (xx_cal3d_skeleton *)xx_mem_alloc(sizeof(*r));
    if (r) xx_cal3d_skeleton_init(r, d, at);
    return r;
}
void xx_cal3d_skeleton_destroy(xx_cal3d_skeleton *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_cal3d_skeleton_free(xx_cal3d_skeleton *r)
{
    if (r) {
        xx_cal3d_skeleton_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_cal3d_skeleton_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_cal3d_skeleton_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
