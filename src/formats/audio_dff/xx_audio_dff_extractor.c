/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_dff/xx_audio_dff.h"
#ifndef AUDIO_DFF
#define XX_FILE_TYPE_AUDIO_DFF ((xx_file_type_t)814)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_DFF};
static const uint8_t a0[]={0x46,0x52,0x4d,0x38};
static const uint8_t a1[]={0x44,0x53,0x44,0x20};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0},{a1,sizeof(a1),12}};
static Abstractformat *open_reader(xx_io_device*d) {xx_audio_dff*r=xx_audio_dff_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat*f) {xx_audio_dff_free((xx_audio_dff*)f);}
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) {(void)self;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) {(void)self;return xx_format_search_current(s);}
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) {(void)self;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*self,xx_format_search_state*s) {(void)self;xx_format_search_free(s);}
xx_format_extractor xx_audio_dff_extractor={create_search,current,next,free_search};
