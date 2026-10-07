/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://planetside.co.uk/wiki/index.php?title=Terragen_.TER_Format
 * Terragen classic TER: unique SIZE/XPTS/YPTS geometry, positive finite SCAL/CRAD and fixed CRVM chunks, complete ALTW signed16 heightfield and terminal EOF. Unknown chunks and terrain rendering unsupported. Original heightfield and descriptors exported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/terragen_ter/xx_terragen_ter.h"
#include "../wavefront_obj/xx_eleventh_media.h"
#include "xxfclib/data/xx_data.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[16];return n>=40&&pm_read(f,0,b,16)&&eg_tag(b,"TERRAGENTERRAIN ",16);}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=16;uint32_t seen=0,w=0,h=0,size=0;bool height=false;
 if(!eg_tag(b,"TERRAGENTERRAIN ",16)||!eg_emit(f,s,"descriptor.ter",0,16,n))return false;
 while(p<n){uint64_t start=p,z=0;unsigned bit=0;const char *label;
  if(eg_stop(pd)||!eg_span(p,4,n))return false;
  if(eg_tag(b+p,"EOF ",4)){p+=4;if(p!=n||!height)return false;if(!eg_emit(f,s,"terminator.ter",start,4,n))return false;s->size=(int64_t)n;return true;}
  if(height)return false;
  if(eg_tag(b+p,"SIZE",4)){bit=1;z=8;label="size.ter";if(!eg_span(p,z,n)||!eg_zero(b+p+6,2))return false;size=xx_data_get_u16(b+p+4, 2, 0, false)+1U;}
  else if(eg_tag(b+p,"XPTS",4)){bit=2;z=8;label="width.ter";if(!(seen&1)||!eg_span(p,z,n)||!eg_zero(b+p+6,2)||(w=xx_data_get_u16(b+p+4, 2, 0, false))==0)return false;}
  else if(eg_tag(b+p,"YPTS",4)){bit=4;z=8;label="height.ter";if(!(seen&1)||!eg_span(p,z,n)||!eg_zero(b+p+6,2)||(h=xx_data_get_u16(b+p+4, 2, 0, false))==0)return false;}
  else if(eg_tag(b+p,"SCAL",4)){unsigned i;bit=8;z=16;label="scale.ter";if(!eg_span(p,z,n))return false;for(i=0;i<3;++i){uint32_t u=xx_data_get_u32(b+p+4+i*4, 4, 0, false);if(!eg_f32(u)||(u&0x80000000U)||!(u&0x7fffffffU))return false;}}
  else if(eg_tag(b+p,"CRAD",4)){uint32_t u;bit=16;z=8;label="radius.ter";if(!eg_span(p,z,n)||!eg_f32(u=xx_data_get_u32(b+p+4, 4, 0, false))||(u&0x80000000U)||!(u&0x7fffffffU))return false;}
  else if(eg_tag(b+p,"CRVM",4)){bit=32;z=8;label="curve-mode.ter";if(!eg_span(p,z,n)||xx_data_get_u32(b+p+4, 4, 0, false)>1)return false;}
  else if(eg_tag(b+p,"ALTW",4)){uint64_t cells;if(!(seen&1))return false;if(!w)w=size;if(!h)h=size;if((w<h?w:h)!=size)return false;cells=(uint64_t)w*h;if(!cells||cells>16777216)return false;z=8+cells*2;z=(z+3)&~3ULL;label="heightfield.ter";bit=64;height=true;if(!eg_span(p,z,n)||!eg_zero(b+p+8+cells*2,z-8-cells*2))return false;}
  else return false;
  if((seen&bit)||!eg_span(p,z,n)||!eg_emit(f,s,label,start,z,n)) {return false; } seen|=bit;p+=z;
 }
 return false;
}

void xx_terragen_ter_init(xx_terragen_ter *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_TERRAGEN_TER,"ter");}}
xx_terragen_ter *xx_terragen_ter_create(xx_io_device *d,int64_t at) {xx_terragen_ter *r=(xx_terragen_ter *)xx_mem_alloc(sizeof(*r));if(r)xx_terragen_ter_init(r,d,at);return r;}
void xx_terragen_ter_destroy(xx_terragen_ter *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_terragen_ter_free(xx_terragen_ter *r) {if(r){xx_terragen_ter_destroy(r);xx_mem_free(r);}}
bool xx_terragen_ter_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_terragen_ter_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
