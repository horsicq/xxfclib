/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_AMSTRAD_CPC_DSK};
static Abstractformat *open_reader(xx_io_device *d) { xx_amstrad_cpc_dsk *r=xx_amstrad_cpc_dsk_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_amstrad_cpc_dsk_free((xx_amstrad_cpc_dsk *)f); }
static const uint8_t bytes_0[] = {0x4d,0x56,0x20,0x2d,0x20,0x43,0x50,0x43,0x45,0x4d,0x55,0x20,0x44,0x69,0x73,0x6b,0x2d,0x46,0x69,0x6c,0x65,0x0d,0x0a,0x44,0x69,0x73,0x6b,0x2d,0x49,0x6e,0x66,0x6f,0x0d,0x0a};
static const uint8_t bytes_1[] = {0x45,0x58,0x54,0x45,0x4e,0x44,0x45,0x44,0x20,0x43,0x50,0x43,0x20,0x44,0x53,0x4b,0x20,0x46,0x69,0x6c,0x65,0x0d,0x0a,0x44,0x69,0x73,0x6b,0x2d,0x49,0x6e,0x66,0x6f,0x0d,0x0a};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0},{bytes_1,sizeof(bytes_1),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_amstrad_cpc_dsk_extractor = {create_search,current_search,next_search,free_search};
