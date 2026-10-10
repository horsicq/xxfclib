/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://worldofspectrum.net/TZXformat.html
 * TZX 1.00-1.20 straight-line data, tone, pulse, pause and descriptive/group blocks only. Standard data may intentionally contain non-ROM or bad-checksum tape bytes.
 * Control-flow, CSW/generalized blocks rejected. Original block export, no tape playback.
 */
#include "xxfclib/formats/zx_spectrum_tzx/xx_zx_spectrum_tzx.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f, pm_stream *s, retro_disk_blob *b)
{
    uint32_t at = 10, blocks = 0, groups = 0;
    char name[48];
    if (!retro_disk_range(b, 0, 10) || xx_rt_memcmp(b->p, "ZXTape!\x1a", 8) || b->p[8] != 1 || b->p[9] > 20 || !retro_disk_emit(f, s, b, "tape-descriptor.bin", 0, 10))
        return false;
    while (at < b->n) {
        uint32_t start = at, z = 0, i;
        uint8_t id = b->p[at++];
        if (++blocks > 4095 || !retro_disk_poll(b)) return false;
        switch (id) {
            case 0x10:
                if (!retro_disk_range(b, at, 4) || !(z = xx_data_get_u16(b->p + at + 2, 2, 0, false))) return false;
                z += 4;
                break;
            case 0x11:
                if (!retro_disk_range(b, at, 18) || !xx_data_get_u16(b->p + at, 2, 0, false) || !xx_data_get_u16(b->p + at + 2, 2, 0, false) ||
                    !xx_data_get_u16(b->p + at + 4, 2, 0, false) || !xx_data_get_u16(b->p + at + 6, 2, 0, false) || !xx_data_get_u16(b->p + at + 8, 2, 0, false) ||
                    !b->p[at + 12] || b->p[at + 12] > 8 || !(z = xx_data_get_u24(b->p + at + 15, 3, 0, false)))
                    return false;
                z += 18;
                break;
            case 0x12:
                if (!retro_disk_range(b, at, 4) || !xx_data_get_u16(b->p + at, 2, 0, false) || !xx_data_get_u16(b->p + at + 2, 2, 0, false)) return false;
                z = 4;
                break;
            case 0x13:
                if (!retro_disk_range(b, at, 1) || !b->p[at]) return false;
                z = 1 + (uint32_t)b->p[at] * 2;
                if (!retro_disk_range(b, at, z)) return false;
                for (i = 1; i < z; i += 2)
                    if (!xx_data_get_u16(b->p + at + i, 2, 0, false)) return false;
                break;
            case 0x14:
                if (!retro_disk_range(b, at, 10) || !xx_data_get_u16(b->p + at, 2, 0, false) || !xx_data_get_u16(b->p + at + 2, 2, 0, false) || !b->p[at + 4] ||
                    b->p[at + 4] > 8 || !(z = xx_data_get_u24(b->p + at + 7, 3, 0, false)))
                    return false;
                z += 10;
                break;
            case 0x20: z = 2; break;
            case 0x21:
                if (++groups > 16) return false; /* fall through */
            case 0x30:
                if (!retro_disk_range(b, at, 1) || !b->p[at]) return false;
                z = 1 + b->p[at];
                if (!retro_disk_range(b, at, z) || !retro_disk_ascii(b->p + at + 1, z - 1, false)) return false;
                break;
            case 0x22:
                if (!groups) return false;
                --groups;
                break;
            case 0x32:
                if (!retro_disk_range(b, at, 3) || (z = xx_data_get_u16(b->p + at, 2, 0, false)) < 1 || !retro_disk_range(b, at + 2, z)) return false;
                {
                    uint32_t p = at + 3, end = at + 2 + z, c = b->p[at + 2];
                    for (i = 0; i < c; ++i) {
                        uint32_t k;
                        if (end - p < 2 || ((b->p[p] > 8) && b->p[p] != 255) || !(k = b->p[p + 1]) || k > end - p - 2 || !retro_disk_ascii(b->p + p + 2, k, false))
                            return false;
                        p += 2 + k;
                    }
                    if (p != end) return false;
                }
                z += 2;
                break;
            case 0x35:
                if (!retro_disk_range(b, at, 20) || !retro_disk_ascii(b->p + at, 16, true) || (z = xx_data_get_u32(b->p + at + 16, 4, 0, false)) > RETRO_DISK_LIMIT - 20)
                    return false;
                z += 20;
                break;
            default: return false;
        }
        if (!retro_disk_range(b, at, z)) {
            return false;
        }
        at += z;
        xx_rt_snprintf(name, sizeof(name), "block-%u-%02x.bin", blocks - 1, id);
        if (!retro_disk_emit(f, s, b, name, start, at - start)) return false;
    }
    if (!blocks || groups) {
        return false;
    }
    s->size = at;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_disk_blob b;
    bool ok;
    if (!retro_disk_load(f, &b, pd)) return false;
    ok = parse_blob(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}

void xx_zx_spectrum_tzx_init(xx_zx_spectrum_tzx *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_ZX_SPECTRUM_TZX, "tzx");
    }
}
xx_zx_spectrum_tzx *xx_zx_spectrum_tzx_create(xx_io_device *d, int64_t b)
{
    xx_zx_spectrum_tzx *r = (xx_zx_spectrum_tzx *)xx_mem_alloc(sizeof(*r));
    if (r) xx_zx_spectrum_tzx_init(r, d, b);
    return r;
}
void xx_zx_spectrum_tzx_destroy(xx_zx_spectrum_tzx *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_zx_spectrum_tzx_free(xx_zx_spectrum_tzx *r)
{
    if (r) {
        xx_zx_spectrum_tzx_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_zx_spectrum_tzx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_zx_spectrum_tzx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
