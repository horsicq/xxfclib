/* SPDX-License-Identifier: MIT. Complete physical-container search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/amos_music_bank/xx_amos_music_bank.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_AMOS_MUSIC_BANK};
static Abstractformat *open_reader(xx_io_device *d) {xx_amos_music_bank *r=xx_amos_music_bank_create(d,0);return r?&r->format:NULL;}
static void close_reader(Abstractformat *f) {xx_amos_music_bank_free((xx_amos_music_bank *)f);}
static const xx_format_search_desc desc={types,1U,NULL,0U,open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) {(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) {(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;xx_format_search_free(s);}
xx_format_extractor xx_amos_music_bank_extractor={create_search,current_search,next_search,free_search};
