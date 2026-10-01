/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://source.android.com/docs/core/architecture/dto/partitions
 * Version 0, stored DTB/DTBO entries; no overlay application or newer compressed entries.
 */
#include "xxfclib/formats/android_dtbo/xx_android_dtbo.h"
#include "../xx_payload_members.h"


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[32],e[32],dt[8]; uint32_t total,hs,es,count,at,i; int64_t tableend;
    if(!pm_read(f,0,h,32) || pm_be32(h)!=0xd7b7ab1eU) return false;
    total=pm_be32(h+4); hs=pm_be32(h+8); es=pm_be32(h+12); count=pm_be32(h+16); at=pm_be32(h+20);
    if(hs!=32 || es!=32 || !count || count>65536 || at<hs || pm_be32(h+28)!=0 || total>pm_available(f)) return false;
    tableend=(int64_t)at+(int64_t)count*es; if(tableend>total) return false;
    for(i=0;i<count;++i) {
        uint32_t off,size; char name[64];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at+(int64_t)i*es,e,32)) return false;
        size=pm_be32(e); off=pm_be32(e+4);
        if(size<40 || off<tableend || off>total || size>total-off || !pm_read(f,off,dt,8) || pm_be32(dt)!=0xd00dfeed || pm_be32(dt+4)!=size) return false;
        xx_rt_snprintf(name,sizeof(name),"device-tree-%u.dtb",i);
        if(!pm_add(f,s,name,off,size)) return false;
    }
    s->size=total; return true;
}

void xx_android_dtbo_init(xx_android_dtbo *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ANDROID_DTBO,"android_dtbo"); } }
xx_android_dtbo *xx_android_dtbo_create(xx_io_device *d,int64_t b) { xx_android_dtbo *r=(xx_android_dtbo *)xx_mem_alloc(sizeof(*r)); if(r) xx_android_dtbo_init(r,d,b); return r; }
void xx_android_dtbo_destroy(xx_android_dtbo *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_android_dtbo_free(xx_android_dtbo *r) { if(r) { xx_android_dtbo_destroy(r); xx_mem_free(r); } }
bool xx_android_dtbo_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_android_dtbo_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
