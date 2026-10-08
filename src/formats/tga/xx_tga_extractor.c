/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.ludorg.net/amnesia/TGA_File_Format_Spec.html
 * Stored encoded component extraction; no media decoding claims.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tga/xx_tga.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_TGA };
static Abstractformat *open_reader(xx_io_device *d) { xx_tga *r=xx_tga_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tga_free((xx_tga *)f); }
static const uint8_t bytes_0[] = {0x01};
static const uint8_t bytes_1[] = {0x02};
static const uint8_t bytes_2[] = {0x03};
static const uint8_t bytes_3[] = {0x09};
static const uint8_t bytes_4[] = {0x0A};
static const uint8_t bytes_5[] = {0x0B};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),2}, {bytes_1,sizeof(bytes_1),2}, {bytes_2,sizeof(bytes_2),2}, {bytes_3,sizeof(bytes_3),2}, {bytes_4,sizeof(bytes_4),2}, {bytes_5,sizeof(bytes_5),2} };
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tga_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(tga, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
