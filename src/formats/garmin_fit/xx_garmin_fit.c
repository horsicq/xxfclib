/* SPDX-License-Identifier: MIT
 * Independently implemented from https://developer.garmin.com/fit/protocol/ */
#include "xxfclib/formats/garmin_fit/xx_garmin_fit.h"
#include "../common/xx_memory_blob.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b={0};uint8_t h[12];uint32_t sizes[16]={0};bool stamp[16]={0},ok=false;uint64_t at,end;unsigned records=0;
    if(!pm_read(f,0,h,sizeof(h)) || (h[0]!=12 && h[0]!=14) || xx_rt_memcmp(h+8,".FIT",4)) return false;
    BLOB_NEED(blob_load(f,&b,pd));end=(uint64_t)h[0]+xx_data_get_u32(h+4, 4, 0, false);
    BLOB_NEED(end+2==b.n && h[1]>=16 && h[1]<64 && blob_crc16(b.p,(size_t)b.n)==0);
    if(h[0]==14) BLOB_NEED(blob_crc16(b.p,14)==0);
    BLOB_NEED(blob_add(f,s,&b,"header",0,h[0]));at=h[0];
    while(at<end) {uint64_t start=at;uint8_t code=b.p[(size_t)at++];unsigned local=code&15;
        BLOB_NEED(++records<=4094 && !binary_stop(pd));
        if(code&128) {local=(code>>5)&3;BLOB_NEED(sizes[local]>=4 && stamp[local] && record_span(at,sizes[local]-4,end));at+=sizes[local]-4;}
        else if(code&64) {unsigned i,count;uint32_t bytes=0;uint8_t used[256]={0};static const uint8_t widths[]={1,1,1,2,2,4,4,1,4,8,1,2,4,1,8,8,8};
            BLOB_NEED(!(code&16) && record_span(at,5,end) && b.p[(size_t)at]==0 && b.p[(size_t)at+1]<=1);
            count=b.p[(size_t)at+4];at+=5;stamp[local]=false;BLOB_NEED(count && record_span(at,(uint64_t)count*3,end));
            for(i=0;i<count;++i) {uint8_t field=b.p[(size_t)at],n=b.p[(size_t)at+1],type=b.p[(size_t)at+2]&31;
                BLOB_NEED(!used[field] && n && type<sizeof(widths) && !(b.p[(size_t)at+2]&96) && n%widths[type]==0);used[field]=1;bytes+=n;
                if(!i && field==253 && n==4 && type==6) { stamp[local]=true; } at+=3;
            }
            if(code&32) {BLOB_NEED(at<end);count=b.p[(size_t)at++];BLOB_NEED(record_span(at,(uint64_t)count*3,end));for(i=0;i<count;++i) {BLOB_NEED(b.p[(size_t)at+1]);bytes+=b.p[(size_t)at+1];at+=3;}}
            sizes[local]=bytes;
        }else {BLOB_NEED(!(code&48) && sizes[local] && record_span(at,sizes[local],end));at+=sizes[local];}
        BLOB_NEED(blob_add(f,s,&b,(code&64) && !(code&128) ? "definition":"data",start,at-start));
    }
    BLOB_NEED(records && blob_add(f,s,&b,"crc16",end,2));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}

void xx_garmin_fit_init(xx_garmin_fit *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GARMIN_FIT,"garmin_fit"); } }
xx_garmin_fit *xx_garmin_fit_create(xx_io_device *d,int64_t b) { xx_garmin_fit *r=(xx_garmin_fit *)xx_mem_alloc(sizeof(*r)); if(r) xx_garmin_fit_init(r,d,b); return r; }
void xx_garmin_fit_destroy(xx_garmin_fit *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_garmin_fit_free(xx_garmin_fit *r) { if(r) { xx_garmin_fit_destroy(r); xx_mem_free(r); } }
bool xx_garmin_fit_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_garmin_fit_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
