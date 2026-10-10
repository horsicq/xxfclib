/* SPDX-License-Identifier: MIT
 * Independently implemented from https://pointclouds.org/documentation/tutorials/pcd_file_format.html */
#include "xxfclib/formats/pointcloud_pcd/xx_pointcloud_pcd.h"
#include "../common/xx_numeric_values.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    char line[4096], *v[65], fields[64][64];
    uint64_t sizes[64], counts[64], width = 0, height = 0, points = 0, stride = 0, n;
    char types[64];
    unsigned stage = 0, nf = 0, lines = 0, i, j, t;
    int64_t available = pm_available(f);
    binary_cursor c = {f, 0, 0, pd, 0};
    c.end = (uint64_t)available;
    if (binary_stop(pd) || available < 50) return false;
    while (c.at < 65536 && ++lines <= 128 && scientific_number_line(&c, line, sizeof(line))) {
        char *p = scientific_number_trim(line);
        if (*p == '#' || !*p) continue;
        t = numeric_tokens(p, v, 65);
        if (t < 2 || t > 65) return false;
        if (!xx_rt_strcmp(v[0], "VERSION")) {
            if (stage || t != 2 || (xx_rt_strcmp(v[1], ".7") && xx_rt_strcmp(v[1], "0.7"))) return false;
            stage = 1;
        } else if (!xx_rt_strcmp(v[0], "FIELDS")) {
            if (stage != 1 || t < 2 || t > 65) return false;
            nf = t - 1;
            for (i = 0; i < nf; ++i) {
                size_t z = xx_rt_strlen(v[i + 1]);
                if (!z || z >= sizeof(fields[i])) return false;
                xx_rt_memcpy(fields[i], v[i + 1], z + 1);
                for (j = 0; j < i; ++j)
                    if (!xx_rt_strcmp(fields[j], fields[i]) && xx_rt_strcmp(fields[i], "_")) return false;
                counts[i] = 1;
            }
            stage = 2;
        } else if (!xx_rt_strcmp(v[0], "SIZE")) {
            if (stage != 2 || t != nf + 1) return false;
            for (i = 0; i < nf; ++i)
                if (!scientific_number_uint(v[i + 1], &sizes[i]) || (sizes[i] != 1 && sizes[i] != 2 && sizes[i] != 4 && sizes[i] != 8)) return false;
            stage = 3;
        } else if (!xx_rt_strcmp(v[0], "TYPE")) {
            if (stage != 3 || t != nf + 1) return false;
            for (i = 0; i < nf; ++i) {
                types[i] = v[i + 1][0];
                if (v[i + 1][1] || (types[i] != 'I' && types[i] != 'U' && types[i] != 'F') || (types[i] == 'F' && sizes[i] < 4) || (types[i] != 'F' && sizes[i] > 4))
                    return false;
            }
            stage = 4;
        } else if (!xx_rt_strcmp(v[0], "COUNT")) {
            if (stage != 4 || t != nf + 1) return false;
            for (i = 0; i < nf; ++i)
                if (!scientific_number_uint(v[i + 1], &counts[i]) || !counts[i] || counts[i] > 65536) return false;
            stage = 5;
        } else if (!xx_rt_strcmp(v[0], "WIDTH")) {
            if ((stage != 4 && stage != 5) || t != 2 || !scientific_number_uint(v[1], &width) || !width || width > 1000000) return false;
            stage = 6;
        } else if (!xx_rt_strcmp(v[0], "HEIGHT")) {
            if (stage != 6 || t != 2 || !scientific_number_uint(v[1], &height) || !height || height > 1000000) return false;
            stage = 7;
        } else if (!xx_rt_strcmp(v[0], "VIEWPOINT")) {
            if (stage != 7 || t != 8) return false;
            for (i = 1; i < t; ++i)
                if (!scientific_number_float_token(v[i])) return false;
            stage = 8;
        } else if (!xx_rt_strcmp(v[0], "POINTS")) {
            if ((stage != 7 && stage != 8) || t != 2 || !scientific_number_uint(v[1], &points) || !points || points > 1000000 || width * height != points) return false;
            stage = 9;
        } else if (!xx_rt_strcmp(v[0], "DATA")) {
            if (stage != 9 || t != 2 || xx_rt_strcmp(v[1], "binary")) return false;
            stage = 10;
            break;
        } else return false;
    }
    if (stage != 10 || c.at > 65536) return false;
    for (i = 0; i < nf; ++i) {
        if (!binary_mul(sizes[i], counts[i], &n) || stride > (uint64_t)INT64_MAX - n) return false;
        stride += n;
    }
    if (!binary_mul(stride, points, &n) || !binary_range(c.at, n, (uint64_t)available) || !pm_add(f, s, "pcd-header.txt", 0, (int64_t)c.at) ||
        !pm_add(f, s, "points.bin", (int64_t)c.at, (int64_t)n))
        return false;
    s->size = (int64_t)(c.at + n);
    return true;
}

void xx_pointcloud_pcd_init(xx_pointcloud_pcd *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_POINTCLOUD_PCD, "pointcloud_pcd");
    }
}
xx_pointcloud_pcd *xx_pointcloud_pcd_create(xx_io_device *d, int64_t b)
{
    xx_pointcloud_pcd *r = (xx_pointcloud_pcd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_pointcloud_pcd_init(r, d, b);
    return r;
}
void xx_pointcloud_pcd_destroy(xx_pointcloud_pcd *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_pointcloud_pcd_free(xx_pointcloud_pcd *r)
{
    if (r) {
        xx_pointcloud_pcd_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_pointcloud_pcd_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_pointcloud_pcd_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
