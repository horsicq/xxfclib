/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://developer.gimp.org/core/standards/pat/
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gimp_pat/xx_gimp_pat.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_GIMP_PAT };
static Abstractformat *open_reader(xx_io_device *d) { xx_gimp_pat *r=xx_gimp_pat_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_gimp_pat_free((xx_gimp_pat *)f); }
static const uint8_t bytes_0[] = {0x47,0x50,0x41,0x54};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),20} };
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_gimp_pat_extractor = {create_search,current_search,next_search,free_search};
