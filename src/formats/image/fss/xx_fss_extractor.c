/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * FSS image container: the format search of the fss reader, so that the format is a
 * family of its own (XFU_SUPPORT_FORMAT_FSS) and not only a row of the
 * complete library.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/image/fss/xx_fss.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_FSS};
static const uint8_t a0[] = {'S', 'S', 'B', 'O', 'B', 1};
static const xx_format_search_anchor anchors[] = {{a0, sizeof(a0), 0U}};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_fss *r = xx_fss_create(d, 0);
    Abstractformat *format = r ? (Abstractformat *)r : NULL;
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_FSS;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_fss_free((xx_fss *)f);
}
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
xx_format_extractor xx_fss_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(fss, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
