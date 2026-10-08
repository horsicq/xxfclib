/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.teledynelecroy.com/support/knowledgebase.aspx?docid=556 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/lecroy_trc/xx_lecroy_trc.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_LECROY_TRC };
static Abstractformat *open_reader(xx_io_device *d) { xx_lecroy_trc *r=xx_lecroy_trc_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_lecroy_trc_free((xx_lecroy_trc *)f); }
static const uint8_t bytes_0[] = {87,65,86,69,68,69,83,67};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_lecroy_trc_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(lecroy_trc, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
