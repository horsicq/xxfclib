/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xchz.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/archive/sfx_chz/xx_sfx_chz.h"
#include "../../common/xx_executable_carrier.h"

#include "xxfclib/formats/archive/chz/xx_chz.h"

static bool executable_carrier_chz_at(Abstractformat *f, pm_stream *s, int64_t at, xx_pd_struct *pd)
{
    xx_chz *child = xx_chz_create(f->device, f->base_address + at);
    int64_t size = -1;
    bool valid = child && xx_chz_handle_base_info(&child->format, pd);
    if (valid) size = child->format.format_size;
    xx_chz_free(child);
    /* The native chain reader retains only complete prefix records and owns
     * ChArc decoding. Do not publish its packed streams as raw files. */
    return valid && size > 0 && executable_carrier_component(f, s, at, size, "payload.chz");
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[64], newh[2];
    uint32_t at;
    static const uint8_t sig[] = {'S', 'C', 'h'};
    if (!pm_read(f, 0, h, 64) || (xx_rt_memcmp(h, "MZ", 2) && xx_rt_memcmp(h, "ZM", 2))) return false;
    at = xx_data_get_u32(h + 60, 4, 0, false);
    if (xx_data_get_u16(h + 24, 2, 0, false) >= 64 && at && pm_read(f, at, newh, 2) &&
        (!xx_rt_memcmp(newh, "PE", 2) || !xx_rt_memcmp(newh, "NE", 2) || !xx_rt_memcmp(newh, "LE", 2) || !xx_rt_memcmp(newh, "LX", 2)))
        return false;
    return executable_carrier_first(f, s, sig, 3, 0, false, false, executable_carrier_chz_at, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
#define SFX_MEMBER_TYPE xx_sfx_chz
static Abstractformat *member_child_create(xx_io_device *d, int64_t at)
{
    xx_chz *child = xx_chz_create(d, at);
    return child ? &child->format : NULL;
}
static void member_child_free(Abstractformat *f)
{
    xx_chz_free((xx_chz *)f);
}
static bool member_child_complete(Abstractformat *f)
{
    return ((xx_chz *)f)->is_complete;
}
#include "xx_sfx_member_delegate.h"

void xx_sfx_chz_init(xx_sfx_chz *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_CHZ, "exe");
        member_callbacks(&r->format);
        r->format.check_is_valid = xx_sfx_chz_check_is_valid;
        r->format.handle_base_info = xx_sfx_chz_handle_base_info;
    }
}
xx_sfx_chz *xx_sfx_chz_create(xx_io_device *d, int64_t b)
{
    xx_sfx_chz *r = (xx_sfx_chz *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_chz_init(r, d, b);
    return r;
}
void xx_sfx_chz_destroy(xx_sfx_chz *r)
{
    if (r) member_destroy(&r->format);
}
void xx_sfx_chz_free(xx_sfx_chz *r)
{
    if (r) {
        xx_sfx_chz_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_chz_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return member_open(f, pd);
}
bool xx_sfx_chz_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return member_open(f, pd);
}
