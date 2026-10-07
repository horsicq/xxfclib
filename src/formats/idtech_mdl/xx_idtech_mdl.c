/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/modelgen.h
 * Quake alias model IDPO version6, single skins and single animation frames with up to64 skins/4096 vertices/65536 triangles/1024 frames. Validates geometry indices, finite header/texture coordinates and normal indices. Exports skin planes, texcoords, triangles and encoded frame data; grouped skins/frames, rendering and external palettes unsupported.
 */
#include "xxfclib/formats/idtech_mdl/xx_idtech_mdl.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
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

    uint8_t h[84],e[28],p[4]; uint32_t skins,w,height,verts,tris,frames,i,j; uint64_t at=84,total=(uint64_t)pm_available(f),start,n; char label[40];
    if(!pm_read(f,0,h,84) || xx_rt_memcmp(h,"IDPO",4) || xx_data_get_u32(h+4, 4, 0, false)!=6) return false;
    for(i=8;i<48;i+=4) { if(!finite32(h+i,false)) return false; } if(!finite32(h+80,false)) return false;
    skins=xx_data_get_u32(h+48, 4, 0, false); w=xx_data_get_u32(h+52, 4, 0, false); height=xx_data_get_u32(h+56, 4, 0, false); verts=xx_data_get_u32(h+60, 4, 0, false); tris=xx_data_get_u32(h+64, 4, 0, false); frames=xx_data_get_u32(h+68, 4, 0, false);
    if(!skins || skins>64 || !w || w>4096 || !height || height>4096 || !verts || verts>4096 || !tris || tris>65536 || !frames || frames>1024 || xx_data_get_u32(h+72, 4, 0, false)>1) return false;
    for(i=0;i<skins;++i) { if(!take(f,&at,total,p,4,pd) || xx_data_get_u32(p, 4, 0, false)) return false; n=(uint64_t)w*height; xx_rt_snprintf(label,sizeof(label),"skin-%u.indices",i); if(!emit(f,s,label,at,n,total)) return false; at+=n; }
    start=at; for(i=0;i<verts;++i) { if(!take(f,&at,total,e,12,pd) || (xx_data_get_u32(e, 4, 0, false)!=0 && xx_data_get_u32(e, 4, 0, false)!=32) || xx_data_get_u32(e+4, 4, 0, false)>=w || xx_data_get_u32(e+8, 4, 0, false)>=height) return false; }
    if(!emit(f,s,"texture-coordinates.bin",start,at-start,total)) return false;
    start=at; for(i=0;i<tris;++i) { if(!take(f,&at,total,e,16,pd) || (xx_data_get_u32(e, 4, 0, false)!=0 && xx_data_get_u32(e, 4, 0, false)!=1 && xx_data_get_u32(e, 4, 0, false)!=16)) return false; for(j=1;j<4;++j) if(xx_data_get_u32(e+j*4, 4, 0, false)>=verts) return false; }
    if(!emit(f,s,"triangles.bin",start,at-start,total)) return false;
    for(i=0;i<frames;++i) { start=at; if(!take(f,&at,total,e,28,pd) || xx_data_get_u32(e, 4, 0, false) || !xx_rt_memchr(e+12,0,16)) return false;
      for(j=0;j<verts;++j) { if(!take(f,&at,total,p,4,pd) || p[3]>=162) return false; } xx_rt_snprintf(label,sizeof(label),"frame-%u.bin",i); if(!emit(f,s,label,start+4,at-start-4,total)) return false; }
    s->size=(int64_t)at; return true;

}

void xx_idtech_mdl_init(xx_idtech_mdl *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IDTECH_MDL,"mdl"); } }
xx_idtech_mdl *xx_idtech_mdl_create(xx_io_device *d,int64_t b) { xx_idtech_mdl *r=(xx_idtech_mdl *)xx_mem_alloc(sizeof(*r)); if(r) xx_idtech_mdl_init(r,d,b); return r; }
void xx_idtech_mdl_destroy(xx_idtech_mdl *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_idtech_mdl_free(xx_idtech_mdl *r) { if(r) { xx_idtech_mdl_destroy(r); xx_mem_free(r); } }
bool xx_idtech_mdl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_idtech_mdl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
