/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/stun_message/xx_stun_message.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_STUN_MESSAGE};
static Abstractformat *open_reader(xx_io_device *d) { xx_stun_message *r=xx_stun_message_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_stun_message_free((xx_stun_message *)f); }
static const xx_format_search_desc desc = {types,1U,NULL,0U,open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)x; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; return xx_format_search_current(s); }
static bool next_search(xx_format_extractor *x,xx_format_search_state *s,xx_pd_struct *pd) { (void)x; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *x,xx_format_search_state *s) { (void)x; xx_format_search_free(s); }
xx_format_extractor xx_stun_message_extractor = {create_search,current_search,next_search,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(stun_message, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
