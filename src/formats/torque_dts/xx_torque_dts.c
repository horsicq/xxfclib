/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/TorqueGameEngines/Torque3D/development/Engine/source/ts/tsShape.cpp
 * Torque DTS24 static node-only shapes with1-256 nodes, one subshape, default transforms/names and no meshes/objects/materials/sequences. Parses all three split scalar buffers and17 synchronized guards, hierarchy and bounded name records. Exports nodes/default transforms/names; geometry, animation, newer encrypted versions and rendering unsupported.
 */
#include "xxfclib/formats/torque_dts/xx_torque_dts.h"
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
static bool floats(Abstractformat *f,uint64_t at,uint64_t count,bool be,xx_pd_struct *pd) { uint8_t p[4]; uint64_t i; for(i=0;i<count;++i) if(stop(pd) || !pm_read(f,(int64_t)(at+i*4),p,4) || !finite32(p,be)) return false; return true; }
static bool cstring(Abstractformat *f,uint64_t *at,uint64_t end,unsigned maximum,bool empty,xx_pd_struct *pd) { uint8_t c; unsigned i; for(i=0;i<maximum;++i) { if(!take(f,at,end,&c,1,pd)) return false; if(!c) return empty || i!=0; } return false; }
typedef struct rg { uint64_t at,n; } rg;
static bool zeros(Abstractformat *f,uint64_t at,uint64_t n,xx_pd_struct *pd) { size_t capacity=xx_get_file_buffer_size(); uint8_t *b=NULL; bool buffer_result=false;
    if(n) { if(capacity>n) capacity=(size_t)n; b=(uint8_t *)xx_mem_alloc(capacity); if(!b) { buffer_result = (false); goto buffer_done; } } size_t i; while(n) { size_t part=n>capacity ? capacity:(size_t)n; if(stop(pd) || !pm_read(f,(int64_t)at,b,part)) { buffer_result = (false); goto buffer_done; } for(i=0;i<part;++i) if(b[i]) { buffer_result = (false); goto buffer_done; } at+=part; n-=part; } { buffer_result = (true); goto buffer_done; } 
buffer_done:
    xx_mem_free(b);
    return buffer_result;
}
static bool dts_guard(Abstractformat *f,uint64_t *a,uint64_t e32,uint64_t *b,uint64_t e16,uint64_t *c,uint64_t e8,unsigned value,xx_pd_struct *pd) { uint8_t p[4],q[2],r; return take(f,a,e32,p,4,pd) && xx_data_get_u32(p, 4, 0, false)==value && take(f,b,e16,q,2,pd) && xx_data_get_u16(q, 2, 0, false)==value && take(f,c,e8,&r,1,pd) && r==value; }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[16],counts[76],p[20],q[12],footer[9]; uint64_t e32,e16,e8,a=16,b,c,total,nodes_at,rotations,translations,names; uint32_t size,start16,start8,n,nn,i,j; unsigned guard=0;
    if(!pm_read(f,0,h,16) || (xx_data_get_u32(h, 4, 0, false)&0xffffU)!=24 || !(size=xx_data_get_u32(h+4, 4, 0, false)) || size>4194304 || (start16=xx_data_get_u32(h+8, 4, 0, false))<19 || (start8=xx_data_get_u32(h+12, 4, 0, false))<start16 || start8>size) { return false; } total=16+(uint64_t)size*4+9; if(total>(uint64_t)pm_available(f)) return false; e32=16+(uint64_t)start16*4; b=e32; e16=16+(uint64_t)start8*4; c=e16; e8=16+(uint64_t)size*4;
    if(!take(f,&a,e32,counts,76,pd) || !(n=xx_data_get_u32(counts, 4, 0, false)) || n>256 || xx_data_get_u32(counts+12, 4, 0, false)!=1 || !(nn=xx_data_get_u32(counts+64, 4, 0, false)) || nn>1024) return false;
    for(i=1;i<16;++i) if(i!=3 && xx_data_get_u32(counts+i*4, 4, 0, false)) return false;
    if(!dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd) || !span(a,44,e32) || !floats(f,a,11,false,pd)) { return false; } a+=44; if(!dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd)) return false;
    nodes_at=a; for(i=0;i<n;++i) { int32_t parent; if(!take(f,&a,e32,p,20,pd) || xx_data_get_u32(p, 4, 0, false)>=nn || (parent=(int32_t)xx_data_get_u32(p+4, 4, 0, false))>=(int32_t)i || parent<-1 || xx_data_get_u32(p+8, 4, 0, false)!=UINT32_MAX) return false; for(j=12;j<20;j+=4) { uint32_t link=xx_data_get_u32(p+j, 4, 0, false); if(link!=UINT32_MAX && (link<=i || link>=n || !pm_read(f,(int64_t)(nodes_at+(uint64_t)link*20+4),q,4) || (int32_t)xx_data_get_u32(q, 4, 0, false)!=(j==12 ? (int32_t)i:parent))) return false; } }
    for(i=2;i<=5;++i) if(!dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd)) return false;
    if(!take(f,&a,e32,q,12,pd) || xx_data_get_u32(q, 4, 0, false) || xx_data_get_u32(q+4, 4, 0, false) || xx_data_get_u32(q+8, 4, 0, false) || !dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd) || !take(f,&a,e32,q,12,pd) || xx_data_get_u32(q, 4, 0, false)!=n || xx_data_get_u32(q+4, 4, 0, false) || xx_data_get_u32(q+8, 4, 0, false) || !dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd)) return false;
    rotations=b; if(!span(b,(uint64_t)n*8,e16)) return false; b+=(uint64_t)n*8; translations=a; if(!span(a,(uint64_t)n*12,e32) || !floats(f,a,(uint64_t)n*3,false,pd)) return false; a+=(uint64_t)n*12;
    for(i=8;i<=15;++i) if(!dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd)) return false;
    names=c; for(i=0;i<nn;++i) if(!cstring(f,&c,e8,4096,false,pd)) return false;
    if(!emit(f,s,"nodes.bin",nodes_at,(uint64_t)n*20,total) || !emit(f,s,"default-rotations.bin",rotations,(uint64_t)n*8,total) || !emit(f,s,"default-translations.bin",translations,(uint64_t)n*12,total) || !emit(f,s,"names.bin",names,c-names,total) || !dts_guard(f,&a,e32,&b,e16,&c,e8,guard++,pd) || a!=e32 || e16-b>3 || e8-c>3 || !zeros(f,b,e16-b,pd) || !zeros(f,c,e8-c,pd) || !pm_read(f,(int64_t)e8,footer,9) || xx_data_get_u32(footer, 4, 0, false) || footer[4]!=1 || xx_data_get_u32(footer+5, 4, 0, false)) { return false; } s->size=(int64_t)total; return true;

}

void xx_torque_dts_init(xx_torque_dts *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TORQUE_DTS,"dts"); } }
xx_torque_dts *xx_torque_dts_create(xx_io_device *d,int64_t b) { xx_torque_dts *r=(xx_torque_dts *)xx_mem_alloc(sizeof(*r)); if(r) xx_torque_dts_init(r,d,b); return r; }
void xx_torque_dts_destroy(xx_torque_dts *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_torque_dts_free(xx_torque_dts *r) { if(r) { xx_torque_dts_destroy(r); xx_mem_free(r); } }
bool xx_torque_dts_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_torque_dts_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
