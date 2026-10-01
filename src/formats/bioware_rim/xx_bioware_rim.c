/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xoreos/xoreos/master/src/aurora/rimfile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/bioware_rim/xx_bioware_rim.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[120],r[32]; uint32_t count,table,i; int64_t total=pm_available(f); uint64_t floor;
    if(!gm_read(f,total,0,h,120) || xx_rt_memcmp(h,"RIM V1.0",8)) return false;
    count=pm_le32(h+12); table=pm_le32(h+16);
    if(count>65536 || table<120 || !gm_range(total,table,(uint64_t)count*32)) return false;
    floor=table+(uint64_t)count*32; s->size=(int64_t)floor;
    for(i=0;i<count;++i) {
        if(gm_stopped(pd) || !gm_read(f,total,table+(uint64_t)i*32,r,32) || !gm_add(f,s,"resource.bin",pm_le32(r+24),pm_le32(r+28),floor,total)) return false;
    }
    return true;
}
void xx_bioware_rim_init(xx_bioware_rim *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BIOWARE_RIM,"bin"); } }
xx_bioware_rim *xx_bioware_rim_create(xx_io_device *d,int64_t b) { xx_bioware_rim *r=(xx_bioware_rim *)xx_mem_alloc(sizeof(*r)); if(r) xx_bioware_rim_init(r,d,b); return r; }
void xx_bioware_rim_destroy(xx_bioware_rim *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_bioware_rim_free(xx_bioware_rim *r) { if(r) { xx_bioware_rim_destroy(r); xx_mem_free(r); } }
bool xx_bioware_rim_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_bioware_rim_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
