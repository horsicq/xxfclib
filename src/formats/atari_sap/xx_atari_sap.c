/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/atari_sap/xx_atari_sap.h"
#include "../snes_spc/xx_tenth_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,th_blob *b) {
 const uint8_t *p=b->p;uint32_t a=5,songs=1,first=0,init=0,player=0,count=0;bool type=false,haveinit=false,haveplayer=false,mappedinit=false,mappedplayer=false;
 if(b->n<11 || xx_rt_memcmp(p,"SAP\r\n",5)) return false;
 while(th_range(b,a,2) && pm_le16(p+a)!=65535) {
  uint32_t line=a,key,value,z;if(a>65536 || !th_poll(b)) return false;while(a<b->n && p[a]!=13) {if(p[a]<32 || p[a]>126 || a-line>=255) return false;++a;}if(!th_range(b,a,2) || p[a+1]!=10) return false;
  key=line;while(key<a && p[key]!=' ') ++key;value=key<a ? key+1:key;z=a-value;
  if(th_match(p+line,key-line,"TYPE")) {if(type || z!=1 || p[value]!='B') return false;type=true;}
  else if(th_match(p+line,key-line,"INIT")) {if(haveinit || !th_hex16(p+value,z,&init)) return false;haveinit=true;}
  else if(th_match(p+line,key-line,"PLAYER")) {if(haveplayer || !th_hex16(p+value,z,&player)) return false;haveplayer=true;}
  else if(th_match(p+line,key-line,"SONGS")) {if(!th_decimal(p+value,z,32,&songs) || !songs) return false;}
  else if(th_match(p+line,key-line,"DEFSONG")) {if(!th_decimal(p+value,z,31,&first)) return false;}
  else if(th_match(p+line,key-line,"FASTPLAY")) {uint32_t speed;if(!th_decimal(p+value,z,32767,&speed) || !speed) return false;}
  a+=2;
 }
 if(!type || !haveinit || !haveplayer || first>=songs || !th_range(b,a,2) || pm_le16(p+a)!=65535 || !th_emit(f,s,b,"music-descriptor.sap",0,a+2)) return false;a+=2;
 while(a<b->n) {
  uint32_t start,end,z;char name[48];if(!th_poll(b) || ++count>64 || !th_range(b,a,4)) return false;
  if(pm_le16(p+a)==65535) {a+=2;if(!th_range(b,a,4)) return false;}
  start=pm_le16(p+a);end=pm_le16(p+a+2);if(end<start) return false;z=end-start+1;if(!th_range(b,a+4,z)) return false;
  if(init>=start && init<=end) mappedinit=true;if(player>=start && player<=end) mappedplayer=true;
  xx_rt_snprintf(name,sizeof(name),"segment-%u.atari",count-1);if(!th_emit(f,s,b,name,a,4+z)) return false;a+=4+z;
 }
 if(!count || !mappedinit || !mappedplayer) return false;s->size=b->n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 th_blob b;bool ok;if(!th_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_atari_sap_init(xx_atari_sap *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ATARI_SAP,"atari_sap"); } }
xx_atari_sap *xx_atari_sap_create(xx_io_device *d,int64_t b) { xx_atari_sap *r=(xx_atari_sap *)xx_mem_alloc(sizeof(*r));if(r) xx_atari_sap_init(r,d,b);return r; }
void xx_atari_sap_destroy(xx_atari_sap *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_atari_sap_free(xx_atari_sap *r) { if(r) {xx_atari_sap_destroy(r);xx_mem_free(r);} }
bool xx_atari_sap_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_atari_sap_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
