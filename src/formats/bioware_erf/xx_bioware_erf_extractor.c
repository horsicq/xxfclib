/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xoreos/xoreos/master/src/aurora/erffile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/bioware_erf/xx_bioware_erf.h"
static const xx_file_type_t types[]={ XX_FILE_TYPE_BIOWARE_ERF };
static const uint8_t a0[]={0x45,0x52,0x46,0x20};
static const uint8_t a1[]={0x4d,0x4f,0x44,0x20};
static const uint8_t a2[]={0x48,0x41,0x4b,0x20};
static const uint8_t a3[]={0x53,0x41,0x56,0x20};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0},{a1,sizeof(a1),0},{a2,sizeof(a2),0},{a3,sizeof(a3),0}};
static Abstractformat *open_reader(xx_io_device *d) { xx_bioware_erf *r=xx_bioware_erf_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_bioware_erf_free((xx_bioware_erf *)f); }
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *self,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current(xx_format_extractor *self,xx_format_search_state *s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor *self,xx_format_search_state *s,xx_pd_struct *pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *self,xx_format_search_state *s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_bioware_erf_extractor={create_search,current,next,free_search};
