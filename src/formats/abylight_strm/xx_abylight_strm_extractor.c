/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/abylight_strm/xx_abylight_strm.h"
#ifndef ABYLIGHT_STRM
#define XX_FILE_TYPE_ABYLIGHT_STRM ((xx_file_type_t)1542)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_ABYLIGHT_STRM};
static const uint8_t a0[]={'S','T','R','M',0xe8,0x03,0x00,0x00};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0}};
static Abstractformat *open_reader(xx_io_device*d) {xx_abylight_strm*r=xx_abylight_strm_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat*f) {xx_abylight_strm_free((xx_abylight_strm*)f);}
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) {(void)self;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) {(void)self;return xx_format_search_current(s);}
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) {(void)self;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*self,xx_format_search_state*s) {(void)self;xx_format_search_free(s);}
xx_format_extractor xx_abylight_strm_extractor={create_search,current,next,free_search};
