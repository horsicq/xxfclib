/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_emod/xx_tracker_emod.h"
#ifndef TRACKER_EMOD
#define XX_FILE_TYPE_TRACKER_EMOD ((xx_file_type_t)811)
#endif
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_EMOD};
static const uint8_t a0[] = {0x46, 0x4f, 0x52, 0x4d};
static const uint8_t a1[] = {0x45, 0x4d, 0x4f, 0x44};
static const xx_format_search_anchor anchors[] = {{a0, sizeof(a0), 0}, {a1, sizeof(a1), 8}};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_tracker_emod *r = xx_tracker_emod_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_tracker_emod_free((xx_tracker_emod *)f);
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
xx_format_extractor xx_tracker_emod_extractor = {create_search, current, next, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(tracker_emod, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
