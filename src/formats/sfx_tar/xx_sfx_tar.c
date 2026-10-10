/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_tar/xx_sfx_tar.h"
#include "../common/xx_executable_carrier.h"

static bool executable_carrier_tar_at(Abstractformat *f, pm_stream *s, int64_t at, xx_pd_struct *pd)
{
    int64_t end = pm_available(f);
    return carrier_tar(f, at, end, pd) && executable_carrier_component(f, s, at, end - at, "payload.tar");
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {'u', 's', 't', 'a', 'r'};
    return executable_carrier_scan(f, s, sig, 5, -257, false, false, executable_carrier_tar_at, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_sfx_tar_init(xx_sfx_tar *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_TAR, "exe");
    }
}
xx_sfx_tar *xx_sfx_tar_create(xx_io_device *d, int64_t b)
{
    xx_sfx_tar *r = (xx_sfx_tar *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_tar_init(r, d, b);
    return r;
}
void xx_sfx_tar_destroy(xx_sfx_tar *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_tar_free(xx_sfx_tar *r)
{
    if (r) {
        xx_sfx_tar_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_tar_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_tar_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
