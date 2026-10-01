/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/imf_load.c
 * Imago Orpheus IMF1.0 with832-byte descriptor, up to256 patterns/instruments and32 channels; instrument II10/sample IS10 records, PCM8/16. Validates packed row events, multisample maps, bounded envelopes, sample loops/rates and complete sequential extents. Exports descriptor/patterns/instrument/sample headers and original PCM. Alternate IW10, compression and unknown extensions rejected; no playback.256MiB cap/4096 output components.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_imf/xx_tracker_imf.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_IMF};
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_imf *r=xx_tracker_imf_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_imf_free((xx_tracker_imf *)f); }
static const uint8_t bytes_0[] = {0x49,0x4d,0x31,0x30};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),60}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_imf_extractor = {create_search,current_search,next_search,free_search};
