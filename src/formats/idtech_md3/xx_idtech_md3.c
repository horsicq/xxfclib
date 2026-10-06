/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/qcommon/qfiles.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/idtech_md3/xx_idtech_md3.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[108],r[108],tri[12]; uint32_t frames,tags,surfaces,end,i,j,k; uint64_t at,n,off; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,108) || xx_rt_memcmp(h,"IDP3",4) || pm_le32(h+4)!=15) return false;
    frames=pm_le32(h+76); tags=pm_le32(h+80); surfaces=pm_le32(h+84); end=pm_le32(h+104);
    if(!frames || frames>1024 || tags>16 || !surfaces || surfaces>32 || end<108 || !gm_range(total,0,end)) { return false; } s->size=end;
    n=(uint64_t)frames*56;
    if((uint64_t)pm_le32(h+92)+n>pm_le32(h+100) || !gm_add(f,s,"frames.bin",pm_le32(h+92),n,108,end)) return false;
    if(tags && ((uint64_t)pm_le32(h+92)+n>pm_le32(h+96))) return false;
    n=(uint64_t)frames*tags*112;
    if(n && ((uint64_t)pm_le32(h+96)+n>pm_le32(h+100) || !gm_add(f,s,"tags.bin",pm_le32(h+96),n,108,end))) return false;
    at=pm_le32(h+100);
    for(i=0;i<surfaces;++i) { uint32_t size,v,t,sh; uint64_t count[4]; uint32_t offsets[4];
        if(gm_stopped(pd) || at<108 || !gm_read(f,end,at,r,108) || xx_rt_memcmp(r,"IDP3",4) || pm_le32(r+72)!=frames) return false;
        sh=pm_le32(r+76); v=pm_le32(r+80); t=pm_le32(r+84); size=pm_le32(r+104);
        if(sh>256 || !v || v>4096 || !t || t>8192 || size<108 || !gm_range(end,at,size)) return false;
        count[0]=(uint64_t)t*12; count[1]=(uint64_t)sh*68; count[2]=(uint64_t)v*8; count[3]=(uint64_t)v*frames*8;
        for(j=0;j<4;++j) { offsets[j]=pm_le32(r+88+j*4); if(offsets[j]<108 || !gm_range(size,offsets[j],count[j])) return false;
            for(k=0;k<j;++k) if(count[k] && count[j] && offsets[j]<(uint64_t)offsets[k]+count[k] && offsets[k]<(uint64_t)offsets[j]+count[j]) return false; }
        off=at+offsets[0]; for(j=0;j<t;++j) { if(!gm_read(f,end,off+(uint64_t)j*12,tri,12)) return false; for(k=0;k<3;++k) if(pm_le32(tri+k*4)>=v) return false; }
        if(!gm_add(f,s,"surface.md3part",at,size,108,end)) { return false; } at+=size;
    }
    return at==end;
}
void xx_idtech_md3_init(xx_idtech_md3 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IDTECH_MD3,"bin"); } }
xx_idtech_md3 *xx_idtech_md3_create(xx_io_device *d,int64_t b) { xx_idtech_md3 *r=(xx_idtech_md3 *)xx_mem_alloc(sizeof(*r)); if(r) xx_idtech_md3_init(r,d,b); return r; }
void xx_idtech_md3_destroy(xx_idtech_md3 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_idtech_md3_free(xx_idtech_md3 *r) { if(r) { xx_idtech_md3_destroy(r); xx_mem_free(r); } }
bool xx_idtech_md3_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_idtech_md3_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
