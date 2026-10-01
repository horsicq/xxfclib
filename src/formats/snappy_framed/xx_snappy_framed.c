/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/google/snappy/blob/main/framing_format.txt */
#include "xxfclib/formats/snappy_framed/xx_snappy_framed.h"
#include "../microsoft_msf/xx_tenth_containers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;uint8_t *out=NULL;uint64_t at=0,total=0,n,raw,body;unsigned chunks=0;bool ok=false;
    if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,10) && !xx_rt_memcmp(b.p,"\xff\x06\0\0sNaPpY",10));out=(uint8_t *)xx_mem_alloc(67108864);NH_NEED(out);
    while(at<b.n) {unsigned type;NH_NEED(nh_span(&b,at,4) && ++chunks<=4094);type=b.p[(size_t)at];n=(uint64_t)b.p[(size_t)at+1]|(uint64_t)b.p[(size_t)at+2]<<8|(uint64_t)b.p[(size_t)at+3]<<16;body=at+4;NH_NEED(nh_span(&b,body,n));
        if(type==255) NH_NEED(n==6 && !xx_rt_memcmp(b.p+(size_t)body,"sNaPpY",6));
        else if(type<2) {uint32_t crc;NH_NEED(n>=4);crc=pm_le32(b.p+(size_t)body);raw=n-4;
            if(!type) {uint64_t pos=body+4;NH_NEED(th_var(&b,&pos,body+n,&raw) && raw<=65536);}
            NH_NEED(raw<=65536 && raw<=67108864-total);if(type) xx_rt_memcpy(out+(size_t)total,b.p+(size_t)body+4,(size_t)raw);else NH_NEED(th_snappy(b.p+(size_t)body+4,n-4,out+(size_t)total,raw,pd));
            NH_NEED(!fd_stop(pd) && th_mask(xx_crc32c_calc(0,out+(size_t)total,(size_t)raw))==crc);total+=raw;
        }else NH_NEED(type>=128);at=body+n;
    }NH_NEED(nh_add(f,s,&b,"stream-identifier",0,10) && th_mem(f,s,"decoded-payload",&out,total));s->size=(int64_t)b.n;ok=true;
done:if(out) xx_mem_free(out);xx_mem_free(b.p);return ok;
}
void xx_snappy_framed_init(xx_snappy_framed *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SNAPPY_FRAMED,"sz"); } }
xx_snappy_framed *xx_snappy_framed_create(xx_io_device *d,int64_t b) { xx_snappy_framed *r=(xx_snappy_framed *)xx_mem_alloc(sizeof(*r)); if(r) xx_snappy_framed_init(r,d,b); return r; }
void xx_snappy_framed_destroy(xx_snappy_framed *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_snappy_framed_free(xx_snappy_framed *r) { if(r) { xx_snappy_framed_destroy(r); xx_mem_free(r); } }
bool xx_snappy_framed_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_snappy_framed_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
