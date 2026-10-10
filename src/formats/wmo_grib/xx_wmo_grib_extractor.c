/* SPDX-License-Identifier: MIT
 * Wire specification: https://codes.ecmwf.int/grib/format/grib2/regulations/ */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/wmo_grib/xx_wmo_grib.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_WMO_GRIB};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_wmo_grib *r = xx_wmo_grib_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_wmo_grib_free((xx_wmo_grib *)f);
}
static const uint8_t bytes_0[] = {71, 82, 73, 66};
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
xx_format_extractor xx_wmo_grib_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(wmo_grib, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
