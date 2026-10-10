/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/NIFTI-Imaging/nifti_clib/master/nifti2/nifti2.h */
#include "xxfclib/formats/nifti2/xx_nifti2.h"
#include "../common/xx_numeric_values.h"

static unsigned voxel_bits(unsigned t)
{
    switch (t) {
        case 2:
        case 256: return 8;
        case 4:
        case 512: return 16;
        case 8:
        case 16:
        case 768:
        case 2304: return 32;
        case 32:
        case 64:
        case 1024:
        case 1280: return 64;
        case 128: return 24;
        case 1536:
        case 1792: return 128;
        case 2048: return 256;
        default: return 0;
    }
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[544], b[8];
    bool be;
    unsigned i, bits;
    uint64_t dim, count = 1, at, n;
    int64_t available = pm_available(f);
    if (binary_stop(pd) || available < 544 || !pm_read(f, 0, h, sizeof(h)) || xx_rt_memcmp(h + 4, "n+2\0\r\n\x1a\n", 8)) return false;
    be = xx_data_get_u32(h, 4, 0, true) == 540;
    if (!be && xx_data_get_u32(h, 4, 0, false) != 540) return false;
    dim = xx_data_get_u64(h + 16, 8, 0, be);
    at = xx_data_get_u64(h + 168, 8, 0, be);
    bits = voxel_bits(xx_data_get_u16(h + 12, 2, 0, be));
    if (dim < 1 || dim > 7 || !bits || bits != xx_data_get_u16(h + 14, 2, 0, be) || at < 544 || at > INT64_MAX || h[540] > 1 || h[541] || h[542] || h[543]) return false;
    for (i = 0; i < dim; ++i) {
        uint64_t d = xx_data_get_u64(h + 24 + i * 8, 8, 0, be);
        if (!d || d > INT64_MAX || !binary_mul(count, d, &count)) return false;
    }
    if (!binary_mul(count, bits / 8, &n) || !binary_range(at, n, (uint64_t)available) || !pm_add(f, s, "nifti2-header.bin", 0, h[540] ? 544 : (int64_t)at)) return false;
    if (h[540]) {
        uint64_t p = 544;
        unsigned extensions = 0;
        while (p < at) {
            uint32_t z, code;
            char label[64];
            if (binary_stop(pd) || ++extensions > 1024 || !binary_range(p, 8, at) || !pm_read(f, (int64_t)p, b, 8)) return false;
            z = xx_data_get_u32(b, 4, 0, be);
            code = xx_data_get_u32(b + 4, 4, 0, be);
            if (z < 16 || z % 16 || code > INT32_MAX || !binary_range(p, z, at)) return false;
            xx_rt_snprintf(label, sizeof(label), "extension-%u-code-%u.bin", extensions - 1, code);
            if (!pm_add(f, s, label, (int64_t)p, z)) return false;
            p += z;
        }
    }
    if (!pm_add(f, s, "voxels.bin", (int64_t)at, (int64_t)n)) {
        return false;
    }
    s->size = (int64_t)(at + n);
    return true;
}

void xx_nifti2_init(xx_nifti2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NIFTI2, "nifti2");
    }
}
xx_nifti2 *xx_nifti2_create(xx_io_device *d, int64_t b)
{
    xx_nifti2 *r = (xx_nifti2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nifti2_init(r, d, b);
    return r;
}
void xx_nifti2_destroy(xx_nifti2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nifti2_free(xx_nifti2 *r)
{
    if (r) {
        xx_nifti2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nifti2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nifti2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
