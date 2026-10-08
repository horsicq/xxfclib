/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_pvf/xx_audio_pvf.h"
#ifndef XX_FILE_TYPE_AUDIO_PVF
#define XX_FILE_TYPE_AUDIO_PVF ((xx_file_type_t)1524)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_PVF};
static const uint8_t one[]={'P','V','F','1','\n'},two[]={'P','V','F','2','\n'};
static const xx_format_search_anchor anchors[]={{one,sizeof(one),0},{two,sizeof(two),0}};
static Abstractformat *open_reader(xx_io_device*d) { xx_audio_pvf*r=xx_audio_pvf_create(d,0); return r ? &r->format:NULL; }
static void close_reader(Abstractformat*f) { xx_audio_pvf_free((xx_audio_pvf*)f); }
static const xx_format_search_desc desc={types,1U,anchors,2U,open_reader,close_reader, false};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor*self,xx_format_search_state*s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_audio_pvf_extractor={create_search,current,next,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(audio_pvf, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
