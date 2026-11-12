/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Central Point CPZ archive: the format search of the cpz reader (offset zero only; the
 * reader verifies the checksums of the whole file, so a magic alone never
 * identifies).
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/archive/cpz/xx_cpz.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_CPZ};
static Abstractformat *open_reader(xx_io_device *d)
{
    return xx_cpz_create(d, 0);
}
static const xx_format_search_desc desc = {types, 1U, NULL, 0U, open_reader, xx_cpz_free, true};
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
xx_format_extractor xx_cpz_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(cpz, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
