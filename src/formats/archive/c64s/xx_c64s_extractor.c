/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * C64S emulator DOS self-extracting archive: the format search of the c64s reader (offset zero only; the reader
 * validates the whole container, so no search anchors are used).
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/archive/c64s/xx_c64s.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_C64S};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_c64s *r = xx_c64s_create(d, 0);
    Abstractformat *format = r ? (Abstractformat *)r : NULL;
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_C64S;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_c64s_free((xx_c64s *)f);
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
xx_format_extractor xx_c64s_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(c64s, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
