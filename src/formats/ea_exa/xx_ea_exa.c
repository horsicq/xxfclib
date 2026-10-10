/* SPDX-License-Identifier: MIT.
 * EA SCHl block framing: https://github.com/vgmstream/vgmstream/blob/master/src/layout/blocked_ea_schl.c
 * Exports original stream blocks, leaving their coded samples unchanged.
 */
#include "xxfclib/formats/ea_exa/xx_ea_exa.h"
#include "../common/xx_binary_cursor.h"
#ifndef EA_EXA
#define XX_FILE_TYPE_EA_EXA ((xx_file_type_t)1512)
#endif
static bool known(const uint8_t *p)
{
    return !xx_rt_memcmp(p, "SCHl", 4) || !xx_rt_memcmp(p, "SCCl", 4) || !xx_rt_memcmp(p, "SCDl", 4) || !xx_rt_memcmp(p, "SCEl", 4);
}
static bool scan(Abstractformat *f, bool be, xx_pd_struct *pd, uint64_t *length, unsigned *count)
{
    int64_t n = pm_available(f);
    uint64_t p = 0;
    unsigned blocks = 0, data = 0;
    bool end = false;
    while (p < (uint64_t)n && !end) {
        uint8_t h[16];
        uint32_t size;
        if (binary_stop(pd) || ++blocks > 4096 || !binary_range(p, 8, (uint64_t)n) || !pm_read(f, (int64_t)p, h, 8) || !known(h)) return false;
        size = xx_data_get_u32(h + 4, 4, 0, be);
        if (size < 8 || !binary_range(p, size, (uint64_t)n)) return false;
        if (blocks == 1) {
            if (xx_rt_memcmp(h, "SCHl", 4) || size < 16 || !pm_read(f, (int64_t)p + 8, h, 8) ||
                (xx_rt_memcmp(h, "GSTR", 4) && xx_rt_memcmp(h, "PT", 2) && xx_rt_memcmp(h + 4, "GSTR", 4) && xx_rt_memcmp(h + 4, "PT", 2)))
                return false;
        } else if (!xx_rt_memcmp(h, "SCDl", 4)) {
            if (size < 12) return false;
            ++data;
        } else if (!xx_rt_memcmp(h, "SCEl", 4)) end = true;
        p += size;
    }
    if (!data || (!end && p != (uint64_t)n)) return false;
    *length = p;
    *count = blocks;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint64_t length, p = 0;
    unsigned count, i;
    bool be = false;
    if (!scan(f, false, pd, &length, &count)) {
        if (!scan(f, true, pd, &length, &count)) return false;
        be = true;
    }
    for (i = 0; i < count; ++i) {
        uint8_t h[8];
        uint32_t size;
        char label[40], tag[5];
        if (binary_stop(pd) || !pm_read(f, (int64_t)p, h, 8)) return false;
        size = xx_data_get_u32(h + 4, 4, 0, be);
        xx_rt_memcpy(tag, h, 4);
        tag[4] = 0;
        xx_rt_snprintf(label, sizeof(label), "%s-block.bin", tag);
        if (!pm_add(f, s, label, (int64_t)p, size)) return false;
        p += size;
    }
    if (p != length) return false;
    s->size = (int64_t)length;
    return true;
}
void xx_ea_exa_init(xx_ea_exa *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_EA_EXA, "exa");
    }
}
xx_ea_exa *xx_ea_exa_create(xx_io_device *d, int64_t b)
{
    xx_ea_exa *r = (xx_ea_exa *)xx_mem_alloc(sizeof(*r));
    if (r) xx_ea_exa_init(r, d, b);
    return r;
}
void xx_ea_exa_destroy(xx_ea_exa *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_ea_exa_free(xx_ea_exa *r)
{
    if (r) {
        xx_ea_exa_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_ea_exa_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_ea_exa_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
