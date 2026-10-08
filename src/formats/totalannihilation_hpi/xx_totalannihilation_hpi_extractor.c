/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/coreprime/kbot-io/blob/main/formats/hpi/v1/reader.go
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/totalannihilation_hpi/xx_totalannihilation_hpi.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_TOTALANNIHILATION_HPI};
static const uint8_t a0[]={0x48,0x41,0x50,0x49};
static const xx_format_search_anchor anchors[]={ {a0,sizeof(a0),0} };
static Abstractformat *open_reader(xx_io_device *d) { xx_totalannihilation_hpi *r=xx_totalannihilation_hpi_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_totalannihilation_hpi_free((xx_totalannihilation_hpi *)f); }
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *self,xx_io_device *d,const xx_list_s *o,xx_pd_struct *pd) { (void)self; return xx_format_search_create(&desc,d,o,pd); }
static const xx_format_search_info *current(xx_format_extractor *self,xx_format_search_state *s) { (void)self; return xx_format_search_current(s); }
static bool next(xx_format_extractor *self,xx_format_search_state *s,xx_pd_struct *pd) { (void)self; return xx_format_search_find_next(s,pd); }
static void free_search(xx_format_extractor *self,xx_format_search_state *s) { (void)self; xx_format_search_free(s); }
xx_format_extractor xx_totalannihilation_hpi_extractor={create_search,current,next,free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(totalannihilation_hpi, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
