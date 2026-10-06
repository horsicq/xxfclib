/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://github.com/stahta01/xroar/blob/master/src/tape_cas.c
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/dragon_cas/xx_dragon_cas.h"
#include "../atari_7800_a78/xx_eleventh_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,er_blob *b) {
 uint32_t a=0,count=0,files=0,data=0,load=0,total=0;bool active=false,binary=false;const uint8_t *p=b->p;
 while(a<b->n) {uint32_t leader=a,z,i,sum,type;char name[16];if(!er_poll(b) || ++count>4096) return false;
  while(a<b->n && p[a]==0x55) {if(a-leader>=4096) return false;++a;}if(a==b->n) {if(active || !files) return false;break;}
  if(a==leader || !er_range(b,a,3) || p[a]!=0x3c) { return false; } type=p[a+1];z=p[a+2];a+=3;if(!er_range(b,a,z+2) || p[a+z+1]!=0x55) return false;
  sum=type+z;for(i=0;i<z;++i) sum+=p[a+i];if((sum&255U)!=p[a+z]) return false;
  if(type==0) {if(active || z!=15 || !er_name(p+a,8,name,false) || p[a+8]>2 || (p[a+9]!=0 && p[a+9]!=255) || (p[a+10]!=0 && p[a+10]!=255)) return false;
   load=pm_be16(p+a+13);binary=p[a+8]==2 && p[a+9]==0;total=data=0;active=true;if(!er_emit(f,s,b,"filename-descriptor.bin",a,z)) return false;
  } else if(type==1) {if(!active || !z || ++data>1024 || (binary && z>65536U-load-total)) return false;total+=z;if(!er_emit(f,s,b,"cassette-data.bin",a,z)) return false;
  } else if(type==255) {if(!active || z || !data) return false;active=false;++files;
  } else return false;a+=z+1;
 }s->size=b->n;return !active && files!=0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { er_blob b;bool ok;if(!er_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_dragon_cas_init(xx_dragon_cas *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_DRAGON_CAS,"dragon_cas");} }
xx_dragon_cas *xx_dragon_cas_create(xx_io_device *d,int64_t b) { xx_dragon_cas *r=(xx_dragon_cas *)xx_mem_alloc(sizeof(*r));if(r) xx_dragon_cas_init(r,d,b);return r; }
void xx_dragon_cas_destroy(xx_dragon_cas *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dragon_cas_free(xx_dragon_cas *r) { if(r) {xx_dragon_cas_destroy(r);xx_mem_free(r);} }
bool xx_dragon_cas_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dragon_cas_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
