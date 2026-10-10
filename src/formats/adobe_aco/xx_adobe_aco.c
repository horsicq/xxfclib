/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/adobe_aco/xx_adobe_aco.h"
#include "../common/xx_binary_cursor.h"

static bool sm_utf16(Abstractformat *f,uint64_t at,uint32_t units,uint64_t end,bool nul,xx_pd_struct *pd) {
    uint8_t b[2048]; uint32_t i; bool high=false;
    if(!units || units>1024 || !binary_range(at,(uint64_t)units*2,end) || !pm_read(f,(int64_t)at,b,(size_t)units*2)) return false;
    for(i=0;i<units;++i) { uint16_t v=xx_data_get_u16(b+i*2, 2, 0, true); if(binary_stop(pd)) return false;
        if(nul && i==units-1) return !high && !v;
        if(!v) return false;
        if(high) { if(v<0xdc00 || v>0xdfff) return false; high=false; }
        else if(v>=0xd800 && v<=0xdbff) high=true;
        else if(v>=0xdc00 && v<=0xdfff) return false;
    } return !high;
}

static bool sm_color(const uint8_t *p) {
    uint16_t kind=xx_data_get_u16(p, 2, 0, true),a=xx_data_get_u16(p+2, 2, 0, true); int16_t b=(int16_t)xx_data_get_u16(p+4, 2, 0, true),c=(int16_t)xx_data_get_u16(p+6, 2, 0, true);
    if(kind==0 || kind==1 || kind==2) return true;
    if(kind==7) return a<=10000 && b>=-12800 && b<=12700 && c>=-12800 && c<=12700;
    return kind==8 && a<=10000;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[4],b[14],old[10]; uint32_t count,i,units; uint64_t at,end=(uint64_t)pm_available(f); char label[48];
    if(!pm_read(f,0,h,4) || xx_data_get_u16(h, 2, 0, true)!=1 || !(count=xx_data_get_u16(h+2, 2, 0, true)) || count>4096 || !binary_range(4,(uint64_t)count*10+4,end) || !pm_add(f,s,"aco-v1-header.bin",0,4)) return false;
    at=4;
    for(i=0;i<count;++i) { if(binary_stop(pd) || !pm_read(f,(int64_t)at,b,10) || !sm_color(b)) return false;
        xx_rt_snprintf(label,sizeof(label),"aco-v1-color-%u.bin",i); if(!pm_add(f,s,label,(int64_t)at,10)) return false; at+=10; }
    if(!pm_read(f,(int64_t)at,h,4) || xx_data_get_u16(h, 2, 0, true)!=2 || xx_data_get_u16(h+2, 2, 0, true)!=count || !pm_add(f,s,"aco-v2-header.bin",(int64_t)at,4)) { return false; } at+=4;
    for(i=0;i<count;++i) { uint64_t stop;
        if(binary_stop(pd) || !binary_range(at,14,end) || !pm_read(f,(int64_t)at,b,14) || !pm_read(f,4+(int64_t)i*10,old,10) || xx_rt_memcmp(old,b,10)) return false;
        units=xx_data_get_u32(b+10, 4, 0, true); if(!sm_utf16(f,at+14,units,end,true,pd)) return false; stop=at+14+(uint64_t)units*2;
        xx_rt_snprintf(label,sizeof(label),"aco-v2-color-%u.bin",i); if(!pm_add(f,s,label,(int64_t)at,(int64_t)(stop-at))) return false; at=stop;
    } s->size=(int64_t)at; return true;
}

void xx_adobe_aco_init(xx_adobe_aco *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ADOBE_ACO,"aco"); } }
xx_adobe_aco *xx_adobe_aco_create(xx_io_device *d,int64_t b) { xx_adobe_aco *r=(xx_adobe_aco *)xx_mem_alloc(sizeof(*r)); if(r) xx_adobe_aco_init(r,d,b); return r; }
void xx_adobe_aco_destroy(xx_adobe_aco *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_adobe_aco_free(xx_adobe_aco *r) { if(r) { xx_adobe_aco_destroy(r); xx_mem_free(r); } }
bool xx_adobe_aco_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_adobe_aco_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
