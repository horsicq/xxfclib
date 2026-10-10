/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/iff_8svx/xx_iff_8svx.h"
#ifndef IFF_8SVX
#define XX_FILE_TYPE_IFF_8SVX ((xx_file_type_t)819)
#endif
static const xx_file_type_t types[] = {XX_FILE_TYPE_IFF_8SVX};
static const uint8_t a0[] = {0x46, 0x4f, 0x52, 0x4d};
static const uint8_t a1[] = {0x38, 0x53, 0x56, 0x58};
static const xx_format_search_anchor anchors[] = {{a0, sizeof(a0), 0}, {a1, sizeof(a1), 8}};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_iff_8svx *r = xx_iff_8svx_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_iff_8svx_free((xx_iff_8svx *)f);
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
xx_format_extractor xx_iff_8svx_extractor = {create_search, current, next, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(iff_8svx, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
