/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * MS-MPI setup bootstrapper payloads: the format search of
 * the msmpi_bootstrapper reader. Bounded independent carrier parser.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/installer/msmpi_bootstrapper/xx_msmpi_bootstrapper.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_MSMPI_BOOTSTRAPPER};
static const uint8_t a0[] = {0x4d, 0x5a};
static const uint8_t a1[] = {0x5a, 0x4d};
static const xx_format_search_anchor anchors[] = {{a0, sizeof(a0), 0}, {a1, sizeof(a1), 0}};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_msmpi_bootstrapper *r = xx_msmpi_bootstrapper_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_msmpi_bootstrapper_free((xx_msmpi_bootstrapper *)f);
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
xx_format_extractor xx_msmpi_bootstrapper_extractor = {create_search, current, next, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(msmpi_bootstrapper, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
