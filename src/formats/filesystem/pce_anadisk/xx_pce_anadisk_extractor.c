/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * AnaDisk sector disk image: the format search of the pce_anadisk reader, so that the format is a
 * family of its own (XFU_SUPPORT_FORMAT_PCE_ANADISK) and not only a reader-only
 * row of the complete library.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/filesystem/pce_anadisk/xx_pce_anadisk.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_PCE_ANADISK};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_pce_anadisk *r = xx_pce_anadisk_create(d, 0);
    Abstractformat *format = r ? (Abstractformat *)r : NULL;
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_PCE_ANADISK;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_pce_anadisk_free((xx_pce_anadisk *)f);
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
xx_format_extractor xx_pce_anadisk_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pce_anadisk, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
