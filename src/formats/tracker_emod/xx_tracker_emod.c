/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_emod/xx_tracker_emod.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_EMOD
#define XX_FILE_TYPE_TRACKER_EMOD ((xx_file_type_t)811)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=12;unsigned samples=0,patterns=0,i,seen=0;uint64_t raw=0,patternbytes=0;
 if(!e8_eq(c,0,"FORM",4) || !e8_eq(c,8,"EMOD",4) || pm_be32(c->b+4)!=c->n-8 || !e8_add(c,"header.bin",0,12))return false;
 while(p<c->n){size_t at=p+8;uint32_t z;unsigned bit;char name[24];if(!e8_range(c,p,8) || (z=pm_be32(c->b+p+4))>c->n-at)return false;
  if(e8_eq(c,p,"EMIC",4)){size_t q=at+44;uint8_t ids[256]={0};unsigned orders;if(seen || z<47 || pm_be16(c->b+at)!=1 || c->b[at+42]<32)return false;bit=1;samples=c->b[at+43];for(i=0;i<samples;++i){uint32_t len,loop,start;if(!e8_range(c,q,34) || q+34>at+z || c->b[q+1]>64 || c->b[q+24]&~1U)return false;len=2U*pm_be16(c->b+q+2);start=2U*pm_be16(c->b+q+26);loop=2U*pm_be16(c->b+q+28);if(start>len || loop>len-start)return false;raw+=len;q+=34;}if(q+2>at+z)return false;q++;patterns=c->b[q++];if(!patterns)return false;for(i=0;i<patterns;++i){unsigned id,rows;if(q+26>at+z)return false;id=c->b[q];rows=1U+c->b[q+1];if(ids[id])return false;ids[id]=1;patternbytes+=16U*rows;q+=26;}if(q==at+z || !(orders=c->b[q++]) || q+orders!=at+z)return false;for(i=0;i<orders;++i)if(!ids[c->b[q+i]])return false;}
  else if(e8_eq(c,p,"PATT",4)){bit=2;if(!(seen&1) || z!=patternbytes)return false;}
  else if(e8_eq(c,p,"8SMP",4)){bit=4;if(!(seen&1) || z!=raw)return false;}
  else { if(e8_eq(c,p,"MDIN",4)){bit=8;if(z!=20)return false;}else return false; } if(seen&bit)return false;seen|=bit;xx_rt_snprintf(name,sizeof(name),"%.4s.bin",c->b+p);if(!e8_add(c,name,p,8U+z))return false;p=at+z;if(z&1){if(!e8_zero(c,p,1))return false;++p;}
 }return p==c->n && (seen&7)==7;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_emod_init(xx_tracker_emod*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_EMOD,"bin");}}
xx_tracker_emod*xx_tracker_emod_create(xx_io_device*d,int64_t b) {xx_tracker_emod*r=(xx_tracker_emod*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_emod_init(r,d,b);return r;}
void xx_tracker_emod_destroy(xx_tracker_emod*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_emod_free(xx_tracker_emod*r) {if(r){xx_tracker_emod_destroy(r);xx_mem_free(r);}}
bool xx_tracker_emod_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_emod_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
