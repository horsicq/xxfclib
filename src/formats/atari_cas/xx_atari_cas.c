/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://github.com/atari800/atari800/blob/master/src/img_tape.c
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/atari_cas/xx_atari_cas.h"
#include "../common/xx_retro_tape_components.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f, pm_stream *s, retro_tape_blob *b)
{
    uint32_t a = 0, count = 0, records = 0;
    const uint8_t *p = b->p;
    while (a < b->n) {
        uint32_t z;
        uint16_t aux;
        char label[40];
        if (!retro_tape_poll(b) || ++count > 2048 || !retro_tape_range(b, a, 8)) return false;
        z = xx_data_get_u16(p + a + 4, 2, 0, false);
        aux = xx_data_get_u16(p + a + 6, 2, 0, false);
        if (!retro_tape_range(b, a + 8, z)) return false;
        if (!a) {
            uint32_t i;
            if (xx_rt_memcmp(p, "FUJI", 4) || aux || z > 4096) return false;
            for (i = 0; i < z; ++i)
                if (p[8 + i] < 32 || p[8 + i] > 126) return false;
            if (!retro_tape_emit(f, s, b, "cassette-description.bin", 0, 8 + z)) return false;
        } else if (!xx_rt_memcmp(p + a, "baud", 4)) {
            if (z || !aux) return false;
        } else if (!xx_rt_memcmp(p + a, "data", 4) || !xx_rt_memcmp(p + a, "fsk ", 4)) {
            bool fsk = p[a] == 'f';
            if (!z || (fsk && (z & 1U))) return false;
            xx_rt_snprintf(label, sizeof(label), "record-%04u.%s", records++, fsk ? "fsk" : "bin");
            if (!retro_tape_emit(f, s, b, label, a + 8, z)) return false;
        } else {
            return false;
        }
        a += 8 + z;
    }
    s->size = b->n;
    return records != 0;
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
void xx_atari_cas_init(xx_atari_cas *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ATARI_CAS, "atari_cas");
    }
}
xx_atari_cas *xx_atari_cas_create(xx_io_device *d, int64_t b)
{
    xx_atari_cas *r = (xx_atari_cas *)xx_mem_alloc(sizeof(*r));
    if (r) xx_atari_cas_init(r, d, b);
    return r;
}
void xx_atari_cas_destroy(xx_atari_cas *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_atari_cas_free(xx_atari_cas *r)
{
    if (r) {
        xx_atari_cas_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_atari_cas_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_atari_cas_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
