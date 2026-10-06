/* SPDX-License-Identifier: MIT. Validated signature search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/olympus_dss/xx_olympus_dss.h"
#ifndef OLYMPUS_DSS
#define XX_FILE_TYPE_OLYMPUS_DSS ((xx_file_type_t)1511)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_OLYMPUS_DSS};
static Abstractformat *open_reader(xx_io_device*d){xx_olympus_dss*r=xx_olympus_dss_create(d,0);return r?&r->format:NULL;}
static void close_reader(Abstractformat*f){xx_olympus_dss_free((xx_olympus_dss*)f);}
static const uint8_t bytes_0[]={0x02,0x64,0x73,0x73};
static const uint8_t bytes_1[]={0x03,0x64,0x73,0x73};
static const uint8_t bytes_2[]={0x02,0x64,0x73,0x32};
static const uint8_t bytes_3[]={0x03,0x64,0x73,0x32};
static const uint8_t bytes_4[]={0x02,0x65,0x6e,0x63};
static const uint8_t bytes_5[]={0x03,0x65,0x6e,0x63};
static const xx_format_search_anchor anchors[]={{bytes_0,sizeof(bytes_0),0},{bytes_1,sizeof(bytes_1),0},{bytes_2,sizeof(bytes_2),0},{bytes_3,sizeof(bytes_3),0},{bytes_4,sizeof(bytes_4),0},{bytes_5,sizeof(bytes_5),0}};
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor*x,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd){(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor*x,xx_format_search_state*s){(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor*x,xx_format_search_state*s,xx_pd_struct*pd){(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*x,xx_format_search_state*s){(void)x;xx_format_search_free(s);}
xx_format_extractor xx_olympus_dss_extractor={create_search,current_search,next_search,free_search};
