/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/archive/gemdos_lha/xx_gemdos_lha.h"
#include "../../common/xx_sfx_carrier.h"

#include "xxfclib/formats/archive/lha/xx_lha.h"
#include "../lha/xx_lha_atari_inline_internal.h"
#include "xxfclib/formats/archive/sfx_zipcentral/xx_sfx_zipcentral.h"
static Abstractformat *nested_open(xx_io_device *d, int64_t at)
{
    static const uint8_t code[18] = {0x41, 0xfa, 0x00, 0x10, 0x20, 0x3a, 0xff, 0xee, 0x72, 0x01, 0xc2, 0x40, 0xd0, 0x81, 0xd1, 0xc0, 0x4e, 0xd0};
    uint8_t head[60];
    xx_lha *r = xx_lha_create(d, at);
    /* Level-2 headers without the common CRC are taken only behind the exact
     * inline stub the LHA self-extractor reader accepts: the GEMDOS header,
     * the branch over a 32-byte descriptor, "SFX", its dispatcher, and the
     * archive right after it at offset 60. Any other program keeps the CRC
     * requirement. */
    if (r && at >= 60 && xx_io_seek64(d, at - 60, SEEK_SET) == 0 && xx_io_read(d, head, sizeof(head)) == (ssize_t)sizeof(head) && head[0] == 0x60 && head[1] == 0x1a &&
        head[28] == 0x60 && head[29] == 0x0c && xx_data_get_u32(head + 34, 4, 0, true) == 32U && !xx_rt_memcmp(head + 38, "SFX\0", 4) &&
        !xx_rt_memcmp(head + 42, code, sizeof(code)))
        xx_lha_set_atari_inline_context(r);
    return r ? &r->format : NULL;
}
static void nested_close(Abstractformat *f)
{
    xx_lha_free((xx_lha *)f);
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t lh_sig[] = {0x2d, 0x6c, 0x68};
    static const uint8_t lz5_sig[] = {0x2d, 0x6c, 0x7a, 0x35, 0x2d};
    xx_sfx_zipcentral zip;
    bool zip_valid;
    int64_t low;
    if (!sfx_carrier_carrier(f, true, &low, pd)) return false;
    /* Preserve the established LH candidate selection even when a packed
     * executable contains an earlier LArc archive. LZ5 is a fallback only. */
    if (sfx_carrier_embedded(f, s, low, lh_sig, sizeof(lh_sig), -2, nested_open, nested_close, "payload.lzh", pd)) return true;
    if (carrier_stop(pd)) return false;
    /* An established ZIP central-directory carrier must retain its reader
     * and payload extent. Reuse its complete framing check, not PK magic. */
    xx_sfx_zipcentral_init(&zip, f->device, f->base_address);
    zip_valid = xx_sfx_zipcentral_check_is_valid(&zip.format, pd);
    xx_sfx_zipcentral_destroy(&zip);
    if (zip_valid || carrier_stop(pd)) return false;
    return sfx_carrier_embedded(f, s, low, lz5_sig, sizeof(lz5_sig), -2, nested_open, nested_close, "payload.lzh", pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_gemdos_lha_init(xx_gemdos_lha *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GEMDOS_LHA, "exe");
    }
}
xx_gemdos_lha *xx_gemdos_lha_create(xx_io_device *d, int64_t b)
{
    xx_gemdos_lha *r = (xx_gemdos_lha *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gemdos_lha_init(r, d, b);
    return r;
}
void xx_gemdos_lha_destroy(xx_gemdos_lha *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gemdos_lha_free(xx_gemdos_lha *r)
{
    if (r) {
        xx_gemdos_lha_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gemdos_lha_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gemdos_lha_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
