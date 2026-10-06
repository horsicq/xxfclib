/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Gericom/EveryFileExplorer/master/3DS/NintendoWare/GFX/CGFX.cs
 * Little-endian 3DS CGFX version0x05000000 with DATA and optional IMAG blocks. Validates sixteen dictionary counts, dictionary node/name/data references and block extents. Exports encoded DATA/IMAG components; object/patricia-tree semantics, graphics decoding, GPU commands and rendering unsupported.
 */
#include "xxfclib/formats/nintendo_cgfx/xx_nintendo_cgfx.h"
#include "../xx_payload_members.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool take(Abstractformat *f,uint64_t *at,uint64_t end,void *p,size_t n,xx_pd_struct *pd) { if(stop(pd) || !span(*at,n,end) || !pm_read(f,(int64_t)*at,p,n)) return false; *at+=n; return true; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool cstring(Abstractformat *f,uint64_t *at,uint64_t end,unsigned maximum,bool empty,xx_pd_struct *pd) { uint8_t c; unsigned i; for(i=0;i<maximum;++i) { if(!take(f,at,end,&c,1,pd)) return false; if(!c) return empty || i!=0; } return false; }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[20],b[136],d[12],e[16]; uint32_t total,blocks,ds,i,j,k; uint64_t data=20,at,end,dicts[16]={0},sizes[16]={0};
    if(!pm_read(f,0,h,20) || xx_rt_memcmp(h,"CGFX",4) || pm_le16(h+4)!=0xfeff || pm_le16(h+6)!=20 || pm_le32(h+8)!=0x05000000U) return false;
    total=pm_le32(h+12); blocks=pm_le32(h+16); if(!blocks || blocks>2 || total>(uint64_t)pm_available(f) || !pm_read(f,20,b,136) || xx_rt_memcmp(b,"DATA",4) || (ds=pm_le32(b+4))<136 || !span(20,ds,total)) return false; end=20U+(uint64_t)ds;
    for(i=0;i<16;++i) { uint32_t count=pm_le32(b+8+i*8),rel=pm_le32(b+12+i*8); int64_t target;
      if(!count) { if(rel) return false; continue; } if(count>1024 || !rel) return false; target=32+(int64_t)i*8+(int32_t)rel;
      if(target<156 || !span((uint64_t)target,12,end) || !pm_read(f,target,d,12) || xx_rt_memcmp(d,"DICT",4) || pm_le32(d+8)!=count || pm_le32(d+4)!=12U+16U*(count+1U) || !span((uint64_t)target,pm_le32(d+4),end)) { return false; } dicts[i]=(uint64_t)target; sizes[i]=pm_le32(d+4);
      for(j=0;j<i;++j) if(overlap(dicts[i],sizes[i],dicts[j],sizes[j])) return false;
    }
    for(i=0;i<16;++i) if(sizes[i]) { uint32_t count=(uint32_t)((sizes[i]-12)/16-1);
      for(j=0;j<=count;++j) { uint64_t node=dicts[i]+12+j*16; int64_t target; uint64_t pos;
        if(stop(pd) || !pm_read(f,(int64_t)node,e,16) || pm_le16(e+4)>count || pm_le16(e+6)>count) { return false; } if(!j) { if(pm_le32(e+8) || pm_le32(e+12)) return false; continue; }
        target=(int64_t)node+8+(int32_t)pm_le32(e+8); if(!pm_le32(e+8) || target<156 || (uint64_t)target>=end) return false; pos=(uint64_t)target; if(!cstring(f,&pos,end,4096,false,pd)) return false;
        for(k=0;k<16;++k) if(overlap((uint64_t)target,pos-(uint64_t)target,dicts[k],sizes[k])) return false;
        target=(int64_t)node+12+(int32_t)pm_le32(e+12); if(!pm_le32(e+12) || target<156 || !span((uint64_t)target,8,end)) return false; for(k=0;k<16;++k) if(overlap((uint64_t)target,8,dicts[k],sizes[k])) return false;
      }
    }
    if(!emit(f,s,"graphics-data.bin",data,ds,total)) { return false; } at=end;
    if(blocks==2) { uint8_t p[8]; uint32_t n; if(!pm_read(f,(int64_t)at,p,8) || xx_rt_memcmp(p,"IMAG",4) || (n=pm_le32(p+4))<8 || !span(at,n,total) || !emit(f,s,"image-block.bin",at,n,total)) return false; at+=n; }
    if(at!=total) { return false; } s->size=total; return true;

}

void xx_nintendo_cgfx_init(xx_nintendo_cgfx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_CGFX,"bcres"); } }
xx_nintendo_cgfx *xx_nintendo_cgfx_create(xx_io_device *d,int64_t b) { xx_nintendo_cgfx *r=(xx_nintendo_cgfx *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_cgfx_init(r,d,b); return r; }
void xx_nintendo_cgfx_destroy(xx_nintendo_cgfx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_cgfx_free(xx_nintendo_cgfx *r) { if(r) { xx_nintendo_cgfx_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_cgfx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_cgfx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
