/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/xm_load.c, https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/xm.h
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_xm/xx_tracker_xm.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_TRACKER_XM };
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_xm *r=xx_tracker_xm_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_xm_free((xx_tracker_xm *)f); }
static const uint8_t bytes_0[] = {0x45,0x78,0x74,0x65,0x6E,0x64,0x65,0x64,0x20,0x4D,0x6F,0x64,0x75,0x6C,0x65,0x3A,0x20};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),0} };
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_xm_extractor = {create_search,current_search,next_search,free_search};
