/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/scummvm/scummvm/master/engines/scumm/imuse_digi/dimuse_bndmgr.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/lucas_bun/xx_lucas_bun.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[12],r[32]; uint32_t count,table,step,i; int64_t total=pm_available(f); uint64_t tableend;
    if(!gm_read(f,total,0,h,12)) return false;
    if(!xx_rt_memcmp(h,"LB83",4)) step=20; else if(!xx_rt_memcmp(h,"LB23",4)) step=32; else return false;
    table=pm_be32(h+4); count=pm_be32(h+8); tableend=table+(uint64_t)count*step;
    if(count>65536 || table<12 || !gm_range(total,table,(uint64_t)count*step)) { return false; } s->size=(int64_t)tableend;
    for(i=0;i<count;++i) {
        uint64_t at,n;
        if(gm_stopped(pd) || !gm_read(f,total,table+(uint64_t)i*step,r,step)) return false;
        at=pm_be32(r+step-8); n=pm_be32(r+step-4);
        if(!gm_range(total,at,n) || at<12 || (at<tableend && at+n>table) || !gm_add(f,s,"resource.bin",at,n,12,total)) return false;
    }
    return true;
}
void xx_lucas_bun_init(xx_lucas_bun *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LUCAS_BUN,"bin"); } }
xx_lucas_bun *xx_lucas_bun_create(xx_io_device *d,int64_t b) { xx_lucas_bun *r=(xx_lucas_bun *)xx_mem_alloc(sizeof(*r)); if(r) xx_lucas_bun_init(r,d,b); return r; }
void xx_lucas_bun_destroy(xx_lucas_bun *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lucas_bun_free(xx_lucas_bun *r) { if(r) { xx_lucas_bun_destroy(r); xx_mem_free(r); } }
bool xx_lucas_bun_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lucas_bun_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
