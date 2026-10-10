/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/apple-oss-distributions/CF/blob/main/CFBinaryPList.c */
#include "xxfclib/formats/binary_plist/xx_binary_plist.h"
#include "../common/xx_container_codec_helpers.h"

static uint64_t pl_int(const uint8_t *p,unsigned width) {uint64_t v=0;unsigned i;for(i=0;i<width;++i) v=(v<<8)|p[i];return v;}
static bool pl_count(memory_blob *b,uint64_t *at,uint64_t end,unsigned low,uint64_t *count) {
    unsigned width;if(low<15) {*count=low;return true;}if(*at>=end || (b->p[(size_t)*at]>>4)!=1 || (b->p[(size_t)*at]&15)>3) return false;width=1U<<(b->p[(size_t)(*at)++]&15);if(!record_span(*at,width,end)) return false;*count=pl_int(b->p+(size_t)*at,width);*at+=width;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    memory_blob b;uint64_t n,table,root,i,j,at,end,len,bytes,ref,*offsets=NULL;unsigned ow,rw,kind,low,width;bool ok=false;
    if(!blob_load(f,&b,pd)) { return false; } BLOB_NEED(blob_span(&b,0,40) && !xx_rt_memcmp(b.p,"bplist00",8));at=b.n-32;BLOB_NEED(blob_zero(&b,at,6));ow=b.p[(size_t)at+6];rw=b.p[(size_t)at+7];n=xx_data_get_u64(b.p+(size_t)at+8, 8, 0, true);root=xx_data_get_u64(b.p+(size_t)at+16, 8, 0, true);table=xx_data_get_u64(b.p+(size_t)at+24, 8, 0, true);
    /* Apple writes the smallest byte width, including three-byte offsets. */
    BLOB_NEED(ow>=1 && ow<=8 && rw>=1 && rw<=8 && n && n<=4093 && root<n && table>=8 && table<=b.n-32 && n*ow==b.n-32-table);
    offsets=(uint64_t *)xx_mem_alloc((size_t)n*sizeof(*offsets));BLOB_NEED(offsets);
    for(i=0;i<n;++i) {offsets[i]=pl_int(b.p+(size_t)(table+i*ow),ow);BLOB_NEED(offsets[i]>=8 && offsets[i]<table);for(j=0;j<i;++j) BLOB_NEED(offsets[j]!=offsets[i]);}
    BLOB_NEED(blob_add(f,s,&b,"plist-header",0,8));
    for(i=0;i<n;++i) {uint64_t start=offsets[i];end=table;for(j=0;j<n;++j) if(offsets[j]>start && offsets[j]<end) end=offsets[j];at=start;kind=b.p[(size_t)at]>>4;low=b.p[(size_t)at++]&15;len=0;bytes=0;
        if(!kind) BLOB_NEED(low==0 || low==8 || low==9);
        else if(kind==1 || kind==2 || kind==3 || kind==8) {width=kind==8 ? low+1:1U<<low;BLOB_NEED((kind==1 && low<=3) || (kind==2 && (low==2 || low==3)) || (kind==3 && low==3) || (kind==8 && width<=8));bytes=width;}
        else {BLOB_NEED(pl_count(&b,&at,end,low,&len));if(kind==4 || kind==5) bytes=len;else if(kind==6) BLOB_NEED(binary_mul(len,2,&bytes));else if(kind==10 || kind==11 || kind==12 || kind==13) {BLOB_NEED(len<=4093 && binary_mul(len,(uint64_t)rw*(kind==13 ? 2:1),&bytes));}else BLOB_NEED(false);}
        BLOB_NEED(record_span(at,bytes,end) && blob_span(&b,at,bytes));
        if(kind==5) BLOB_NEED(blob_ascii(b.p+(size_t)at,(size_t)bytes,true));
        if(kind==6) {for(j=0;j<bytes;j+=2) {uint16_t c=xx_data_get_u16(b.p+(size_t)(at+j), 2, 0, true);if(c>=0xd800 && c<=0xdbff) {BLOB_NEED(j+4<=bytes);c=xx_data_get_u16(b.p+(size_t)(at+j+2), 2, 0, true);BLOB_NEED(c>=0xdc00 && c<=0xdfff);j+=2;}else BLOB_NEED(c<0xdc00 || c>0xdfff);}}
        if(kind>=10) for(j=0;j<bytes;j+=rw) {ref=pl_int(b.p+(size_t)(at+j),rw);BLOB_NEED(ref<n);}
        BLOB_NEED(blob_add(f,s,&b,kind==4 ? "data":"object",kind==4 ? at:start,kind==4 ? bytes:at+bytes-start));at+=bytes;
        while(at<end) {BLOB_NEED(!binary_stop(pd) && (b.p[(size_t)at]==0 || b.p[(size_t)at]==15));++at;}
    }BLOB_NEED(blob_add(f,s,&b,"offset-table",table,n*ow) && blob_add(f,s,&b,"plist-trailer",b.n-32,32));s->size=(int64_t)b.n;ok=true;
done:if(offsets) xx_mem_free(offsets);xx_mem_free(b.p);return ok;
}
void xx_binary_plist_init(xx_binary_plist *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BINARY_PLIST,"plist"); } }
xx_binary_plist *xx_binary_plist_create(xx_io_device *d,int64_t b) { xx_binary_plist *r=(xx_binary_plist *)xx_mem_alloc(sizeof(*r)); if(r) xx_binary_plist_init(r,d,b); return r; }
void xx_binary_plist_destroy(xx_binary_plist *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_binary_plist_free(xx_binary_plist *r) { if(r) { xx_binary_plist_destroy(r); xx_mem_free(r); } }
bool xx_binary_plist_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_binary_plist_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
