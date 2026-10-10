/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/adobe_aco/xx_adobe_aco.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_ADOBE_ACO};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_adobe_aco *r = xx_adobe_aco_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_adobe_aco_free((xx_adobe_aco *)f);
}
static const xx_format_search_desc desc = {types, 1U, NULL, 0, open_reader, close_reader, false};
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
xx_format_extractor xx_adobe_aco_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(adobe_aco, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
