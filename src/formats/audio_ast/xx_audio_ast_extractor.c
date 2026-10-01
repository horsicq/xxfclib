/* SPDX-License-Identifier: MIT. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_ast/xx_audio_ast.h"
#ifndef AUDIO_AST
#define XX_FILE_TYPE_AUDIO_AST ((xx_file_type_t)817)
#endif
static const xx_file_type_t types[]={XX_FILE_TYPE_AUDIO_AST};
static const uint8_t a0[]={0x53,0x54,0x52,0x4d};
static const xx_format_search_anchor anchors[]={{a0,sizeof(a0),0}};
static Abstractformat *open_reader(xx_io_device*d) {xx_audio_ast*r=xx_audio_ast_create(d,0);return r ? &r->format:NULL;}
static void close_reader(Abstractformat*f) {xx_audio_ast_free((xx_audio_ast*)f);}
static const xx_format_search_desc desc={types,1U,anchors,sizeof(anchors)/sizeof(anchors[0]),open_reader,close_reader};
static xx_format_search_state*create_search(xx_format_extractor*self,xx_io_device*d,const xx_list_s*o,xx_pd_struct*pd) {(void)self;return xx_format_search_create(&desc,d,o,pd);}
static const xx_format_search_info*current(xx_format_extractor*self,xx_format_search_state*s) {(void)self;return xx_format_search_current(s);}
static bool next(xx_format_extractor*self,xx_format_search_state*s,xx_pd_struct*pd) {(void)self;return xx_format_search_find_next(s,pd);}
static void free_search(xx_format_extractor*self,xx_format_search_state*s) {(void)self;xx_format_search_free(s);}
xx_format_extractor xx_audio_ast_extractor={create_search,current,next,free_search};
