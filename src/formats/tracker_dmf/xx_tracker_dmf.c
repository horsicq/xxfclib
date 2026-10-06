/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/tracker_dmf/xx_tracker_dmf.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef TRACKER_DMF
#define XX_FILE_TYPE_TRACKER_DMF ((xx_file_type_t)807)
#endif
static bool e8_parse(e8_blob*c) {
 size_t p=66,seq=0,seqz=0,si=0,siz=0,sd=0,sdz=0;unsigned seen=0,patterns=0,channels=0,i;
 if(!e8_eq(c,0,"DDMF",4) || !e8_range(c,0,66) || c->b[4]!=8 || !e8_add(c,"header.bin",0,66))return false;
 while(p<c->n) {uint32_t z;size_t at=p+8;unsigned bit=0;char name[24];if(e8_eq(c,p,"ENDE",4)){if(p+4!=c->n || !e8_add(c,"ENDE.bin",p,4))return false;p+=4;break;}if(!e8_range(c,p,8) || (z=pm_le32(c->b+p+4))>c->n-at)return false;
  if(e8_eq(c,p,"SEQU",4)){bit=1;seq=at;seqz=z;}
  else if(e8_eq(c,p,"PATT",4)){size_t q=at+3;if(z<3 || !(patterns=pm_le16(c->b+at)) || patterns>1024 || !(channels=c->b[at+2]) || channels>32)return false;bit=2;for(i=0;i<patterns;++i){uint32_t n;unsigned rows;if(q+8>at+z || !e8_range(c,q,8) || c->b[q]==0 || c->b[q]>channels || !(rows=pm_le16(c->b+q+2)) || rows>1024 || (n=pm_le32(c->b+q+4))>at+z-q-8)return false;q+=8U+n;}if(q!=at+z)return false;}
  else if(e8_eq(c,p,"SMPI",4)){bit=4;si=at;siz=z;}
  else if(e8_eq(c,p,"SMPD",4)){bit=8;sd=at;sdz=z;}
  else if(!e8_eq(c,p,"CMSG",4) && !e8_eq(c,p,"SETT",4))return false;
  if(bit && (seen&bit)) {return false; } seen|=bit;xx_rt_snprintf(name,sizeof(name),"%.4s.bin",c->b+p);if(!e8_add(c,name,p,8U+z))return false;p=at+z;
 }
 if(p!=c->n || seen!=15 || !e8_eq(c,c->n-4,"ENDE",4) || seqz<6 || (seqz&1) || pm_le16(c->b+seq)>pm_le16(c->b+seq+2) || pm_le16(c->b+seq+2)>=(seqz-4)/2)return false;
 for(i=4;i<seqz;i+=2)if(pm_le16(c->b+seq+i)>=patterns)return false;
 if(!siz) {return false; } {size_t q=si+1,r=sd;unsigned count=c->b[si];for(i=0;i<count;++i){unsigned len;uint32_t raw,stored;uint8_t flags;if(q>=si+siz)return false;len=c->b[q++];if(len>si+siz-q || si+siz-q-len<30)return false;q+=len;raw=pm_le32(c->b+q);flags=c->b[q+15];if(flags&~15U || pm_le32(c->b+q+4)>pm_le32(c->b+q+8) || pm_le32(c->b+q+8)>raw || ((flags&2) && (raw&1)))return false;q+=30;if(sd+sdz-r<4)return false;stored=pm_le32(c->b+r);r+=4;if(stored>sd+sdz-r || (!(flags&12) && stored!=raw))return false;r+=stored;}if(q!=si+siz || r!=sd+sdz)return false;}
 return true;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_tracker_dmf_init(xx_tracker_dmf*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_DMF,"bin");}}
xx_tracker_dmf*xx_tracker_dmf_create(xx_io_device*d,int64_t b) {xx_tracker_dmf*r=(xx_tracker_dmf*)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_dmf_init(r,d,b);return r;}
void xx_tracker_dmf_destroy(xx_tracker_dmf*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_dmf_free(xx_tracker_dmf*r) {if(r){xx_tracker_dmf_destroy(r);xx_mem_free(r);}}
bool xx_tracker_dmf_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_tracker_dmf_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
