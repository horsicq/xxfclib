/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_shockwave_swa/xx_audio_shockwave_swa.h"
#ifndef XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA
#define XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA ((xx_file_type_t)1525)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_SHOCKWAVE_SWA};
static const uint8_t marker[]={'M','A','C','R','Z'};
static const xx_format_search_anchor anchors[]={{marker,sizeof(marker),36}};
static Abstractformat *open_reader(xx_io_device*d) { xx_audio_shockwave_swa*r=xx_audio_shockwave_swa_create(d,0); return r ? &r->format:NULL; }
static void close_reader(Abstractformat*f) { xx_audio_shockwave_swa_free((xx_audio_shockwave_swa*)f); }
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor*self,xx_format_search_state*s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_audio_shockwave_swa_extractor={create_search,current,next,free_search};
