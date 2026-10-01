/* SPDX-License-Identifier: MIT
 * Independently implemented from https://codes.ecmwf.int/grib/format/grib2/regulations/ */
#include "xxfclib/formats/wmo_grib/xx_wmo_grib.h"
#include "../xx_ninth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16];nh_blob b={0};bool ok=false;uint64_t at=0;unsigned messages=0;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"GRIB",4) || h[7]!=2) return false;NH_NEED(nh_load(f,&b,pd));
    while(at<b.n) {uint64_t start=at,end;unsigned section=1;NH_NEED(++messages<=400 && nh_span(&b,at,16) && !xx_rt_memcmp(b.p+(size_t)at,"GRIB",4) && b.p[(size_t)at+7]==2);end=at+fd_be64(b.p+(size_t)at+8);NH_NEED(end>=at+20 && end<=b.n && nh_add(f,s,&b,"indicator",at,16));at+=16;
        while(at<end-4) {uint32_t n;uint8_t id;static const uint8_t minimum[]={0,21,5,14,9,11,6,5};NH_NEED(eh_span(at,5,end-4));n=pm_be32(b.p+(size_t)at);id=b.p[(size_t)at+4];if(section==2 && id==3) section=3;NH_NEED(id==section && id<=7 && n>=minimum[id] && eh_span(at,n,end-4) && nh_add(f,s,&b,"section",at,n));at+=n;++section;}
        NH_NEED(section==8 && at==end-4 && !xx_rt_memcmp(b.p+(size_t)at,"7777",4) && nh_add(f,s,&b,"end",at,4));at=end;NH_NEED(at>start);
    }s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_wmo_grib_init(xx_wmo_grib *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_WMO_GRIB,"wmo_grib"); } }
xx_wmo_grib *xx_wmo_grib_create(xx_io_device *d,int64_t b) { xx_wmo_grib *r=(xx_wmo_grib *)xx_mem_alloc(sizeof(*r)); if(r) xx_wmo_grib_init(r,d,b); return r; }
void xx_wmo_grib_destroy(xx_wmo_grib *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_wmo_grib_free(xx_wmo_grib *r) { if(r) { xx_wmo_grib_destroy(r); xx_mem_free(r); } }
bool xx_wmo_grib_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_wmo_grib_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
