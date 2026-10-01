/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/RenolY2/j3dview/master/j3d/ttk1.py
 * J3D1 btk1 with one TTK1 texture-matrix animation section, up to256 matrices and constant/spline scalar selections. Checks all table extents, finite float values, string/texture indices and selection ranges. Exports encoded animation/table components; interpolation, rendering and other animation revisions unsupported.
 */
#include "xxfclib/formats/nintendo_j3d_btk/xx_nintendo_j3d_btk.h"
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
static bool cstring(Abstractformat *f,uint64_t *at,uint64_t end,unsigned maximum,bool empty,xx_pd_struct *pd) { uint8_t c; unsigned i; for(i=0;i<maximum;++i) { if(!take(f,at,end,&c,1,pd)) return false; if(!c) return empty || i!=0; } return false; }
typedef struct rg { uint64_t at,n; } rg;
static bool reserve(rg *r,unsigned *count,unsigned maximum,uint64_t at,uint64_t n,uint64_t lower,uint64_t end) { unsigned i; if(*count>=maximum || at<lower || !span(at,n,end)) return false; for(i=0;i<*count;++i) if(overlap(at,n,r[i].at,r[i].n)) return false; r[*count].at=at; r[*count].n=n; ++*count; return true; }
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && pm_be32(h+4)==n; }


static bool j3d_names(Abstractformat *f,uint64_t at,uint64_t end,uint32_t expected,xx_pd_struct *pd) { uint8_t h[4],p[4]; uint32_t i; if(!span(at,4+(uint64_t)expected*4,end) || !pm_read(f,(int64_t)at,h,4) || pm_be16(h)!=expected) return false; for(i=0;i<expected;++i) { uint64_t value; if(stop(pd) || !pm_read(f,(int64_t)(at+4+i*4),p,4) || pm_be16(p+2)<4+expected*4) return false; value=at+pm_be16(p+2); if(!cstring(f,&value,end,1024,false,pd)) return false; } return true; }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32],b[52],e[18],p[2]; uint32_t total,block,n,counts[3],offsets[8],lengths[8],i,j,nranges=0; rg ranges[8]; char label[40];
    if(!pm_read(f,0,h,32) || xx_rt_memcmp(h,"J3D1btk1",8) || pm_be32(h+12)!=1 || (total=pm_be32(h+8))>(uint64_t)pm_available(f) || !pm_read(f,32,b,52) || !section(f,32,total,"TTK1",block=pm_be32(b+4),pd) || block<52 || b[8]>4 || b[9]>15 || !pm_be16(b+10)) return false;
    n=pm_be16(b+12); if(!n || n%3 || n>768) return false; for(i=0;i<3;++i) counts[i]=pm_be16(b+14+i*2);
    for(i=0;i<8;++i) offsets[i]=pm_be32(b+20+i*4); lengths[0]=n*18; lengths[1]=n/3*2; lengths[2]=0; lengths[3]=n/3; lengths[4]=n*4; lengths[5]=counts[0]*4; lengths[6]=counts[1]*2; lengths[7]=counts[2]*4;
    if(offsets[2]<52 || offsets[2]>=block || !j3d_names(f,32+offsets[2],32+block,n/3,pd)) return false;
    { uint32_t next=block; for(i=0;i<8;++i) if(i!=2 && offsets[i]>offsets[2] && offsets[i]<next) next=offsets[i]; lengths[2]=next-offsets[2]; }
    for(i=0;i<8;++i) { if(!lengths[i] || !reserve(ranges,&nranges,8,offsets[i],lengths[i],52,block)) return false; }
    for(i=0;i<n;++i) { if(stop(pd) || !pm_read(f,32+(int64_t)offsets[0]+i*18,e,18)) return false; for(j=0;j<3;++j) { uint32_t count=pm_be16(e+j*6),first=pm_be16(e+j*6+2),tangent=pm_be16(e+j*6+4),extent; if(!count || tangent>1) return false; extent=count==1 ? 1:count*(tangent ? 4:3); if(!span(first,extent,counts[j])) return false; } }
    for(i=0;i<n/3;++i) { uint8_t index; if(!pm_read(f,32+(int64_t)offsets[1]+i*2,p,2) || pm_be16(p)!=i || !pm_read(f,32+(int64_t)offsets[3]+i,&index,1) || index>9) return false; }
    if(!floats(f,32+offsets[4],n,true,pd) || !floats(f,32+offsets[5],counts[0],true,pd) || !floats(f,32+offsets[7],counts[2],true,pd)) return false;
    for(i=0;i<8;++i) { xx_rt_snprintf(label,sizeof(label),"table-%u.bin",i); if(!emit(f,s,label,32+offsets[i],lengths[i],total)) return false; }
    if(32U+(uint64_t)block!=total) return false; s->size=total; return true;

}

void xx_nintendo_j3d_btk_init(xx_nintendo_j3d_btk *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_J3D_BTK,"btk"); } }
xx_nintendo_j3d_btk *xx_nintendo_j3d_btk_create(xx_io_device *d,int64_t b) { xx_nintendo_j3d_btk *r=(xx_nintendo_j3d_btk *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_j3d_btk_init(r,d,b); return r; }
void xx_nintendo_j3d_btk_destroy(xx_nintendo_j3d_btk *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_j3d_btk_free(xx_nintendo_j3d_btk *r) { if(r) { xx_nintendo_j3d_btk_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_j3d_btk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_j3d_btk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
