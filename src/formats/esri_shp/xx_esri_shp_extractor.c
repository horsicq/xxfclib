/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.esri.com/library/whitepapers/pdfs/shapefile.pdf */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/esri_shp/xx_esri_shp.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_ESRI_SHP};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_esri_shp *r = xx_esri_shp_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_esri_shp_free((xx_esri_shp *)f);
}
static const uint8_t bytes_0[] = {0, 0, 39, 10};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 0}};
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
xx_format_extractor xx_esri_shp_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(esri_shp, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
