/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/gdm_load.c
 * General Digital Music1.0 with157-byte header, disjoint offset-based orders/instruments/patterns/samples and no message/scroller blocks. Up to256 patterns/samples and32 channels. Checks full packed event fields,1-64 row terminators, sample flags/loops (GDM loopEnd minus one, in frames) and physical overlap. Exports descriptor/orders/instruments/patterns and original8/16-bit sample bytes; no playback.256MiB cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_gdm/xx_tracker_gdm.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_GDM};
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_gdm *r=xx_tracker_gdm_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_gdm_free((xx_tracker_gdm *)f); }
static const uint8_t bytes_0[] = {0x47,0x44,0x4d,0xfe};
static const uint8_t bytes_1[] = {0x47,0x4d,0x46,0x53};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0},{bytes_1,sizeof(bytes_1),71}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_gdm_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(tracker_gdm, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
