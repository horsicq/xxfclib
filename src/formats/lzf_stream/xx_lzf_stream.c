/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/nemequ/liblzf/blob/master/lzf.c */
#include "xxfclib/formats/lzf_stream/xx_lzf_stream.h"
#include "../microsoft_msf/xx_tenth_containers.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b;uint8_t *out=NULL;uint64_t at=0,n,raw,h,total=0;unsigned count=0;bool ok=false;
    if(!nh_load(f,&b,pd)) return false;NH_NEED(nh_span(&b,0,5) && !xx_rt_memcmp(b.p,"ZV",2));out=(uint8_t *)xx_mem_alloc(67108864);NH_NEED(out);
    while(at<b.n) {if(!b.p[(size_t)at]) {NH_NEED(at+1==b.n);++at;break;}NH_NEED(nh_span(&b,at,5) && !xx_rt_memcmp(b.p+(size_t)at,"ZV",2) && ++count<=4094);h=b.p[(size_t)at+2];NH_NEED(h<2);n=pm_be16(b.p+(size_t)at+3);raw=n;
        if(h) {NH_NEED(nh_span(&b,at,7));raw=pm_be16(b.p+(size_t)at+5);}h=h ? 7:5;NH_NEED(n && raw && nh_span(&b,at+h,n) && raw<=67108864-total);
        if(h==5) xx_rt_memcpy(out+(size_t)total,b.p+(size_t)(at+h),(size_t)raw);else NH_NEED(th_lz(b.p+(size_t)(at+h),n,out+(size_t)total,raw,pd,0));total+=raw;at+=h+n;
    }NH_NEED(nh_add(f,s,&b,"block-header",0,b.p[2] ? 7:5) && th_mem(f,s,"decoded-payload",&out,total));s->size=(int64_t)b.n;ok=true;
done:if(out) xx_mem_free(out);xx_mem_free(b.p);return ok;
}
void xx_lzf_stream_init(xx_lzf_stream *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LZF_STREAM,"lzf"); } }
xx_lzf_stream *xx_lzf_stream_create(xx_io_device *d,int64_t b) { xx_lzf_stream *r=(xx_lzf_stream *)xx_mem_alloc(sizeof(*r)); if(r) xx_lzf_stream_init(r,d,b); return r; }
void xx_lzf_stream_destroy(xx_lzf_stream *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lzf_stream_free(xx_lzf_stream *r) { if(r) { xx_lzf_stream_destroy(r); xx_mem_free(r); } }
bool xx_lzf_stream_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lzf_stream_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
