/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_riff_ima/xx_audio_riff_ima.h"
#ifndef XX_FILE_TYPE_AUDIO_RIFF_IMA
#define XX_FILE_TYPE_AUDIO_RIFF_IMA ((xx_file_type_t)1523)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_RIFF_IMA};
static const uint8_t magic[]={'R','I','F','F'};
static const xx_format_search_anchor anchors[]={{magic,sizeof(magic),0}};
static Abstractformat *open_reader(xx_io_device*d) { xx_audio_riff_ima*r=xx_audio_riff_ima_create(d,0); return r ? &r->format:NULL; }
static void close_reader(Abstractformat*f) { xx_audio_riff_ima_free((xx_audio_riff_ima*)f); }
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader, false};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor*self,xx_format_search_state*s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_audio_riff_ima_extractor={create_search,current,next,free_search};
