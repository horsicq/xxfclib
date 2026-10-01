/* SPDX-License-Identifier: MIT
 * Independently implemented from https://github.com/ecmwf/eccodes/tree/develop/definitions/bufr */
#include "xxfclib/formats/wmo_bufr/xx_wmo_bufr.h"
#include "../xx_ninth_data.h"

static uint32_t length24(const uint8_t *p) {return (uint32_t)p[0]*65536U+(uint32_t)p[1]*256U+p[2];}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[8];nh_blob b={0};bool ok=false;uint64_t at=0;unsigned messages=0;
    if(!pm_read(f,0,h,8) || xx_rt_memcmp(h,"BUFR",4) || h[7]!=4) return false;NH_NEED(nh_load(f,&b,pd));
    while(at<b.n) {uint64_t end;uint32_t n;unsigned section;bool local;NH_NEED(++messages<=600 && nh_span(&b,at,8) && !xx_rt_memcmp(b.p+(size_t)at,"BUFR",4) && b.p[(size_t)at+7]==4);end=at+length24(b.p+(size_t)at+4);NH_NEED(end>=at+45 && end<=b.n && nh_add(f,s,&b,"indicator",at,8));at+=8;
        NH_NEED(eh_span(at,22,end-4));n=length24(b.p+(size_t)at);NH_NEED(n>=22 && eh_span(at,n,end-4));local=(b.p[(size_t)at+9]&128)!=0;
        NH_NEED(!(b.p[(size_t)at+9]&127) && pm_be16(b.p+(size_t)at+15)>=1900 && b.p[(size_t)at+17]>=1 && b.p[(size_t)at+17]<=12 && b.p[(size_t)at+18]>=1 && b.p[(size_t)at+18]<=31 && b.p[(size_t)at+19]<24 && b.p[(size_t)at+20]<60 && b.p[(size_t)at+21]<61 && nh_add(f,s,&b,"identification",at,n));at+=n;
        for(section=local ? 2:3;section<=4;++section) {NH_NEED(eh_span(at,4,end-4));n=length24(b.p+(size_t)at);NH_NEED(n>=(section==3 ? 7U:4U) && eh_span(at,n,end-4) && b.p[(size_t)at+3]==0);if(section==3) NH_NEED((n-7)%2==0 && pm_be16(b.p+(size_t)at+4) && !(b.p[(size_t)at+6]&63));NH_NEED(nh_add(f,s,&b,"section",at,n));at+=n;}
        NH_NEED(at==end-4 && !xx_rt_memcmp(b.p+(size_t)at,"7777",4) && nh_add(f,s,&b,"end",at,4));at=end;
    }s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_wmo_bufr_init(xx_wmo_bufr *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WMO_BUFR,"wmo_bufr"); } }
xx_wmo_bufr *xx_wmo_bufr_create(xx_io_device *d,int64_t b) { xx_wmo_bufr *r=(xx_wmo_bufr *)xx_mem_alloc(sizeof(*r)); if(r) xx_wmo_bufr_init(r,d,b); return r; }
void xx_wmo_bufr_destroy(xx_wmo_bufr *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_wmo_bufr_free(xx_wmo_bufr *r) { if(r) { xx_wmo_bufr_destroy(r); xx_mem_free(r); } }
bool xx_wmo_bufr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_wmo_bufr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
