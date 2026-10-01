/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/hatari/hatari/blob/main/src/floppies/msa.c
 * MSA 1-36 sectors per track, one/two sides, tracks 0-85; validates and decompresses exact E5 RLE tracks; exports decoded track bytes.
 */
#include "xxfclib/formats/atari_st_msa/xx_atari_st_msa.h"
#include "../vice_x64/xx_ninth_retro.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t sectors,heads,start,end,at=10,t,h,raw; char name[48];
 if(!nh_range(b,0,10) || pm_be16(b->p)!=0x0e0f || !(sectors=pm_be16(b->p+2)) || sectors>36 || pm_be16(b->p+4)>1 || (start=pm_be16(b->p+6))>(end=pm_be16(b->p+8)) || end>85 || !nh_emit(f,s,b,"disk-descriptor.bin",0,10)) return false;
 heads=pm_be16(b->p+4)+1U; raw=sectors*512U;
 for(t=start;t<=end;++t) for(h=0;h<heads;++h) {
  uint32_t packed,p=0,w=0; uint8_t *data;
  if(!nh_range(b,at,2) || !(packed=pm_be16(b->p+at)) || packed>raw || !nh_range(b,at+2,packed)) return false; at+=2;
  xx_rt_snprintf(name,sizeof(name),"track-%u-side-%u.bin",t,h);
  if(packed==raw) { if(!nh_emit(f,s,b,name,at,raw)) return false; }
  else {
   data=(uint8_t *)xx_mem_alloc(raw); if(!data) return false;
   while(p<packed) { uint32_t count=1; uint8_t v=b->p[at+p++]; if(!nh_poll(b)) { xx_mem_free(data); return false; }
    if(v==0xe5) { if(packed-p<3) { xx_mem_free(data); return false; } v=b->p[at+p]; count=pm_be16(b->p+at+p+1); p+=3; }
    if(!count || count>raw-w) { xx_mem_free(data); return false; } xx_rt_memset(data+w,v,count); w+=count;
   }
   if(w!=raw) { xx_mem_free(data); return false; } if(!nh_memory(f,s,b,name,at,data,raw)) return false;
  }
  at+=packed;
 }
 s->size=at; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_atari_st_msa_init(xx_atari_st_msa *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ATARI_ST_MSA,"msa"); } }
xx_atari_st_msa *xx_atari_st_msa_create(xx_io_device *d,int64_t b) { xx_atari_st_msa *r=(xx_atari_st_msa *)xx_mem_alloc(sizeof(*r)); if(r) xx_atari_st_msa_init(r,d,b); return r; }
void xx_atari_st_msa_destroy(xx_atari_st_msa *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_atari_st_msa_free(xx_atari_st_msa *r) { if(r) { xx_atari_st_msa_destroy(r); xx_mem_free(r); } }
bool xx_atari_st_msa_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_atari_st_msa_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
