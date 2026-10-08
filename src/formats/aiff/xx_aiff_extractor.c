/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-1.3.pdf, https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/aiff.c
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/aiff/xx_aiff.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_AIFF };
static Abstractformat *open_reader(xx_io_device *d) { xx_aiff *r=xx_aiff_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_aiff_free((xx_aiff *)f); }
static const uint8_t bytes_0[] = {0x46,0x4F,0x52,0x4D};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),0} };
static const xx_format_search_desc desc = { types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false };
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_aiff_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(aiff, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
