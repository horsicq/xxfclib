/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.adobe.com/content/dam/acom/en/devnet/font/pdfs/T1_SPEC.pdf
 * Type1 PFB complete ASCII/binary/ASCII segment ordering and final marker, bounded segment sizes, font program identity, eexec transition and ASCII closing program. Original encrypted font programs exported; no PostScript execution or glyph decryption.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/font_type1_pfb/xx_font_type1_pfb.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[8];return n>=30&&pm_read(f,0,b,8)&&b[0]==128&&b[1]==1&&b[6]=='%'&&b[7]=='!';}
static bool pf_find(const uint8_t *b,uint64_t z,const char *text) {uint64_t p;size_t n=xx_rt_strlen(text);for(p=0;n<=z&&p<=z-n;++p)if(eg_tag(b+p,text,n))return true;return false;}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=0;unsigned phase=0,segments=0;bool binary=false,trailer=false;
 while(p<n){uint64_t start=p;uint32_t z;uint8_t type;const char *label;
  if(eg_stop(pd)||!eg_span(p,2,n)||b[p++]!=128)return false;type=b[p++];
  if(type==3){if(p!=n||!binary||!trailer||!eg_emit(f,s,"terminator.pfb",start,2,n))return false;s->size=(int64_t)n;return true;}
  if(type<1||type>2||++segments>1024||!eg_span(p,4,n))return false;z=pm_le32(b+p);p+=4;if(!z||!eg_span(p,z,n))return false;
  if(type==2){if(phase==0||phase==3||z<4)return false;phase=2;binary=true;label="encrypted-font-program.pfb";}
  else{uint64_t i;if(!phase){if(z<15||!eg_tag(b+p,"%!PS-AdobeFont-",14)||!pf_find(b+p,z,"currentfile eexec"))return false;phase=1;label="font-definition.pfb";}
   else{if(!binary)return false;phase=3;trailer=pf_find(b+p,z,"cleartomark");label="closing-program.pfb";}
   for(i=0;i<z;++i)if(b[p+i]>126||(b[p+i]<32&&b[p+i]!=9&&b[p+i]!=10&&b[p+i]!=13&&b[p+i]!=12))return false;}
  if(!eg_emit(f,s,label,start,(uint64_t)z+6,n))return false;p+=z;
 }
 return false;
}

void xx_font_type1_pfb_init(xx_font_type1_pfb *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONT_TYPE1_PFB,"pfb");}}
xx_font_type1_pfb *xx_font_type1_pfb_create(xx_io_device *d,int64_t at) {xx_font_type1_pfb *r=(xx_font_type1_pfb *)xx_mem_alloc(sizeof(*r));if(r)xx_font_type1_pfb_init(r,d,at);return r;}
void xx_font_type1_pfb_destroy(xx_font_type1_pfb *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_font_type1_pfb_free(xx_font_type1_pfb *r) {if(r){xx_font_type1_pfb_destroy(r);xx_mem_free(r);}}
bool xx_font_type1_pfb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_font_type1_pfb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
