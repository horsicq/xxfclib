/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mod_load.c
 * 31-sample four-channel ProTracker-compatible MOD with M.K./M!K!/4CHN/FLT4 signature,1-128 orders/patterns and original signed8-bit samples. Checks order/sample/note
 * references, loops and all physical extents; exports descriptor, each1024-byte pattern and sample.15-sample/signatureless modules, alternate channels and
 * packed/external samples rejected; no playback.256MiB physical cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_mod/xx_tracker_mod.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_MOD};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_tracker_mod *r = xx_tracker_mod_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_tracker_mod_free((xx_tracker_mod *)f);
}
static const uint8_t bytes_0[] = {0x4d, 0x2e, 0x4b, 0x2e};
static const uint8_t bytes_1[] = {0x4d, 0x21, 0x4b, 0x21};
static const uint8_t bytes_2[] = {0x34, 0x43, 0x48, 0x4e};
static const uint8_t bytes_3[] = {0x46, 0x4c, 0x54, 0x34};
static const xx_format_search_anchor anchors[] = {
    {bytes_0, sizeof(bytes_0), 1080}, {bytes_1, sizeof(bytes_1), 1080}, {bytes_2, sizeof(bytes_2), 1080}, {bytes_3, sizeof(bytes_3), 1080}};
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
xx_format_extractor xx_tracker_mod_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(tracker_mod, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
