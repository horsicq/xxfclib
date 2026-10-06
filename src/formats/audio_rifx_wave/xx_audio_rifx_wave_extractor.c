/* SPDX-License-Identifier: MIT. Validated RIFX/WAVE signature search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_rifx_wave/xx_audio_rifx_wave.h"
#ifndef AUDIO_RIFX_WAVE
#define XX_FILE_TYPE_AUDIO_RIFX_WAVE ((xx_file_type_t)1513)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_RIFX_WAVE};
static const uint8_t magic[]={0x52,0x49,0x46,0x58};
static const xx_format_search_anchor anchors[]={{magic,sizeof(magic),0}};
static Abstractformat *open_reader(xx_io_device*d){xx_audio_rifx_wave*r=xx_audio_rifx_wave_create(d,0);return r?&r->format:NULL;}
static void close_reader(Abstractformat*f){xx_audio_rifx_wave_free((xx_audio_rifx_wave*)f);}
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor*x,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd){(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor*x,xx_format_search_state*s){(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor*x,xx_format_search_state*s,xx_pd_struct*pd){(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*x,xx_format_search_state*s){(void)x;xx_format_search_free(s);}
xx_format_extractor xx_audio_rifx_wave_extractor={create_search,current_search,next_search,free_search};
