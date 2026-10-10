/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.chg.ox.ac.uk/~gav/bgen_format/spec/v1.2.html */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/genomics_bgen/xx_genomics_bgen.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_GENOMICS_BGEN};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_genomics_bgen *r = xx_genomics_bgen_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_genomics_bgen_free((xx_genomics_bgen *)f);
}
static const uint8_t bytes_0[] = {98, 103, 101, 110};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 16}};
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
xx_format_extractor xx_genomics_bgen_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(genomics_bgen, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
