/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xoreos/xoreos/master/src/aurora/biffile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/bioware_biff/xx_bioware_biff.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[20],r[20]; uint32_t count,table,step,i; int64_t total=pm_available(f); uint64_t floor;
    if(!gm_read(f,total,0,h,20) || xx_rt_memcmp(h,"BIFF",4)) return false;
    if(!xx_rt_memcmp(h+4,"V1  ",4)) step=16;
    else if(!xx_rt_memcmp(h+4,"V1.1",4)) step=20; else return false;
    count=pm_le32(h+8); table=pm_le32(h+16);
    if(pm_le32(h+12)!=0 || count>65536 || table<20 || !gm_range(total,table,(uint64_t)count*step)) return false;
    floor=table+(uint64_t)count*step; s->size=(int64_t)floor;
    for(i=0;i<count;++i) {
        uint32_t at,n; if(gm_stopped(pd) || !gm_read(f,total,table+(uint64_t)i*step,r,step)) return false;
        if(step==20 && pm_le32(r+4)!=0) return false;
        at=pm_le32(r+step-12); n=pm_le32(r+step-8);
        if(!gm_add(f,s,"resource.bin",at,n,floor,total)) return false;
    }
    return true;
}
void xx_bioware_biff_init(xx_bioware_biff *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BIOWARE_BIFF,"bin"); } }
xx_bioware_biff *xx_bioware_biff_create(xx_io_device *d,int64_t b) { xx_bioware_biff *r=(xx_bioware_biff *)xx_mem_alloc(sizeof(*r)); if(r) xx_bioware_biff_init(r,d,b); return r; }
void xx_bioware_biff_destroy(xx_bioware_biff *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_bioware_biff_free(xx_bioware_biff *r) { if(r) { xx_bioware_biff_destroy(r); xx_mem_free(r); } }
bool xx_bioware_biff_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_bioware_biff_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
