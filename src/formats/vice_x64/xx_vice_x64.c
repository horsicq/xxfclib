/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://vice-emu.sourceforge.io/vice_17.html
 * X64 v1.02, single-sided 1541 35/40-track images; closed PRG/SEQ sector chains are assembled, directory/file cycles and shared sectors rejected. BAM free-count consistency is intentionally not inferred.
 */
#include "xxfclib/formats/vice_x64/xx_vice_x64.h"
#include "../vice_x64/xx_ninth_retro.h"
#include "xxfclib/data/xx_data.h"

static uint32_t sectors(uint32_t t) { return t<=17 ? 21U : t<=24 ? 19U : t<=30 ? 18U : 17U; }
static bool sector_at(uint32_t tracks,uint32_t t,uint32_t z,uint32_t *idx) { uint32_t i,a=0; if(!t || t>tracks || z>=sectors(t)) return false; for(i=1;i<t;++i) a+=sectors(i); *idx=a+z; return true; }
static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t tracks,total=0,i,didx,dt=18,ds=1,files=0; uint8_t used[802]={0}; char name[48];
 if(!nh_range(b,0,64) || xx_rt_memcmp(b->p,"C\x15\x41\x64",4) || b->p[4]!=1 || b->p[5]!=2 || b->p[6]!=1 || ((tracks=b->p[7])!=35 && tracks!=40) || b->p[8] || b->p[9]>1 || !nh_zero(b->p+10,22) || b->p[63]) return false;
 for(i=1;i<=tracks;++i) total+=sectors(i);
 if(!nh_range(b,64,total*256U+(b->p[9] ? total : 0)) || !sector_at(tracks,18,0,&didx)) return false;
 used[didx]=1;
 if(b->p[64+didx*256U]!=18 || b->p[65+didx*256U]!=1 || b->p[66+didx*256U]!=0x41 || !nh_emit(f,s,b,"disk-descriptor.bin",0,64)) return false;
 while(dt) {
  const uint8_t *dir;
  if(!nh_poll(b) || !sector_at(tracks,dt,ds,&didx) || used[didx]) { return false; } used[didx]=1; dir=b->p+64+didx*256U;
  for(i=0;i<8;++i) {
   const uint8_t *e=dir+i*32U; uint32_t t=e[3],z=e[4],count=0,len=0,idx; uint8_t *data;
   if(!e[2]) continue;
   if(e[2]!=0x81 && e[2]!=0x82) return false;
   if(!xx_data_get_u16(e+30, 2, 0, false) || xx_data_get_u16(e+30, 2, 0, false)>total || !t || files>=144) return false;
   data=(uint8_t *)xx_mem_alloc((size_t)total*254U); if(!data) return false;
   while(t) {
    const uint8_t *p; uint32_t n;
    if(!nh_poll(b) || !sector_at(tracks,t,z,&idx) || used[idx] || ++count>total) { xx_mem_free(data); return false; }
    used[idx]=1; p=b->p+64+idx*256U; n=p[0] ? 254U : (uint32_t)p[1]-1U;
    if(!p[0] && p[1]<2) { xx_mem_free(data); return false; }
    xx_rt_memcpy(data+len,p+2,n); len+=n; t=p[0]; z=p[1];
   }
   if(count!=xx_data_get_u16(e+30, 2, 0, false)) { xx_mem_free(data); return false; }
   xx_rt_snprintf(name,sizeof(name),"file-%u.%s",files++,e[2]==0x82 ? "prg" : "seq");
   if(!nh_memory(f,s,b,name,64+didx*256U,data,len)) return false;
  }
  dt=dir[0]; ds=dir[1]; if(!dt && ds!=255) return false;
 }
 if(!files) { return false; } s->size=64+(int64_t)total*256+(b->p[9] ? total : 0); return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_vice_x64_init(xx_vice_x64 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VICE_X64,"x64"); } }
xx_vice_x64 *xx_vice_x64_create(xx_io_device *d,int64_t b) { xx_vice_x64 *r=(xx_vice_x64 *)xx_mem_alloc(sizeof(*r)); if(r) xx_vice_x64_init(r,d,b); return r; }
void xx_vice_x64_destroy(xx_vice_x64 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_vice_x64_free(xx_vice_x64 *r) { if(r) { xx_vice_x64_destroy(r); xx_mem_free(r); } }
bool xx_vice_x64_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_vice_x64_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
