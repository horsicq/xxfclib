/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/libyal/libevtx/blob/main/documentation/Windows%20XML%20Event%20Log%20(EVTX).asciidoc */
#include "xxfclib/formats/windows_evtx/xx_windows_evtx.h"
#include "../microsoft_msf/xx_tenth_containers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;uint64_t at,chunks,j,record,lastid=0;uint32_t count;bool ok=false;
    if(!nh_load(f,&b,pd)) return false;
    NH_NEED(nh_span(&b,0,4096) && !xx_rt_memcmp(b.p,"ElfFile\0",8) && pm_le32(b.p+32)==128 && pm_le16(b.p+36)==1 && pm_le16(b.p+38)==3 && pm_le16(b.p+40)==4096);
    count=pm_le32(b.p+42);chunks=(b.n-4096)/65536;NH_NEED((b.n-4096)%65536==0 && count==chunks && chunks && chunks<=1023 && !(pm_le32(b.p+120)&~3U));
    NH_NEED(fd_le64(b.p+16)>=fd_le64(b.p+8) && fd_le64(b.p+16)-fd_le64(b.p+8)+1==chunks && th_crc(&b,0,120,pm_le32(b.p+124),false));NH_NEED(nh_add(f,s,&b,"file-header",0,4096));
    for(j=0;j<chunks;++j) {uint32_t freepos,lastpos,crc;uint64_t last=0,n,first=0,num=0;at=4096+j*65536;NH_NEED(nh_span(&b,at,65536) && !xx_rt_memcmp(b.p+(size_t)at,"ElfChnk\0",8) && pm_le32(b.p+(size_t)at+40)==128);
        freepos=pm_le32(b.p+(size_t)at+48);lastpos=pm_le32(b.p+(size_t)at+44);NH_NEED(freepos>=512 && freepos<=65536 && lastpos>=512 && lastpos<freepos);
        crc=xx_crc32_calc(0,b.p+(size_t)at,120);crc=xx_crc32_calc(crc,b.p+(size_t)at+128,384);NH_NEED(crc==pm_le32(b.p+(size_t)at+124) && th_crc(&b,at+512,freepos-512,pm_le32(b.p+(size_t)at+52),false));NH_NEED(nh_add(f,s,&b,"chunk-header",at,512));
        for(record=at+512;record<at+freepos;record+=n) {uint64_t id;NH_NEED(nh_span(&b,record,28) && !xx_rt_memcmp(b.p+(size_t)record,"\x2a\x2a\0\0",4));n=pm_le32(b.p+(size_t)record+4);NH_NEED(n>=28 && !(n&7) && eh_span(record,n,at+freepos) && pm_le32(b.p+(size_t)(record+n-4))==n);id=fd_le64(b.p+(size_t)record+8);if(!num) first=id;lastid=id;last=record-at;++num;NH_NEED(nh_add(f,s,&b,"event-record",record,n));}
        NH_NEED(last==lastpos && first==fd_le64(b.p+(size_t)at+24) && lastid==fd_le64(b.p+(size_t)at+32) && fd_le64(b.p+(size_t)at+16)>=fd_le64(b.p+(size_t)at+8) && fd_le64(b.p+(size_t)at+16)-fd_le64(b.p+(size_t)at+8)+1==num);
    }s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_windows_evtx_init(xx_windows_evtx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WINDOWS_EVTX,"evtx"); } }
xx_windows_evtx *xx_windows_evtx_create(xx_io_device *d,int64_t b) { xx_windows_evtx *r=(xx_windows_evtx *)xx_mem_alloc(sizeof(*r)); if(r) xx_windows_evtx_init(r,d,b); return r; }
void xx_windows_evtx_destroy(xx_windows_evtx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_windows_evtx_free(xx_windows_evtx *r) { if(r) { xx_windows_evtx_destroy(r); xx_mem_free(r); } }
bool xx_windows_evtx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_windows_evtx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
