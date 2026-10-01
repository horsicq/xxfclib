/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.cs.cmu.edu/~maxwell/misc/vascHelpPages/sunRasterFormat.html, https://gitlab.gnome.org/GNOME/gimp/-/blob/master/plug-ins/common/file-sunras.c
 * Stored encoded component extraction; no media decoding claims.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sun_raster/xx_sun_raster.h"
static const xx_file_type_t types[] = { XX_FILE_TYPE_SUN_RASTER };
static Abstractformat *open_reader(xx_io_device *d) { xx_sun_raster *r=xx_sun_raster_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_sun_raster_free((xx_sun_raster *)f); }
static const uint8_t bytes_0[] = {0x59,0xA6,0x6A,0x95};
static const xx_format_search_anchor anchors[] = { {bytes_0,sizeof(bytes_0),0} };
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_sun_raster_extractor = {create_search,current_search,next_search,free_search};
