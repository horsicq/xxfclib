/* SPDX-License-Identifier: MIT. Validated signature search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/adobe_director_cxt/xx_adobe_director_cxt.h"
#ifndef ADOBE_DIRECTOR_CXT
#define XX_FILE_TYPE_ADOBE_DIRECTOR_CXT ((xx_file_type_t)1510)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_ADOBE_DIRECTOR_CXT};
static Abstractformat *open_reader(xx_io_device*d){xx_adobe_director_cxt*r=xx_adobe_director_cxt_create(d,0);return r?&r->format:NULL;}
static void close_reader(Abstractformat*f){xx_adobe_director_cxt_free((xx_adobe_director_cxt*)f);}
static const uint8_t bytes_0[]={0x52,0x49,0x46,0x58};
static const uint8_t bytes_1[]={0x58,0x46,0x49,0x52};
static const xx_format_search_anchor anchors[]={{bytes_0,sizeof(bytes_0),0},{bytes_1,sizeof(bytes_1),0}};
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor*x,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd){(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor*x,xx_format_search_state*s){(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor*x,xx_format_search_state*s,xx_pd_struct*pd){(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*x,xx_format_search_state*s){(void)x;xx_format_search_free(s);}
xx_format_extractor xx_adobe_director_cxt_extractor={create_search,current_search,next_search,free_search};
