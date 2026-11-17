/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Compaq diagnostics MSGDATA message container: the format search of the compaq_msgdata reader (offset zero only; the reader
 * validates the whole container, so no search anchors are used).
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/archive/compaq_msgdata/xx_compaq_msgdata.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_COMPAQ_MSGDATA};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_compaq_msgdata *r = xx_compaq_msgdata_create(d, 0);
    Abstractformat *format = r ? (Abstractformat *)r : NULL;
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_COMPAQ_MSGDATA;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_compaq_msgdata_free((xx_compaq_msgdata *)f);
}
static const xx_format_search_desc desc = {types, 1U, NULL, 0U, open_reader, close_reader, true};
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
xx_format_extractor xx_compaq_msgdata_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(compaq_msgdata, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
