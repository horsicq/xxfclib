/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://floyd.lbl.gov/radiance/refer/filefmts.pdf, https://raw.githubusercontent.com/NREL/Radiance/master/src/common/color.c
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/radiance_hdr/xx_radiance_hdr.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_RADIANCE_HDR};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_radiance_hdr *r = xx_radiance_hdr_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_radiance_hdr_free((xx_radiance_hdr *)f);
}
static const uint8_t bytes_0[] = {0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45};
static const uint8_t bytes_1[] = {0x23, 0x3F, 0x52, 0x47, 0x42, 0x45};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 0}, {bytes_1, sizeof(bytes_1), 0}};
static const xx_format_search_desc desc = {types, 1U, anchors, sizeof(anchors) / sizeof(anchors[0]), open_reader, close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x, xx_io_device *d, const xx_list_s *o, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_create(&desc, d, o, pd);
}
static const xx_format_search_info *current_search(xx_format_extractor *x, xx_format_search_state *s)
{
    (void)x;
    return xx_format_search_current(s);
}
static bool next_search(xx_format_extractor *x, xx_format_search_state *s, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_find_next(s, pd);
}
static void free_search(xx_format_extractor *x, xx_format_search_state *s)
{
    (void)x;
    xx_format_search_free(s);
}
xx_format_extractor xx_radiance_hdr_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(radiance_hdr, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
