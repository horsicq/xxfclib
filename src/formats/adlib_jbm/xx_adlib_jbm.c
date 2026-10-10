/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/adlib_jbm/xx_adlib_jbm.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    music_blob b = {0};
    uint8_t *marks = NULL, used[255] = {0};
    uint32_t refs[11], table, inst, first = 65536, pool = 65536, ns, ni, i;
    uint64_t at, start;
    bool ok = false;
    MUSIC_NEED(music_load(f, &b, pd) && music_span(&b, 0, 32) && xx_data_get_u16(b.p, 2, 0, false) == 2 && xx_data_get_u16(b.p + 2, 2, 0, false) &&
               !(xx_data_get_u16(b.p + 8, 2, 0, false) & ~1U));
    table = xx_data_get_u16(b.p + 4, 2, 0, false);
    inst = xx_data_get_u16(b.p + 6, 2, 0, false);
    MUSIC_NEED(table == 32 && inst < b.n && inst > 32 && (b.n - inst) % 16 == 0);
    ni = (uint32_t)((b.n - inst) / 16);
    MUSIC_NEED(ni && ni <= 256);
    for (i = 0; i < 11; ++i) {
        refs[i] = xx_data_get_u16(b.p + 10 + i * 2, 2, 0, false);
        if (refs[i]) {
            MUSIC_NEED(refs[i] >= table && refs[i] < inst);
            if (refs[i] < first) first = refs[i];
        }
    }
    MUSIC_NEED(first < inst && first > table && !((first - table) & 1));
    ns = (first - table) / 2;
    MUSIC_NEED(ns && ns <= 255);
    for (i = 0; i < 11; ++i)
        if (refs[i]) {
            bool end = false;
            at = refs[i];
            while (at < inst) {
                uint8_t v = b.p[(size_t)at++];
                MUSIC_NEED(music_work(&b, 1));
                if (v == 255) {
                    end = true;
                    break;
                }
                MUSIC_NEED(v < ns);
                used[v] = 1;
            }
            MUSIC_NEED(end);
        }
    for (i = 0; i < ns; ++i) {
        if (used[i]) {
            uint32_t p = xx_data_get_u16(b.p + table + i * 2, 2, 0, false);
            MUSIC_NEED(p > first && p < inst);
            if (p < pool) pool = p;
        }
    }
    MUSIC_NEED(pool < inst);
    for (i = 0; i < 11; ++i)
        if (refs[i]) {
            at = refs[i];
            MUSIC_NEED(at < pool);
            while (at < pool && b.p[(size_t)at] != 255) {
                MUSIC_NEED(music_work(&b, 1));
                ++at;
            }
            MUSIC_NEED(at < pool);
        }
    marks = (uint8_t *)xx_mem_alloc(inst);
    MUSIC_NEED(marks);
    xx_mem_zero(marks, inst);
    MUSIC_NEED(music_emit(f, s, &b, "descriptor.jbm", 0, 32) && music_emit(f, s, &b, "sequence-index.jbm", table, first - table) &&
               music_emit(f, s, &b, "voice-orders.jbm", first, pool - first));
    at = pool;
    while (at < inst) {
        bool end = false;
        start = at;
        marks[(size_t)at] = 1;
        while (at < inst) {
            uint8_t c = b.p[(size_t)at++];
            MUSIC_NEED(music_work(&b, 1));
            if (c == 255) {
                end = true;
                break;
            }
            if (c == 253) {
                MUSIC_NEED(at < inst && b.p[(size_t)at] < ni);
                ++at;
            } else {
                MUSIC_NEED((c & 127) <= 95 && at <= inst && inst - at >= 3 && b.p[(size_t)at] <= 63);
                at += 3;
            }
        }
        MUSIC_NEED(end && music_emit(f, s, &b, "sequence.jbm", start, at - start));
    }
    for (i = 0; i < ns; ++i) {
        if (used[i]) MUSIC_NEED(marks[xx_data_get_u16(b.p + table + i * 2, 2, 0, false)]);
    }
    for (i = 0; i < ni; ++i) MUSIC_NEED(music_emit(f, s, &b, "instrument.jbm", inst + i * 16, 16));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(marks);
    xx_mem_free(b.p);
    return ok;
}
void xx_adlib_jbm_init(xx_adlib_jbm *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ADLIB_JBM, "adlib_jbm");
    }
}
xx_adlib_jbm *xx_adlib_jbm_create(xx_io_device *d, int64_t b)
{
    xx_adlib_jbm *r = (xx_adlib_jbm *)xx_mem_alloc(sizeof(*r));
    if (r) xx_adlib_jbm_init(r, d, b);
    return r;
}
void xx_adlib_jbm_destroy(xx_adlib_jbm *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_adlib_jbm_free(xx_adlib_jbm *r)
{
    if (r) {
        xx_adlib_jbm_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_adlib_jbm_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_adlib_jbm_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
