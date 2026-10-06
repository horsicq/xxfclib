/* SPDX-License-Identifier: MIT. Validated original-component search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/apple_a2r/xx_apple_a2r.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_APPLE_A2R};
static Abstractformat *open_reader(xx_io_device *d) {xx_apple_a2r *r=xx_apple_a2r_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat *f) {xx_apple_a2r_free((xx_apple_a2r *)f);}
static const uint8_t bytes_0[]={0x41,0x32,0x52,0x32,0xff,0x0a,0x0d,0x0a};
static const uint8_t bytes_1[]={0x41,0x32,0x52,0x33,0xff,0x0a,0x0d,0x0a};
static const xx_format_search_anchor anchors[]={{bytes_0,sizeof(bytes_0),0},{bytes_1,sizeof(bytes_1),0}};
static const xx_format_search_desc desc={types,1U,anchors,2U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) {(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) {(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;xx_format_search_free(s);}
xx_format_extractor xx_apple_a2r_extractor={create_search,current_search,next_search,free_search};
