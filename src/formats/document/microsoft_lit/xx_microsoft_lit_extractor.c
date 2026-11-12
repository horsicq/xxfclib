/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Microsoft Lit: the format search of the microsoft_lit reader, so that the format is a
 * family of its own (XFU_SUPPORT_FORMAT_MICROSOFT_LIT) and not only a row of the
 * complete library.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/document/microsoft_lit/xx_microsoft_lit.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_MICROSOFT_LIT};
static Abstractformat *open_reader(xx_io_device *d)
{
    Abstractformat *format = (Abstractformat *)xx_microsoft_lit_create(d, 0);
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_MICROSOFT_LIT;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_microsoft_lit_free((xx_microsoft_lit *)f);
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
xx_format_extractor xx_microsoft_lit_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(microsoft_lit, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
