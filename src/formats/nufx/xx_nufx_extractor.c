/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://nulib.com/library/FTN.e08002.htm
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/nufx/xx_nufx.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_NUFX};
static const uint8_t a0[] = {0x4e, 0xf5, 0x46, 0xe9, 0x6c, 0xe5};
static const xx_format_search_anchor anchors[] = {{a0, sizeof(a0), 0}};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_nufx *r = xx_nufx_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_nufx_free((xx_nufx *)f);
}
static const xx_format_search_desc desc = {types, 1U, anchors, sizeof(anchors) / sizeof(anchors[0]), open_reader, close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *self, xx_io_device *d, const xx_list_s *o, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&desc, d, o, pd);
}
static const xx_format_search_info *current(xx_format_extractor *self, xx_format_search_state *s)
{
    (void)self;
    return xx_format_search_current(s);
}
static bool next(xx_format_extractor *self, xx_format_search_state *s, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(s, pd);
}
static void free_search(xx_format_extractor *self, xx_format_search_state *s)
{
    (void)self;
    xx_format_search_free(s);
}
xx_format_extractor xx_nufx_extractor = {create_search, current, next, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(nufx, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
