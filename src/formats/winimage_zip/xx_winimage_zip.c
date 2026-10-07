/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xwinimageziparchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/winimage_zip/xx_winimage_zip.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t *tail,h[46],l[30]; int64_t limit=pm_available(f),low,ecd=-1,dir,archive,at,local; size_t n,i; uint16_t count; uint32_t bytes,relative; bool ok=false;
    if(limit<98 || !pm_read(f,0,h,2) || xx_rt_memcmp(h,"MZ",2)) { return false; } low=limit>65557 ? limit-65557 : 0; n=(size_t)(limit-low);
    tail=(uint8_t *)xx_mem_alloc(n); if(!tail || !pm_read(f,low,tail,n)) { if(tail) xx_mem_free(tail); return false; }
    for(i=n-22+1;i>0;--i) { size_t p=i-1; if((p&4095U)==0 && wg_stop(pd)) goto done; if(!xx_rt_memcmp(tail+p,"PK\5\6",4) && p+22+xx_data_get_u16(tail+p+20, 2, 0, false)==n) { ecd=low+(int64_t)p; xx_rt_memcpy(h,tail+p,22); break; } }
    if(ecd<0 || xx_data_get_u16(h+4, 2, 0, false) || xx_data_get_u16(h+6, 2, 0, false) || !(count=xx_data_get_u16(h+10, 2, 0, false)) || count==65535 || count!=xx_data_get_u16(h+8, 2, 0, false) || (bytes=xx_data_get_u32(h+12, 4, 0, false))>(uint64_t)ecd || (relative=xx_data_get_u32(h+16, 4, 0, false))==UINT32_MAX) goto done;
    dir=ecd-bytes; archive=dir-relative; if(archive<=4 || archive>=dir || !pm_read(f,archive-4,h,4) || xx_rt_memcmp(h,"WSfx",4)) goto done; local=archive; at=dir;
    for(i=0;i<count;++i) { uint32_t packed,raw,crc; uint16_t name,extra,comment; int64_t data; char label[48];
        if(wg_stop(pd) || ecd-at<46 || !pm_read(f,at,h,46) || xx_rt_memcmp(h,"PK\1\2",4) || xx_data_get_u16(h+8, 2, 0, false)&9 || xx_data_get_u16(h+10, 2, 0, false) || xx_data_get_u16(h+34, 2, 0, false) || xx_data_get_u32(h+42, 4, 0, false)!=(uint64_t)local) goto done;
        name=xx_data_get_u16(h+28, 2, 0, false); extra=xx_data_get_u16(h+30, 2, 0, false); comment=xx_data_get_u16(h+32, 2, 0, false); packed=xx_data_get_u32(h+20, 4, 0, false); raw=xx_data_get_u32(h+24, 4, 0, false); crc=xx_data_get_u32(h+16, 4, 0, false);
        if(!name || packed!=raw || (uint64_t)46+name+extra+comment>(uint64_t)(ecd-at) || dir-local<30 || !pm_read(f,local,l,30) || xx_rt_memcmp(l,"PK\3\4",4) || xx_data_get_u16(l+6, 2, 0, false)!=xx_data_get_u16(h+8, 2, 0, false) || xx_data_get_u16(l+8, 2, 0, false) || xx_data_get_u32(l+14, 4, 0, false)!=crc || xx_data_get_u32(l+18, 4, 0, false)!=packed || xx_data_get_u32(l+22, 4, 0, false)!=raw || xx_data_get_u16(l+26, 2, 0, false)!=name || !wg_equal(f,local+30,at+46,name,pd)) goto done;
        data=local+30+name+xx_data_get_u16(l+28, 2, 0, false); if(!wg_range(dir,data,packed) || !w5_crc(f,data,packed,crc,pd)) goto done;
        xx_rt_snprintf(label,sizeof(label),"file-%u.bin",(unsigned)i); if(!pm_add(f,s,label,data,raw)) goto done; local=data+packed; at+=46+name+extra+comment;
    } if(local!=dir || at!=ecd) goto done; s->size=limit; ok=true;
done: xx_mem_free(tail); return ok;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_winimage_zip_init(xx_winimage_zip *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WINIMAGE_ZIP,"exe"); } }
xx_winimage_zip *xx_winimage_zip_create(xx_io_device *d,int64_t b) { xx_winimage_zip *r=(xx_winimage_zip *)xx_mem_alloc(sizeof(*r)); if(r) xx_winimage_zip_init(r,d,b); return r; }
void xx_winimage_zip_destroy(xx_winimage_zip *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_winimage_zip_free(xx_winimage_zip *r) { if(r) { xx_winimage_zip_destroy(r); xx_mem_free(r); } }
bool xx_winimage_zip_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_winimage_zip_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
