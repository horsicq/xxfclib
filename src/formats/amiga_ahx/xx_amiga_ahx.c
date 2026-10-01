/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://github.com/pete-gordon/planet-hively/blob/master/hvl_replay.c
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/amiga_ahx/xx_amiga_ahx.h"
#include "../atari_7800_a78/xx_eleventh_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,er_blob *b) {
 uint32_t a=14,pos,tracks,rows,instruments,subs,i,j,name_start;const uint8_t *p=b->p;
 if(b->n<14 || xx_rt_memcmp(p,"THX",3) || p[3]>1 || (p[6]&0x10) || (p[3]==0 && (p[6]&0x60))) return false;
 pos=pm_be16(p+6)&4095U;tracks=p[11];rows=p[10];instruments=p[12];subs=p[13];if(!pos || pos>1000 || pm_be16(p+8)>=pos || !rows || rows>64 || instruments>63) return false;
 if(!er_emit(f,s,b,"module-descriptor.bin",0,14) || !er_range(b,a,subs*2)) return false;
 for(i=0;i<subs;++i) if(pm_be16(p+a+i*2)>=pos) return false;if(subs && !er_emit(f,s,b,"subsong-positions.bin",a,subs*2)) return false;a+=subs*2;
 if(!er_range(b,a,pos*8)) return false;for(i=0;i<pos*8;i+=2) if(p[a+i]>tracks) return false;if(!er_emit(f,s,b,"order-positions.bin",a,pos*8)) return false;a+=pos*8;
 {uint32_t size=(tracks+((p[6]&128U) ? 0U:1U))*rows*3U;if(!er_range(b,a,size)) return false;
  for(i=0;i<size;i+=3) {unsigned note=p[a+i]>>2,ins=((p[a+i]&3U)<<4)|(p[a+i+1]>>4);if(note>60 || ins>instruments) return false;}
  if(size && !er_emit(f,s,b,"encoded-tracks.bin",a,size)) return false;a+=size;}
 for(i=0;i<instruments;++i) {uint32_t size;char label[40];if(!er_poll(b) || !er_range(b,a,22) || p[a]>64 || (p[a+1]&7U)>5 || p[a+3]>64 || p[a+5]>64 || p[a+8]>64) return false;size=22U+(uint32_t)p[a+21]*4U;if(!er_range(b,a,size)) return false;
  for(j=22;j<size;j+=4) if((p[a+j+1]&63U)>60) return false;xx_rt_snprintf(label,sizeof(label),"instrument-%02u.bin",i+1);if(!er_emit(f,s,b,label,a,size)) return false;a+=size;}
 name_start=a;if((a&65535U)!=pm_be16(p+4)) return false;
 for(i=0;i<=instruments;++i) {uint32_t n=0;while(a<b->n && p[a]) {if(++n>255) return false;++a;}if(a==b->n) return false;++a;}
 if(a!=b->n || !er_emit(f,s,b,"module-and-instrument-names.bin",name_start,a-name_start)) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { er_blob b;bool ok;if(!er_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_amiga_ahx_init(xx_amiga_ahx *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AMIGA_AHX,"amiga_ahx");} }
xx_amiga_ahx *xx_amiga_ahx_create(xx_io_device *d,int64_t b) { xx_amiga_ahx *r=(xx_amiga_ahx *)xx_mem_alloc(sizeof(*r));if(r) xx_amiga_ahx_init(r,d,b);return r; }
void xx_amiga_ahx_destroy(xx_amiga_ahx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_amiga_ahx_free(xx_amiga_ahx *r) { if(r) {xx_amiga_ahx_destroy(r);xx_mem_free(r);} }
bool xx_amiga_ahx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_amiga_ahx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
