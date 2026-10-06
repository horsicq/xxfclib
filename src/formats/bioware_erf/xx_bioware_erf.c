/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xoreos/xoreos/master/src/aurora/erffile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/bioware_erf/xx_bioware_erf.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[160],key[40],r[8]; uint32_t count,kt,rt,step,i,langs,descs,dt; int64_t total=pm_available(f); uint64_t floor;
    if(!gm_read(f,total,0,h,160) || (xx_rt_memcmp(h,"ERF ",4) && xx_rt_memcmp(h,"MOD ",4) && xx_rt_memcmp(h,"HAK ",4) && xx_rt_memcmp(h,"SAV ",4))) return false;
    if(!xx_rt_memcmp(h+4,"V1.0",4)) step=24; else if(!xx_rt_memcmp(h+4,"V1.1",4)) step=40; else return false;
    langs=pm_le32(h+8); descs=pm_le32(h+12); count=pm_le32(h+16); dt=pm_le32(h+20); kt=pm_le32(h+24); rt=pm_le32(h+28);
    if(count>65536 || langs>32 || kt<160 || rt<160 || !gm_range(total,kt,(uint64_t)count*step) || !gm_range(total,rt,(uint64_t)count*8) || (descs && (dt<160 || !gm_range(total,dt,descs)))) return false;
    floor=kt+(uint64_t)count*step; if(rt+(uint64_t)count*8>floor) floor=rt+(uint64_t)count*8;
    if(descs && (uint64_t)dt+descs>floor) { floor=(uint64_t)dt+descs; } s->size=(int64_t)floor;
    for(i=0;i<count;++i) {
        uint32_t id; if(gm_stopped(pd) || !gm_read(f,total,kt+(uint64_t)i*step,key,step)) return false;
        id=pm_le32(key+step-8); if(id>=count || !gm_read(f,total,rt+(uint64_t)id*8,r,8)) return false;
        if(!gm_add(f,s,"resource.bin",pm_le32(r),pm_le32(r+4),floor,total)) return false;
    }
    return true;
}
void xx_bioware_erf_init(xx_bioware_erf *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BIOWARE_ERF,"bin"); } }
xx_bioware_erf *xx_bioware_erf_create(xx_io_device *d,int64_t b) { xx_bioware_erf *r=(xx_bioware_erf *)xx_mem_alloc(sizeof(*r)); if(r) xx_bioware_erf_init(r,d,b); return r; }
void xx_bioware_erf_destroy(xx_bioware_erf *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_bioware_erf_free(xx_bioware_erf *r) { if(r) { xx_bioware_erf_destroy(r); xx_mem_free(r); } }
bool xx_bioware_erf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_bioware_erf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
