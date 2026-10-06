/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/MS3D/MS3DLoader.cpp
 * MilkShape3D version4 base static models without joints or optional extension blocks, up to8192 vertices/16384 triangles/128 groups/materials. Validates finite geometry/materials and vertex/triangle/group/material references. Exports geometry/group/material tables; skeletons/extra comments/weights, texture loading and rendering unsupported.
 */
#include "xxfclib/formats/milkshape_ms3d/xx_milkshape_ms3d.h"
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
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[14],p[361],used[2048]={0}; uint64_t at=14,total=(uint64_t)pm_available(f),start,triangle_at; uint32_t verts,tris,groups,mats,i,j; uint8_t material[128]; char label[40];
    if(!pm_read(f,0,h,14) || xx_rt_memcmp(h,"MS3D000000",10) || pm_le32(h+10)!=4 || !take(f,&at,total,p,2,pd) || !(verts=pm_le16(p)) || verts>8192) return false;
    start=at; for(i=0;i<verts;++i) { if(!take(f,&at,total,p,15,pd) || p[13]!=255) return false; for(j=1;j<13;j+=4) if(!finite32(p+j,false)) return false; }
    if(!emit(f,s,"vertices.bin",start,at-start,total) || !take(f,&at,total,p,2,pd) || !(tris=pm_le16(p)) || tris>16384) return false;
    start=triangle_at=at; for(i=0;i<tris;++i) { if(!take(f,&at,total,p,70,pd)) return false; for(j=0;j<3;++j) if(pm_le16(p+2+j*2)>=verts) return false; for(j=8;j<68;j+=4) if(!finite32(p+j,false)) return false; }
    if(!emit(f,s,"triangles.bin",start,at-start,total) || !take(f,&at,total,p,2,pd) || !(groups=pm_le16(p)) || groups>128) return false;
    for(i=0;i<groups;++i) { uint32_t n; start=at; if(!take(f,&at,total,p,35,pd) || !xx_rt_memchr(p+1,0,32) || !(n=pm_le16(p+33))) return false;
      for(j=0;j<n;++j) { uint32_t index; uint8_t group; if(!take(f,&at,total,p,2,pd) || (index=pm_le16(p))>=tris || (used[index/8]&(1U<<(index%8))) || !pm_read(f,(int64_t)(triangle_at+index*70+69),&group,1) || group!=i) return false; used[index/8]|=(uint8_t)(1U<<(index%8)); }
      if(!take(f,&at,total,&material[i],1,pd)) { return false; } xx_rt_snprintf(label,sizeof(label),"group-%u.bin",i); if(!emit(f,s,label,start,at-start,total)) return false; }
    for(i=0;i<tris;++i) if(!(used[i/8]&(1U<<(i%8)))) return false;
    if(!take(f,&at,total,p,2,pd) || (mats=pm_le16(p))>128) { return false; } for(i=0;i<groups;++i) if(material[i]!=255 && material[i]>=mats) return false;
    for(i=0;i<mats;++i) { start=at; if(!take(f,&at,total,p,361,pd) || !xx_rt_memchr(p,0,32) || !xx_rt_memchr(p+105,0,128) || !xx_rt_memchr(p+233,0,128)) return false; for(j=32;j<104;j+=4) if(!finite32(p+j,false)) return false;
      { uint32_t shine=pm_le32(p+96),alpha=pm_le32(p+100); if(((shine&0x80000000U) && (shine&0x7fffffffU)) || (shine&0x7fffffffU)>0x43000000U || ((alpha&0x80000000U) && (alpha&0x7fffffffU)) || (alpha&0x7fffffffU)>0x3f800000U) return false; }
      xx_rt_snprintf(label,sizeof(label),"material-%u.bin",i); if(!emit(f,s,label,start,361,total)) return false; }
    if(!take(f,&at,total,p,14,pd) || !finite32(p,false) || !finite32(p+4,false) || (pm_le32(p)&0x80000000U) || !pm_le32(p) || pm_le16(p+12) || at!=total) { return false; } s->size=(int64_t)at; return true;

}

void xx_milkshape_ms3d_init(xx_milkshape_ms3d *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MILKSHAPE_MS3D,"ms3d"); } }
xx_milkshape_ms3d *xx_milkshape_ms3d_create(xx_io_device *d,int64_t b) { xx_milkshape_ms3d *r=(xx_milkshape_ms3d *)xx_mem_alloc(sizeof(*r)); if(r) xx_milkshape_ms3d_init(r,d,b); return r; }
void xx_milkshape_ms3d_destroy(xx_milkshape_ms3d *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_milkshape_ms3d_free(xx_milkshape_ms3d *r) { if(r) { xx_milkshape_ms3d_destroy(r); xx_mem_free(r); } }
bool xx_milkshape_ms3d_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_milkshape_ms3d_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
