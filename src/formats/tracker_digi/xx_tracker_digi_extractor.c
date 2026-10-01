/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_digi/xx_tracker_digi.h"
#ifndef TRACKER_DIGI
#define XX_FILE_TYPE_TRACKER_DIGI ((xx_file_type_t)810)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_TRACKER_DIGI};
static const uint8_t a0[]={0x44,0x49,0x47,0x49,0x20,0x42,0x6f,0x6f,0x73,0x74,0x65,0x72,0x20,0x6d,0x6f,0x64,0x75,0x6c,0x65,0x00};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0}};
static Abstractformat *open_reader(xx_io_device*d) {xx_tracker_digi*r=xx_tracker_digi_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat*f) {xx_tracker_digi_free((xx_tracker_digi*)f);}
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) {(void)self;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) {(void)self;return xx_format_search_current(s);}
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) {(void)self;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*self,xx_format_search_state*s) {(void)self;xx_format_search_free(s);}
xx_format_extractor xx_tracker_digi_extractor={create_search,current,next,free_search};
