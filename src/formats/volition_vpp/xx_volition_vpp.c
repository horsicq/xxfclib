/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/gibbed/Gibbed.Volition/blob/master/projects/Gibbed.Volition.FileFormats/PackageFileV3.cs
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/volition_vpp/xx_volition_vpp.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static uint32_t vp32(bool be,const uint8_t *p) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[376]; bool be; uint32_t count,size,dirs,names,i; int64_t namebase,data,limit=pm_available(f);
    if(!pm_read(f,0,h,376)) { return false; } be=xx_data_get_u32(h, 4, 0, true)==0x51890ace; if((!be && xx_data_get_u32(h, 4, 0, false)!=0x51890ace) || vp32(be,h+4)!=3 || vp32(be,h+332)) return false;
    count=vp32(be,h+340); size=vp32(be,h+344); dirs=vp32(be,h+348); names=vp32(be,h+352);
    if(!count || count>65536 || dirs!=(uint64_t)count*28 || !names || names>16777216 || size>(uint64_t)limit) return false;
    namebase=2048+((uint64_t)dirs+2047)/2048*2048; data=namebase+((uint64_t)names+2047)/2048*2048; if(data>size || !wg_range(size,2048,dirs) || !wg_range(data,namebase,names)) return false;
    for(i=0;i<count;++i) { uint32_t off,bytes,npos,packed; int64_t p; char text[4097],label[48];
        if(wg_stop(pd) || !pm_read(f,2048+(int64_t)i*28,h,28)) { return false; } npos=vp32(be,h); off=vp32(be,h+8); bytes=vp32(be,h+16); packed=vp32(be,h+20);
        if(npos>=names || (packed!=UINT32_MAX && packed && packed!=bytes) || !wg_range(size,(uint64_t)data+off,bytes)) return false;
        p=namebase+npos; if(!wg_string(f,&p,namebase+names,text,sizeof(text)) || !text[0]) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",i); if(!pm_add(f,s,label,data+off,bytes)) return false;
    } s->size=size; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_volition_vpp_init(xx_volition_vpp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VOLITION_VPP,"vpp"); } }
xx_volition_vpp *xx_volition_vpp_create(xx_io_device *d,int64_t b) { xx_volition_vpp *r=(xx_volition_vpp *)xx_mem_alloc(sizeof(*r)); if(r) xx_volition_vpp_init(r,d,b); return r; }
void xx_volition_vpp_destroy(xx_volition_vpp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_volition_vpp_free(xx_volition_vpp *r) { if(r) { xx_volition_vpp_destroy(r); xx_mem_free(r); } }
bool xx_volition_vpp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_volition_vpp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
