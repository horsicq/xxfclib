/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/OpenMPT/openmpt/master/soundlib/Load_psm.cpp
 * New Epic MASI PSM FILE/MAINSONG containers with regular4-byte pattern IDs, up to256 patterns/samples/32 channels and one song. Validates chunk tiling, OPLH playlist/settings opcode framing, packed row extents (including well-framed inactive rows, at most256 total), all used sample identities/loops/rates and order references. Exports original chunks; sample bytes remain delta-coded, no decoding/playback. PSM16/Sinaria, unknown opcodes/chunks and extension modes rejected.256MiB cap.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tracker_psm/xx_tracker_psm.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_TRACKER_PSM};
static Abstractformat *open_reader(xx_io_device *d) { xx_tracker_psm *r=xx_tracker_psm_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_tracker_psm_free((xx_tracker_psm *)f); }
static const uint8_t bytes_0[] = {0x50,0x53,0x4d,0x20};
static const xx_format_search_anchor anchors[] = {{bytes_0,sizeof(bytes_0),0}};
static const xx_format_search_desc desc = {types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_tracker_psm_extractor = {create_search,current_search,next_search,free_search};
