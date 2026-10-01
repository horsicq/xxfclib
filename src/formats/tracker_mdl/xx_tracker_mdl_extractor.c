/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mdl_load.c
 * DigiTrakker MDL1.0/1.1 typed chunks with IN/PA/TR/II/IS/SA and optional VE/PE/FE/ME, up to256 patterns/instruments/samples and4096 tracks. Checks packed track commands, instrument/sample references, envelope records, exact stored or length-framed packed sample extents. Exports complete original chunks; compressed sample bytes and effects remain encoded, no decoding/playback.256MiB cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_mdl/xx_tracker_mdl.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_MDL};
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_mdl *r=xx_tracker_mdl_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_mdl_free((xx_tracker_mdl *)f); }
static const uint8_t bytes_0[] = {0x44,0x4d,0x44,0x4c};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_mdl_extractor = {create_search,current_search,next_search,free_search};
