/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * GTA IMG v2 archive.
 * https://github.com/connorhaigh/gta-img/blob/master/src/read.rs
 */
#include "xxfclib/formats/gta_img/xx_gta_img.h"
#include "../xx_game_resource_helpers.h"
#include "../xx_format_abstract_extractor_adapter.h"

static bool gta_img_gta(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32];
    uint64_t n = (uint64_t)pm_available(f), table;
    uint32_t i, count;
    if (n < 8 || !pm_read(f, 0, h, 8) || memcmp(h, "VER2", 4)) return false;
    count = xgr_le32(h + 4);
    table = 8ULL + 32ULL * count;
    if (count > XGR_MAX_RECORDS || table > n) return false;
    for (i = 0; i < count; ++i) {
        char name[25];
        size_t k;
        uint64_t at, size;
        if (xgr_stop(pd) || !pm_read(f, 8 + (int64_t)i * 32, h, 32)) return false;
        at = (uint64_t)xgr_le32(h) * 2048U;
        /* VER2 uses a 16-bit sector count plus a reserved 16-bit field;
         * accepting a 32-bit length incorrectly turns flags into gigabytes. */
        size = (uint64_t)xgr_le16(h + 4) * 2048U;
        if (xgr_le16(h + 6) != 0 || !xgr_range(at, size, n) || (size && at < table)) return false;
        for (k = 0; k < 24 && h[8 + k]; ++k) name[k] = (char)h[8 + k];
        name[k] = 0;
        if (!xgr_add(f, s, name, at, size)) return false;
    }
    s->size = (int64_t)n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return pm_available(f) >= 0 && !xgr_stop(pd) && gta_img_gta(f, s, pd);
}
Abstractformat *xx_gta_img_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (!f) return NULL;
    xx_mem_zero(f, sizeof(*f));
    pm_init(f, d, base, XX_FILE_TYPE_GTA_IMG, "img");
    return f;
}
void xx_gta_img_free(Abstractformat *f)
{
    if (f) {
        xx_format_cleanup_extra_parameters(f);
        xx_mem_free(f);
    }
}
xx_file_type_t xx_gta_img_detect(xx_io_device *d, int64_t base)
{
    Abstractformat f;
    uint8_t h[64];
    int64_t size, old = xx_io_tell(d);
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    xx_mem_zero(&f, sizeof(f));
    f.device = d;
    f.base_address = base;
    size = pm_available(&f);
    if (size < 8 || !pm_read(&f, 0, h, (size_t)(size < 64 ? size : 64))) goto done;
    {
        uint32_t magic = xgr_le32(h);
        if (magic == 0x32524556U) type = XX_FILE_TYPE_GTA_IMG;
    }
done:
    if (old >= 0) xx_io_seek64(d, old, SEEK_SET);
    return type;
}
static Abstractformat *gta_img_open(xx_io_device *d)
{
    return xx_gta_img_create(d, 0);
}
static const xx_file_type_t gta_img_types[] = {XX_FILE_TYPE_GTA_IMG};
static const xx_format_search_desc gta_img_desc = {gta_img_types, 1, NULL, 0, gta_img_open, xx_gta_img_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(gta_img, gta_img_desc)
