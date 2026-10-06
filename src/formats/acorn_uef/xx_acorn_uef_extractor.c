/* SPDX-License-Identifier: MIT. Bounded primary-format component search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/acorn_uef/xx_acorn_uef.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_ACORN_UEF};
static Abstractformat *open_reader(xx_io_device *d) { xx_acorn_uef *r=xx_acorn_uef_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_acorn_uef_free((xx_acorn_uef *)f); }
static const uint8_t bytes_0[]={0x55,0x45,0x46,0x20,0x46,0x69,0x6c,0x65,0x21,0x00};
static const xx_format_search_anchor anchors[]={{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x;return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x;return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x;return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x;xx_format_search_free(s); }
xx_format_extractor xx_acorn_uef_extractor={create_search,current_search,next_search,free_search};
