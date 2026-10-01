/* SPDX-License-Identifier: MIT. Validated original-component search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/atari_cas/xx_atari_cas.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_ATARI_CAS};
static Abstractformat *open_reader(xx_io_device *d) {xx_atari_cas *r=xx_atari_cas_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat *f) {xx_atari_cas_free((xx_atari_cas *)f);}
static const uint8_t bytes_0[]={0x46,0x55,0x4a,0x49};
static const xx_format_search_anchor anchors[]={{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc={types,1U,anchors,1U,open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) {(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) {(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;xx_format_search_free(s);}
xx_format_extractor xx_atari_cas_extractor={create_search,current_search,next_search,free_search};
