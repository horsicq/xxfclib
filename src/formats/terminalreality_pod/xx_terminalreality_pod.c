/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/jopadan/termpod/blob/master/include/termpod/pod.hpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/terminalreality_pod/xx_terminalreality_pod.h"
#include "../makeself/xx_fourth_wrapper_table.h"

static bool wg_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[96]; uint32_t count,audits,i,crc; int64_t limit=pm_available(f),names,first,audit;
    if(!pm_read(f,0,h,96) || xx_rt_memcmp(h,"POD2",4) || !(count=xx_data_get_u32(h+88, 4, 0, false)) || count>65536 || (audits=xx_data_get_u32(h+92, 4, 0, false))>4096) { return false; } crc=xx_data_get_u32(h+4, 4, 0, false);
    names=96+(int64_t)count*20; if(names>limit || (uint64_t)audits*312>(uint64_t)(limit-names)) return false; audit=limit-(int64_t)audits*312; first=audit;
    for(i=0;i<count;++i) { uint32_t at,bytes; if(wg_stop(pd) || !pm_read(f,96+(int64_t)i*20,h,20)) return false; bytes=xx_data_get_u32(h+4, 4, 0, false); at=xx_data_get_u32(h+8, 4, 0, false); if(at<(uint64_t)names || !wg_range(audit,at,bytes)) return false; if(at<first) first=at; }
    if(!wg_crc_mpeg(f,8,limit-8,crc,pd)) return false;
    for(i=0;i<count;++i) { uint32_t at,bytes,nameoff,check; int64_t namepos; char text[4097],name[48];
        if(!pm_read(f,96+(int64_t)i*20,h,20)) { return false; } nameoff=xx_data_get_u32(h, 4, 0, false); bytes=xx_data_get_u32(h+4, 4, 0, false); at=xx_data_get_u32(h+8, 4, 0, false); check=xx_data_get_u32(h+16, 4, 0, false); namepos=names+nameoff;
        if(namepos>=first || !wg_string(f,&namepos,first,text,sizeof(text)) || !text[0] || !wg_crc_mpeg(f,at,bytes,check,pd)) return false;
        xx_rt_snprintf(name,sizeof(name),"file-%u.bin",i); if(!pm_add(f,s,name,at,bytes)) return false;
    } if(audits && !pm_add(f,s,"audit-records.bin",audit,(int64_t)audits*312)) return false; s->size=limit; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return wg_parse(f,s,pd) && wg_members(s,pd); }
void xx_terminalreality_pod_init(xx_terminalreality_pod *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TERMINALREALITY_POD,"pod"); } }
xx_terminalreality_pod *xx_terminalreality_pod_create(xx_io_device *d,int64_t b) { xx_terminalreality_pod *r=(xx_terminalreality_pod *)xx_mem_alloc(sizeof(*r)); if(r) xx_terminalreality_pod_init(r,d,b); return r; }
void xx_terminalreality_pod_destroy(xx_terminalreality_pod *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_terminalreality_pod_free(xx_terminalreality_pod *r) { if(r) { xx_terminalreality_pod_destroy(r); xx_mem_free(r); } }
bool xx_terminalreality_pod_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_terminalreality_pod_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
