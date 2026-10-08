/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/dbm_load.c
 * DigiBooster Pro2 DBM0 with NAME/INFO/SONG/INST/PATT/SMPL complete chunk tables, one song, up to256 patterns/instruments/samples and64 channels. Checks row packet fields, instrument/sample references, sample formats8/16/32-bit, loop spans and complete chunk consumption, with at most four zero row-padding bytes per pattern. Exports encoded chunks without playback. Envelope/echo/other extension chunks unsupported.256MiB cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_dbm/xx_tracker_dbm.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_DBM};
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_dbm *r=xx_tracker_dbm_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_dbm_free((xx_tracker_dbm *)f); }
static const uint8_t bytes_0[] = {0x44,0x42,0x4d,0x30};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_dbm_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(tracker_dbm, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
