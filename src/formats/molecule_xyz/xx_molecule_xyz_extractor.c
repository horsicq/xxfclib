/* SPDX-License-Identifier: MIT
 * Wire specification: https://ase-lib.org/_modules/ase/io/xyz.html */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/molecule_xyz/xx_molecule_xyz.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_MOLECULE_XYZ };
static Abstractformat *open_reader(xx_io_device *d) { xx_molecule_xyz *r=xx_molecule_xyz_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_molecule_xyz_free((xx_molecule_xyz *)f); }
static const xx_format_search_desc desc = {types,1U,NULL,0U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_molecule_xyz_extractor = {create_search,current_search,next_search,free_search};
