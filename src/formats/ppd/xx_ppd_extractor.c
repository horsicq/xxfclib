/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PPD has no short signature. Probe offset zero only, then let the reader
 * validate the table, every packed span, and each decoded size.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ppd/xx_ppd.h"

static const xx_file_type_t ppd_types[] = {XX_FILE_TYPE_PPD};
static Abstractformat *ppd_open(xx_io_device *device)
{
    xx_ppd *reader = xx_ppd_create(device, 0);
    return reader ? &reader->format : NULL;
}
static void ppd_close(Abstractformat *format)
{
    xx_ppd_free((xx_ppd *)format);
}
static const xx_format_search_desc ppd_search = {ppd_types, 1U, NULL, 0U, ppd_open, ppd_close, false};
static xx_format_search_state *ppd_create_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&ppd_search, device, options, pd);
}
static const xx_format_search_info *ppd_current(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}
static bool ppd_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void ppd_free_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_ppd_extractor = {ppd_create_search, ppd_current, ppd_next, ppd_free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(ppd, ppd_search)
/* END GENERATED ABSTRACT EXTRACTOR */
