/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://netpbm.sourceforge.net/doc/pbm.html, https://netpbm.sourceforge.net/doc/pgm.html, https://netpbm.sourceforge.net/doc/ppm.html,
 * https://netpbm.sourceforge.net/doc/pam.html Stored encoded component extraction; no media decoding claims.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pnm/xx_pnm.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_PNM};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_pnm *r = xx_pnm_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_pnm_free((xx_pnm *)f);
}
static const uint8_t bytes_0[] = {0x50, 0x31};
static const uint8_t bytes_1[] = {0x50, 0x32};
static const uint8_t bytes_2[] = {0x50, 0x33};
static const uint8_t bytes_3[] = {0x50, 0x34};
static const uint8_t bytes_4[] = {0x50, 0x35};
static const uint8_t bytes_5[] = {0x50, 0x36};
static const uint8_t bytes_6[] = {0x50, 0x37};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 0}, {bytes_1, sizeof(bytes_1), 0}, {bytes_2, sizeof(bytes_2), 0},
                                                  {bytes_3, sizeof(bytes_3), 0}, {bytes_4, sizeof(bytes_4), 0}, {bytes_5, sizeof(bytes_5), 0},
                                                  {bytes_6, sizeof(bytes_6), 0}};
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
xx_format_extractor xx_pnm_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pnm, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
