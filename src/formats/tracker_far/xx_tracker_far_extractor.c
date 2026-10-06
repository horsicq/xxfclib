/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/far_load.c
 * Farandole Composer1.0 complete header/order tables, up to256 stored16-channel patterns and64 stored8/16-bit samples. Validates event references, row counts, loop extents and sample bitmap; exports descriptor/comment, patterns, instrument records and samples. Header extensions preserved; no playback.256MiB cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_far/xx_tracker_far.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_FAR};
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_far *r=xx_tracker_far_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_far_free((xx_tracker_far *)f); }
static const uint8_t bytes_0[] = {0x46,0x41,0x52,0xfe};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_far_extractor = {create_search,current_search,next_search,free_search};
