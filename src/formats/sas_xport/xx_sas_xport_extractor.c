/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.sas.com/content/dam/sasdam/documents/20260302/record-layout-of-a-sas-version-5-or-6-data-set-in-sas-transport-xport-format.pdf
 * Bounded encoded-component extraction. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sas_xport/xx_sas_xport.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_SAS_XPORT };
static Abstractformat *open_reader(xx_io_device *d) { xx_sas_xport *r=xx_sas_xport_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_sas_xport_free((xx_sas_xport *)f); }
static const uint8_t bytes_0[] = {72,69,65,68,69,82,32,82,69,67,79,82,68,42,42,42,42,42,42,42,76,73,66,82,65,82,89,32,72,69,65,68,69,82,32,82,69,67,79,82,68,33,33,33,33,33,33,33};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_sas_xport_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sas_xport, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
