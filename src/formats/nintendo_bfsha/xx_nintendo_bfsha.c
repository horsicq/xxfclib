/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/KillzXGaming/BfshaLibrary/master/ShaderLibrary/WiiU/BfshaSaverWiiU.cs
 * WiiU big-endian FSHA2.0 archives with up to32 shader models and256 simple programs each (4096 output components maximum), no options/attributes/samplers/uniform
 * metadata, standard GX2 vertex/pixel headers. Validates signed relative pointers, dictionary records, at most4096 string-pool records, archive backlinks, header/data
 * separation and aligned code extents. Exports model/program/GX2 headers and stored shader code; overlapping/shared payloads, geometry shaders, complex bindings, other
 * versions, Switch relocation, GPU execution and rendering unsupported.
 */
#include "xxfclib/formats/nintendo_bfsha/xx_nintendo_bfsha.h"
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
static bool take(Abstractformat *f, uint64_t *at, uint64_t end, void *p, size_t n, xx_pd_struct *pd)
{
    if (stop(pd) || !span(*at, n, end) || !pm_read(f, (int64_t)*at, p, n)) return false;
    *at += n;
    return true;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool cstring(Abstractformat *f, uint64_t *at, uint64_t end, unsigned maximum, bool empty, xx_pd_struct *pd)
{
    uint8_t c;
    unsigned i;
    for (i = 0; i < maximum; ++i) {
        if (!take(f, at, end, &c, 1, pd)) return false;
        if (!c) return empty || i != 0;
    }
    return false;
}
typedef struct rg {
    uint64_t at, n;
} rg;
static bool zeros(Abstractformat *f, uint64_t at, uint64_t n, xx_pd_struct *pd)
{
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *b = NULL;
    bool buffer_result = false;
    if (n) {
        if (capacity > n) capacity = (size_t)n;
        b = (uint8_t *)xx_mem_alloc(capacity);
        if (!b) {
            buffer_result = (false);
            goto buffer_done;
        }
    }
    size_t i;
    while (n) {
        size_t part = n > capacity ? capacity : (size_t)n;
        if (stop(pd) || !pm_read(f, (int64_t)at, b, part)) {
            buffer_result = (false);
            goto buffer_done;
        }
        for (i = 0; i < part; ++i)
            if (b[i]) {
                buffer_result = (false);
                goto buffer_done;
            }
        at += part;
        n -= part;
    }
    {
        buffer_result = (true);
        goto buffer_done;
    }
buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool fsha_ref(Abstractformat *f, uint64_t field, uint64_t total, uint64_t *value)
{
    uint8_t p[4];
    int64_t target;
    if (!pm_read(f, (int64_t)field, p, 4)) return false;
    if (!xx_data_get_u32(p, 4, 0, true)) {
        *value = 0;
        return true;
    }
    target = (int64_t)field + (int32_t)xx_data_get_u32(p, 4, 0, true);
    if (target < 56 || (uint64_t)target >= total) return false;
    *value = (uint64_t)target;
    return true;
}
static bool fsha_pool(Abstractformat *f, uint64_t pool, uint32_t size, uint32_t *strings, unsigned *count, xx_pd_struct *pd)
{
    uint8_t p[4];
    uint64_t at = pool, end = pool + size;
    *count = 0;
    if (pool & 3) return false;
    while (at < end) {
        uint32_t n;
        uint64_t text, next;
        if (*count >= 4096 || !span(at, 4, end) || !pm_read(f, (int64_t)at, p, 4) || (n = xx_data_get_u32(p, 4, 0, true)) > 4096 || !span(at + 4, (uint64_t)n + 1, end))
            return false;
        strings[(*count)++] = (uint32_t)(text = at + 4);
        if (!cstring(f, &text, at + 5 + n, 4096, true, pd) || text != at + 5 + n) return false;
        next = (text + 3) & ~3ULL;
        if (next > end || !zeros(f, text, next - text, pd)) return false;
        at = next;
    }
    return *count != 0;
}
static bool fsha_name(uint64_t value, const uint32_t *strings, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        if (strings[i] == value) return true;
    return false;
}
static bool fsha_shader(Abstractformat *f, pm_stream *s, uint64_t at, unsigned regs, unsigned unused, uint32_t alignment, uint32_t total, xx_pd_struct *pd)
{
    uint8_t p[12];
    uint64_t data;
    uint32_t n, header = regs * 4 + 12 + unused * 4;
    if (!span(at, header, total) || !pm_read(f, (int64_t)(at + regs * 4), p, 12) || !(n = xx_data_get_u32(p, 4, 0, true)) || n > 8388608 || (n & 7) ||
        xx_data_get_u32(p + 8, 4, 0, true) > 2 || !fsha_ref(f, at + regs * 4 + 4, total, &data) || !data || data % alignment ||
        !zeros(f, at + regs * 4 + 12, unused * 4, pd) || !emit(f, s, "shader-header.bin", at, header, total) || !emit(f, s, "shader-code.bin", data, n, total))
        return false;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[56], d[16], m[112], p[56], q[4];
    uint32_t total, align, size, count, i, j, n, strings[4096];
    unsigned ns;
    uint64_t pool, dict, name, path, model, program, vertex, pixel, parent, ptr;
    if (!pm_read(f, 0, h, 56) || xx_rt_memcmp(h, "FSHA", 4) || h[4] != 2 || h[5] || h[6] || h[7] || xx_data_get_u16(h + 8, 2, 0, true) != 0xfeff ||
        xx_data_get_u16(h + 10, 2, 0, true) != 16 || (total = xx_data_get_u32(h + 12, 4, 0, true)) > (uint64_t)pm_available(f) ||
        (align = xx_data_get_u32(h + 16, 4, 0, true)) < 8 || align > 65536 || (align & (align - 1)) || (size = xx_data_get_u32(h + 24, 4, 0, true)) > 1048576 || !size ||
        !(count = xx_data_get_u16(h + 36, 2, 0, true)) || count > 32 || xx_data_get_u16(h + 38, 2, 0, true) || xx_data_get_u32(h + 40, 4, 0, true) ||
        xx_data_get_u32(h + 48, 4, 0, true) || xx_data_get_u32(h + 52, 4, 0, true))
        return false;
    if (!fsha_ref(f, 28, total, &pool) || !pool || !span(pool, size, total) || !fsha_pool(f, pool, size, strings, &ns, pd) || !fsha_ref(f, 20, total, &name) ||
        !fsha_name(name, strings, ns) || !fsha_ref(f, 32, total, &path) || !fsha_name(path, strings, ns) || !fsha_ref(f, 44, total, &dict) || !dict ||
        !pm_read(f, (int64_t)dict, q, 4) || xx_data_get_u32(q, 4, 0, true) != 8 + 16 * (count + 1) || !pm_read(f, (int64_t)dict + 4, q, 4) ||
        xx_data_get_u32(q, 4, 0, true) != count || !emit(f, s, "model-dictionary.bin", dict, 8 + 16 * (count + 1), total) ||
        !emit(f, s, "string-pool.bin", pool, size, total))
        return false;
    for (i = 0; i <= count; ++i) {
        uint64_t node = dict + 8 + i * 16;
        if (stop(pd) || !pm_read(f, (int64_t)node, d, 16) || xx_data_get_u16(d + 4, 2, 0, true) > count || xx_data_get_u16(d + 6, 2, 0, true) > count ||
            !fsha_ref(f, node + 8, total, &name) || !fsha_ref(f, node + 12, total, &model))
            return false;
        if (!i) {
            if (model || name) return false;
            continue;
        }
        if (!model || !fsha_name(name, strings, ns) || !pm_read(f, (int64_t)model, m, 112) || m[0] || m[1] || xx_data_get_u16(m + 2, 2, 0, true) ||
            xx_data_get_u16(m + 4, 2, 0, true) || !(n = xx_data_get_u16(m + 6, 2, 0, true)) || n > 256 || m[8] || m[9] || m[10] || m[11] ||
            xx_data_get_u32(m + 20, 4, 0, true) || xx_data_get_u32(m + 28, 4, 0, true) >= n || !zeros(f, model + 44, 44, pd) || xx_data_get_u32(m + 92, 4, 0, true) ||
            model + 96 > INT32_MAX || xx_data_get_u32(m + 96, 4, 0, true) != 0U - (uint32_t)(model + 96) || !zeros(f, model + 100, 12, pd) ||
            !fsha_ref(f, model + 32, total, &name) || !fsha_name(name, strings, ns) || !fsha_ref(f, model + 88, total, &program) || !program ||
            !emit(f, s, "model-header.bin", model, 112, total))
            return false;
        for (j = 0; j < n; ++j) {
            uint64_t at = program + (uint64_t)j * 56;
            if (!pm_read(f, (int64_t)at, p, 56) || p[2] || p[3] || xx_data_get_u32(p + 4, 4, 0, true) || xx_data_get_u32(p + 28, 4, 0, true) ||
                xx_data_get_u32(p + 32, 4, 0, true) || xx_data_get_u32(p + 36, 4, 0, true) || !fsha_ref(f, at + 40, total, &vertex) || !vertex ||
                !fsha_ref(f, at + 44, total, &ptr) || ptr || !fsha_ref(f, at + 48, total, &pixel) || !fsha_ref(f, at + 52, total, &parent) || parent != model ||
                !emit(f, s, "program-header.bin", at, 56, total) || !fsha_shader(f, s, vertex, 52, 22, align, total, pd) ||
                (pixel && !fsha_shader(f, s, pixel, 41, 14, align, total, pd)))
                return false;
        }
    }
    s->size = total;
    return true;
}

void xx_nintendo_bfsha_init(xx_nintendo_bfsha *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NINTENDO_BFSHA, "bfsha");
    }
}
xx_nintendo_bfsha *xx_nintendo_bfsha_create(xx_io_device *d, int64_t b)
{
    xx_nintendo_bfsha *r = (xx_nintendo_bfsha *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nintendo_bfsha_init(r, d, b);
    return r;
}
void xx_nintendo_bfsha_destroy(xx_nintendo_bfsha *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nintendo_bfsha_free(xx_nintendo_bfsha *r)
{
    if (r) {
        xx_nintendo_bfsha_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nintendo_bfsha_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nintendo_bfsha_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
