/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://1fish2.github.io/IFF/IFF%20docs%20with%20Commodore%20revisions/ILBM.pdf, https://1fish2.github.io/IFF/
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/iff_ilbm/xx_iff_ilbm.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_IFF_ILBM};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_iff_ilbm *r = xx_iff_ilbm_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_iff_ilbm_free((xx_iff_ilbm *)f);
}
static const uint8_t bytes_0[] = {0x49, 0x4C, 0x42, 0x4D};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 8}};
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
xx_format_extractor xx_iff_ilbm_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(iff_ilbm, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
