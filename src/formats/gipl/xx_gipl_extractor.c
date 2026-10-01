/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/InsightSoftwareConsortium/ITK/master/Modules/IO/GIPL/src/itkGiplImageIO.cxx */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gipl/xx_gipl.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_GIPL };
static Abstractformat *open_reader(xx_io_device *d) { xx_gipl *r=xx_gipl_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_gipl_free((xx_gipl *)f); }
static const uint8_t bytes_0[] = {239,255,233,176};
static const uint8_t bytes_1[] = {176,233,255,239};
static const uint8_t bytes_2[] = {42,227,137,184};
static const uint8_t bytes_3[] = {184,137,227,42};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),252},{bytes_1,sizeof(bytes_1),252},{bytes_2,sizeof(bytes_2),252},{bytes_3,sizeof(bytes_3),252}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_gipl_extractor = {create_search,current_search,next_search,free_search};
