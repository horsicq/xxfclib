/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/SciresM/hactool/blob/master/pfs0.h
 * Stored partition files with bounded string/range tables and numeric names.
 */
#include "xxfclib/formats/nintendo_pfs0/xx_nintendo_pfs0.h"
#include "../xx_payload_members.h"

static uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[16],e[64],c; uint32_t count,strings,i; uint64_t table,data,end;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"PFS0",4) || pm_le32(h+12)) return false;
    count=pm_le32(h+4); strings=pm_le32(h+8); if(count>65536 || strings>16U*1024U*1024U) return false;
    table=16+(uint64_t)count*24; data=table+strings;
    if(data>(uint64_t)pm_available(f) || (count && !strings)) return false; end=data;
    for(i=0;i<count;++i) {
        uint64_t off,size,j; uint32_t n; bool ended=false; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,16+(int64_t)i*24,e,24)) return false;
        off=r64(e,false); size=r64(e+8,false); n=pm_le32(e+16);
        if(n>=strings || off>(uint64_t)pm_available(f)-data || size>(uint64_t)pm_available(f)-data-off) return false;
        for(j=n;j<strings;++j) { if(!pm_read(f,(int64_t)(table+j),&c,1)) return false; if(!c) { ended=true; break; } }
        if(!ended) return false;
        if(pm_le32(e+20)) return false;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",(unsigned)i);
        if(!pm_add(f,s,label,(int64_t)(data+off),(int64_t)size)) return false;
        if(data+off+size>end) end=data+off+size;
    }
    s->size=(int64_t)end; return true;
}

void xx_nintendo_pfs0_init(xx_nintendo_pfs0 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_PFS0,"pfs0"); } }
xx_nintendo_pfs0 *xx_nintendo_pfs0_create(xx_io_device *d,int64_t b) { xx_nintendo_pfs0 *r=(xx_nintendo_pfs0 *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_pfs0_init(r,d,b); return r; }
void xx_nintendo_pfs0_destroy(xx_nintendo_pfs0 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_pfs0_free(xx_nintendo_pfs0 *r) { if(r) { xx_nintendo_pfs0_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_pfs0_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_pfs0_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
