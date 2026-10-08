/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Westwood PAK has no magic; only the whole stream is probed.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/westwood_pak/xx_westwood_pak.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_WESTWOOD_PAK};
static Abstractformat *open_reader(xx_io_device *d) {
    xx_westwood_pak *r=xx_westwood_pak_create(d,0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f) { xx_westwood_pak_free((xx_westwood_pak *)f); }
static const xx_format_search_desc desc={types,1U,NULL,0U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) {
    (void)x; return xx_format_search_create(&desc,d,o,pd);
}
static const xx_format_search_info *current(xx_format_extractor *x,xx_format_search_state *s) {
    (void)x; return xx_format_search_current(s);
}
static bool next(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) {
    (void)x; return xx_format_search_find_next(s,pd);
}
static void free_search(xx_format_extractor *x,xx_format_search_state *s) {
    (void)x; xx_format_search_free(s);
}
xx_format_extractor xx_westwood_pak_extractor={create_search,current,next,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(westwood_pak, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
