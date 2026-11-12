/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CORK compressed installer member: the format search of the cork reader (offset zero only; the
 * reader runs the whole stream through its trial decode, so the magic alone
 * never identifies).
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/archive/cork/xx_cork.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_CORK};
static Abstractformat *open_reader(xx_io_device *d)
{
    return xx_cork_create(d, 0);
}
static const xx_format_search_desc desc = {types, 1U, NULL, 0U, open_reader, xx_cork_free, true};
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
xx_format_extractor xx_cork_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(cork, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
