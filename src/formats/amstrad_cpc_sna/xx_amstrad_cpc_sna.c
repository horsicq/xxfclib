/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://cpctech.cpcwiki.de/docs/snapshot.html
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/amstrad_cpc_sna/xx_amstrad_cpc_sna.h"
#include "../common/xx_retro_tape_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f, pm_stream *s, retro_tape_blob *b)
{
    uint32_t ram, i;
    const uint8_t *p = b->p;
    if (b->n < 256 || xx_rt_memcmp(p, "MV - SNA", 8) || !retro_tape_zero(p + 8, 8) || p[16] < 1 || p[16] > 3 || p[0x1b] > 1 || p[0x1c] > 1 || p[0x25] > 2 ||
        p[0x2e] > 16 || p[0x42] > 31 || p[0x5a] > 15)
        return false;
    for (i = 0x2f; i <= 0x3f; ++i) {
        if (p[i] > 31) return false;
    }
    ram = xx_data_get_u16(p + 0x6b, 2, 0, false);
    if ((ram != 64 && ram != 128) || b->n != 256 + ram * 1024) return false;
    if ((p[16] == 1 && !retro_tape_zero(p + 0x6d, 147)) || (p[16] > 1 && p[0x6d] > 5)) return false;
    if (!retro_tape_emit(f, s, b, "machine-state.bin", 0, 256) || !retro_tape_emit(f, s, b, "physical-ram.bin", 256, ram * 1024)) {
        return false;
    }
    s->size = b->n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_tape_blob b;
    bool ok;
    if (!retro_tape_load(f, &b, pd)) return false;
    ok = read_components(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}
void xx_amstrad_cpc_sna_init(xx_amstrad_cpc_sna *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AMSTRAD_CPC_SNA, "amstrad_cpc_sna");
    }
}
xx_amstrad_cpc_sna *xx_amstrad_cpc_sna_create(xx_io_device *d, int64_t b)
{
    xx_amstrad_cpc_sna *r = (xx_amstrad_cpc_sna *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amstrad_cpc_sna_init(r, d, b);
    return r;
}
void xx_amstrad_cpc_sna_destroy(xx_amstrad_cpc_sna *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amstrad_cpc_sna_free(xx_amstrad_cpc_sna *r)
{
    if (r) {
        xx_amstrad_cpc_sna_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amstrad_cpc_sna_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_amstrad_cpc_sna_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
