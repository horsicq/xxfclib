/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/RenolY2/j3dview/master/j3d/model.py
 * J3D2 bmd3 models with exactly the eight ordered INF1/VTX1/EVP1/DRW1/JNT1/SHP1/MAT3/TEX1 sections,32-byte aligned lengths. Exports each framed encoded section; nested GPU/geometry structures are preserved without rendering or pointer interpretation. Other J3D revisions and BDL extensions unsupported.
 */
#include "xxfclib/formats/nintendo_j3d_bmd/xx_nintendo_j3d_bmd.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
typedef struct rg { uint64_t at,n; } rg;
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && xx_data_get_u32(h+4, 4, 0, true)==n; }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[32],b[8]; uint32_t total,i,n; uint64_t at=32; char label[40]; static const char *tags[]={"INF1","VTX1","EVP1","DRW1","JNT1","SHP1","MAT3","TEX1"};
    if(!pm_read(f,0,h,32) || xx_rt_memcmp(h,"J3D2bmd3",8) || xx_data_get_u32(h+12, 4, 0, true)!=8 || (total=xx_data_get_u32(h+8, 4, 0, true))<32 || total>(uint64_t)pm_available(f)) return false;
    for(i=0;i<8;++i) { if(!pm_read(f,(int64_t)at,b,8) || (n=xx_data_get_u32(b+4, 4, 0, true))<32 || (n&31) || !section(f,at,total,tags[i],n,pd)) return false; xx_rt_snprintf(label,sizeof(label),"section-%s.bin",tags[i]); if(!emit(f,s,label,at,n,total)) return false; at+=n; }
    if(at!=total) { return false; } s->size=total; return true;

}

void xx_nintendo_j3d_bmd_init(xx_nintendo_j3d_bmd *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_J3D_BMD,"bmd"); } }
xx_nintendo_j3d_bmd *xx_nintendo_j3d_bmd_create(xx_io_device *d,int64_t b) { xx_nintendo_j3d_bmd *r=(xx_nintendo_j3d_bmd *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_j3d_bmd_init(r,d,b); return r; }
void xx_nintendo_j3d_bmd_destroy(xx_nintendo_j3d_bmd *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_j3d_bmd_free(xx_nintendo_j3d_bmd *r) { if(r) { xx_nintendo_j3d_bmd_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_j3d_bmd_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_j3d_bmd_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
