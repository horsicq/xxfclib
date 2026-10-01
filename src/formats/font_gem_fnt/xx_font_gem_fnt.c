/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/emutos/emutos/master/include/fonthdr.h
 * Classic GEM/GDOS bitmap fonts with bounded endian-selected88-byte descriptor, character range, monotonic bit-offset table, optional horizontal offsets and exact scanline bitmap. Original font tables and raster exported; chained/extended/compressed fonts and rendering unsupported. Signatureless offset-zero detection only.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/font_gem_fnt/xx_font_gem_fnt.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[88];return n>=90&&pm_read(f,0,b,88);}
static uint16_t gm16(const uint8_t *b,bool be) {return be?pm_be16(b):pm_le16(b);}
static uint32_t gm32(const uint8_t *b,bool be) {return be?pm_be32(b):pm_le32(b);}
static bool gm_header(const uint8_t *b,uint64_t n,bool be) {
 uint32_t first=gm16(b+36,be),last=gm16(b+38,be),point=gm16(b+2,be),flags=gm16(b+66,be),width=gm16(b+80,be),height=gm16(b+82,be),off=gm32(b+72,be),data=gm32(b+76,be);
 return point>0&&point<256&&first<=last&&last<256&&!(flags&~15U)&&width>0&&height>0&&height<=8192&&off>=88&&data>=88&&gm32(b+84,be)==0&&eg_span(off,(uint64_t)(last-first+2)*2,n)&&eg_span(data,(uint64_t)width*height,n);
}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 bool be=false;uint32_t first,last,count,flags,hor,off,data,width,height,i,prev=0;uint64_t p=88,bytes;bool glyph=false;
 if(n<90)return false;if(!gm_header(b,n,false)){be=true;if(!gm_header(b,n,true))return false;}
 first=gm16(b+36,be);last=gm16(b+38,be);count=last-first+1;flags=gm16(b+66,be);hor=gm32(b+68,be);off=gm32(b+72,be);data=gm32(b+76,be);width=gm16(b+80,be);height=gm16(b+82,be);
 for(i=4;i<36;++i){if(b[i]>126|| (b[i]&&b[i]<32))return false;}
 if(flags&2){if(hor<88||hor>90||!eg_zero(b+88,hor-88)||!eg_span(hor,(uint64_t)count*2,n)||off!=hor+count*2)return false;p=off;}
 else{if(hor||off<88||off>90||!eg_zero(b+88,off-88))return false;p=off;}
 if(data!=off+(count+1)*2)return false;
 for(i=0;i<=count;++i){uint32_t next=gm16(b+off+i*2,be);if(eg_stop(pd)||next<prev||next>width*8U||(i==0&&next))return false;if(next>prev)glyph=true;prev=next;}
 if(!glyph||gm16(b+50,be)>width*8U||gm16(b+52,be)>width*8U)return false;bytes=(uint64_t)width*height;if(data+bytes!=n)return false;
 if(!eg_emit(f,s,"descriptor.fnt",0,(flags&2)?hor:p,n))return false;
 if((flags&2)&&!eg_emit(f,s,"horizontal-offsets.fnt",hor,count*2,n))return false;
 if(!eg_emit(f,s,"glyph-bit-offsets.fnt",off,(count+1)*2,n)||!eg_emit(f,s,"bitmap.fnt",data,bytes,n))return false;s->size=(int64_t)n;return true;
}

void xx_font_gem_fnt_init(xx_font_gem_fnt *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONT_GEM_FNT,"fnt");}}
xx_font_gem_fnt *xx_font_gem_fnt_create(xx_io_device *d,int64_t at) {xx_font_gem_fnt *r=(xx_font_gem_fnt *)xx_mem_alloc(sizeof(*r));if(r)xx_font_gem_fnt_init(r,d,at);return r;}
void xx_font_gem_fnt_destroy(xx_font_gem_fnt *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_font_gem_fnt_free(xx_font_gem_fnt *r) {if(r){xx_font_gem_fnt_destroy(r);xx_mem_free(r);}}
bool xx_font_gem_fnt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_font_gem_fnt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
