/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf
 * Bounded encoded-component extraction. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/matlab_mat5/xx_matlab_mat5.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_MATLAB_MAT5 };
static Abstractformat *open_reader(xx_io_device *d) { xx_matlab_mat5 *r=xx_matlab_mat5_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_matlab_mat5_free((xx_matlab_mat5 *)f); }
static const uint8_t bytes_0[] = {73,77};
static const uint8_t bytes_1[] = {77,73};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),126},{bytes_1,sizeof(bytes_1),126}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_matlab_mat5_extractor = {create_search,current_search,next_search,free_search};
