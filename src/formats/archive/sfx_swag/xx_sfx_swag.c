/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: src/formats/archive/swag/xx_swag.c
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/archive/sfx_swag/xx_sfx_swag.h"
#include "../../common/xx_executable_carrier.h"
#include "xxfclib/formats/archive/swag/xx_swag.h"

static bool executable_carrier_swag_at(Abstractformat *f, pm_stream *s, int64_t base, xx_pd_struct *pd)
{
    uint8_t h[257], footer[129];
    int64_t at = base, end = pm_available(f) - 129;
    unsigned count, i;
    if (end <= base || !pm_read(f, end, footer, 129) || footer[0] > 60 || footer[61] > 65 || !(count = xx_data_get_u16(footer + 127, 2, 0, false)) || count > 4096)
        return false;
    for (i = 0; i < count; ++i) {
        unsigned size, name, j, sum = 0;
        uint32_t packed;
        if (carrier_stop(pd) || end - at < 187 || !pm_read(f, at, h, 187) || xx_rt_memcmp(h + 2, "-sw1-", 5) || !(name = h[186]) || name > 12 ||
            (size = h[0]) != 187 + name || end - at < (int64_t)size + 2 || !pm_read(f, at, h, size + 2))
            return false;
        for (j = 2; j < size + 2; ++j) {
            sum += h[j];
        }
        if ((sum & 255) != h[1]) return false;
        for (j = 0; j < name; ++j)
            if (h[187 + j] < 32 || h[187 + j] > 126) return false;
        packed = xx_data_get_u32(h + 7, 4, 0, false);
        if (packed > INT32_MAX || xx_data_get_u32(h + 11, 4, 0, false) > 67108864 || !carrier_range(end, at + size + 2, packed)) return false;
        at += size + 2 + packed;
    }
    /* Some historical self-extractors append the exact same footer twice.
     * Authenticate both copies; arbitrary trailing bytes are not accepted. */
    if (at == end - 129 || at == end - 130) {
        uint8_t duplicate[129];
        if (!pm_read(f, end - 129, duplicate, 129) || xx_rt_memcmp(duplicate, footer, 129)) return false;
        end -= 129;
    }
    if (at == end - 1) {
        uint8_t marker;
        if (!pm_read(f, at, &marker, 1) || marker) return false;
        ++at;
    }
    return at == end && executable_carrier_component(f, s, base, end + 129 - base, "payload.swg");
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {'-', 's', 'w', '1', '-'};
    return executable_carrier_scan(f, s, sig, 5, -2, false, false, executable_carrier_swag_at, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
#define SFX_MEMBER_TYPE xx_sfx_swag
static Abstractformat *member_child_create(xx_io_device *d, int64_t at)
{
    xx_swag *child = xx_swag_create(d, at);
    return child ? &child->format : NULL;
}
static void member_child_free(Abstractformat *f)
{
    xx_swag_free((xx_swag *)f);
}
static bool member_child_complete(Abstractformat *f)
{
    return ((xx_swag *)f)->is_complete;
}
#include "../sfx_chz/xx_sfx_member_delegate.h"

void xx_sfx_swag_init(xx_sfx_swag *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_SWAG, "exe");
        member_callbacks(&r->format);
        r->format.check_is_valid = xx_sfx_swag_check_is_valid;
        r->format.handle_base_info = xx_sfx_swag_handle_base_info;
    }
}
xx_sfx_swag *xx_sfx_swag_create(xx_io_device *d, int64_t b)
{
    xx_sfx_swag *r = (xx_sfx_swag *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_swag_init(r, d, b);
    return r;
}
void xx_sfx_swag_destroy(xx_sfx_swag *r)
{
    if (r) member_destroy(&r->format);
}
void xx_sfx_swag_free(xx_sfx_swag *r)
{
    if (r) {
        xx_sfx_swag_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_swag_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return member_open(f, pd);
}
bool xx_sfx_swag_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return member_open(f, pd);
}
