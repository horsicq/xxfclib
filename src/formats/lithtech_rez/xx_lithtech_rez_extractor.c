/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/bibendovsky/ltjs/blob/master/engine/libs/rezmgr/rezmgr.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/lithtech_rez/xx_lithtech_rez.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_LITHTECH_REZ};
static const uint8_t a0[]={0x0d,0x0a,0x52,0x65,0x7a,0x4d,0x67,0x72,0x20,0x56,0x65,0x72,0x73,0x69,0x6f,0x6e,0x20,0x31};
static const xx_format_search_anchor anchors[]={ {a0,sizeof(a0),0} };
static Abstractformat *open_reader(xx_io_device *d) { xx_lithtech_rez *r=xx_lithtech_rez_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_lithtech_rez_free((xx_lithtech_rez *)f); }
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *self,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current(xx_format_extractor *self,xx_format_search_state *s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor *self,xx_format_search_state *s,xx_pd_struct *pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *self,xx_format_search_state *s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_lithtech_rez_extractor={create_search,current,next,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(lithtech_rez, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
