/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DFSEE_IMZ: the format search of the dfsee_imz reader, so that the format is a
 * family of its own (XFU_SUPPORT_FORMAT_DFSEE_IMZ) and not only a reader-only
 * row of the complete library.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/filesystem/dfsee_imz/xx_dfsee_imz.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_DFSEE_IMZ};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_dfsee_imz *r = xx_dfsee_imz_create(d, 0);
    Abstractformat *format = r ? (Abstractformat *)r : NULL;
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_DFSEE_IMZ;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_dfsee_imz_free((xx_dfsee_imz *)f);
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
xx_format_extractor xx_dfsee_imz_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(dfsee_imz, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
