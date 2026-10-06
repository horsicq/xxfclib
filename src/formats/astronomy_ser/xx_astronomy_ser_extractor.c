/* SPDX-License-Identifier: MIT
 * Wire specification: https://grischa-hahn.hier-im-netz.de/astro/ser/SER%20Doc%20V2.pdf */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/astronomy_ser/xx_astronomy_ser.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_ASTRONOMY_SER };
static Abstractformat *open_reader(xx_io_device *d) { xx_astronomy_ser *r=xx_astronomy_ser_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_astronomy_ser_free((xx_astronomy_ser *)f); }
static const uint8_t bytes_0[] = {76,85,67,65,77,45,82,69,67,79,82,68,69,82};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_astronomy_ser_extractor = {create_search,current_search,next_search,free_search};
