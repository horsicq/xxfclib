/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/dvitype.web
 * TeX DVI2 complete font/page/opcode/postamble grammar, exact backward page pointers, balanced stacks and declared font references. Original page programs and metadata exported; Japanese dialects, font lookup, specials execution and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#include "xxfclib/formats/tex_dvi/xx_tex_dvi.h"
#include "../common/xx_texture_font_components.h"
static bool texture_font_quick(Abstractformat *f,uint64_t n) {uint8_t b[15];return texture_font_probe(f,n,b,15)&&b[0]==247&&b[1]==2;}
typedef struct dv_font {uint32_t id;uint64_t at,size;} dv_font;
static bool dv_def(const uint8_t *b,uint64_t *p,uint64_t n,uint32_t op,dv_font *fonts,uint32_t *count) {unsigned bytes=op-242;uint64_t start=*p-1,after;uint32_t id,i;uint8_t a,l;if(!texture_font_span(*p,bytes+14U,n))return false;id=texture_font_uint(b+*p,bytes);*p+=bytes;if(!xx_data_get_u32(b+*p+4, 4, 0, true)||!xx_data_get_u32(b+*p+8, 4, 0, true))return false;a=b[*p+12];l=b[*p+13];*p+=14;if(!l||!texture_font_span(*p,(uint32_t)a+l,n))return false;after=*p+a+l;for(i=0;i<(uint32_t)a+l;++i)if(b[*p+i]==0)return false;*p=after;
 for(i=0;i<*count;++i)if(fonts[i].id==id){uint64_t old=fonts[i].at+1;unsigned oldBytes=b[fonts[i].at]-242;if(fonts[i].size-oldBytes!=after-start-bytes||xx_rt_memcmp(b+old+oldBytes,b+start+1+bytes,(size_t)(after-start-1-bytes))!=0)return false;return true;}
 if(*count>=512) {return false; } fonts[*count].id=id;fonts[*count].at=start;fonts[*count].size=after-start;++*count;return true;
}
static bool dv_used(uint32_t *used,uint32_t *count,uint32_t id) {uint32_t i;for(i=0;i<*count;++i)if(used[i]==id)return true;if(*count>=512)return false;used[(*count)++]=id;return true;}
static bool texture_font_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=15+(uint64_t)b[14],pageAt=0,lastBop=0xffffffffU,postAt=0,postpost=0,at;uint32_t pages=0,depth=0,maxDepth=0,fontsCount=0,usedCount=0,currentFont=0,i;uint32_t used[512];dv_font fonts[512];bool page=false,post=false,selected=false;char label[64];
 if(p>n||!xx_data_get_u32(b+2, 4, 0, true)||xx_data_get_u32(b+2, 4, 0, true)>0x7fffffffU||!xx_data_get_u32(b+6, 4, 0, true)||xx_data_get_u32(b+6, 4, 0, true)>0x7fffffffU||!xx_data_get_u32(b+10, 4, 0, true)||xx_data_get_u32(b+10, 4, 0, true)>1000000||!texture_font_emit(f,s,"dvi-preamble.bin",0,p,n))return false;
 while(p<n){uint32_t op=b[p++],bytes=0;at=p-1;if(texture_font_stop(pd))return false;
  if(op>=243&&op<=246){if(!dv_def(b,&p,n,op,fonts,&fontsCount))return false;if(!page&&!post){xx_rt_snprintf(label,sizeof(label),"font-definition-%u.dvi",(unsigned)s->count);if(!texture_font_emit(f,s,label,at,p-at,n))return false;}continue;}
  if(post){if(op==138)continue;if(op!=249||!texture_font_span(p,5,n)||xx_data_get_u32(b+p, 4, 0, true)!=postAt||b[p+4]!=2)return false;p+=5;postpost=p;if(n-p<4||n%4)return false;while(p<n)if(b[p++]!=223)return false;break;}
  if(op==139){if(page||!texture_font_span(p,44,n)||xx_data_get_u32(b+p+40, 4, 0, true)!=lastBop)return false;lastBop=at;pageAt=at;p+=44;page=true;selected=false;depth=0;continue;}
  if(op==248){if(page||!pages||!texture_font_span(p,28,n)||xx_data_get_u32(b+p, 4, 0, true)!=lastBop||xx_data_get_u32(b+p+4, 4, 0, true)!=xx_data_get_u32(b+2, 4, 0, true)||xx_data_get_u32(b+p+8, 4, 0, true)!=xx_data_get_u32(b+6, 4, 0, true)||xx_data_get_u32(b+p+12, 4, 0, true)!=xx_data_get_u32(b+10, 4, 0, true)||xx_data_get_u32(b+p+16, 4, 0, true)>0x7fffffffU||xx_data_get_u32(b+p+20, 4, 0, true)>0x7fffffffU||xx_data_get_u16(b+p+24, 2, 0, true)<maxDepth||xx_data_get_u16(b+p+24, 2, 0, true)>1024||xx_data_get_u16(b+p+26, 2, 0, true)!=pages)return false;post=true;postAt=at;p+=28;continue;}
  if(op==138){if(!page&&!texture_font_emit(f,s,"dvi-nop.bin",at,1,n))return false;continue;}if(!page)return false;
  if(op==140){if(depth||++pages>4090)return false;page=false;xx_rt_snprintf(label,sizeof(label),"page-%u.dvi",pages-1);if(!texture_font_emit(f,s,label,pageAt,p-pageAt,n))return false;continue;}
  if(op==141){if(++depth>1024)return false;if(depth>maxDepth)maxDepth=depth;continue;}if(op==142){if(!depth)return false;--depth;continue;}
  if(op<=127){if(!selected||!dv_used(used,&usedCount,currentFont))return false;continue;}
  if(op>=128&&op<=131){bytes=op-127;if(!selected||!dv_used(used,&usedCount,currentFont))return false;}
  else if(op==132||op==137)bytes=8;
  else if(op>=133&&op<=136){bytes=op-132;if(!selected||!dv_used(used,&usedCount,currentFont))return false;}
  else if(op>=143&&op<=170){if(op==147||op==152||op==161||op==166)bytes=0;else if(op<147)bytes=op-142;else if(op<152)bytes=op-147;else if(op<157)bytes=op-152;else if(op<161)bytes=op-156;else if(op<166)bytes=op-161;else bytes=op-166;}
  else if(op>=171&&op<=234){currentFont=op-171;selected=true;continue;}
  else if(op>=235&&op<=238){bytes=op-234;if(!texture_font_span(p,bytes,n))return false;currentFont=texture_font_uint(b+p,bytes);selected=true;p+=bytes;continue;}
  else if(op>=239&&op<=242){uint32_t size;bytes=op-238;if(!texture_font_span(p,bytes,n))return false;size=texture_font_uint(b+p,bytes);p+=bytes;if(!texture_font_span(p,size,n))return false;p+=size;continue;}else return false;
  if(!texture_font_span(p,bytes,n)) {return false; } p+=bytes;
 }
 if(!post||!postpost||page||!fontsCount) {return false; } for(i=0;i<usedCount;++i){uint32_t j;bool found=false;for(j=0;j<fontsCount;++j)if(fonts[j].id==used[i])found=true;if(!found)return false;}
 if(!texture_font_emit(f,s,"dvi-postamble.bin",postAt,n-postAt,n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_tex_dvi_init(xx_tex_dvi *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_TEX_DVI,"dvi");}}
xx_tex_dvi *xx_tex_dvi_create(xx_io_device *d,int64_t at) {xx_tex_dvi *r=(xx_tex_dvi *)xx_mem_alloc(sizeof(*r));if(r)xx_tex_dvi_init(r,d,at);return r;}
void xx_tex_dvi_destroy(xx_tex_dvi *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tex_dvi_free(xx_tex_dvi *r) {if(r){xx_tex_dvi_destroy(r);xx_mem_free(r);}}
bool xx_tex_dvi_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tex_dvi_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
