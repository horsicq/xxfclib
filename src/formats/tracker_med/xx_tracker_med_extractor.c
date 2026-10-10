/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mmd1_load.c
 * OctaMED MMD0/MMD1 one-song containers, up to255 patterns/63 stored mono8-bit samples/32 channels/3200 rows. Checks pointer tables, disjoint metadata/pattern/sample
 * extents, note/sample references, order list and repeat ranges. Optional80-byte expansion supports bounded instrument extension/name records, annotation and song name
 * only. Exports original song/tables/patterns/sample/metadata records. MMD2/MMD3, synthetic/multioctave/packed/stereo/external samples, linked songs and other expansion
 * pointers rejected; no playback.256MiB cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_med/xx_tracker_med.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_MED};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_tracker_med *r = xx_tracker_med_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_tracker_med_free((xx_tracker_med *)f);
}
static const uint8_t bytes_0[] = {0x4d, 0x4d, 0x44, 0x30};
static const uint8_t bytes_1[] = {0x4d, 0x4d, 0x44, 0x31};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 0}, {bytes_1, sizeof(bytes_1), 0}};
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
xx_format_extractor xx_tracker_med_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(tracker_med, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
