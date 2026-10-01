/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_ast/xx_audio_ast.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef AUDIO_AST
#define XX_FILE_TYPE_AUDIO_AST ((xx_file_type_t)817)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=64;unsigned channels;uint32_t samples,rate;uint64_t count=0;unsigned blocks=0;
 if(!e8_range(c,0,64) || !e8_eq(c,0,"STRM",4) || pm_be32(c->b+4)!=c->n-64 || pm_be16(c->b+8)!=1 || pm_be16(c->b+10)!=16 || !(channels=pm_be16(c->b+12)) || channels>32 || (pm_be16(c->b+14)!=0 && pm_be16(c->b+14)!=65535) || !(rate=pm_be32(c->b+16)) || rate>384000 || !(samples=pm_be32(c->b+20)) || pm_be32(c->b+24)>pm_be32(c->b+28) || pm_be32(c->b+28)>samples || !e8_add(c,"header.bin",0,64))return false;
 while(p<c->n){uint32_t z;uint64_t n;if(++blocks>4095 || !e8_range(c,p,32) || !e8_eq(c,p,"BLCK",4) || !(z=pm_be32(c->b+p+4)) || (z&1) || !e8_zero(c,p+8,24) || (n=(uint64_t)z*channels)>c->n-p-32 || !e8_add(c,"pcm-channel-block.bin",p,32U+(size_t)n))return false;count+=z/2U;p+=32U+(size_t)n;}
 return p==c->n && blocks && count==samples;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_audio_ast_init(xx_audio_ast*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_AST,"bin");}}
xx_audio_ast*xx_audio_ast_create(xx_io_device*d,int64_t b) {xx_audio_ast*r=(xx_audio_ast*)xx_mem_alloc(sizeof(*r));if(r)xx_audio_ast_init(r,d,b);return r;}
void xx_audio_ast_destroy(xx_audio_ast*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_ast_free(xx_audio_ast*r) {if(r){xx_audio_ast_destroy(r);xx_mem_free(r);}}
bool xx_audio_ast_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_audio_ast_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
