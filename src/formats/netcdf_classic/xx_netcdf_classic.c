/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.unidata.ucar.edu/netcdf-c/4.9.2/file_format_specifications.html */
#include "xxfclib/formats/netcdf_classic/xx_netcdf_classic.h"
#include "../common/xx_binary_cursor.h"

typedef struct nc_var {
    char name[256];
    uint64_t at, bytes;
    uint32_t padded;
    bool record;
} nc_var;
static bool nc_name(binary_cursor *c, char *name)
{
    uint32_t n;
    uint8_t pad[3];
    size_t i;
    if (!binary_be(c, &n) || !n || n > 255 || !binary_get(c, name, n) || !bounded_utf8((uint8_t *)name, n, c->pd)) return false;
    for (i = 0; i < n; ++i) {
        if ((uint8_t)name[i] < 32 || name[i] == '/') return false;
    }
    name[n] = 0;
    if (!binary_get(c, pad, (4U - (n & 3U)) & 3U)) {
        return false;
    }
    for (i = 0; i < ((4U - (n & 3U)) & 3U); ++i)
        if (pad[i]) return false;
    return true;
}
static uint32_t nc_width(uint32_t type)
{
    return type == 1 || type == 2 ? 1 : type == 3 ? 2 : type == 4 || type == 5 ? 4 : type == 6 ? 8 : 0;
}
static bool nc_list(binary_cursor *c, uint32_t wanted, uint32_t limit, uint32_t *n)
{
    uint32_t tag;
    return binary_be(c, &tag) && binary_be(c, n) && *n <= limit && ((tag == 0 && *n == 0) || tag == wanted);
}
static bool nc_attrs(binary_cursor *c)
{
    uint32_t count, i;
    if (!nc_list(c, 12, 1024, &count)) return false;
    for (i = 0; i < count; ++i) {
        char name[256];
        uint32_t type, n, width;
        uint64_t bytes;
        if (!nc_name(c, name) || !binary_be(c, &type) || !(width = nc_width(type)) || !binary_be(c, &n) || !binary_mul(n, width, &bytes) ||
            !binary_skip(c, (bytes + 3) & ~3ULL))
            return false;
    }
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[8];
    binary_cursor c = {f, 8, (uint64_t)pm_available(f), pd, 0};
    uint32_t records, dims, vars, i, j, lens[128], recordvars = 0;
    int unlimited = -1;
    nc_var *v = NULL;
    uint64_t stride = 0, end = 8;
    bool result = false;
    if (!pm_read(f, 0, h, 8) || xx_rt_memcmp(h, "CDF", 3) || (h[3] != 1 && h[3] != 2) || (records = xx_data_get_u32(h + 4, 4, 0, true)) > 65535 ||
        !nc_list(&c, 10, 128, &dims))
        return false;
    for (i = 0; i < dims; ++i) {
        char name[256];
        if (!nc_name(&c, name) || !binary_be(&c, &lens[i]) || lens[i] > INT32_MAX) return false;
        if (!lens[i]) {
            if (unlimited >= 0) return false;
            unlimited = (int)i;
        }
    }
    if (!nc_attrs(&c) || !nc_list(&c, 11, 256, &vars) || !vars) return false;
    v = (nc_var *)xx_mem_alloc(vars * sizeof(*v));
    if (!v) return false;
    xx_mem_zero(v, vars * sizeof(*v));
    for (i = 0; i < vars; ++i) {
        uint32_t n, type, width, padded;
        uint64_t elems = 1;
        uint8_t begin[8];
        if (!nc_name(&c, v[i].name) || !binary_be(&c, &n) || n > 128) goto done;
        for (j = 0; j < n; ++j) {
            uint32_t id;
            if (!binary_be(&c, &id) || id >= dims || (id == (uint32_t)unlimited && j != 0)) goto done;
            if (id == (uint32_t)unlimited) v[i].record = true;
            else if (!binary_mul(elems, lens[id], &elems)) goto done;
        }
        if (!nc_attrs(&c) || !binary_be(&c, &type) || !(width = nc_width(type)) || !binary_be(&c, &padded) || !binary_mul(elems, width, &v[i].bytes) ||
            v[i].bytes > UINT32_MAX - 3 || (padded != ((v[i].bytes + 3) & ~3ULL) && (!v[i].record || padded != v[i].bytes)) || !binary_get(&c, begin, h[3] == 1 ? 4 : 8))
            goto done;
        v[i].at = h[3] == 1 ? xx_data_get_u32(begin, 4, 0, true) : xx_data_get_u64(begin, 8, 0, true);
        v[i].padded = padded;
        if (v[i].at & 3) {
            goto done;
        }
        if (v[i].record) {
            ++recordvars;
            stride += padded;
        }
        for (j = 0; j < i; ++j)
            if (!xx_rt_strcmp(v[i].name, v[j].name)) goto done;
    }
    if ((uint64_t)recordvars * records + vars > 4095) goto done;
    if (recordvars == 1) {
        for (i = 0; i < vars; ++i)
            if (v[i].record) stride = v[i].bytes;
    } else
        for (i = 0; i < vars; ++i)
            if (v[i].record && v[i].padded != ((v[i].bytes + 3) & ~3ULL)) goto done;
    end = c.at;
    for (i = 0; i < vars; ++i) {
        uint64_t extent;
        if (v[i].at < c.at || v[i].at > c.end) goto done;
        if (v[i].record) {
            uint32_t k;
            extent = recordvars == 1 ? v[i].bytes : v[i].padded;
            for (k = 0; k < records; ++k) {
                char name[320];
                uint64_t displacement, at;
                if (binary_stop(pd) || !binary_mul(k, stride, &displacement) || v[i].at > (uint64_t)INT64_MAX - displacement) {
                    goto done;
                }
                at = v[i].at + displacement;
                if (!binary_range(at, extent, c.end)) {
                    goto done;
                }
                if (at + extent > end) end = at + extent;
                xx_rt_snprintf(name, sizeof(name), "%s-record-%u.bin", v[i].name, k);
                if (!pm_add(f, s, name, (int64_t)at, (int64_t)v[i].bytes)) goto done;
                s->items[s->count - 1].packed_size = (int64_t)extent;
            }
        } else {
            extent = v[i].padded;
            if (!binary_range(v[i].at, extent, c.end)) goto done;
            if (v[i].at + extent > end) end = v[i].at + extent;
            {
                char name[300];
                xx_rt_snprintf(name, sizeof(name), "%s.bin", v[i].name);
                if (!pm_add(f, s, name, (int64_t)v[i].at, (int64_t)v[i].bytes)) goto done;
                s->items[s->count - 1].packed_size = (int64_t)extent;
            }
        }
    }
    for (i = 0; i < s->count; ++i) {
        int64_t actual = s->items[i].size;
        s->items[i].size = s->items[i].packed_size;
        s->items[i].packed_size = actual;
    }
    if (!binary_disjoint(s, (uint64_t)f->base_address + c.at) || !pm_add(f, s, "netcdf-header.bin", 0, (int64_t)c.at)) goto done;
    for (i = 0; i < s->count - 1; ++i) s->items[i].size = s->items[i].packed_size;
    s->size = (int64_t)end;
    result = true;
done:
    xx_mem_free(v);
    return result;
}

void xx_netcdf_classic_init(xx_netcdf_classic *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NETCDF_CLASSIC, "netcdf_classic");
    }
}
xx_netcdf_classic *xx_netcdf_classic_create(xx_io_device *d, int64_t b)
{
    xx_netcdf_classic *r = (xx_netcdf_classic *)xx_mem_alloc(sizeof(*r));
    if (r) xx_netcdf_classic_init(r, d, b);
    return r;
}
void xx_netcdf_classic_destroy(xx_netcdf_classic *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_netcdf_classic_free(xx_netcdf_classic *r)
{
    if (r) {
        xx_netcdf_classic_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_netcdf_classic_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_netcdf_classic_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
