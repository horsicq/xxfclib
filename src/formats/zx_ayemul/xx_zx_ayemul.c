/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/zx_ayemul/xx_zx_ayemul.h"
#include "../common/xx_retro_resource_components.h"
#include "xxfclib/data/xx_data.h"

static bool ay_ptr(retro_resource_blob *b, uint32_t a, uint32_t z, uint32_t *out)
{
    int32_t v;
    if (!retro_resource_span(b, a, 2)) return false;
    v = (int16_t)xx_data_get_u16(b->p + a, 2, 0, true);
    if (!v || (int64_t)a + v < 20 || (int64_t)a + v > UINT32_MAX) return false;
    *out = (uint32_t)((int64_t)a + v);
    return retro_resource_span(b, *out, z);
}
static bool read_components(Abstractformat *f, pm_stream *s, retro_resource_blob *b)
{
    uint32_t table, i, at, z, maxend = 20, n, count = 0, trackcounts;
    retro_resource_extent ext[4096];
    const uint8_t *p = b->p;
    char label[64];
    if (b->n < 20 || xx_rt_memcmp(p, "ZXAYEMUL", 8) || p[8] > 2 || xx_data_get_u16(p + 10, 2, 0, true) || p[17] > p[16]) {
        return false;
    }
    trackcounts = (uint32_t)p[16] + 1;
    if (!retro_resource_claim(b, ext, &count, 0, 20, false) || !retro_resource_emit(f, s, b, "ay-file-descriptor.bin", 0, 20)) return false;
    for (i = 12; i <= 14; i += 2) {
        if (!ay_ptr(b, i, 1, &at) || !retro_resource_string(b, at, b->n, &z, false) || !retro_resource_claim(b, ext, &count, at, z, true) ||
            !retro_resource_emit(f, s, b, i == 12 ? "author.bin" : "comment.bin", at, z))
            return false;
        if (at + z > maxend) maxend = at + z;
    }
    if (!ay_ptr(b, 18, trackcounts * 4, &table) || !retro_resource_claim(b, ext, &count, table, trackcounts * 4, false) ||
        !retro_resource_emit(f, s, b, "song-index.bin", table, trackcounts * 4)) {
        return false;
    }
    if (table + trackcounts * 4 > maxend) maxend = table + trackcounts * 4;
    for (i = 0; i < trackcounts; ++i) {
        uint32_t data, state, blocks, start, entries = 0;
        if (!ay_ptr(b, table + i * 4, 1, &at) || !retro_resource_string(b, at, b->n, &z, false) || !retro_resource_claim(b, ext, &count, at, z, true)) return false;
        xx_rt_snprintf(label, sizeof(label), "song-%03u-name.bin", i);
        if (!retro_resource_emit(f, s, b, label, at, z)) return false;
        if (at + z > maxend) maxend = at + z;
        if (!ay_ptr(b, table + i * 4 + 2, 14, &data) || !retro_resource_claim(b, ext, &count, data, 14, true) || !ay_ptr(b, data + 10, 6, &state) ||
            !retro_resource_claim(b, ext, &count, state, 6, true) || !ay_ptr(b, data + 12, 2, &blocks))
            return false;
        xx_rt_snprintf(label, sizeof(label), "song-%03u-descriptor.bin", i);
        if (!retro_resource_emit(f, s, b, label, data, 14)) return false;
        xx_rt_snprintf(label, sizeof(label), "song-%03u-cpu-state.bin", i);
        if (!retro_resource_emit(f, s, b, label, state, 6)) return false;
        if (data + 14 > maxend) {
            maxend = data + 14;
        }
        if (state + 6 > maxend) maxend = state + 6;
        start = blocks;
        for (;;) {
            uint32_t address;
            if (!retro_resource_work(b, 1) || !retro_resource_span(b, blocks, 2)) return false;
            address = xx_data_get_u16(p + blocks, 2, 0, true);
            if (!address) {
                blocks += 2;
                break;
            }
            if (entries >= 256 || !retro_resource_span(b, blocks, 6)) {
                return false;
            }
            n = xx_data_get_u16(p + blocks + 2, 2, 0, true);
            if (!n || n > 65536 - address || !ay_ptr(b, blocks + 4, n, &at) || !retro_resource_claim(b, ext, &count, at, n, true)) return false;
            xx_rt_snprintf(label, sizeof(label), "song-%03u-memory-%04x.bin", i, address);
            if (!retro_resource_emit(f, s, b, label, at, n)) return false;
            if (at + n > maxend) maxend = at + n;
            blocks += 6;
            ++entries;
        }
        if (!entries || !retro_resource_claim(b, ext, &count, start, blocks - start, true)) return false;
        xx_rt_snprintf(label, sizeof(label), "song-%03u-block-index.bin", i);
        if (!retro_resource_emit(f, s, b, label, start, blocks - start)) return false;
        if (blocks > maxend) maxend = blocks;
    }
    if (maxend != b->n) return false;
    s->size = b->n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_resource_blob b;
    bool ok;
    if (!retro_resource_load(f, &b, pd)) return false;
    ok = read_components(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}
void xx_zx_ayemul_init(xx_zx_ayemul *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ZX_AYEMUL, "zx_ayemul");
    }
}
xx_zx_ayemul *xx_zx_ayemul_create(xx_io_device *d, int64_t b)
{
    xx_zx_ayemul *r = (xx_zx_ayemul *)xx_mem_alloc(sizeof(*r));
    if (r) xx_zx_ayemul_init(r, d, b);
    return r;
}
void xx_zx_ayemul_destroy(xx_zx_ayemul *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_zx_ayemul_free(xx_zx_ayemul *r)
{
    if (r) {
        xx_zx_ayemul_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_zx_ayemul_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_zx_ayemul_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
