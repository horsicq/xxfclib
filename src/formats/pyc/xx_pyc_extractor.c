/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pyc/xx_pyc.h"
static const uint8_t pyc_marker[]={13,10};
static const xx_format_search_anchor anchors[]={{pyc_marker,sizeof(pyc_marker),2U}};
static const xx_file_type_t types[]={XX_FILE_TYPE_PYC};
static Abstractformat *open_reader(xx_io_device *d) { xx_pyc *r=xx_pyc_create(d,0);return r?&r->format:NULL; }
static void close_reader(Abstractformat *f) { xx_pyc_free((xx_pyc *)f); }
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader,true};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x;return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x;return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x;return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x;xx_format_search_free(s); }
xx_format_extractor xx_pyc_extractor={create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pyc, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
