/* SPDX-License-Identifier: MIT. Complete physical-container search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/mad_tracker/xx_mad_tracker.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_MAD_TRACKER};
static Abstractformat *open_reader(xx_io_device *d) {xx_mad_tracker *r=xx_mad_tracker_create(d,0);return r?&r->format:NULL;}
static void close_reader(Abstractformat *f) {xx_mad_tracker_free((xx_mad_tracker *)f);}
static const xx_format_search_desc desc={types,1U,NULL,0U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) {(void)x;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;return xx_format_search_current(s);}
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) {(void)x;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor *x,xx_format_search_state *s) {(void)x;xx_format_search_free(s);}
xx_format_extractor xx_mad_tracker_extractor={create_search,current_search,next_search,free_search};
