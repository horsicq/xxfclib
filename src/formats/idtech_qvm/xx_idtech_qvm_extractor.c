/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/qcommon/qfiles.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/idtech_qvm/xx_idtech_qvm.h"
static const xx_file_type_t types[]={ XX_FILE_TYPE_IDTECH_QVM };
static const uint8_t a0[]={0x44,0x14,0x72,0x12};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0}};
static Abstractformat *open_reader(xx_io_device *d) { xx_idtech_qvm *r=xx_idtech_qvm_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_idtech_qvm_free((xx_idtech_qvm *)f); }
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *self,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current(xx_format_extractor *self,xx_format_search_state *s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor *self,xx_format_search_state *s,xx_pd_struct *pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *self,xx_format_search_state *s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_idtech_qvm_extractor={create_search,current,next,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(idtech_qvm, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
