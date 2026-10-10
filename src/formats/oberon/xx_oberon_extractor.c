/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Oberon .Z has no trustworthy short signature at byte zero. The shared
 * search engine probes offset zero only when anchors are NULL; the reader
 * validates every 136-byte record and the exact end of its SZDD chain. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/oberon/xx_oberon.h"

static const xx_file_type_t ob_types[] = {XX_FILE_TYPE_OBERON};
static Abstractformat *ob_open(xx_io_device *window)
{
    xx_oberon *reader = xx_oberon_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void ob_close(Abstractformat *format)
{
    xx_oberon_free((xx_oberon *)format);
}
static const xx_format_search_desc ob_search = {ob_types, sizeof(ob_types) / sizeof(ob_types[0]), NULL, 0U, ob_open, ob_close, false};
static xx_format_search_state *ob_create_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&ob_search, device, options, pd);
}
static const xx_format_search_info *ob_current(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}
static bool ob_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void ob_free_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_oberon_extractor = {ob_create_search, ob_current, ob_next, ob_free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(oberon, ob_search)
/* END GENERATED ABSTRACT EXTRACTOR */
