/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/WizardMac/ReadStat/master/src/spss/readstat_sav_read.c */
#include "xxfclib/formats/spss_sav/xx_spss_sav.h"
#include "../common/xx_binary_cursor.h"

static bool sv_i(binary_cursor *c, bool be, uint32_t *v)
{
    uint8_t b[4];
    if (!binary_get(c, b, 4)) return false;
    *v = xx_data_get_u32(b, 4, 0, be);
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[176];
    bool be;
    uint32_t slots, cases, variables = 0, continuations = 0, tag;
    binary_cursor c = {f, 176, (uint64_t)pm_available(f), pd, 0};
    uint64_t bytes;
    if (!pm_read(f, 0, h, 176) || xx_rt_memcmp(h, "$FL2", 4)) return false;
    be = xx_data_get_u32(h + 64, 4, 0, true) == 2;
    if (xx_data_get_u32(h + 64, 4, 0, be) != 2 || xx_data_get_u32(h + 72, 4, 0, be) != 0 || !(slots = xx_data_get_u32(h + 68, 4, 0, be)) || slots > 4096 ||
        (cases = xx_data_get_u32(h + 80, 4, 0, be)) > 65535 || xx_data_get_u32(h + 76, 4, 0, be) > slots)
        return false;
    for (;;) {
        if (!sv_i(&c, be, &tag)) return false;
        if (tag == 2) {
            uint8_t v[28];
            uint32_t type, label, missing;
            if (variables >= slots || !binary_get(&c, v, 28)) return false;
            type = xx_data_get_u32(v, 4, 0, be);
            label = xx_data_get_u32(v + 4, 4, 0, be);
            missing = xx_data_get_u32(v + 8, 4, 0, be);
            if (label > 1 || ((int32_t)missing < -3 || (int32_t)missing > 3)) return false;
            if (continuations) {
                if (type != UINT32_MAX || label || missing) return false;
                --continuations;
            } else {
                if (type > 255 || !v[20] || v[20] == ' ') return false;
                if (type > 8) continuations = (type + 7) / 8 - 1;
            }
            if (label) {
                uint32_t n;
                if (!sv_i(&c, be, &n) || n > 65535 || !binary_skip(&c, ((uint64_t)n + 3) & ~3ULL)) return false;
            }
            if (!binary_skip(&c, (missing > INT32_MAX ? (uint64_t)(-(int32_t)missing) : missing) * 8)) {
                return false;
            }
            ++variables;
        } else if (tag == 3) {
            uint32_t n, i, indexcount, link;
            if (!sv_i(&c, be, &n) || n > 4096) return false;
            for (i = 0; i < n; ++i) {
                uint8_t len;
                if (!binary_skip(&c, 8) || !binary_get(&c, &len, 1) || !binary_skip(&c, (((uint64_t)len + 1 + 7) & ~7ULL) - 1)) return false;
            }
            if (!sv_i(&c, be, &link) || link != 4 || !sv_i(&c, be, &indexcount) || !indexcount || indexcount > slots) return false;
            for (i = 0; i < indexcount; ++i) {
                uint32_t id;
                if (!sv_i(&c, be, &id) || !id || id > slots) return false;
            }
        } else if (tag == 6) {
            uint32_t n;
            if (!sv_i(&c, be, &n) || n > 4096 || !binary_skip(&c, (uint64_t)n * 80)) return false;
        } else if (tag == 7) {
            uint32_t subtype, size, n;
            uint64_t amount;
            if (!sv_i(&c, be, &subtype) || !subtype || !sv_i(&c, be, &size) || !size || !sv_i(&c, be, &n) || !binary_mul(size, n, &amount) || amount > 16777216 ||
                !binary_skip(&c, amount))
                return false;
        } else if (tag == 999) {
            uint32_t zero;
            if (!sv_i(&c, be, &zero) || zero || variables != slots || continuations) return false;
            break;
        } else return false;
    }
    if (!binary_mul((uint64_t)slots * 8, cases, &bytes) || !binary_range(c.at, bytes, c.end) || !pm_add(f, s, "sav-dictionary.bin", 0, (int64_t)c.at) ||
        !pm_add(f, s, "cases.bin", (int64_t)c.at, (int64_t)bytes))
        return false;
    s->size = (int64_t)(c.at + bytes);
    return true;
}

void xx_spss_sav_init(xx_spss_sav *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SPSS_SAV, "spss_sav");
    }
}
xx_spss_sav *xx_spss_sav_create(xx_io_device *d, int64_t b)
{
    xx_spss_sav *r = (xx_spss_sav *)xx_mem_alloc(sizeof(*r));
    if (r) xx_spss_sav_init(r, d, b);
    return r;
}
void xx_spss_sav_destroy(xx_spss_sav *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_spss_sav_free(xx_spss_sav *r)
{
    if (r) {
        xx_spss_sav_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_spss_sav_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_spss_sav_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
