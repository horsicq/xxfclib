/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Signature search followed by complete bounded container validation.
 */
#include "xxfclib/formats/llvm_bitcode_wrapper/xx_llvm_bitcode_wrapper.h"
#include "../xx_format_extractor_engine.h"
static const xx_file_type_t types[]={XX_FILE_TYPE_LLVM_BITCODE_WRAPPER};
static Abstractformat *open_reader(xx_io_device *d) { xx_llvm_bitcode_wrapper *r=xx_llvm_bitcode_wrapper_create(d,0); return r ? &r->format : NULL; }
static void close_reader(Abstractformat *f) { xx_llvm_bitcode_wrapper_free((xx_llvm_bitcode_wrapper *)f); }
static const uint8_t signature_0[]={222,192,23,11};

static const xx_format_search_anchor anchors[]={ {signature_0,sizeof(signature_0),0} };
static const xx_format_search_desc desc={types,1,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader, false};
static xx_format_search_state *create(xx_format_extractor *e,xx_io_device *d,const xx_list_s *o,xx_pd_struct *p) { (void)e; return xx_format_search_create(&desc,d,o,p); }
static const xx_format_search_info *current(xx_format_extractor *e,xx_format_search_state *s) { (void)e; return xx_format_search_current(s); }
static bool next(xx_format_extractor *e,xx_format_search_state *s,xx_pd_struct *p) { (void)e; return xx_format_search_find_next(s,p); }
static void release(xx_format_extractor *e,xx_format_search_state *s) { (void)e; xx_format_search_free(s); }
xx_format_extractor xx_llvm_bitcode_wrapper_extractor={create,current,next,release};
