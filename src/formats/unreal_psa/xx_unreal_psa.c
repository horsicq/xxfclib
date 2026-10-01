/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/DarklightGames/psk_psa_py/master/src/psk_psa_py/psa/writer.py
 * Classic ActorX PSA with BONENAMES/ANIMINFO/ANIMKEYS, up to256 bones,1024 sequences and262144 uncompressed transform keys. Checks hierarchy, finite transforms/rates, sequence frame/bone counts and key bounds. Exports encoded bone/sequence/key tables; scale-key extensions, compression and animation playback unsupported.
 */
#include "xxfclib/formats/unreal_psa/xx_unreal_psa.h"
#include "../xx_payload_members.h"

static uint32_t g32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool take(Abstractformat *f,uint64_t *at,uint64_t end,void *p,size_t n,xx_pd_struct *pd) { if(stop(pd) || !span(*at,n,end) || !pm_read(f,(int64_t)*at,p,n)) return false; *at+=n; return true; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool finite32(const uint8_t *p,bool be) { return (g32(p,be)&0x7f800000U)!=0x7f800000U; }
typedef struct rg { uint64_t at,n; } rg;
static bool psx_chunk(Abstractformat *f,uint64_t *at,uint64_t total,const char *name,uint32_t stride,uint32_t maximum,uint32_t *count,xx_pd_struct *pd) { uint8_t h[32]; size_t n=xx_rt_strlen(name); if(!take(f,at,total,h,32,pd) || xx_rt_memcmp(h,name,n) || !xx_rt_memchr(h+n,0,20-n) || pm_le32(h+20)!=1999801 || pm_le32(h+24)!=stride || (*count=pm_le32(h+28))>maximum || !span(*at,(uint64_t)*count*stride,total)) return false; return true; }
static bool psx_bones(Abstractformat *f,uint64_t at,uint32_t count,xx_pd_struct *pd) { uint8_t b[120]; uint32_t i,j,children[256]={0},declared[256]; if(!count || count>256) return false; for(i=0;i<count;++i) { int32_t parent; if(stop(pd) || !pm_read(f,(int64_t)(at+(uint64_t)i*120),b,120) || !xx_rt_memchr(b,0,64) || (declared[i]=pm_le32(b+68))>count) return false; parent=(int32_t)pm_le32(b+72); if(!i) { if(parent!=0 && parent!=-1) return false; } else { if(parent<0 || (uint32_t)parent>=i) return false; ++children[parent]; } for(j=76;j<120;j+=4) if(!finite32(b+j,false)) return false; }
    for(i=0;i<count;++i) if(children[i]!=declared[i]) return false; return true; }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint64_t at=0,total=(uint64_t)pm_available(f),bones,info,keys; uint32_t n,nb,ni,nk,i,j; uint8_t p[168];
    if(!psx_chunk(f,&at,total,"ANIMHEAD",0,0,&n,pd) || !psx_chunk(f,&at,total,"BONENAMES",120,256,&nb,pd) || !nb) return false; bones=at; at+=(uint64_t)nb*120;
    if(!psx_chunk(f,&at,total,"ANIMINFO",168,1024,&ni,pd) || !ni) return false; info=at; at+=(uint64_t)ni*168;
    if(!psx_chunk(f,&at,total,"ANIMKEYS",32,262144,&nk,pd) || !nk) return false; keys=at; at+=(uint64_t)nk*32;
    if(at!=total || !psx_bones(f,bones,nb,pd)) return false;
    for(i=0;i<ni;++i) { uint32_t frames,first; if(stop(pd) || !pm_read(f,(int64_t)(info+(uint64_t)i*168),p,168) || !xx_rt_memchr(p,0,64) || !xx_rt_memchr(p+64,0,64) || pm_le32(p+128)!=nb || pm_le32(p+132)>1 || pm_le32(p+136) || pm_le32(p+156) || !finite32(p+144,false) || !finite32(p+148,false) || !finite32(p+152,false) || (pm_le32(p+152)&0x80000000U) || !pm_le32(p+152) || !(frames=pm_le32(p+164)) || !span((uint64_t)(first=pm_le32(p+160))*nb,(uint64_t)frames*nb,nk)) return false; }
    for(i=0;i<nk;++i) { if(stop(pd) || !pm_read(f,(int64_t)(keys+(uint64_t)i*32),p,32)) return false; for(j=0;j<32;j+=4) if(!finite32(p+j,false)) return false; if(pm_le32(p+28)&0x80000000U) return false; }
    if(!emit(f,s,"bones.bin",bones,(uint64_t)nb*120,total) || !emit(f,s,"sequences.bin",info,(uint64_t)ni*168,total) || !emit(f,s,"keys.bin",keys,(uint64_t)nk*32,total)) return false; s->size=(int64_t)total; return true;

}

void xx_unreal_psa_init(xx_unreal_psa *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_UNREAL_PSA,"psa"); } }
xx_unreal_psa *xx_unreal_psa_create(xx_io_device *d,int64_t b) { xx_unreal_psa *r=(xx_unreal_psa *)xx_mem_alloc(sizeof(*r)); if(r) xx_unreal_psa_init(r,d,b); return r; }
void xx_unreal_psa_destroy(xx_unreal_psa *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_unreal_psa_free(xx_unreal_psa *r) { if(r) { xx_unreal_psa_destroy(r); xx_mem_free(r); } }
bool xx_unreal_psa_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_unreal_psa_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
