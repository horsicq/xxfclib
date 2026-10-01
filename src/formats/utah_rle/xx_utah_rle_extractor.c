/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/sarnold/urt/master/lib/rle_getrow.c, https://brlcad.org/OLD/doxygen/d6/d94/rle__code_8h-source.html
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/utah_rle/xx_utah_rle.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_UTAH_RLE };
static Abstractformat *open_reader(xx_io_device *d) { xx_utah_rle *r=xx_utah_rle_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_utah_rle_free((xx_utah_rle *)f); }
static const uint8_t bytes_0[] = {0x52,0xCC};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),0} };
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_utah_rle_extractor = {create_search,current_search,next_search,free_search};
