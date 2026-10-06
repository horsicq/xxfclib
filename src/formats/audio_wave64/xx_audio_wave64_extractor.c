/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_wave64/xx_audio_wave64.h"
#ifndef AUDIO_WAVE64
#define XX_FILE_TYPE_AUDIO_WAVE64 ((xx_file_type_t)815)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_WAVE64};
static const uint8_t a0[]={0x72,0x69,0x66,0x66,0x2e,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0}};
static Abstractformat *open_reader(xx_io_device*d) {xx_audio_wave64*r=xx_audio_wave64_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat*f) {xx_audio_wave64_free((xx_audio_wave64*)f);}
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) {(void)self;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) {(void)self;return xx_format_search_current(s);}
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) {(void)self;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*self,xx_format_search_state*s) {(void)self;xx_format_search_free(s);}
xx_format_extractor xx_audio_wave64_extractor={create_search,current,next,free_search};
