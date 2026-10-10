/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/JKAnderson/SoulsFormats/blob/master/SoulsFormats/Binder/BND3/BND3.cs
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/fromsoftware_binder/xx_fromsoftware_binder.h"
#include "../common/xx_carrier_helpers.h"

static uint8_t bn_reverse(uint8_t b) { b=(uint8_t)((b>>4)|(b<<4)); b=(uint8_t)(((b&0xcc)>>2)|((b&0x33)<<2)); return (uint8_t)(((b&0xaa)>>1)|((b&0x55)<<1)); }
static uint32_t bn32(bool be,const uint8_t *p) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static uint64_t bn64(bool be,const uint8_t *p) { return be ? carrier_be64(p) : carrier_u64(p); }
static bool carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[64]; bool four,be,bitbe,unicode=false; unsigned format,entry,count,i; uint64_t headers,table,limit=(uint64_t)pm_available(f),extent; size_t name_budget=16777216;
    if(!pm_read(f,0,h,32) || (xx_rt_memcmp(h,"BND3",4) && xx_rt_memcmp(h,"BND4",4))) { return false; } four=h[3]=='4';
    if(four) { if(!pm_read(f,0,h,64) || h[4]>1 || h[5]>1 || h[6] || h[7] || h[8] || h[9]>1 || h[10]>1 || h[11] || h[48]>1 || h[50] || h[51] || !carrier_zero(h+52,12)) return false;
        be=h[9]!=0; bitbe=!h[10]; unicode=h[48]!=0; format=bitbe || ((h[49]&1) && !(h[49]&128)) ? h[49] : bn_reverse(h[49]); count=bn32(be,h+12); headers=bn64(be,h+40); table=64;
        if(bn64(be,h+16)!=64) { return false; } entry=16+((format&16) ? 8U : 4U)+((format&32) ? 8U : 0U)+((format&2) ? 4U : 0U)+((format&12) ? 4U : 0U)+(format==4 ? 8U : 0U); if(bn64(be,h+32)!=entry) return false;
    } else { if(h[13]>1 || h[14]>1 || h[15] || !carrier_zero(h+28,4)) return false; bitbe=h[14]!=0; format=bitbe || ((h[12]&1) && !(h[12]&128)) ? h[12] : bn_reverse(h[12]); be=h[13] || (format&1); count=bn32(be,h+16); headers=bn32(be,h+20); table=32;
        if(bn32(be,h+24)!=0 && bn32(be,h+24)!=0x80000000U) { return false; } entry=8+((format&16) ? 8U : 4U)+((format&2) ? 4U : 0U)+((format&12) ? 4U : 0U)+((format&32) ? 4U : 0U);
    }
    if((format&~63U) || (format&64U) || !count || count>65536 || headers<table+(uint64_t)count*entry || headers>limit) { return false; } extent=headers;
    for(i=0;i<count;++i) { uint8_t flags; uint64_t bytes,offset,raw; unsigned p; uint32_t nameoff; char label[48]; int64_t namepos;
        if(carrier_stop(pd) || !pm_read(f,(int64_t)(table+(uint64_t)i*entry),h,entry) || h[1] || h[2] || h[3]) { return false; } flags=bitbe ? h[0] : bn_reverse(h[0]); if(flags&1) return false;
        if(four) { if(bn32(be,h+4)!=UINT32_MAX) return false; bytes=bn64(be,h+8); p=16; if(format&32) { raw=bn64(be,h+p); p+=8; if(raw!=bytes) return false; } }
        else { bytes=bn32(be,h+4); if(bytes>INT32_MAX) return false; p=8; }
        offset=(format&16) ? bn64(be,h+p) : bn32(be,h+p); p+=(format&16) ? 8 : 4; if(format&2) p+=4;
        if(format&12) { nameoff=bn32(be,h+p); p+=4; namepos=nameoff;
            if(nameoff<table+(uint64_t)count*entry || nameoff>=headers) return false;
            if(!carrier_name(f,namepos,(int64_t)headers,unicode,&name_budget,pd)) return false;
        }
        if(!four && (format&32) && bn32(be,h+p)!=bytes) return false;
        if(offset<headers || !carrier_range((int64_t)limit,offset,bytes)) { return false; } xx_rt_snprintf(label,sizeof(label),"file-%u.bin",i); if(!pm_add(f,s,label,(int64_t)offset,(int64_t)bytes)) return false; if(offset+bytes>extent) extent=offset+bytes;
    } s->size=(int64_t)extent; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_fromsoftware_binder_init(xx_fromsoftware_binder *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FROMSOFTWARE_BINDER,"bnd"); } }
xx_fromsoftware_binder *xx_fromsoftware_binder_create(xx_io_device *d,int64_t b) { xx_fromsoftware_binder *r=(xx_fromsoftware_binder *)xx_mem_alloc(sizeof(*r)); if(r) xx_fromsoftware_binder_init(r,d,b); return r; }
void xx_fromsoftware_binder_destroy(xx_fromsoftware_binder *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_fromsoftware_binder_free(xx_fromsoftware_binder *r) { if(r) { xx_fromsoftware_binder_destroy(r); xx_mem_free(r); } }
bool xx_fromsoftware_binder_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_fromsoftware_binder_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
