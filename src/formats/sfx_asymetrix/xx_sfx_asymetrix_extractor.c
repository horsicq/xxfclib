/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sfx_asymetrix/xx_sfx_asymetrix.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_SFX_ASYMETRIX};
static const uint8_t a0[]={0x4d,0x5a};
static const uint8_t a1[]={0x60,0x1a};
static const xx_format_search_anchor anchors[]={ {a0,sizeof(a0),0},{a1,sizeof(a1),0} };
static Abstractformat *open_reader(xx_io_device *d) { xx_sfx_asymetrix *r=xx_sfx_asymetrix_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_sfx_asymetrix_free((xx_sfx_asymetrix *)f); }
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *self,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current(xx_format_extractor *self,xx_format_search_state *s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor *self,xx_format_search_state *s,xx_pd_struct *pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *self,xx_format_search_state *s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_sfx_asymetrix_extractor={create_search,current,next,free_search};
