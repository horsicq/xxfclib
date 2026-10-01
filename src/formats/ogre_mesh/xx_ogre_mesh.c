/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/OGRECave/ogre/master/OgreMain/src/OgreMeshSerializerImpl.cpp
 * Ogre MeshSerializer_v1.100 little-endian static meshes with nonshared FLOAT3 position-only geometry,16-bit triangle indices and finite bounds. Exports encoded submesh/geometry metadata, indices, vertex buffers and bounds; shared/multiattribute buffers, skeletons, LOD/edges, older serializer revisions and rendering unsupported.
 */
#include "xxfclib/formats/ogre_mesh/xx_ogre_mesh.h"
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
static bool floats(Abstractformat *f,uint64_t at,uint64_t count,bool be,xx_pd_struct *pd) { uint8_t p[4]; uint64_t i; for(i=0;i<count;++i) if(stop(pd) || !pm_read(f,(int64_t)(at+i*4),p,4) || !finite32(p,be)) return false; return true; }
static bool og_chunk(Abstractformat *f,uint64_t *at,uint64_t end,uint16_t *kind,uint64_t *payload_end,xx_pd_struct *pd) { uint8_t h[6]; uint32_t n; if(!take(f,at,end,h,6,pd) || (n=pm_le32(h+2))<6 || !span(*at,n-6,end)) return false; *kind=pm_le16(h); *payload_end=*at+n-6; return true; }
static bool og_line(Abstractformat *f,uint64_t *at,uint64_t end,xx_pd_struct *pd) { uint8_t c; unsigned i; for(i=0;i<4096;++i) { if(!take(f,at,end,&c,1,pd)) return false; if(c==10) return true; if(c<32) return false; } return false; }
static bool og_geometry(Abstractformat *f,pm_stream *s,uint64_t at,uint64_t end,uint64_t total,uint32_t *vertices,xx_pd_struct *pd) {
    uint8_t p[10]; uint16_t kind; uint64_t decl,elem,buffer,raw,begin=at;
    if(!take(f,&at,end,p,4,pd) || !(*vertices=pm_le32(p)) || *vertices>65536 || !og_chunk(f,&at,end,&kind,&decl,pd) || kind!=0x5100 || !og_chunk(f,&at,decl,&kind,&elem,pd) || kind!=0x5110 || elem-at!=10 || !take(f,&at,elem,p,10,pd)) return false;
    if(pm_le16(p) || pm_le16(p+2)!=2 || pm_le16(p+4)!=1 || pm_le16(p+6) || pm_le16(p+8) || at!=decl || !og_chunk(f,&at,end,&kind,&buffer,pd) || kind!=0x5200 || !take(f,&at,buffer,p,4,pd) || pm_le16(p) || pm_le16(p+2)!=12 || !og_chunk(f,&at,buffer,&kind,&raw,pd) || kind!=0x5210 || raw-at!=(uint64_t)*vertices*12 || raw!=buffer || buffer!=end || !floats(f,at,(uint64_t)*vertices*3,false,pd)) return false;
    return emit(f,s,"geometry-metadata.bin",begin,at-begin,total) && emit(f,s,"vertex-positions.bin",at,raw-at,total);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[26],p[28]; uint16_t kind; uint64_t at=26,root_end,child,start,indices,geometry; uint32_t meshes=0; bool bounds=false;
    if(!pm_read(f,0,h,26) || xx_rt_memcmp(h,"\0\x10[MeshSerializer_v1.100]\n",26) || !og_chunk(f,&at,(uint64_t)pm_available(f),&kind,&root_end,pd) || kind!=0x3000 || !take(f,&at,root_end,p,1,pd) || p[0]) return false;
    while(at<root_end) { if(!og_chunk(f,&at,root_end,&kind,&child,pd)) return false; start=at;
      if(kind==0x4000) { uint32_t n,verts,i; if(++meshes>1024 || !og_line(f,&at,child,pd) || !take(f,&at,child,p,6,pd) || p[0] || p[5] || !(n=pm_le32(p+1)) || n>196608 || n%3) return false; indices=at;
        if(!span(at,(uint64_t)n*2,child) || !emit(f,s,"submesh-metadata.bin",start,at-start,root_end)) return false; at+=(uint64_t)n*2;
        if(!og_chunk(f,&at,child,&kind,&geometry,pd) || kind!=0x5000 || !og_geometry(f,s,at,geometry,root_end,&verts,pd)) return false; at=geometry;
        for(i=0;i<n;++i) if(stop(pd) || !pm_read(f,(int64_t)(indices+i*2),p,2) || pm_le16(p)>=verts) return false; if(!emit(f,s,"triangle-indices.bin",indices,(uint64_t)n*2,root_end)) return false;
        if(at<child) { uint64_t operation; if(!og_chunk(f,&at,child,&kind,&operation,pd) || kind!=0x4010 || operation-at!=2 || !take(f,&at,operation,p,2,pd) || pm_le16(p)!=4) return false; }
        if(at!=child) return false;
      } else if(kind==0x9000) { unsigned i; if(bounds || child-at!=28 || !take(f,&at,child,p,28,pd)) return false; bounds=true; for(i=0;i<28;i+=4) if(!finite32(p+i,false)) return false; if(pm_le32(p+24)&0x80000000U || !emit(f,s,"mesh-bounds.bin",start,28,root_end)) return false; }
      else return false; at=child;
    }
    if(!meshes || !bounds) return false; s->size=(int64_t)root_end; return true;

}

void xx_ogre_mesh_init(xx_ogre_mesh *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_OGRE_MESH,"mesh"); } }
xx_ogre_mesh *xx_ogre_mesh_create(xx_io_device *d,int64_t b) { xx_ogre_mesh *r=(xx_ogre_mesh *)xx_mem_alloc(sizeof(*r)); if(r) xx_ogre_mesh_init(r,d,b); return r; }
void xx_ogre_mesh_destroy(xx_ogre_mesh *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ogre_mesh_free(xx_ogre_mesh *r) { if(r) { xx_ogre_mesh_destroy(r); xx_mem_free(r); } }
bool xx_ogre_mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ogre_mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
