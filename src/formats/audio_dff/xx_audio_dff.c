/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_dff/xx_audio_dff.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef AUDIO_DFF
#define XX_FILE_TYPE_AUDIO_DFF ((xx_file_type_t)814)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=16;unsigned seen=0,channels=0;uint32_t rate=0;uint64_t data=0;
 if(!e8_eq(c,0,"FRM8",4) || !e8_eq(c,12,"DSD ",4) || fd_be64(c->b+4)!=c->n-12 || !e8_add(c,"header.bin",0,16))return false;
 while(p<c->n){size_t at=p+12;uint64_t z;unsigned bit=0;char name[24];if(!e8_range(c,p,12) || (z=fd_be64(c->b+p+4))>c->n-at)return false;
  if(e8_eq(c,p,"FVER",4)){bit=1;if(z!=4 || (pm_be32(c->b+at)>>24)!=1)return false;}
  else if(e8_eq(c,p,"PROP",4)){size_t q=at+4;unsigned ps=0;bit=2;if(!e8_eq(c,at,"SND ",4))return false;while(q<at+z){uint64_t n;size_t x=q+12;unsigned pb;if(q+12>at+z || (n=fd_be64(c->b+q+4))>at+z-x)return false;if(e8_eq(c,q,"FS  ",4)){pb=1;if(n!=4 || !(rate=pm_be32(c->b+x)) || rate>24576000 || (rate&7))return false;}else if(e8_eq(c,q,"CHNL",4)){pb=2;if(n<2 || !(channels=pm_be16(c->b+x)) || channels>32 || n!=2U+4U*channels)return false;}else if(e8_eq(c,q,"CMPR",4)){pb=4;if(n<5 || !e8_eq(c,x,"DSD ",4) || n!=5U+c->b[x+4])return false;}else return false;if(ps&pb)return false;ps|=pb;q=x+(size_t)n;if(n&1){if(q>=at+z || c->b[q])return false;++q;}}if(q!=at+z || ps!=7)return false;}
  else if(e8_eq(c,p,"DSD ",4)){bit=4;data=z;if(!z)return false;}
  else { return false; } if(seen&bit)return false;seen|=bit;xx_rt_snprintf(name,sizeof(name),"%.4s.bin",c->b+p);if(!e8_add(c,name,p,12U+(size_t)z))return false;p=at+(size_t)z;if(z&1){if(!e8_zero(c,p,1))return false;++p;}
 }return p==c->n && seen==7 && channels && data%channels==0;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_audio_dff_init(xx_audio_dff*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_DFF,"bin");}}
xx_audio_dff*xx_audio_dff_create(xx_io_device*d,int64_t b) {xx_audio_dff*r=(xx_audio_dff*)xx_mem_alloc(sizeof(*r));if(r)xx_audio_dff_init(r,d,b);return r;}
void xx_audio_dff_destroy(xx_audio_dff*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_dff_free(xx_audio_dff*r) {if(r){xx_audio_dff_destroy(r);xx_mem_free(r);}}
bool xx_audio_dff_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_audio_dff_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
