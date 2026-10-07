/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/unicode-org/icu/blob/main/icu4c/source/common/udata.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/icu_data_package/xx_icu_data_package.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

static uint32_t ic32(bool be,const uint8_t *p) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static uint16_t ic16(bool be,const uint8_t *p) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32]; bool be; uint16_t header; uint32_t count,i,previous=0; int64_t limit=pm_available(f),toc,first; char prior[4097]; prior[0]=0;
    if(!pm_read(f,0,h,24) || h[2]!=0xda || h[3]!=0x27 || h[8]>1 || h[9] || h[10]!=2 || h[11] || xx_rt_memcmp(h+12,"CmnD",4) || h[16]!=1 || h[17] || h[18] || h[19]) return false;
    be=h[8]!=0; header=ic16(be,h); if(header<24 || header%16 || ic16(be,h+4)<20 || ic16(be,h+4)>header-4 || ic16(be,h+6) || !wg_range(limit,header,4) || !pm_read(f,header,h,4) || !(count=ic32(be,h)) || count>65536 || !wg_range(limit,(uint64_t)header+4,(uint64_t)count*8)) return false; toc=header;
    if(!pm_read(f,toc+4,h,8)) { return false; } first=toc+ic32(be,h+4); if(first<toc+4+(int64_t)count*8 || first>limit) return false;
    for(i=0;i<count;++i) { uint32_t name,at,next; int64_t p; char text[4097],label[48]; size_t j;
        if(wg_stop(pd) || !pm_read(f,toc+4+(int64_t)i*8,h,8)) { return false; } name=ic32(be,h); at=ic32(be,h+4);
        if(i+1<count) { if(!pm_read(f,toc+4+(int64_t)(i+1)*8,h,8)) return false; next=ic32(be,h+4); } else { if(limit-toc>UINT32_MAX) return false; next=(uint32_t)(limit-toc); }
        if(at<=previous || next<=at || (toc+at)%16 || !wg_range(limit,toc+at,next-at) || name<4U+count*8U || toc+name>=first) { return false; } previous=at; p=toc+name;
        if(!wg_string(f,&p,first,text,sizeof(text)) || !text[0]) { return false; } for(j=0;text[j];++j) if((uint8_t)text[j]<32 || (uint8_t)text[j]>126) return false; if(i && xx_rt_strcmp(prior,text)>=0) return false; xx_rt_memcpy(prior,text,xx_rt_strlen(text)+1);
        if(next-at<24 || !pm_read(f,toc+at,h,24) || h[2]!=0xda || h[3]!=0x27 || h[8]!=be || h[9] || h[10]!=2 || h[11] || ic16(be,h+6) || ic16(be,h)<24 || ic16(be,h)>next-at || ic16(be,h+4)<20 || ic16(be,h+4)>ic16(be,h)-4) return false;
        xx_rt_snprintf(label,sizeof(label),"member-%u.icu",i); if(!pm_add(f,s,label,toc+at,next-at)) return false;
    } s->size=limit; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_icu_data_package_init(xx_icu_data_package *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ICU_DATA_PACKAGE,"dat"); } }
xx_icu_data_package *xx_icu_data_package_create(xx_io_device *d,int64_t b) { xx_icu_data_package *r=(xx_icu_data_package *)xx_mem_alloc(sizeof(*r)); if(r) xx_icu_data_package_init(r,d,b); return r; }
void xx_icu_data_package_destroy(xx_icu_data_package *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_icu_data_package_free(xx_icu_data_package *r) { if(r) { xx_icu_data_package_destroy(r); xx_mem_free(r); } }
bool xx_icu_data_package_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_icu_data_package_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
