/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/libsdl-org/libtiff/master/libtiff/tif_dirread.c, https://libtiff.gitlab.io/libtiff/specification/bigtiff.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tiff/xx_tiff.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_TIFF };
static Abstractformat *open_reader(xx_io_device *d) { xx_tiff *r=xx_tiff_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tiff_free((xx_tiff *)f); }
static const uint8_t bytes_0[] = {0x49,0x49,0x2A,0x00};
static const uint8_t bytes_1[] = {0x4D,0x4D,0x00,0x2A};
static const uint8_t bytes_2[] = {0x49,0x49,0x2B,0x00};
static const uint8_t bytes_3[] = {0x4D,0x4D,0x00,0x2B};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),0}, {bytes_1,sizeof(bytes_1),0}, {bytes_2,sizeof(bytes_2),0}, {bytes_3,sizeof(bytes_3),0} };
static const xx_format_search_desc desc = { types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader };
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tiff_extractor = {create_search,current_search,next_search,free_search};
