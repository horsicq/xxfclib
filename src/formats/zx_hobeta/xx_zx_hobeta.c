/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://www.zxpress.ru/en/ezines/netus-news/07/hobetta-header-format-for-tr-dos-disks-file-structure-description-first-13-bytes-match-tr-dos
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/zx_hobeta/xx_zx_hobeta.h"
#include "../common/xx_retro_tape_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f, pm_stream *s, retro_tape_blob *b)
{
    uint32_t i, z, length, sum = 105;
    char name[16];
    const uint8_t *p = b->p;
    if (b->n < 273 || p[13] || !p[14] || (p[8] != 'B' && p[8] != 'C' && p[8] != 'D') || !retro_tape_name(p, 8, name, false)) return false;
    for (i = 0; i < 15; ++i) {
        sum += (uint32_t)p[i] * 257U;
    }
    if ((sum & 65535U) != xx_data_get_u16(p + 15, 2, 0, false)) return false;
    z = (uint32_t)p[14] * 256U;
    length = xx_data_get_u16(p + 11, 2, 0, false);
    if (b->n != 17 + z || !length || length > z || (p[8] == 'C' && length > 65536U - xx_data_get_u16(p + 9, 2, 0, false))) return false;
    if (!retro_tape_emit(f, s, b, "trdos-file-descriptor.bin", 0, 17) || !retro_tape_emit(f, s, b, "original-file-sectors.bin", 17, z)) {
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
void xx_zx_hobeta_init(xx_zx_hobeta *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ZX_HOBETA, "zx_hobeta");
    }
}
xx_zx_hobeta *xx_zx_hobeta_create(xx_io_device *d, int64_t b)
{
    xx_zx_hobeta *r = (xx_zx_hobeta *)xx_mem_alloc(sizeof(*r));
    if (r) xx_zx_hobeta_init(r, d, b);
    return r;
}
void xx_zx_hobeta_destroy(xx_zx_hobeta *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_zx_hobeta_free(xx_zx_hobeta *r)
{
    if (r) {
        xx_zx_hobeta_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_zx_hobeta_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_zx_hobeta_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
