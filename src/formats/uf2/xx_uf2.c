/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/microsoft/uf2/blob/master/README.md
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/uf2/xx_uf2.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    (void)pd;

    uint8_t h[512];
    int64_t at, total = pm_available(f);
    uint32_t blocks, seen = 0;
    uint8_t *numbers = NULL;
    bool ok = false;
    if (total <= 0 || total % 512 || total / 512 > 65536 || !pm_read(f, 0, h, 512)) return false;
    blocks = xx_data_get_u32(h + 24, 4, 0, false);
    if (blocks == 0 || blocks > 65536 || blocks != (uint64_t)(total / 512)) return false;
    numbers = (uint8_t *)xx_mem_alloc(blocks);
    if (!numbers) return false;
    xx_mem_zero(numbers, blocks);
    for (at = 0; at < total; at += 512) {
        uint32_t flags, n, bytes, addr;
        char name[64];
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at, h, 512)) goto done;
        flags = xx_data_get_u32(h + 8, 4, 0, false);
        addr = xx_data_get_u32(h + 12, 4, 0, false);
        bytes = xx_data_get_u32(h + 16, 4, 0, false);
        n = xx_data_get_u32(h + 20, 4, 0, false);
        if (xx_data_get_u32(h, 4, 0, false) != 0x0a324655 || xx_data_get_u32(h + 4, 4, 0, false) != 0x9e5d5157 || xx_data_get_u32(h + 508, 4, 0, false) != 0x0ab16f30 ||
            xx_data_get_u32(h + 24, 4, 0, false) != blocks || n >= blocks || numbers[n] || bytes == 0 || bytes > 476 || flags & ~(uint32_t)0x00002001 ||
            addr > UINT32_MAX - bytes)
            goto done;
        numbers[n] = 1;
        ++seen;
        xx_rt_snprintf(name, sizeof(name), "block-%u-address-%08x%s.bin", (unsigned)n, (unsigned)addr, (flags & 1) ? "-comment" : "");
        if (!pm_add(f, s, name, at + 32, bytes)) goto done;
    }
    s->size = total;
    ok = seen == blocks;
done:
    xx_mem_free(numbers);
    return ok;
}
void xx_uf2_init(xx_uf2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UF2, "uf2");
    }
}
xx_uf2 *xx_uf2_create(xx_io_device *d, int64_t b)
{
    xx_uf2 *r = (xx_uf2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_uf2_init(r, d, b);
    return r;
}
void xx_uf2_destroy(xx_uf2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_uf2_free(xx_uf2 *r)
{
    if (r) {
        xx_uf2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_uf2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_uf2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
